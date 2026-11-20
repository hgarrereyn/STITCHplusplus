//! The fuzzing campaign: LibAFL state, fork executor, stages and the main loop.
use crate::cmplog::CmplogStage;
use crate::ffi;
use crate::gen;
use crate::health;
use crate::mutators::StitchMutator;
use crate::program::Program;
use libafl::corpus::{Corpus, InMemoryOnDiskCorpus, OnDiskCorpus};
use libafl::events::SimpleEventManager;
use libafl::executors::{ExitKind, InProcessForkExecutor};
use libafl::feedbacks::{Feedback, MaxMapFeedback, StateInitializer};
use libafl::fuzzer::{Evaluator, Fuzzer, StdFuzzer};
use libafl::monitors::SimpleMonitor;
use libafl::observers::{CanTrack, HitcountsMapObserver, StdMapObserver};
use libafl::schedulers::{IndexesLenTimeMinimizerScheduler, QueueScheduler};
use libafl::stages::StdMutationalStage;
use libafl::common::HasMetadata;
use libafl::state::{HasCorpus, HasExecutions, HasMaxSize, HasRand, HasSolutions, StdState};
use libafl::{feedback_or, Error};
use libafl_bolts::rands::StdRand;
use libafl_bolts::shmem::{ShMemProvider, StdShMemProvider};
use libafl_bolts::tuples::tuple_list;
use libafl_bolts::Named;
use std::borrow::Cow;
use std::io::Write;
use std::path::PathBuf;
use std::time::{Duration, Instant};

pub struct FuzzConfig {
    pub out: PathBuf,
    pub secs: Option<u64>,
    pub seed: u64,
    pub timeout_ms: u64,
    pub import: Vec<PathBuf>,
    /// corpus directories of sibling workers, polled periodically
    pub sync: Vec<PathBuf>,
    pub quiet: bool,
    /// programs per forked worker (0: fork for every program)
    pub persistent: u32,
}

/// Records every execution into the health statistics; never marks anything interesting.
pub struct StatsFeedback {
    edges: *mut u8,
    n_edges: usize,
}

impl Named for StatsFeedback {
    fn name(&self) -> &Cow<'static, str> {
        static N: Cow<'static, str> = Cow::Borrowed("StitchStats");
        &N
    }
}

impl<S> StateInitializer<S> for StatsFeedback {}

impl<EM, OT, S> Feedback<EM, Program, OT, S> for StatsFeedback {
    fn is_interesting(
        &mut self,
        _state: &mut S,
        _manager: &mut EM,
        input: &Program,
        _observers: &OT,
        exit_kind: &ExitKind,
    ) -> Result<bool, Error> {
        let edges = unsafe { std::slice::from_raw_parts(self.edges, self.n_edges) };
        health::with(|h| h.record(input, exit_kind, edges));
        Ok(false)
    }
}

/// Objective: crashes and timeouts, keeping at most `cap` saved programs per (endpoint, kind)
/// so a shallow bug does not flood the crash directory. Triage buckets them by stack later.
pub struct SolutionFeedback {
    cap: u64,
    saved: std::collections::HashMap<(u32, bool, u64), u64>,
}

impl Named for SolutionFeedback {
    fn name(&self) -> &Cow<'static, str> {
        static N: Cow<'static, str> = Cow::Borrowed("StitchSolutions");
        &N
    }
}

impl<S> StateInitializer<S> for SolutionFeedback {}

impl<EM, OT, S> Feedback<EM, Program, OT, S> for SolutionFeedback {
    fn is_interesting(
        &mut self,
        _state: &mut S,
        _manager: &mut EM,
        input: &Program,
        _observers: &OT,
        exit_kind: &ExitKind,
    ) -> Result<bool, Error> {
        let timeout = match exit_kind {
            ExitKind::Crash | ExitKind::Oom => false,
            ExitKind::Timeout => true,
            _ => return Ok(false),
        };
        let r = ffi::results();
        let cur = r.current;
        let ep = if cur >= 0 && (cur as usize) < input.nodes.len() { input.nodes[cur as usize].ep } else { u32::MAX };
        // same endpoint + same faulting pc (or check message) = probably the same bug
        let n = self.saved.entry((ep, timeout, r.crash_pc)).or_default();
        if *n >= self.cap {
            return Ok(false);
        }
        *n += 1;
        Ok(true)
    }
}

fn write_stats(cfg: &FuzzConfig, start: Instant, corpus: usize, solutions: usize) {
    let v = health::with(|h| h.to_json(start.elapsed().as_secs_f64(), corpus, solutions));
    let tmp = cfg.out.join("stats.json.tmp");
    if std::fs::write(&tmp, serde_json::to_string_pretty(&v).unwrap()).is_ok() {
        let _ = std::fs::rename(&tmp, cfg.out.join("stats.json"));
    }
    let line = format!(
        "{},{},{},{},{},{}\n",
        start.elapsed().as_secs(),
        v["execs"],
        v["edges_covered"],
        corpus,
        solutions,
        v["exec_bail_rate"]
    );
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(cfg.out.join("plot.csv")) {
        let _ = f.write_all(line.as_bytes());
    }
}

/// The campaign loop, shared by both executors (a macro: the executor types differ).
macro_rules! campaign {
    ($cfg:ident, $fuzzer:ident, $executor:ident, $state:ident, $mgr:ident, $n_edges:ident) => {{
    let cfg = &$cfg;
    let fuzzer = &mut $fuzzer;
    let executor = &mut $executor;
    let state = &mut $state;
    let mgr = &mut $mgr;
    // resume from the existing corpus + imported programs
    let mut dirs = vec![cfg.out.join("corpus")];
    dirs.extend(cfg.import.iter().cloned());
    let mut loaded = 0;
    for d in dirs {
        let Ok(rd) = std::fs::read_dir(&d) else { continue };
        let mut files: Vec<_> = rd.flatten().map(|e| e.path()).filter(|p| crate::triage::is_program_file(p)).collect();
        files.sort();
        for f in files {
            if let Ok(data) = std::fs::read(&f) {
                if let Some(p) = Program::deserialize(&data) {
                    fuzzer.evaluate_input(state, executor, mgr, &p)?;
                    loaded += 1;
                }
            }
        }
    }
    // initial random programs
    for i in 0..256 {
        let calls = 1 + gen::below(state.rand_mut(), 6);
        let p = gen::random_program(state.rand_mut(), calls);
        if p.nodes.is_empty() {
            continue;
        }
        fuzzer.evaluate_input(state, executor, mgr, &p)?;
        if i >= 64 && state.corpus().count() > 0 {
            break;
        }
    }
    if state.corpus().count() == 0 {
        // nothing increased coverage (e.g. no instrumented code): force one entry
        let mut p = Program::default();
        for _ in 0..16 {
            p = gen::random_program(state.rand_mut(), 3);
            if !p.nodes.is_empty() {
                break;
            }
        }
        if p.nodes.is_empty() {
            return Err(Error::illegal_state("could not generate any program: no reachable endpoints"));
        }
        fuzzer.add_input(state, executor, mgr, p)?;
    }
    println!(
        "[stitch] campaign started: {} corpus entries ({} loaded), {} edges instrumented",
        state.corpus().count(),
        loaded,
        $n_edges
    );

    let mut stages = tuple_list!(CmplogStage, StdMutationalStage::new(StitchMutator::new()));
    let start = Instant::now();
    let mut last_stats = Instant::now();
    let mut last_clean = Instant::now();
    let mut last_sync = Instant::now();
    let mut synced: std::collections::HashSet<PathBuf> = std::collections::HashSet::new();
    write_stats(cfg, start, state.corpus().count(), state.solutions().count());
    loop {
        fuzzer.fuzz_one(&mut stages, executor, state, mgr)?;
        if last_stats.elapsed() >= Duration::from_secs(2) {
            write_stats(cfg, start, state.corpus().count(), state.solutions().count());
            last_stats = Instant::now();
        }
        if last_clean.elapsed() >= Duration::from_secs(30) {
            clean_tmp(&cfg.out.join("tmp"));
            last_clean = Instant::now();
        }
        if !cfg.sync.is_empty() && last_sync.elapsed() >= Duration::from_secs(20) {
            for d in &cfg.sync {
                let Ok(rd) = std::fs::read_dir(d) else { continue };
                for f in rd.flatten().map(|e| e.path()).filter(|p| crate::triage::is_program_file(p)) {
                    if synced.insert(f.clone()) {
                        if let Some(p) = std::fs::read(&f).ok().and_then(|d| Program::deserialize(&d)) {
                            fuzzer.evaluate_input(state, executor, mgr, &p)?;
                        }
                    }
                }
            }
            last_sync = Instant::now();
        }
        if let Some(s) = cfg.secs {
            if start.elapsed() >= Duration::from_secs(s) {
                break;
            }
        }
        let _ = state.executions();
    }
    write_stats(cfg, start, state.corpus().count(), state.solutions().count());
    println!("[stitch] campaign finished");
    Ok(())
    }};
}

pub fn run(cfg: FuzzConfig) -> Result<(), Error> {
    std::fs::create_dir_all(cfg.out.join("corpus"))?;
    std::fs::create_dir_all(cfg.out.join("crashes"))?;
    std::fs::create_dir_all(cfg.out.join("tmp"))?;
    if !cfg.out.join("plot.csv").exists() {
        std::fs::write(cfg.out.join("plot.csv"), "secs,execs,edges,corpus,crashes,exec_bail_rate\n")?;
    }
    unsafe { std::env::set_var("STITCH_TMPDIR", cfg.out.join("tmp")) };

    let mut n_edges = 0usize;
    let edges_ptr = unsafe { ffi::stitch_edges(&mut n_edges) };
    let n_edges = n_edges.max(1) + 1;
    health::init(n_edges);
    unsafe { ffi::stitch_init_once() };

    let edges_observer =
        unsafe { HitcountsMapObserver::new(StdMapObserver::from_mut_ptr("edges", edges_ptr, n_edges)).track_indices() };
    let map_feedback = MaxMapFeedback::new(&edges_observer);
    let mut feedback = map_feedback;
    // the objective runs on every execution (the corpus feedback is skipped for crashes), so the
    // statistics hook lives there
    let mut objective =
        feedback_or!(StatsFeedback { edges: edges_ptr, n_edges }, SolutionFeedback { cap: 5, saved: Default::default() });

    let mut state = StdState::new(
        StdRand::with_seed(cfg.seed),
        InMemoryOnDiskCorpus::<Program>::no_meta(cfg.out.join("corpus"))?,
        OnDiskCorpus::<Program>::no_meta(cfg.out.join("crashes"))?,
        &mut feedback,
        &mut objective,
    )?;
    state.set_max_size(1 << 20);
    {
        let mut toks = libafl::mutators::Tokens::default();
        for t in crate::data::tokens().iter() {
            toks.add_token(t);
        }
        state.add_metadata(toks);
    }

    let quiet = cfg.quiet;
    let log_path = cfg.out.join("fuzzer.log");
    let mut logf = std::fs::OpenOptions::new().create(true).append(true).open(&log_path)?;
    let monitor = SimpleMonitor::new(move |s: &str| {
        let _ = writeln!(logf, "{s}");
        if !quiet {
            eprintln!("{s}");
        }
    });
    let mut mgr = SimpleEventManager::new(monitor);
    let scheduler = IndexesLenTimeMinimizerScheduler::new(&edges_observer, QueueScheduler::new());
    let mut fuzzer = StdFuzzer::new(scheduler, feedback, objective);

    if cfg.persistent > 0 {
        let mut executor = crate::persistent::PersistentExecutor::new(
            tuple_list!(edges_observer),
            Duration::from_millis(cfg.timeout_ms),
            cfg.persistent,
        );
        campaign!(cfg, fuzzer, executor, state, mgr, n_edges)
    } else {
        let mut harness = |input: &Program| {
            let b = input.serialize();
            unsafe { ffi::stitch_exec(b.as_ptr(), b.len(), 0) };
            ExitKind::Ok
        };
        let mut executor = InProcessForkExecutor::new(
            &mut harness,
            tuple_list!(edges_observer),
            &mut fuzzer,
            &mut state,
            &mut mgr,
            Duration::from_millis(cfg.timeout_ms),
            StdShMemProvider::new()?,
        )?;
        campaign!(cfg, fuzzer, executor, state, mgr, n_edges)
    }
}



fn clean_tmp(dir: &std::path::Path) {
    if let Ok(rd) = std::fs::read_dir(dir) {
        for e in rd.flatten() {
            if let Ok(md) = e.metadata() {
                if md.modified().map(|t| t.elapsed().map(|d| d.as_secs() > 20).unwrap_or(false)).unwrap_or(false) {
                    let _ = std::fs::remove_file(e.path());
                }
            }
        }
    }
}
