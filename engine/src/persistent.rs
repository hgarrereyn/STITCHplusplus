//! Persistent fork server: one forked child runs many programs.
//!
//! With ASan, fork() alone costs ~1-2 ms; forking per execution caps throughput at a few hundred
//! execs/s. Here the parent forks a worker once, hands it programs through shared memory, and
//! re-forks after a crash or timeout, or every `max_runs` executions (bounding state carried
//! between programs: library globals, leaked objects). Crashes are re-run standalone by triage,
//! so anything that only crashed because of carried-over state is filtered out there.
use crate::ffi;
use crate::program::Program;
use libafl::executors::{Executor, ExitKind, HasObservers};
use libafl::observers::ObserversTuple;
use libafl::state::HasExecutions;
use libafl::Error;
use libafl_bolts::tuples::RefIndexable;
use std::time::Duration;

const PROG_CAP: usize = 16 << 20;
const WORKER_RSS_LIMIT: usize = 1 << 30;

fn rss_bytes() -> usize {
    // /proc/self/statm: size resident ... (pages); no allocation
    let fd = unsafe { libc::open(b"/proc/self/statm\0".as_ptr() as *const libc::c_char, libc::O_RDONLY) };
    if fd < 0 {
        return 0;
    }
    let mut buf = [0u8; 128];
    let n = unsafe { libc::read(fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len()) };
    unsafe { libc::close(fd) };
    if n <= 0 {
        return 0;
    }
    let text = &buf[..n as usize];
    let mut fields = text.split(|&c| c == b' ');
    fields.next();
    let pages: usize = fields.next().and_then(|f| std::str::from_utf8(f).ok()).and_then(|f| f.trim().parse().ok()).unwrap_or(0);
    pages * unsafe { libc::sysconf(libc::_SC_PAGESIZE) as usize }
}

pub struct PersistentExecutor<OT> {
    observers: OT,
    timeout: Duration,
    max_runs: u32,
    shm: *mut u8, // [u64 len][program bytes]
    child: libc::pid_t,
    cmd_w: i32,
    done_r: i32,
    runs: u32,
    prof: Option<(std::time::Instant, std::time::Instant, Duration, Duration, u64, u64)>, // start, last print, in child, in spawn, execs, spawns
}

impl<OT> PersistentExecutor<OT> {
    pub fn new(observers: OT, timeout: Duration, max_runs: u32) -> Self {
        let shm = unsafe {
            libc::mmap(std::ptr::null_mut(), PROG_CAP + 8, libc::PROT_READ | libc::PROT_WRITE, libc::MAP_SHARED | libc::MAP_ANONYMOUS, -1, 0)
        };
        assert!(shm != libc::MAP_FAILED, "mmap failed");
        let prof = std::env::var_os("STITCH_PROFILE").map(|_| {
            let n = std::time::Instant::now();
            (n, n, Duration::ZERO, Duration::ZERO, 0, 0)
        });
        Self { observers, timeout, max_runs: max_runs.max(1), shm: shm as *mut u8, child: 0, cmd_w: -1, done_r: -1, runs: 0, prof }
    }

    fn spawn(&mut self) -> Result<(), Error> {
        let mut cmd = [0i32; 2];
        let mut done = [0i32; 2];
        unsafe {
            if libc::pipe(cmd.as_mut_ptr()) != 0 || libc::pipe(done.as_mut_ptr()) != 0 {
                return Err(Error::unknown("pipe failed"));
            }
            let pid = libc::fork();
            if pid < 0 {
                return Err(Error::unknown("fork failed"));
            }
            if pid == 0 {
                // worker: run programs until the parent closes the command pipe
                libc::close(cmd[1]);
                libc::close(done[0]);
                let shm = self.shm;
                let mut b = [0u8; 1];
                loop {
                    if libc::read(cmd[0], b.as_mut_ptr() as *mut libc::c_void, 1) != 1 {
                        libc::_exit(0);
                    }
                    let len = (shm as *const u64).read_volatile() as usize;
                    let rc = ffi::stitch_exec(shm.add(8), len.min(PROG_CAP), 0);
                    b[0] = if rc == ffi::EXEC_FINDING { 2 } else { 1 };
                    if libc::write(done[1], b.as_ptr() as *const libc::c_void, 1) != 1 {
                        libc::_exit(0);
                    }
                    // leaked library objects accumulate in a long-lived worker: retire it when big
                    if rss_bytes() > WORKER_RSS_LIMIT {
                        libc::_exit(0);
                    }
                }
            }
            libc::close(cmd[0]);
            libc::close(done[1]);
            self.child = pid;
            self.cmd_w = cmd[1];
            self.done_r = done[0];
            self.runs = 0;
        }
        Ok(())
    }

    fn reap(&mut self, kill: bool) -> Option<i32> {
        if self.child <= 0 {
            return None;
        }
        unsafe {
            if kill {
                libc::kill(self.child, libc::SIGKILL);
            }
            libc::close(self.cmd_w);
            libc::close(self.done_r);
            let mut status = 0;
            libc::waitpid(self.child, &mut status, 0);
            self.child = 0;
            Some(status)
        }
    }
}

impl<OT> Drop for PersistentExecutor<OT> {
    fn drop(&mut self) {
        self.reap(true);
    }
}

impl<OT> HasObservers for PersistentExecutor<OT> {
    type Observers = OT;
    fn observers(&self) -> RefIndexable<&Self::Observers, Self::Observers> {
        RefIndexable::from(&self.observers)
    }
    fn observers_mut(&mut self) -> RefIndexable<&mut Self::Observers, Self::Observers> {
        RefIndexable::from(&mut self.observers)
    }
}

impl<EM, OT, S, Z> Executor<EM, Program, S, Z> for PersistentExecutor<OT>
where
    OT: ObserversTuple<Program, S>,
    S: HasExecutions,
{
    fn run_target(&mut self, _fuzzer: &mut Z, state: &mut S, _mgr: &mut EM, input: &Program) -> Result<ExitKind, Error> {
        *state.executions_mut() += 1;
        let t_spawn = std::time::Instant::now();
        let spawned = self.child == 0 || self.runs >= self.max_runs;
        if spawned {
            self.reap(false);
            self.spawn()?;
        }
        let t0 = std::time::Instant::now();
        self.observers.pre_exec_all(state, input)?;
        let bytes = input.serialize();
        let len = bytes.len().min(PROG_CAP);
        unsafe {
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), self.shm.add(8), len);
            (self.shm as *mut u64).write_volatile(len as u64);
            // a crash leaves the results of the running program; clear the header for this one
            let r = &mut *ffi::stitch_results();
            r.n_nodes = 0;
            r.current = -1;
            r.crash_pc = 0;
        }
        self.runs += 1;
        let b = [1u8; 1];
        let exit = unsafe {
            if libc::write(self.cmd_w, b.as_ptr() as *const libc::c_void, 1) != 1 {
                let st = self.reap(true).unwrap_or(0);
                classify(st)
            } else {
                let mut pfd = libc::pollfd { fd: self.done_r, events: libc::POLLIN, revents: 0 };
                let ms = self.timeout.as_millis().min(i32::MAX as u128) as i32;
                let n = libc::poll(&mut pfd, 1, ms);
                if n == 0 {
                    self.reap(true);
                    ExitKind::Timeout
                } else {
                    let mut rb = [0u8; 1];
                    if libc::read(self.done_r, rb.as_mut_ptr() as *mut libc::c_void, 1) == 1 {
                        // 2: a finding in a runtime that survives it (the worker stays)
                        if rb[0] == 2 { ExitKind::Crash } else { ExitKind::Ok }
                    } else {
                        let st = self.reap(false).unwrap_or(0);
                        classify(st)
                    }
                }
            }
        };
        if let Some(p) = &mut self.prof {
            p.2 += t0.elapsed();
            p.4 += 1;
            if spawned {
                p.3 += t0 - t_spawn;
                p.5 += 1;
            }
            if p.1.elapsed() >= Duration::from_secs(10) {
                let tot = p.0.elapsed().as_secs_f64();
                let line = format!(
                    "[stitch] profile: {} execs, {:.1} us/exec, child {:.1} us/exec ({:.0}%), {} forks ({:.0}% of the time), engine {:.1} us/exec",
                    p.4, tot * 1e6 / p.4 as f64, p.2.as_secs_f64() * 1e6 / p.4 as f64, 100.0 * p.2.as_secs_f64() / tot,
                    p.5, 100.0 * p.3.as_secs_f64() / tot, (tot - p.2.as_secs_f64() - p.3.as_secs_f64()) * 1e6 / p.4 as f64);
                if let Some(path) = std::env::var_os("STITCH_PROFILE") {
                    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(path) {
                        use std::io::Write;
                        let _ = writeln!(f, "{line}");
                    }
                }
                p.1 = std::time::Instant::now();
            }
        }
        self.observers.post_exec_all(state, input, &exit)?;
        Ok(exit)
    }
}

fn classify(status: i32) -> ExitKind {
    if libc::WIFSIGNALED(status) {
        let sig = libc::WTERMSIG(status);
        if sig == libc::SIGALRM || sig == libc::SIGKILL {
            return ExitKind::Timeout;
        }
        return ExitKind::Crash;
    }
    // the library called exit(): not a crash, the worker is simply gone
    ExitKind::Ok
}
