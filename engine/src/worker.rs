//! Worker protocol: drive a language runtime that runs in its own process.
//!
//! C and C++ harnesses link the engine in-process (`ffi` resolves to the C++ runtime). Every other
//! language runs a *worker*: a process started from `STITCH_WORKER` (a shell command) that talks
//! to the engine over two pipes, fd 3 (engine -> worker) and fd 4 (worker -> engine). The
//! `stitch-engine` binary implements the `ffi` ABI on top of this client, so the fuzzer, triage,
//! minimizer and decoder-driven mutations work unchanged. See languages/README.md for the protocol.
//!
//! Messages are `u8 tag, u32 len, payload` (little endian) in both directions.
use crate::ffi;
use crate::program::Program;
use libafl::executors::{Executor, ExitKind, HasObservers};
use libafl::observers::ObserversTuple;
use libafl::state::HasExecutions;
use libafl::Error;
use libafl_bolts::tuples::RefIndexable;
use std::io::{Read, Write};
use std::os::fd::{FromRawFd, RawFd};
use std::os::unix::process::CommandExt;
use std::process::{Child, Command};
use std::sync::Mutex;
use std::time::{Duration, Instant};

pub const OP_EXEC: u8 = b'X';
pub const OP_DECODE: u8 = b'D';
pub const OP_GENERATE: u8 = b'G';
pub const OP_SPANS: u8 = b'S';
pub const OP_DICT: u8 = b'T';
pub const OP_PLAN: u8 = b'P';
pub const OP_QUIT: u8 = b'Q';

pub const R_HELLO: u8 = b'H';
pub const R_OK: u8 = b'O';
pub const R_FINDING: u8 = b'C';
pub const R_VALUE: u8 = b'R';

pub const FLAG_TRACE: u32 = 1;
pub const FLAG_CMPLOG: u32 = 2;

const CMP_RECORD_SIZE: usize = 72; // StitchCmpRecord: 8-byte header + two 32-byte operands

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Outcome {
    Ok,
    /// the runtime caught a bug (exception, failed check) and is still running
    Finding,
    /// the worker process died (native crash, abort, exit)
    Died,
    Timeout,
}

struct Proc {
    child: Child,
    tx: std::fs::File,
    rx: std::fs::File,
    runs: u32,
}

pub struct Client {
    cmd: String,
    proc: Option<Proc>,
    pub manifest: String,
    pub map_size: usize,
    edges: &'static mut [u8],
    /// worker edge index -> dense index (0: not seen yet). Workers hash edges into a large map
    /// (2^16..2^17 slots) so ids are stable; the engine numbers the ones that occur 1, 2, 3, ...
    /// and its per-run passes (hit counts, novelty, statistics) cover only those.
    remap: Vec<u32>,
    /// dense indices in use + 1 (the observer's usable map length; a stable address)
    dense: &'static mut usize,
    results: Box<ffi::Results>,
    cmplog: Box<ffi::Cmplog>,
    dict: Vec<(u32, Vec<u8>)>,
    plan: Option<String>,
    pub max_runs: u32,
    rss_limit: usize,
}

static CLIENT: Mutex<Option<Client>> = Mutex::new(None);

/// True in the `stitch-engine` binary once a worker command is configured.
pub fn active() -> bool {
    CLIENT.lock().map(|c| c.is_some()).unwrap_or(false)
}

/// Run `f` on the global client, starting the worker on first use (after `fuzz` has re-executed
/// itself and redirected stderr). Exits the process if the worker cannot start.
pub fn with<R>(f: impl FnOnce(&mut Client) -> R) -> R {
    let mut g = CLIENT.lock().unwrap_or_else(|e| e.into_inner());
    let c = g.as_mut().expect("worker client not installed");
    if c.manifest.is_empty() {
        if let Err(e) = c.spawn() {
            eprintln!("error: {e}");
            std::process::exit(1);
        }
        // fuzz.rs observes map_size + 1 bytes (as for the C++ runtime's fixed map): leave headroom
        c.edges = Box::leak(vec![0u8; c.map_size.max(1) + 64].into_boxed_slice());
        c.remap = vec![0u32; c.map_size.max(1) + 1];
    }
    f(c)
}

/// Configure the worker command. Called by the `stitch-engine` binary; the worker starts lazily.
pub fn install(cmd: &str) -> Result<(), String> {
    let c = Client {
        cmd: cmd.to_string(),
        proc: None,
        manifest: String::new(),
        map_size: 1 << 16,
        edges: Box::leak(vec![0u8; 1].into_boxed_slice()),
        remap: vec![],
        dense: Box::leak(Box::new(1)),
        results: zeroed_box(),
        cmplog: zeroed_box(),
        dict: vec![],
        plan: None,
        max_runs: u32::MAX,
        rss_limit: std::env::var("STITCH_WORKER_RSS_MB").ok().and_then(|v| v.parse().ok()).unwrap_or(2048) << 20,
    };
    *CLIENT.lock().unwrap() = Some(c);
    Ok(())
}

fn zeroed_box<T>() -> Box<T> {
    // plain-old-data structs from ffi.rs: all-zero is a valid value
    unsafe {
        let layout = std::alloc::Layout::new::<T>();
        let p = std::alloc::alloc_zeroed(layout) as *mut T;
        assert!(!p.is_null());
        Box::from_raw(p)
    }
}

fn pipe() -> (RawFd, RawFd) {
    let mut fds = [0; 2];
    let r = unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_CLOEXEC) };
    assert_eq!(r, 0, "pipe2 failed");
    (fds[0], fds[1])
}

fn rss_of(pid: u32) -> usize {
    let Ok(s) = std::fs::read_to_string(format!("/proc/{pid}/statm")) else { return 0 };
    let pages: usize = s.split_whitespace().nth(1).and_then(|f| f.parse().ok()).unwrap_or(0);
    pages * 4096
}

impl Client {
    pub fn edges_ptr(&mut self) -> *mut u8 {
        self.edges.as_mut_ptr()
    }
    /// Usable length of the edge map (see `remap`).
    pub fn dense_len_ptr(&mut self) -> *mut usize {
        &mut *self.dense
    }
    pub fn results_ptr(&mut self) -> *mut ffi::Results {
        &mut *self.results
    }
    pub fn cmplog_ptr(&mut self) -> *mut ffi::Cmplog {
        &mut *self.cmplog
    }
    pub fn pid(&self) -> Option<u32> {
        self.proc.as_ref().map(|p| p.child.id())
    }

    fn spawn(&mut self) -> Result<(), String> {
        let (to_r, to_w) = pipe();
        let (from_r, from_w) = pipe();
        let mut cmd = Command::new("/bin/sh");
        cmd.arg("-c").arg(format!("exec {}", self.cmd)).env("STITCH_WORKER_MODE", "1");
        unsafe {
            cmd.pre_exec(move || {
                // move the pipe ends out of the way, then onto fds 3 and 4 (dup2 clears CLOEXEC)
                let a = libc::fcntl(to_r, libc::F_DUPFD, 100);
                let b = libc::fcntl(from_w, libc::F_DUPFD, 100);
                if a < 0 || b < 0 || libc::dup2(a, 3) < 0 || libc::dup2(b, 4) < 0 {
                    return Err(std::io::Error::last_os_error());
                }
                libc::close(a);
                libc::close(b);
                // library output on stdout would interleave with nothing useful: send it to stderr
                libc::dup2(2, 1);
                libc::prctl(libc::PR_SET_PDEATHSIG, libc::SIGKILL);
                Ok(())
            });
        }
        let child = cmd.spawn().map_err(|e| format!("cannot start worker `{}`: {e}", self.cmd))?;
        unsafe {
            libc::close(to_r);
            libc::close(from_w);
        }
        let tx = unsafe { std::fs::File::from_raw_fd(to_w) };
        let rx = unsafe { std::fs::File::from_raw_fd(from_r) };
        let mut p = Proc { child, tx, rx, runs: 0 };
        let started = Instant::now();
        let start_timeout = std::env::var("STITCH_WORKER_START_SECS").ok().and_then(|v| v.parse().ok()).unwrap_or(600);
        let (tag, payload) = match read_msg(&mut p.rx, Some(Duration::from_secs(start_timeout))) {
            Ok(m) => m,
            Err(e) => {
                let _ = p.child.kill();
                let st = p.child.wait().ok();
                return Err(format!("worker `{}` did not start ({e}; exit status {st:?})", self.cmd));
            }
        };
        if tag != R_HELLO || payload.len() < 4 {
            let _ = p.child.kill();
            let _ = p.child.wait();
            return Err(format!("worker `{}` sent no hello (tag {tag})", self.cmd));
        }
        let map_size = u32::from_le_bytes(payload[..4].try_into().unwrap()) as usize;
        if self.manifest.is_empty() {
            self.map_size = map_size.clamp(1, ffi::EDGES_SIZE);
            self.manifest = String::from_utf8_lossy(&payload[4..]).into_owned();
        }
        // a fresh worker has none of the learned dictionary tokens or the plan file
        for (dt, tok) in &self.dict {
            let mut m = dt.to_le_bytes().to_vec();
            m.extend_from_slice(tok);
            write_msg(&mut p.tx, OP_DICT, &m).map_err(|e| e.to_string())?;
        }
        if let Some(plan) = &self.plan {
            write_msg(&mut p.tx, OP_PLAN, plan.as_bytes()).map_err(|e| e.to_string())?;
        }
        if std::env::var_os("STITCH_DEBUG").is_some() {
            eprintln!("[stitch] worker {} started in {} ms", p.child.id(), started.elapsed().as_millis());
        }
        self.proc = Some(p);
        Ok(())
    }

    /// Stop the worker. `kill`: do not wait for it to finish the current request.
    pub fn stop(&mut self, kill: bool) -> Option<std::process::ExitStatus> {
        let mut p = self.proc.take()?;
        if std::env::var_os("STITCH_DEBUG").is_some() {
            eprintln!("[stitch] worker {} stopped after {} runs (kill: {kill})", p.child.id(), p.runs);
        }
        if kill {
            let _ = p.child.kill();
        } else {
            let _ = write_msg(&mut p.tx, OP_QUIT, &[]);
            drop(p.tx);
            let t = Instant::now();
            loop {
                if let Ok(Some(st)) = p.child.try_wait() {
                    return Some(st);
                }
                if t.elapsed() > Duration::from_secs(5) {
                    let _ = p.child.kill();
                    break;
                }
                std::thread::sleep(Duration::from_millis(2));
            }
        }
        p.child.wait().ok()
    }

    /// The worker closed its pipe: wait briefly for it to exit, then kill it.
    fn reap_dead(&mut self) -> Option<std::process::ExitStatus> {
        let mut p = self.proc.take()?;
        let t = Instant::now();
        while t.elapsed() < Duration::from_secs(2) {
            if let Ok(Some(st)) = p.child.try_wait() {
                return Some(st);
            }
            std::thread::sleep(Duration::from_millis(1));
        }
        let _ = p.child.kill();
        p.child.wait().ok()
    }

    fn ensure(&mut self) -> Result<(), String> {
        if let Some(p) = &mut self.proc {
            let recycle = p.runs >= self.max_runs || (p.runs % 64 == 63 && rss_of(p.child.id()) > self.rss_limit);
            if !recycle {
                return Ok(());
            }
            self.stop(false);
        }
        self.spawn()
    }

    /// Clear the shared result header before a run (a crash leaves the previous program's).
    fn reset_results(&mut self) {
        self.results.n_nodes = 0;
        self.results.current = -1;
        self.results.crash_pc = 0;
        self.results.str_used = 0;
    }

    /// Run one program. Coverage, results and comparisons land in the client's buffers.
    pub fn exec(&mut self, prog: &[u8], flags: u32, timeout: Option<Duration>) -> Outcome {
        self.reset_results();
        if let Err(e) = self.ensure() {
            eprintln!("[stitch] {e}");
            return Outcome::Died;
        }
        let cmplog = flags & FLAG_CMPLOG != 0;
        let p = self.proc.as_mut().unwrap();
        p.runs += 1;
        let mut m = Vec::with_capacity(prog.len() + 4);
        m.extend_from_slice(&flags.to_le_bytes());
        m.extend_from_slice(prog);
        if write_msg(&mut p.tx, OP_EXEC, &m).is_err() {
            self.stop(true);
            return Outcome::Died;
        }
        match read_msg(&mut p.rx, timeout) {
            Ok((tag, payload)) if tag == R_OK || tag == R_FINDING => {
                self.parse_results(&payload, cmplog);
                if tag == R_OK { Outcome::Ok } else { Outcome::Finding }
            }
            Err(ReadErr::Timeout) => {
                self.stop(true);
                Outcome::Timeout
            }
            _ => {
                // EOF or garbage: the worker died. Keep what it reported before dying. A clean
                // exit (the library called exit(0)) is not a crash, as with the fork executor.
                match self.reap_dead() {
                    Some(st) if st.success() => Outcome::Ok,
                    _ => Outcome::Died,
                }
            }
        }
    }

    fn parse_results(&mut self, b: &[u8], cmplog: bool) {
        let mut r = Cur { b, off: 0 };
        let n = (r.u32() as usize).min(ffi::MAX_NODES);
        let res = &mut *self.results;
        res.magic = 0x53544348;
        res.n_nodes = n as u32;
        res.current = r.u32() as i32;
        res.crash_pc = r.u64();
        for i in 0..n {
            let nd = &mut res.nodes[i];
            nd.status = r.u8();
            nd.n_out = r.u8();
            nd.out_mask = r.u16();
            nd.reason = r.u32();
            nd.miss_key = r.u32();
            nd.consumed = r.u32();
        }
        let sl = r.u32() as usize;
        let s = r.take(sl);
        let k = s.len().min(ffi::STRINGS_SIZE);
        res.strings[..k].copy_from_slice(&s[..k]);
        res.str_used = k as u32;
        let ne = r.u32() as usize;
        for _ in 0..ne {
            let idx = r.u32() as usize;
            let cnt = r.u8();
            let Some(d) = self.remap.get_mut(idx) else { continue };
            if *d == 0 {
                *d = *self.dense as u32;
                *self.dense += 1;
            }
            let d = *d as usize;
            if d < self.edges.len() {
                self.edges[d] = self.edges[d].saturating_add(cnt.max(1));
            }
        }
        let nc = r.u32() as usize;
        for _ in 0..nc {
            let rec = r.take(CMP_RECORD_SIZE);
            if rec.len() < CMP_RECORD_SIZE || !cmplog {
                continue;
            }
            let lg = &mut *self.cmplog;
            if (lg.count as usize) < ffi::CMPLOG_RECORDS {
                let c = &mut lg.records[lg.count as usize];
                c.node = u16::from_le_bytes([rec[0], rec[1]]);
                c.kind = rec[2];
                c.size = rec[3];
                c.len_a = rec[4].min(32);
                c.len_b = rec[5].min(32);
                c.a_const = rec[6];
                c.a.copy_from_slice(&rec[8..40]);
                c.b.copy_from_slice(&rec[40..72]);
                lg.count += 1;
            }
        }
    }

    fn rpc(&mut self, op: u8, payload: &[u8]) -> Vec<u8> {
        for _ in 0..2 {
            if self.ensure().is_err() {
                return vec![];
            }
            let p = self.proc.as_mut().unwrap();
            if write_msg(&mut p.tx, op, payload).is_ok() {
                if let Ok((R_VALUE, v)) = read_msg(&mut p.rx, Some(Duration::from_secs(60))) {
                    return v;
                }
            }
            self.stop(true);
        }
        vec![]
    }

    pub fn decode(&mut self, dt: u32, choices: &[u8], cap: usize) -> Vec<u8> {
        let mut m = dt.to_le_bytes().to_vec();
        m.extend_from_slice(&(cap as u32).to_le_bytes());
        m.extend_from_slice(choices);
        let mut v = self.rpc(OP_DECODE, &m);
        v.truncate(cap);
        v
    }

    pub fn generate(&mut self, dt: u32, seed: u64, prefix: &[u8], cap: usize) -> Vec<u8> {
        let mut m = dt.to_le_bytes().to_vec();
        m.extend_from_slice(&seed.to_le_bytes());
        m.extend_from_slice(&(cap as u32).to_le_bytes());
        m.extend_from_slice(prefix);
        let mut v = self.rpc(OP_GENERATE, &m);
        v.truncate(cap);
        v
    }

    pub fn spans(&mut self, dt: u32, choices: &[u8]) -> Vec<u32> {
        let mut m = dt.to_le_bytes().to_vec();
        m.extend_from_slice(choices);
        let v = self.rpc(OP_SPANS, &m);
        v.chunks_exact(4).map(|c| u32::from_le_bytes(c.try_into().unwrap())).collect()
    }

    pub fn dict_add(&mut self, dt: u32, tok: &[u8]) {
        if self.dict.len() >= 16384 || self.dict.iter().any(|(d, t)| *d == dt && t == tok) {
            return;
        }
        self.dict.push((dt, tok.to_vec()));
        if let Some(p) = &mut self.proc {
            let mut m = dt.to_le_bytes().to_vec();
            m.extend_from_slice(tok);
            if write_msg(&mut p.tx, OP_DICT, &m).is_err() {
                self.stop(true);
            }
        }
    }

    pub fn set_plan(&mut self, path: Option<&str>) {
        self.plan = path.map(|s| s.to_string());
        if let Some(p) = &mut self.proc {
            let _ = write_msg(&mut p.tx, OP_PLAN, path.unwrap_or("").as_bytes());
        }
    }

    pub fn cmplog_reset(&mut self) {
        self.cmplog.count = 0;
    }

    pub fn clear_edges(&mut self) {
        self.edges.fill(0);
    }
}

impl Drop for Client {
    fn drop(&mut self) {
        self.stop(false);
    }
}

/// Stop the worker cleanly (flushes plan files, coverage data).
pub fn shutdown() {
    if let Ok(mut g) = CLIENT.try_lock() {
        if let Some(c) = g.as_mut() {
            c.stop(false);
        }
    }
}

struct Cur<'a> {
    b: &'a [u8],
    off: usize,
}
impl<'a> Cur<'a> {
    fn take(&mut self, n: usize) -> &'a [u8] {
        let e = (self.off + n).min(self.b.len());
        let s = &self.b[self.off.min(e)..e];
        self.off = e;
        s
    }
    fn u8(&mut self) -> u8 {
        self.take(1).first().copied().unwrap_or(0)
    }
    fn u16(&mut self) -> u16 {
        let s = self.take(2);
        if s.len() == 2 { u16::from_le_bytes([s[0], s[1]]) } else { 0 }
    }
    fn u32(&mut self) -> u32 {
        let s = self.take(4);
        if s.len() == 4 { u32::from_le_bytes(s.try_into().unwrap()) } else { 0 }
    }
    fn u64(&mut self) -> u64 {
        let s = self.take(8);
        if s.len() == 8 { u64::from_le_bytes(s.try_into().unwrap()) } else { 0 }
    }
}

#[derive(Debug)]
enum ReadErr {
    Timeout,
    Closed,
}
impl std::fmt::Display for ReadErr {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", if matches!(self, ReadErr::Timeout) { "timed out" } else { "pipe closed" })
    }
}

fn write_msg(w: &mut std::fs::File, tag: u8, payload: &[u8]) -> std::io::Result<()> {
    let mut m = Vec::with_capacity(payload.len() + 5);
    m.push(tag);
    m.extend_from_slice(&(payload.len() as u32).to_le_bytes());
    m.extend_from_slice(payload);
    w.write_all(&m)
}

fn wait_readable(fd: RawFd, deadline: Option<Instant>) -> Result<(), ReadErr> {
    loop {
        let ms = match deadline {
            None => -1,
            Some(d) => {
                let now = Instant::now();
                if now >= d {
                    return Err(ReadErr::Timeout);
                }
                (d - now).as_millis().min(i32::MAX as u128) as i32
            }
        };
        let mut pfd = libc::pollfd { fd, events: libc::POLLIN, revents: 0 };
        let n = unsafe { libc::poll(&mut pfd, 1, ms) };
        if n > 0 {
            return Ok(());
        }
        if n == 0 {
            return Err(ReadErr::Timeout);
        }
        if std::io::Error::last_os_error().kind() != std::io::ErrorKind::Interrupted {
            return Err(ReadErr::Closed);
        }
    }
}

fn read_full(r: &mut std::fs::File, buf: &mut [u8], deadline: Option<Instant>) -> Result<(), ReadErr> {
    use std::os::fd::AsRawFd;
    let mut off = 0;
    while off < buf.len() {
        wait_readable(r.as_raw_fd(), deadline)?;
        match r.read(&mut buf[off..]) {
            Ok(0) => return Err(ReadErr::Closed),
            Ok(n) => off += n,
            Err(e) if e.kind() == std::io::ErrorKind::Interrupted => {}
            Err(_) => return Err(ReadErr::Closed),
        }
    }
    Ok(())
}

fn read_msg(r: &mut std::fs::File, timeout: Option<Duration>) -> Result<(u8, Vec<u8>), ReadErr> {
    let deadline = timeout.map(|t| Instant::now() + t);
    let mut h = [0u8; 5];
    read_full(r, &mut h, deadline)?;
    let len = u32::from_le_bytes(h[1..5].try_into().unwrap()) as usize;
    if len > (256 << 20) {
        return Err(ReadErr::Closed);
    }
    let mut payload = vec![0u8; len];
    read_full(r, &mut payload, deadline)?;
    Ok((h[0], payload))
}

/// LibAFL executor for worker runtimes: one long-lived process runs every program.
pub struct WorkerExecutor<OT> {
    observers: OT,
    timeout: Duration,
    prof: Option<Prof>,
}

/// STITCH_PROFILE=<file>: where the time goes (engine vs worker round trip), printed every 10 s.
struct Prof {
    start: Instant,
    last_print: Instant,
    in_worker: Duration,
    execs: u64,
}

impl<OT> WorkerExecutor<OT> {
    pub fn new(observers: OT, timeout: Duration, max_runs: u32) -> Self {
        with(|c| c.max_runs = if max_runs == 0 { u32::MAX } else { max_runs });
        let prof = std::env::var_os("STITCH_PROFILE").map(|_| Prof { start: Instant::now(), last_print: Instant::now(), in_worker: Duration::ZERO, execs: 0 });
        Self { observers, timeout, prof }
    }
}

impl<OT> HasObservers for WorkerExecutor<OT> {
    type Observers = OT;
    fn observers(&self) -> RefIndexable<&Self::Observers, Self::Observers> {
        RefIndexable::from(&self.observers)
    }
    fn observers_mut(&mut self) -> RefIndexable<&mut Self::Observers, Self::Observers> {
        RefIndexable::from(&mut self.observers)
    }
}

impl<EM, OT, S, Z> Executor<EM, Program, S, Z> for WorkerExecutor<OT>
where
    OT: ObserversTuple<Program, S>,
    S: HasExecutions,
{
    fn run_target(&mut self, _fuzzer: &mut Z, state: &mut S, _mgr: &mut EM, input: &Program) -> Result<ExitKind, Error> {
        *state.executions_mut() += 1;
        self.observers.pre_exec_all(state, input)?;
        let bytes = input.serialize();
        let flags = if ffi::cmplog().enabled != 0 { FLAG_CMPLOG } else { 0 };
        let t0 = Instant::now();
        let out = with(|c| c.exec(&bytes, flags, Some(self.timeout)));
        if let Some(p) = &mut self.prof {
            p.in_worker += t0.elapsed();
            p.execs += 1;
            if p.last_print.elapsed() >= Duration::from_secs(10) {
                let tot = p.start.elapsed();
                let line = format!(
                    "[stitch] profile: {} execs, {:.1} us/exec, worker round trip {:.1} us/exec ({:.0}%), engine {:.1} us/exec",
                    p.execs,
                    tot.as_secs_f64() * 1e6 / p.execs as f64,
                    p.in_worker.as_secs_f64() * 1e6 / p.execs as f64,
                    100.0 * p.in_worker.as_secs_f64() / tot.as_secs_f64(),
                    (tot - p.in_worker).as_secs_f64() * 1e6 / p.execs as f64
                );
                // STITCH_PROFILE is a file to append to (stderr is closed in --quiet campaigns)
                if let Some(path) = std::env::var_os("STITCH_PROFILE") {
                    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(path) {
                        let _ = writeln!(f, "{line}");
                    }
                }
                p.last_print = Instant::now();
            }
        }
        let exit = match out {
            Outcome::Ok => ExitKind::Ok,
            Outcome::Finding | Outcome::Died => ExitKind::Crash,
            Outcome::Timeout => ExitKind::Timeout,
        };
        self.observers.post_exec_all(state, input, &exit)?;
        Ok(exit)
    }
}
