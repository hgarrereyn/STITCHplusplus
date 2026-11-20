// Standalone reproducer driver: runs .stp programs against the harness, no fuzzing engine.
#include <stdio.h>
#include <string.h>
#include "stitch_abi.h"

int main(int argc, char** argv) {
    unsigned flags = 0;
    int nfiles = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trace")) flags |= 1;
        else if (!strcmp(argv[i], "--manifest")) { puts(stitch_manifest()); return 0; }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            fprintf(stderr, "usage: %s [--trace] [--manifest] PROGRAM.stp...\n", argv[0]);
            return 0;
        }
    }
    stitch_init_once();
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        nfiles++;
        if (flags & 1) fprintf(stderr, "[stitch] running %s\n", argv[i]);
        if (stitch_run_file(argv[i], flags) != 0) return 2;
    }
    if (!nfiles) fprintf(stderr, "usage: %s [--trace] [--manifest] PROGRAM.stp...\n", argv[0]);
    return 0;
}
