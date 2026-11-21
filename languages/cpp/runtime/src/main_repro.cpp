// Standalone reproducer driver: runs .stp programs against the harness, no fuzzing engine.
//
//   repro [--trace] [--plan FILE] PROGRAM.stp...   run the programs in this process
//   repro --each [--timeout SECS] PROGRAM.stp...    run each program in a forked child; print one
//                                                   JSON summary line (used by `stitch cov`)
//   repro --manifest                                print the stub manifest
//   --files-from LIST                               also the programs listed in LIST (one per line)
//
// In a coverage build (-fprofile-instr-generate), --each children write their counters when they
// exit, crash or time out, so one crashing program does not lose the coverage of the others.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <unistd.h>
#include <fstream>
#include <string>
#include <vector>
#include "stitch_abi.h"

extern "C" int __llvm_profile_write_file(void) __attribute__((weak));
extern "C" void __llvm_profile_reset_counters(void) __attribute__((weak));

static void flush_and_exit(int sig) {
    if (__llvm_profile_write_file) __llvm_profile_write_file();
    _exit(128 + sig);
}

static void child_setup(unsigned timeout) {
    if (__llvm_profile_reset_counters) __llvm_profile_reset_counters();  // count this program only
    int sigs[] = {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL, SIGALRM, SIGTRAP};
    for (int s : sigs) signal(s, flush_and_exit);
    if (timeout) alarm(timeout);
}

static int run_each(const std::vector<std::string>& files, unsigned timeout) {
    int n = 0, ok = 0, crashed = 0, timeouts = 0, invalid = 0;
    for (const std::string& file : files) {
        n++;
        fflush(stdout);
        fflush(stderr);
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            child_setup(timeout);
            int r = stitch_run_file(file.c_str(), 0);
            exit(r != 0 ? 2 : 0);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        int code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        if (code == 0) ok++;
        else if (code == 2) invalid++;
        else if (code == 128 + SIGALRM) timeouts++;
        else crashed++;
    }
    printf("{\"programs\":%d,\"ok\":%d,\"crashed\":%d,\"timeouts\":%d,\"invalid\":%d}\n", n, ok, crashed, timeouts, invalid);
    return 0;
}

int main(int argc, char** argv) {
#ifdef __linux__
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);  // no core dumps (see the engine's real_main)
#endif
    unsigned flags = 0, timeout = 10;
    bool each = false;
    int nfiles = 0;
    std::vector<std::string> listed;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trace")) flags |= 1;
        else if (!strcmp(argv[i], "--each")) each = true;
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) { timeout = (unsigned)atoi(argv[i + 1]); argv[i + 1] = (char*)"-"; }
        else if (!strcmp(argv[i], "--plan") && i + 1 < argc) { stitch_set_plan(argv[i + 1]); argv[i + 1] = (char*)"-"; }
        else if (!strcmp(argv[i], "--files-from") && i + 1 < argc) {
            std::ifstream in(argv[i + 1]);
            for (std::string l; std::getline(in, l);)
                if (!l.empty()) listed.push_back(l);
            argv[i + 1] = (char*)"-";
        }
        else if (!strcmp(argv[i], "--manifest")) { puts(stitch_manifest()); return 0; }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            fprintf(stderr, "usage: %s [--trace] [--plan FILE] [--each [--timeout SECS]] [--manifest] PROGRAM.stp...\n", argv[0]);
            return 0;
        }
    }
    stitch_init_once();
    std::vector<std::string> files;
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-') files.push_back(argv[i]);
    files.insert(files.end(), listed.begin(), listed.end());
    if (each) return run_each(files, timeout);
    for (const std::string& f : files) {
        nfiles++;
        if (flags & 1) fprintf(stderr, "[stitch] running %s\n", f.c_str());
        if (stitch_run_file(f.c_str(), flags) != 0) return 2;
    }
    if (!nfiles) fprintf(stderr, "usage: %s [--trace] [--plan FILE] [--each [--timeout SECS]] [--manifest] PROGRAM.stp...\n", argv[0]);
    return 0;
}
