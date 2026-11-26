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
use std::sync::atomic::{AtomicI32, Ordering};
use std::time::Duration;

const PROG_CAP: usize = 16 << 20;
const WORKER_RSS_LIMIT: usize = 1 << 30;
const RSS_CHECK_EVERY: u32 = 16;

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

/// The control socket of the running zygote (-1: none). `ffi::dict_add` forwards learned
/// dictionary tokens through it, so workers forked later decode with them.
static ZYGOTE_CTL: AtomicI32 = AtomicI32::new(-1);

const Z_FORK: u8 = b'F';
const Z_WAIT: u8 = b'W';
const Z_DICT: u8 = b'T';

/// Tell the zygote about a dictionary token the runtime just learned in this process.
pub fn forward_dict(dt: usize, tok: &[u8]) {
    let ctl = ZYGOTE_CTL.load(Ordering::Relaxed);
    if ctl < 0 {
        return;
    }
    let mut m = Vec::with_capacity(9 + tok.len());
    m.push(Z_DICT);
    m.extend_from_slice(&(dt as u32).to_le_bytes());
    m.extend_from_slice(&(tok.len() as u32).to_le_bytes());
    m.extend_from_slice(tok);
    unsafe { write_all(ctl, &m) };
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
    /// fork server (pid, control socket): workers are forked from it, not from this process
    zygote: Option<(libc::pid_t, i32)>,
    /// workers let go without waiting for them to exit, reaped at a later spawn
    retired: Vec<libc::pid_t>,
    prof: Option<(std::time::Instant, std::time::Instant, Duration, Duration, u64, u64)>, // start, last print, in child, in spawn, execs, spawns
}

impl<OT> PersistentExecutor<OT> {
    pub fn new(observers: OT, timeout: Duration, max_runs: u32) -> Self {
        let shm = unsafe {
            libc::mmap(std::ptr::null_mut(), PROG_CAP + 8, libc::PROT_READ | libc::PROT_WRITE, libc::MAP_SHARED | libc::MAP_ANONYMOUS, -1, 0)
        };
        assert!(shm != libc::MAP_FAILED, "mmap failed");
        // a worker that retires itself (memory limit) leaves the command pipe without a reader:
        // the next write must fail with EPIPE (handled as a dead worker), not kill the fuzzer
        unsafe { libc::signal(libc::SIGPIPE, libc::SIG_IGN) };
        let prof = std::env::var_os("STITCH_PROFILE").map(|_| {
            let n = std::time::Instant::now();
            (n, n, Duration::ZERO, Duration::ZERO, 0, 0)
        });
        let mut ex = Self {
            observers,
            timeout,
            max_runs: max_runs.max(1),
            shm: shm as *mut u8,
            child: 0,
            cmd_w: -1,
            done_r: -1,
            runs: 0,
            zygote: None,
            retired: vec![],
            prof,
        };
        if std::env::var_os("STITCH_NO_ZYGOTE").is_none() {
            ex.zygote = unsafe { start_zygote(ex.shm) };
        }
        ex
    }

    fn spawn(&mut self) -> Result<(), Error> {
        if let Some((_, ctl)) = self.zygote {
            let got = unsafe {
                if write_all(ctl, &[Z_FORK]) {
                    let mut pid = [0u8; 4];
                    recv_fds(ctl, &mut pid, 2).map(|fds| (i32::from_le_bytes(pid), fds))
                } else {
                    None
                }
            };
            return match got {
                Some((pid, fds)) if pid > 0 => {
                    self.child = pid;
                    self.cmd_w = fds[0];
                    self.done_r = fds[1];
                    self.runs = 0;
                    Ok(())
                }
                _ => Err(Error::unknown("the fork server did not start a worker")),
            };
        }
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
                libc::close(cmd[1]);
                libc::close(done[0]);
                worker_loop(self.shm, cmd[0], done[1]);
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

    /// Let the worker go without waiting for it to exit: closing its command pipe ends it (`kill`:
    /// a hung one). Its exit (tearing down an ASan address space) takes about as long as forking a
    /// new worker; it is reaped later instead of on the fuzzing loop's time.
    fn retire(&mut self, kill: bool) {
        if self.child <= 0 {
            return;
        }
        unsafe {
            if kill {
                libc::kill(self.child, libc::SIGKILL);
            }
            libc::close(self.cmd_w);
            libc::close(self.done_r);
        }
        // the zygote reaps its own children
        if self.zygote.is_none() {
            self.retired.push(self.child);
        }
        self.child = 0;
    }

    fn reap_retired(&mut self, block: bool) {
        let flags = if block { 0 } else { libc::WNOHANG };
        // waitpid: 0 = still running; the pid (reaped) or -1 (gone): forget it
        self.retired.retain(|&pid| unsafe { libc::waitpid(pid, std::ptr::null_mut(), flags) } == 0);
    }

    /// Stop the worker and wait for its exit status.
    fn reap(&mut self, kill: bool) -> Option<i32> {
        if self.child <= 0 {
            return None;
        }
        let pid = self.child;
        self.child = 0;
        unsafe {
            if kill {
                libc::kill(pid, libc::SIGKILL);
            }
            libc::close(self.cmd_w);
            libc::close(self.done_r);
            let mut status = 0;
            match self.zygote {
                Some((_, ctl)) => {
                    let mut m = vec![Z_WAIT];
                    m.extend_from_slice(&pid.to_le_bytes());
                    let mut st = [0u8; 4];
                    if write_all(ctl, &m) && read_exact(ctl, &mut st) {
                        status = i32::from_le_bytes(st);
                    }
                }
                None => {
                    libc::waitpid(pid, &mut status, 0);
                }
            }
            Some(status)
        }
    }
}

impl<OT> Drop for PersistentExecutor<OT> {
    fn drop(&mut self) {
        self.reap(true);
        self.reap_retired(true);
        if let Some((pid, ctl)) = self.zygote.take() {
            ZYGOTE_CTL.store(-1, Ordering::Relaxed);
            unsafe {
                // the zygote waits for its remaining workers and exits when its socket closes
                libc::close(ctl);
                libc::waitpid(pid, std::ptr::null_mut(), 0);
            }
        }
    }
}

/// A worker: run programs from shared memory until the command pipe closes.
unsafe fn worker_loop(shm: *mut u8, cmd_r: i32, done_w: i32) -> ! {
    libc::signal(libc::SIGPIPE, libc::SIG_DFL);
    let mut b = [0u8; 1];
    let mut runs = 0u32;
    loop {
        if libc::read(cmd_r, b.as_mut_ptr() as *mut libc::c_void, 1) != 1 {
            libc::_exit(0);
        }
        let len = (shm as *const u64).read_volatile() as usize;
        let rc = ffi::stitch_exec(shm.add(8), len.min(PROG_CAP), 0);
        b[0] = if rc == ffi::EXEC_FINDING { 2 } else { 1 };
        if libc::write(done_w, b.as_ptr() as *const libc::c_void, 1) != 1 {
            libc::_exit(0);
        }
        // leaked library objects accumulate in a long-lived worker: retire it when big
        // (checked every few runs: reading /proc costs three syscalls)
        runs += 1;
        if runs % RSS_CHECK_EVERY == 0 && rss_bytes() > WORKER_RSS_LIMIT {
            libc::_exit(0);
        }
    }
}

/// Fork server. Forking the fuzzer process itself gets slower as its heap grows (page tables to
/// copy), and every fork write-protects that heap: each page the engine touches afterwards takes a
/// copy-on-write fault, once per page per worker. The zygote is forked once, before the campaign
/// loads anything, and forks every worker from its small, idle address space; the fuzzer process
/// never forks again.
///
/// Protocol on a socket pair (the zygote answers in order):
///   'F'                  fork a worker -> u32 pid, with the worker's command (write) and done
///                        (read) pipe ends as SCM_RIGHTS
///   'W' u32 pid          wait for a worker that died -> i32 wait status
///   'T' u32 dt, u32 len, bytes   add a learned dictionary token (as the fuzzer process did)
/// Retired workers are reaped at the next 'F'.
unsafe fn start_zygote(shm: *mut u8) -> Option<(libc::pid_t, i32)> {
    let mut sv = [0i32; 2];
    if libc::socketpair(libc::AF_UNIX, libc::SOCK_STREAM, 0, sv.as_mut_ptr()) != 0 {
        return None;
    }
    let parent = libc::getpid();
    let pid = libc::fork();
    if pid < 0 {
        libc::close(sv[0]);
        libc::close(sv[1]);
        return None;
    }
    if pid == 0 {
        libc::close(sv[0]);
        libc::prctl(libc::PR_SET_PDEATHSIG, libc::SIGKILL);
        if libc::getppid() != parent {
            libc::_exit(0);
        }
        zygote_main(sv[1], shm);
    }
    libc::close(sv[1]);
    ZYGOTE_CTL.store(sv[0], Ordering::Relaxed);
    Some((pid, sv[0]))
}

/// The zygote is single-threaded, so forking needs none of the atfork handlers (they guard locks
/// another thread could hold). glibc's `_Fork` (2.34+) skips them; ASan's handler alone unlocks
/// every bucket of its stack depot after each fork, writing ~4 MB that the zygote and the new worker
/// then both copy on write.
unsafe fn fork_bare() -> libc::pid_t {
    type ForkFn = unsafe extern "C" fn() -> libc::pid_t;
    static F: std::sync::OnceLock<Option<ForkFn>> = std::sync::OnceLock::new();
    let f = F.get_or_init(|| {
        if std::env::var_os("STITCH_ATFORK").is_some() {
            return None;
        }
        let p = libc::dlsym(libc::RTLD_DEFAULT, c"_Fork".as_ptr());
        if p.is_null() { None } else { Some(std::mem::transmute::<*mut libc::c_void, ForkFn>(p)) }
    });
    match f {
        Some(f) => f(),
        None => libc::fork(),
    }
}

unsafe fn zygote_main(ctl: i32, shm: *mut u8) -> ! {
    let mut op = [0u8; 1];
    loop {
        if !read_exact(ctl, &mut op) {
            // the fuzzer is done: wait for the workers still exiting, then go
            while libc::waitpid(-1, std::ptr::null_mut(), 0) > 0 {}
            libc::_exit(0);
        }
        match op[0] {
            Z_FORK => {
                while libc::waitpid(-1, std::ptr::null_mut(), libc::WNOHANG) > 0 {}
                let mut cmd = [-1i32; 2];
                let mut done = [-1i32; 2];
                let mut pid = -1;
                if libc::pipe(cmd.as_mut_ptr()) == 0 && libc::pipe(done.as_mut_ptr()) == 0 {
                    pid = fork_bare();
                    if pid == 0 {
                        libc::close(ctl);
                        libc::close(cmd[1]);
                        libc::close(done[0]);
                        worker_loop(shm, cmd[0], done[1]);
                    }
                }
                libc::close(cmd[0]);
                libc::close(done[1]);
                let ok = send_fds(ctl, &pid.to_le_bytes(), &[cmd[1], done[0]]);
                libc::close(cmd[1]);
                libc::close(done[0]);
                if !ok {
                    libc::_exit(1);
                }
            }
            Z_WAIT => {
                let mut b = [0u8; 4];
                let mut status = 0i32;
                if read_exact(ctl, &mut b) {
                    libc::waitpid(i32::from_le_bytes(b), &mut status, 0);
                }
                if !write_all(ctl, &status.to_le_bytes()) {
                    libc::_exit(1);
                }
            }
            Z_DICT => {
                let mut h = [0u8; 8];
                if !read_exact(ctl, &mut h) {
                    libc::_exit(1);
                }
                let dt = u32::from_le_bytes(h[..4].try_into().unwrap()) as usize;
                let mut tok = vec![0u8; u32::from_le_bytes(h[4..].try_into().unwrap()) as usize];
                if !read_exact(ctl, &mut tok) {
                    libc::_exit(1);
                }
                ffi::stitch_dict_add(dt as libc::c_int, tok.as_ptr(), tok.len());
            }
            _ => libc::_exit(1),
        }
    }
}

unsafe fn write_all(fd: i32, mut b: &[u8]) -> bool {
    while !b.is_empty() {
        let n = libc::write(fd, b.as_ptr() as *const libc::c_void, b.len());
        if n < 0 && std::io::Error::last_os_error().kind() == std::io::ErrorKind::Interrupted {
            continue;
        }
        if n <= 0 {
            return false;
        }
        b = &b[n as usize..];
    }
    true
}

unsafe fn read_exact(fd: i32, b: &mut [u8]) -> bool {
    let mut off = 0;
    while off < b.len() {
        let n = libc::read(fd, b[off..].as_mut_ptr() as *mut libc::c_void, b.len() - off);
        if n < 0 && std::io::Error::last_os_error().kind() == std::io::ErrorKind::Interrupted {
            continue;
        }
        if n <= 0 {
            return false;
        }
        off += n as usize;
    }
    true
}

/// Send `data` with file descriptors attached (SCM_RIGHTS).
unsafe fn send_fds(sock: i32, data: &[u8], fds: &[i32]) -> bool {
    let bytes = (fds.len() * std::mem::size_of::<i32>()) as u32;
    let mut cbuf = vec![0u8; libc::CMSG_SPACE(bytes) as usize];
    let mut iov = libc::iovec { iov_base: data.as_ptr() as *mut libc::c_void, iov_len: data.len() };
    let mut msg: libc::msghdr = std::mem::zeroed();
    msg.msg_iov = &mut iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf.as_mut_ptr() as *mut libc::c_void;
    msg.msg_controllen = cbuf.len() as _;
    let c = libc::CMSG_FIRSTHDR(&msg);
    (*c).cmsg_level = libc::SOL_SOCKET;
    (*c).cmsg_type = libc::SCM_RIGHTS;
    (*c).cmsg_len = libc::CMSG_LEN(bytes) as _;
    std::ptr::copy_nonoverlapping(fds.as_ptr() as *const u8, libc::CMSG_DATA(c), bytes as usize);
    loop {
        let n = libc::sendmsg(sock, &msg, 0);
        if n < 0 && std::io::Error::last_os_error().kind() == std::io::ErrorKind::Interrupted {
            continue;
        }
        return n == data.len() as isize;
    }
}

/// Receive exactly `data.len()` bytes and the `nfds` descriptors sent with them.
unsafe fn recv_fds(sock: i32, data: &mut [u8], nfds: usize) -> Option<Vec<i32>> {
    let bytes = (nfds * std::mem::size_of::<i32>()) as u32;
    let mut cbuf = vec![0u8; libc::CMSG_SPACE(bytes) as usize];
    let mut iov = libc::iovec { iov_base: data.as_mut_ptr() as *mut libc::c_void, iov_len: data.len() };
    let mut msg: libc::msghdr = std::mem::zeroed();
    msg.msg_iov = &mut iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf.as_mut_ptr() as *mut libc::c_void;
    msg.msg_controllen = cbuf.len() as _;
    let n = loop {
        let n = libc::recvmsg(sock, &mut msg, 0);
        if n < 0 && std::io::Error::last_os_error().kind() == std::io::ErrorKind::Interrupted {
            continue;
        }
        break n;
    };
    if n <= 0 {
        return None;
    }
    let mut fds = vec![];
    let mut c = libc::CMSG_FIRSTHDR(&msg);
    while !c.is_null() {
        if (*c).cmsg_level == libc::SOL_SOCKET && (*c).cmsg_type == libc::SCM_RIGHTS {
            let k = ((*c).cmsg_len as usize - libc::CMSG_LEN(0) as usize) / std::mem::size_of::<i32>();
            for i in 0..k {
                fds.push((libc::CMSG_DATA(c) as *const i32).add(i).read_unaligned());
            }
        }
        c = libc::CMSG_NXTHDR(&msg, c);
    }
    // the descriptors arrive with the first byte; a stream socket may split the rest
    if (n as usize) < data.len() && !read_exact(sock, &mut data[n as usize..]) {
        fds.iter().for_each(|&f| { libc::close(f); });
        return None;
    }
    if fds.len() != nfds || msg.msg_flags & libc::MSG_CTRUNC != 0 {
        fds.iter().for_each(|&f| { libc::close(f); });
        return None;
    }
    Some(fds)
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
            self.retire(false);
            self.reap_retired(false);
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
                    self.retire(true);
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
