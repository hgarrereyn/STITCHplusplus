// objc_exception_throw, interposed: records the stack of each throw (for the few most recent
// exceptions), then throws with the runtime's implementation (libobjc2). This definition is the
// one the dynamic linker binds, for GNUstep Base too, so -raise and @throw both pass through it.
//
// Plain C on purpose: a frame compiled as Objective-C++ gets the Objective-C++ personality, which
// makes an exception that was caught and thrown again uncatchable by @catch further up.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

#define STITCH_THROWN 8
#define STITCH_THROWN_PCS 64

static struct {
    void* exc;
    int n;
    void* pcs[STITCH_THROWN_PCS];
} g_thrown[STITCH_THROWN];
static unsigned g_next;

typedef void (*stitch_throw_fn)(void*);

__attribute__((noreturn)) void objc_exception_throw(void* exc) {
    static stitch_throw_fn real;
    if (!real) real = (stitch_throw_fn)dlsym(RTLD_NEXT, "objc_exception_throw");
    unsigned i = g_next++ % STITCH_THROWN;
    g_thrown[i].exc = exc;
    g_thrown[i].n = backtrace(g_thrown[i].pcs, STITCH_THROWN_PCS);
    if (!real) {
        fprintf(stderr, "[stitch] objc_exception_throw: no runtime implementation\n");
        abort();
    }
    real(exc);
    __builtin_unreachable();
}

// The stack recorded when `exc` was last thrown, innermost first, without this function's frame;
// 0 if it was not recorded.
int stitch_objc_thrown_stack(void* exc, void** pcs, int max) {
    for (unsigned k = 0; k < STITCH_THROWN; k++) {
        unsigned i = (g_next + STITCH_THROWN - 1 - k) % STITCH_THROWN;  // most recent first
        if (g_thrown[i].exc != exc || g_thrown[i].n <= 1) continue;
        int n = 0;
        for (int j = 1; j < g_thrown[i].n && n < max; j++) pcs[n++] = g_thrown[i].pcs[j];
        return n;
    }
    return 0;
}
