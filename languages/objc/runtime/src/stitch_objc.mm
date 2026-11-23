// STITCH++ Objective-C layer: implements Stitch.h on the C/C++ runtime (languages/cpp/runtime).
// Compiled as Objective-C++ with ARC, AddressSanitizer and no coverage instrumentation.
//
// The runtime calls endpoints and decoders through plain function pointers, so each registered
// stub / decoder gets a trampoline by index. A stub runs inside an autorelease pool; STBail throws
// a C++ exception that becomes the runtime's bail, and an Objective-C exception that escapes the
// stub is a bug: it is reported with the stack of the place that threw it, which
// objc_exception_throw (interposed in stitch_objc_throw.c) records.
#import <Foundation/Foundation.h>

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "stitch.h"
#include "stitch_abi.h"
// the C++ API's declaration macros; Stitch.h defines the Objective-C ones
#undef STITCH_OBJECT
#undef STITCH_DATATYPE
#undef STITCH_DECODER
#undef STITCH_ENDPOINT
#undef STITCH_INIT
#import "Stitch/Stitch.h"

#include <objc/runtime.h>

extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void*);
extern "C" void __sanitizer_symbolize_pc(void* pc, const char* fmt, char* out, size_t n) __attribute__((weak));
extern "C" int stitch_objc_thrown_stack(void* exc, void** pcs, int max);  // stitch_objc_throw.c
extern "C" void __sanitizer_weak_hook_memmem(void*, const void*, size_t, const void*, size_t, void*);  // runtime.cpp

using namespace stitch;
using namespace stitch::detail;

// gives an STSource its runtime Source (defined in @implementation STSource: it sets the ivars)
void stitch_objc_source_wrap(STSource* s, Source* src, bool owned);

namespace {

struct ObjCBail {  // thrown by STBail; the stub's caller turns it into a bail
    std::string reason;
};
struct Diverged {  // a reproducer call that does not behave as in the original run
    std::string why;
};

struct Ep {
    std::string name;
    void (*invoke)(void*);
};
struct Dec {
    std::string name;
    id (*fn)(STSource*);
};

std::vector<Ep>& eps() {
    static auto* v = new std::vector<Ep>();
    return *v;
}
std::vector<Dec>& decs() {
    static auto* v = new std::vector<Dec>();
    return *v;
}
std::map<std::string, int>& obj_types() {
    static auto* m = new std::map<std::string, int>();
    return *m;
}

Ctx* g_ctx = nullptr;               // the call in progress
std::vector<std::string> g_cstrs;   // CStr values of the call in progress
bool g_repro = false;               // STReproMain is running the program
int g_repro_node = -1;
std::string g_repro_name = "main";
bool g_repro_trace = getenv("STITCH_REPRO_TRACE") != nullptr;

const char* keep(const char* s) { return s ? strdup(s) : nullptr; }
Ctx& C(void* ctx) { return *static_cast<Ctx*>(ctx); }

uint32_t fnv1a(const void* p, size_t n, uint32_t h = 0x811c9dc5u) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x01000193u;
    return h;
}

// ------------------------------------------------------------------------- objects and text
void free_box(void* storage) {
    void* p = *(void**)storage;
    if (p) {
        id o = (__bridge_transfer id)p;  // the pool's reference
        (void)o;
    }
    delete (void**)storage;
}

int object_type(const char* name) {
    auto& m = obj_types();
    auto it = m.find(name);
    if (it != m.end()) return it->second;
    auto* ops = new TypeOps{keep(name), sizeof(void*), true, &free_box, nullptr};
    int id = register_object(ops);
    m[name] = id;
    return id;
}

// UTF-8 with invalid sequences replaced by U+FFFD (NSString rejects invalid UTF-8 outright)
NSString* lossy_utf8(const uint8_t* b, size_t n) {
    NSString* s = [[NSString alloc] initWithBytes:b length:n encoding:NSUTF8StringEncoding];
    if (s) return s;
    std::u16string out;
    size_t i = 0;
    while (i < n) {
        uint8_t c = b[i];
        uint32_t cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0) len = 2, cp = c & 0x1F;
        else if ((c & 0xF0) == 0xE0) len = 3, cp = c & 0x0F;
        else if ((c & 0xF8) == 0xF0) len = 4, cp = c & 0x07;
        else len = 0;
        bool ok = len > 0 && i + len <= n;
        for (size_t k = 1; ok && k < len; k++) {
            if ((b[i + k] & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (b[i + k] & 0x3F);
        }
        if (ok && len > 1) {
            static const uint32_t min[] = {0, 0, 0x80, 0x800, 0x10000};
            if (cp < min[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) ok = false;
        }
        if (!ok) {
            out.push_back(0xFFFD);
            i += 1;
            continue;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back((char16_t)(0xD800 + (cp >> 10)));
            out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back((char16_t)cp);
        }
        i += len;
    }
    return [NSString stringWithCharacters:(const unichar*)out.data() length:out.size()];
}

std::string utf8(NSString* s) {
    if (!s) return "";
    NSData* d = [s dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:YES];
    return std::string((const char*)d.bytes, d.length);
}

std::vector<uint8_t> as_bytes(id v) {
    if (!v || v == [NSNull null]) return {};
    if ([v isKindOfClass:[NSData class]]) {
        NSData* d = v;
        return std::vector<uint8_t>((const uint8_t*)d.bytes, (const uint8_t*)d.bytes + d.length);
    }
    std::string t = utf8([v isKindOfClass:[NSString class]] ? (NSString*)v : [v description]);
    return std::vector<uint8_t>(t.begin(), t.end());
}

// ------------------------------------------------------------------------- exception stacks
bool runtime_module(void* pc) {
    Dl_info info;
    if (!dladdr(pc, &info) || !info.dli_fname) return false;
    return strstr(info.dli_fname, "libobjc") || strstr(info.dli_fname, "libgnustep-base");
}

std::string one_line(const std::string& s) {
    std::string o;
    for (char c : s) o += (c == '\n' || c == '\r') ? ' ' : c;
    return o.size() > 500 ? o.substr(0, 500) : o;
}

// "[stitch] EXCEPTION in <stub>: <name>: <reason>" and the frames of the throw, innermost first;
// `key` groups the finding: the exception name and the first frames outside the Objective-C
// runtime and GNUstep Base (as module offsets: no symbolization while fuzzing)
std::string exception_report(NSException* e, const std::string& where, uint32_t* key) {
    std::string name = utf8(e.name), reason = one_line(utf8(e.reason));
    std::string out = "\n[stitch] EXCEPTION in " + where + ": " + name + ": " + reason + "\n";
    std::vector<void*> pcs(64);
    pcs.resize(stitch_objc_thrown_stack((__bridge void*)e, pcs.data(), (int)pcs.size()));
    if (pcs.empty()) {
        for (NSNumber* n in e.callStackReturnAddresses) pcs.push_back((void*)(uintptr_t)n.unsignedLongLongValue);
    }
    uint32_t h = fnv1a(name.data(), name.size());
    int keyed = 0;
    for (void* pc : pcs) {
        if (keyed >= 3) break;
        if (runtime_module(pc)) continue;
        Dl_info info;
        uintptr_t off = (uintptr_t)pc;
        if (dladdr(pc, &info) && info.dli_fbase) off -= (uintptr_t)info.dli_fbase;
        h = fnv1a(&off, sizeof off, h);
        keyed++;
    }
    *key = h & 0x7fffffffu;
    if (g_ctx && getenv("STITCH_FUZZING") && !tracing()) return out;  // a finding while fuzzing: not printed
    int n = 0;
    for (size_t i = 0; i < pcs.size() && n < 40; i++) {
        char buf[4096] = {0};
        void* pc = (char*)pcs[i] - 1;  // return addresses: symbolize the call instruction
        if (__sanitizer_symbolize_pc) __sanitizer_symbolize_pc(pc, "%p %F %L", buf, sizeof buf - 1);
        else snprintf(buf, sizeof buf, "%p", pc);
        // one frame per function inlined at this address (each NUL-terminated, innermost first)
        for (const char* f = buf; *f && f < buf + sizeof buf; f += strlen(f) + 1) {
            if (strstr(f, " in stitch_invoke_")) return out + "\n";  // the generated trampoline: harness glue below
            out += "    #" + std::to_string(n++) + " " + f + "\n";
        }
    }
    return out + "\n";
}

bool exceptions_bail() {
    static int v = -1;
    if (v < 0) {
        const char* p = getenv("STITCH_EXCEPTIONS");
        v = p && strcmp(p, "bail") == 0;
    }
    return v == 1;
}

// ------------------------------------------------------------------------- trampolines
void run_endpoint(int i, Ctx& c) {
    Ctx* saved_ctx = g_ctx;
    g_ctx = &c;
    bool bailed = false, raised = false;
    std::string reason, report, exc_name;
    uint32_t key = 0;
    void* pool = objc_autoreleasePoolPush();
    try {
        @try {
            eps()[i].invoke(&c);
        } @catch (NSException* e) {
            raised = true;
            exc_name = utf8(e.name);
            report = exception_report(e, eps()[i].name, &key);
        }
    } catch (ObjCBail& b) {
        bailed = true;
        reason = b.reason;
    } catch (...) {  // the runtime's own signals (a failed check while fuzzing)
        objc_autoreleasePoolPop(pool);
        g_ctx = saved_ctx;
        g_cstrs.clear();
        throw;
    }
    objc_autoreleasePoolPop(pool);
    g_ctx = saved_ctx;
    g_cstrs.clear();
    static std::string bail_reason;  // the runtime interns the reason after the bail unwinds
    if (bailed) {
        bail_reason = reason;
        bail(bail_reason.c_str());
    }
    if (raised) {
        if (exceptions_bail()) {
            bail_reason = exc_name;
            bail(bail_reason.c_str());
        }
        report_finding(key, report.c_str());
    }
}

std::vector<uint8_t> run_decoder(int i, void* src) {
    STSource* s = [[STSource alloc] init];
    stitch_objc_source_wrap(s, static_cast<Source*>(src), false);
    if (g_ctx) return as_bytes(decs()[i].fn(s));  // inside a call: exceptions are the call's
    std::vector<uint8_t> out;
    @try {
        @autoreleasepool {
            out = as_bytes(decs()[i].fn(s));
        }
    } @catch (NSException* e) {
        uint32_t key;
        std::string r = exception_report(e, "decoder " + decs()[i].name, &key);
        fputs(r.c_str(), stderr);
        abort();
    }
    return out;
}

template <int I>
void invoke_tramp(Ctx& c) { run_endpoint(I, c); }
template <int I>
std::vector<uint8_t> decode_tramp(void* src) { return run_decoder(I, src); }

constexpr int kMaxEndpoints = 1024;
constexpr int kMaxDecoders = 256;
template <size_t... I>
constexpr std::array<Invoker, sizeof...(I)> invokers(std::index_sequence<I...>) { return {&invoke_tramp<(int)I>...}; }
template <size_t... I>
constexpr std::array<std::vector<uint8_t> (*)(void*), sizeof...(I)> decoders(std::index_sequence<I...>) {
    return {&decode_tramp<(int)I>...};
}
const auto kInvokers = invokers(std::make_index_sequence<kMaxEndpoints>{});
const auto kDecoders = decoders(std::make_index_sequence<kMaxDecoders>{});

id decoder_value(const char* datatype, STSource* s) {
    for (auto& d : decs())
        if (d.name == datatype) return d.fn(s);
    fprintf(stderr, "[stitch] error: no decoder for data type %s (STITCH_DECODER(%s))\n", datatype, datatype);
    abort();
}

int slot_of(id obj) { return slot_for_ptr((__bridge void*)obj); }

// Objective-C exceptions that nothing catches (reproducers reduced to plain code)
void uncaught(NSException* e) {
    uint32_t key;
    std::string r = exception_report(e, g_repro_name, &key);
    fputs(r.c_str(), stderr);
    fflush(stderr);
    abort();
}

__attribute__((constructor)) void install_uncaught_handler() { NSSetUncaughtExceptionHandler(&uncaught); }

// ------------------------------------------------------------------------- string comparisons
// NSString comparisons run in GNUstep Base, which has no comparison instrumentation. Every NSString
// class that implements one of these methods gets a wrapper that calls its own implementation and,
// while comparisons are recorded, reports the operands of a failed comparison (first 32 UTF-8
// bytes each), like the memcmp / strcmp hooks of C code.
void note_strings(id a, id b) {
    if (![a isKindOfClass:[NSString class]] || ![b isKindOfClass:[NSString class]]) return;
    @autoreleasepool {
        const char* x = [(NSString*)a UTF8String];
        const char* y = [(NSString*)b UTF8String];
        if (x && y) __sanitizer_weak_hook_memmem(nullptr, x, strnlen(x, 32), y, strnlen(y, 32), nullptr);
    }
}

using BoolCmp = BOOL (*)(id, SEL, id);
using OrderCmp = NSComparisonResult (*)(id, SEL, id);
using RangeCmp = NSRange (*)(id, SEL, id);

void wrap_string_methods() {
    Class ns_string = [NSString class];
    const char* bool_sels[] = {"isEqualToString:", "isEqual:", "hasPrefix:", "hasSuffix:", "containsString:"};
    const char* order_sels[] = {"compare:", "caseInsensitiveCompare:"};
    int n = objc_getClassList(nullptr, 0);
    if (n <= 0) return;
    __unsafe_unretained Class* classes = (__unsafe_unretained Class*)calloc((size_t)n, sizeof(Class));
    n = objc_getClassList(classes, n);
    for (int i = 0; i < n; i++) {
        Class c = classes[i];
        bool is_string = false;
        for (Class s = c; s; s = class_getSuperclass(s))
            if (s == ns_string) is_string = true;
        if (!is_string) continue;
        unsigned count = 0;
        Method* own = class_copyMethodList(c, &count);  // methods this class implements itself
        for (unsigned k = 0; k < count; k++) {
            SEL sel = method_getName(own[k]);
            const char* name = sel_getName(sel);
            IMP orig = method_getImplementation(own[k]);
            IMP wrapper = nullptr;
            for (const char* s : bool_sels)
                if (strcmp(name, s) == 0)
                    wrapper = imp_implementationWithBlock(^BOOL(id self, id other) {
                        BOOL r = ((BoolCmp)orig)(self, sel, other);
                        if (!r && other && stitch_cmplog_active()) note_strings(self, other);
                        return r;
                    });
            for (const char* s : order_sels)
                if (strcmp(name, s) == 0)
                    wrapper = imp_implementationWithBlock(^NSComparisonResult(id self, id other) {
                        NSComparisonResult r = ((OrderCmp)orig)(self, sel, other);
                        if (r != NSOrderedSame && other && stitch_cmplog_active()) note_strings(self, other);
                        return r;
                    });
            if (strcmp(name, "rangeOfString:") == 0)
                wrapper = imp_implementationWithBlock(^NSRange(id self, id other) {
                    NSRange r = ((RangeCmp)orig)(self, sel, other);
                    if (r.location == NSNotFound && other && stitch_cmplog_active()) note_strings(self, other);
                    return r;
                });
            if (wrapper) method_setImplementation(own[k], wrapper);
        }
        free(own);
    }
    free(classes);
}

__attribute__((constructor)) void install_string_hooks() {
    @autoreleasepool {
        wrap_string_methods();
    }
}

}  // namespace

// ============================================================================================
// Source
// ============================================================================================
@implementation STSource {
    Source* _src;
    bool _owned;
}

- (void)dealloc {
    if (_owned) delete _src;
}

- (uint8_t)u8 { return _src->u8(); }
- (uint16_t)u16 { return _src->u16(); }
- (uint32_t)u32 { return _src->u32(); }
- (uint64_t)u64 { return _src->u64(); }
- (int8_t)i8 { return _src->i8(); }
- (int16_t)i16 { return _src->i16(); }
- (int32_t)i32 { return _src->i32(); }
- (int64_t)i64 { return _src->i64(); }
- (float)f32 { return _src->f32(); }
- (double)f64 { return _src->f64(); }
- (float)f32:(float)lo to:(float)hi { return _src->f32(lo, hi); }
- (double)f64:(double)lo to:(double)hi { return _src->f64(lo, hi); }
- (int64_t)range:(int64_t)lo to:(int64_t)hi { return _src->range(lo, hi); }
- (NSUInteger)choice:(NSUInteger)n { return n == 0 ? 0 : (NSUInteger)_src->choice(n); }
- (id)pick:(NSArray*)items {
    if (items.count == 0) return nil;
    return items[(NSUInteger)_src->choice(items.count)];
}
- (BOOL)coin { return _src->coin(0.5); }
- (BOOL)coin:(double)p { return _src->coin(p); }
// the loop is identified by its call site and the caller's stack depth (as Source::more in C++)
- (BOOL)more {
    uintptr_t x = (uintptr_t)__builtin_return_address(0);
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    return source_more_at(*_src, (uint32_t)x, (uintptr_t)__builtin_frame_address(0)) ? YES : NO;
}
- (NSString*)str:(NSUInteger)max {
    std::string v = _src->str(max);
    return lossy_utf8((const uint8_t*)v.data(), v.size());
}
- (NSData*)bytes:(NSUInteger)max {
    auto v = _src->bytes(max);
    return [NSData dataWithBytes:v.data() length:v.size()];
}
- (NSData*)fixed:(NSUInteger)n {
    auto v = _src->fixed(n);
    return [NSData dataWithBytes:v.data() length:v.size()];
}
- (NSData*)rest {
    auto v = _src->rest();
    return [NSData dataWithBytes:v.data() length:v.size()];
}
- (NSString*)token {
    std::string v = _src->token();
    return lossy_utf8((const uint8_t*)v.data(), v.size());
}
- (id)draw:(NSString*)datatype { return decoder_value(utf8(datatype).c_str(), self); }
- (NSUInteger)position { return _src->position(); }
- (NSUInteger)remaining { return _src->remaining(); }
- (BOOL)exhausted { return _src->exhausted() ? YES : NO; }

void stitch_objc_source_wrap(STSource* s, Source* src, bool owned) {
    s->_src = src;
    s->_owned = owned;
}
@end

// ============================================================================================
// C API
// ============================================================================================
extern "C" {

// ----------------------------------------------------------------------------- registration
void stitch_objc_object(const char* name) { object_type(name); }
void stitch_objc_datatype(const char* name) { register_datatype(keep(name)); }

void stitch_objc_decoder(const char* name, id (*fn)(STSource*)) {
    for (auto& d : decs())
        if (d.name == name) {
            fprintf(stderr, "[stitch] error: two decoders for data type %s\n", name);
            abort();
        }
    if ((int)decs().size() >= kMaxDecoders) {
        fprintf(stderr, "[stitch] error: more than %d decoders\n", kMaxDecoders);
        abort();
    }
    decs().push_back(Dec{name, fn});
    register_decoder(keep(name), kDecoders[decs().size() - 1]);
}

void stitch_objc_init(void (*fn)(void)) { register_init(fn); }

void stitch_objc_endpoint(const char* name, const char* file, void (*invoke)(void*), int32_t n_args,
                          const uint8_t* arg_is_obj, const char* const* arg_names, const char* const* in_types,
                          const uint8_t* in_take, int32_t n_out, const char* const* out_types, const uint8_t* out_opt,
                          const stitch_objc_param* params) {
    if ((int)eps().size() >= kMaxEndpoints) {
        fprintf(stderr, "[stitch] error: more than %d stubs\n", kMaxEndpoints);
        abort();
    }
    std::vector<int> it, ot;
    std::vector<bool> take, opt;
    std::vector<ParamDesc> ps;
    std::vector<uint8_t> order(arg_is_obj, arg_is_obj + n_args);
    int ni = 0, np = 0;
    for (int i = 0; i < n_args; i++) {
        if (arg_is_obj[i]) {
            it.push_back(object_type(in_types[ni]));
            take.push_back(in_take[ni] != 0);
            ni++;
        } else {
            const stitch_objc_param& p = params[np++];
            ParamDesc d{};
            d.kind = (PKind)p.kind;
            d.size = p.size;
            d.is_signed = p.is_signed != 0;
            d.lo = p.lo;
            d.hi = p.hi;
            if (p.values) d.values.assign(p.values, p.values + p.n_values);
            d.datatype = keep(p.datatype);
            if (d.datatype) register_datatype(d.datatype);
            d.ctype = keep(p.ctype);
            ps.push_back(d);
        }
    }
    for (int i = 0; i < n_out; i++) {
        ot.push_back(object_type(out_types[i]));
        opt.push_back(out_opt[i] != 0);
    }
    const char* ename = keep(name);
    eps().push_back(Ep{name, invoke});
    register_endpoint(ename, keep(file), kInvokers[eps().size() - 1], it, take, ot, opt, ps, order);
    std::vector<std::string> names;
    for (int i = 0; i < n_args; i++) names.push_back(arg_names && arg_names[i] ? arg_names[i] : "");
    set_param_names(ename, names);
}

// ----------------------------------------------------------------------------- arguments
void* stitch_objc_in(void* ctx, int32_t k) { return *(void**)C(ctx).in[k]; }

uint64_t stitch_objc_bits(void* ctx, int32_t k) {
    uint64_t v = 0;
    memcpy(&v, C(ctx).pbuf[k], C(ctx).plen[k] < 8 ? C(ctx).plen[k] : 8);
    return v;
}
double stitch_objc_f64(void* ctx, int32_t k) {
    uint64_t b = stitch_objc_bits(ctx, k);
    double d;
    memcpy(&d, &b, 8);
    return d;
}
float stitch_objc_f32(void* ctx, int32_t k) {
    uint32_t b = (uint32_t)stitch_objc_bits(ctx, k);
    float f;
    memcpy(&f, &b, 4);
    return f;
}
long long stitch_objc_range(void* ctx, int32_t k, long long lo, long long hi) {
    long long raw = (long long)stitch_objc_bits(ctx, k);
    if (raw >= lo && raw <= hi) return raw;
    unsigned long long span = (unsigned long long)hi - (unsigned long long)lo + 1ull;
    unsigned long long off = (unsigned long long)raw - (unsigned long long)lo;
    return (long long)((unsigned long long)lo + (span ? off % span : off));
}
long long stitch_objc_oneof(void* ctx, int32_t k, const long long* values, int32_t n) {
    long long raw = (long long)stitch_objc_bits(ctx, k);
    for (int i = 0; i < n; i++)
        if (values[i] == raw) return raw;
    return n > 0 ? values[(unsigned long long)raw % (unsigned long long)n] : 0;
}
long long stitch_objc_flags(void* ctx, int32_t k, long long mask) { return (long long)stitch_objc_bits(ctx, k) & mask; }

static std::vector<uint8_t> decoded(void* ctx, int32_t k, size_t max, const char* datatype) {
    Ctx& c = C(ctx);
    return decode_param(datatype_id(datatype), c.pbuf[k], c.plen[k], c.pform[k], max);
}
NSString* stitch_objc_str(void* ctx, int32_t k, size_t max, const char* datatype) {
    auto v = decoded(ctx, k, max, datatype);
    return lossy_utf8(v.data(), v.size());
}
const char* stitch_objc_cstr(void* ctx, int32_t k, size_t max, const char* datatype) {
    auto v = decoded(ctx, k, max, datatype);
    g_cstrs.emplace_back(v.begin(), v.end());
    return g_cstrs.back().c_str();
}
NSData* stitch_objc_bytes(void* ctx, int32_t k, size_t max, const char* datatype) {
    auto v = decoded(ctx, k, max, datatype);
    return [NSData dataWithBytes:v.data() length:v.size()];
}
NSString* stitch_objc_file(void* ctx, int32_t k, size_t max, const char* datatype) {
    auto v = decoded(ctx, k, max, datatype);
    std::string p = write_temp_file(v.data(), v.size());
    return [NSString stringWithUTF8String:p.c_str()];
}
void stitch_objc_raw(void* ctx, int32_t k, void* out, size_t n) {
    memset(out, 0, n);
    memcpy(out, C(ctx).pbuf[k], C(ctx).plen[k] < n ? C(ctx).plen[k] : n);
}
id stitch_objc_val(void* ctx, int32_t k, const char* datatype, size_t max) {
    Ctx& c = C(ctx);
    QuietDraws q;  // the decoder's draws are not stub draws in plans
    size_t n = c.plen[k] < max ? c.plen[k] : max;
    STSource* s = [[STSource alloc] init];
    stitch_objc_source_wrap(s, new Source(c.pbuf[k], n, max), true);
    return decoder_value(datatype, s);
}
STSource* stitch_objc_src(void* ctx, int32_t k, size_t max) {
    Ctx& c = C(ctx);
    size_t n = c.plen[k] < max ? c.plen[k] : max;
    STSource* s = [[STSource alloc] init];
    stitch_objc_source_wrap(s, new Source(c.pbuf[k], n, max), true);
    return s;
}

// ----------------------------------------------------------------------------- outputs
void stitch_objc_out(void* ctx, int32_t k, id obj, int32_t optional) {
    if (!obj) {
        // a missing required output: the runtime treats the null handle as a bail
        if (!optional) add_output(C(ctx), k, new void*(nullptr), nullptr);
        return;
    }
    add_output(C(ctx), k, new void*((__bridge_retained void*)obj), nullptr);
}
void stitch_objc_outs(void* ctx, NSArray* outs, int32_t n, const uint8_t* optional) {
    for (int32_t k = 0; k < n; k++) {
        id v = outs && (NSUInteger)k < outs.count ? outs[k] : nil;
        if (v == [NSNull null]) v = nil;
        stitch_objc_out(ctx, k, v, optional[k]);
    }
}

// ----------------------------------------------------------------------------- control
void STBail(NSString* reason) { throw ObjCBail{reason ? utf8(reason) : "bail"}; }
void STRequire(BOOL cond, NSString* reason) {
    if (!cond) STBail(reason);
}
void STCheck(BOOL cond, NSString* what) {
    if (cond) return;
    std::string w = what ? utf8(what) : "check";
    if (!g_ctx && g_repro) {
        fprintf(stderr, "\n[stitch] CHECK FAILED: %s (node %d)\n", w.c_str(), g_repro_node);
        fflush(stderr);
        exit(1);
    }
    static std::string keep_what;
    keep_what = w;
    check(false, keep_what.c_str());
}
BOOL STTracing(void) { return (tracing() || (g_repro && g_repro_trace)) ? YES : NO; }
void STLog(NSString* format, ...) {
    if (!STTracing()) return;
    va_list ap;
    va_start(ap, format);
    NSString* s = [[NSString alloc] initWithFormat:format arguments:ap];
    va_end(ap);
    if (g_ctx) stitch::log("%s", utf8(s).c_str());
    else fprintf(stderr, "[stitch]     log: %s\n", utf8(s).c_str());
}
void STConsume(id obj) { consume_slot(slot_of(obj)); }

// ----------------------------------------------------------------------------- attributes
void STSetInt(id obj, NSString* key, int64_t v) {
    int s = slot_of(obj);
    std::string k = utf8(key);
    if (s >= 0) attr_set_int(s, k.c_str(), v);
    else pending_set_int((__bridge void*)obj, k.c_str(), v);
}
int64_t STGetIntOr(id obj, NSString* key, int64_t def) {
    int64_t v;
    return attr_get_int(slot_of(obj), utf8(key).c_str(), &v) ? v : def;
}
int64_t STGetInt(id obj, NSString* key) {
    int64_t v;
    std::string k = utf8(key);
    if (attr_get_int(slot_of(obj), k.c_str(), &v)) return v;
    note_missing(k.c_str());
    return 0;
}
void STSetStr(id obj, NSString* key, NSString* v) {
    int s = slot_of(obj);
    std::string k = utf8(key);
    if (s >= 0) attr_set_str(s, k.c_str(), utf8(v));
    else pending_set_str((__bridge void*)obj, k.c_str(), utf8(v));
}
NSString* STGetStr(id obj, NSString* key) {
    std::string v, k = utf8(key);
    if (attr_get_str(slot_of(obj), k.c_str(), &v)) return lossy_utf8((const uint8_t*)v.data(), v.size());
    note_missing(k.c_str());
    return nil;
}
NSString* STGetStrOr(id obj, NSString* key, NSString* def) {
    std::string v;
    if (attr_get_str(slot_of(obj), utf8(key).c_str(), &v)) return lossy_utf8((const uint8_t*)v.data(), v.size());
    return def;
}
void STSetObj(id obj, NSString* key, id target) {
    int s = slot_of(obj), t = slot_of(target);
    std::string k = utf8(key);
    if (s >= 0) attr_set_obj(s, k.c_str(), t);
    else pending_set_obj((__bridge void*)obj, k.c_str(), t);
}
id stitch_objc_get_obj(id obj, NSString* key, const char* type) {
    std::string k = utf8(key);
    void* st = attr_get_obj(slot_of(obj), k.c_str(), object_type(type));
    if (!st || !*(void**)st) {
        note_missing(k.c_str());
        return nil;
    }
    return (__bridge id)(*(void**)st);
}
BOOL STHas(id obj, NSString* key) { return attr_has(slot_of(obj), utf8(key).c_str()) ? YES : NO; }
void STDel(id obj, NSString* key) { attr_del(slot_of(obj), utf8(key).c_str()); }
void STGlobalSet(NSString* key, int64_t v) { global_set(utf8(key).c_str(), v); }
int64_t STGlobalGet(NSString* key) { return global_get(utf8(key).c_str()); }
int64_t STGlobalGetOr(NSString* key, int64_t def) { return global_get(utf8(key).c_str(), def); }
void STGlobalSetStr(NSString* key, NSString* v) { global_set_str(utf8(key).c_str(), utf8(v)); }
NSString* STGlobalGetStr(NSString* key) {
    std::string k = utf8(key);
    if (!global_has(k.c_str())) {
        note_missing(k.c_str());
        return nil;
    }
    std::string v = global_get_str(k.c_str(), "");
    return lossy_utf8((const uint8_t*)v.data(), v.size());
}
BOOL STGlobalHas(NSString* key) { return global_has(utf8(key).c_str()) ? YES : NO; }

// ----------------------------------------------------------------------------- reproducers
int STReproMain(void (^program)(void)) {
    g_repro = true;
    int rc = 0;
    void* pool = objc_autoreleasePoolPush();
    stitch_init_once();  // STITCH_INIT blocks, as before fuzzing
    try {
        @try {
            program();
        } @catch (NSException* e) {
            uncaught(e);
        }
    } catch (Diverged& d) {
        fprintf(stderr, "\n[repro] DIVERGED at call #%d %s: %s\n", g_repro_node, g_repro_name.c_str(), d.why.c_str());
        rc = 99;
    } catch (ObjCBail& b) {
        fprintf(stderr, "\n[repro] DIVERGED at call #%d %s: bailed outside a call: \"%s\"\n", g_repro_node,
                g_repro_name.c_str(), b.reason.c_str());
        rc = 99;
    }
    objc_autoreleasePoolPop(pool);
    return rc;
}

id STCall(int node, const char* name, id (^call)(void)) {
    g_repro_node = node;
    g_repro_name = name;
    id r = nil;
    try {
        r = call();
    } catch (ObjCBail& b) {
        throw Diverged{"bailed: \"" + b.reason + "\" (it succeeded in the original run)"};
    }
    if (g_repro_trace) fprintf(stderr, "[repro] #%d %s -> ok\n", node, name);
    return r;
}

void STCallBails(int node, const char* name, void (^call)(void)) {
    g_repro_node = node;
    g_repro_name = name;
    try {
        call();
    } catch (ObjCBail&) {
    }
    if (g_repro_trace) fprintf(stderr, "[repro] #%d %s -> bail\n", node, name);
}

NSString* STString(const char* bytes, size_t len) { return lossy_utf8((const uint8_t*)bytes, len); }
const char* STCString(const char* bytes, size_t len) {
    char* p = (char*)malloc(len + 1);
    memcpy(p, bytes, len);
    p[len] = 0;
    return p;
}
NSData* STData(const char* bytes, size_t len) { return [NSData dataWithBytes:bytes length:len]; }
NSString* STTempFile(const char* bytes, size_t len) {
    std::string p = write_temp_file((const uint8_t*)bytes, len);
    return [NSString stringWithUTF8String:p.c_str()];
}
STSource* STSourceWith(const char* bytes, size_t len, size_t max) {
    STSource* s = [[STSource alloc] init];
    stitch_objc_source_wrap(s, new Source((const uint8_t*)bytes, len, max > len ? max : len), true);
    return s;
}
id STValue(NSString* datatype, const char* bytes, size_t len, size_t max) {
    QuietDraws q;
    STSource* s = [[STSource alloc] init];
    size_t n = len < max ? len : max;
    stitch_objc_source_wrap(s, new Source((const uint8_t*)bytes, n, max), true);
    return decoder_value(utf8(datatype).c_str(), s);
}

}  // extern "C"
