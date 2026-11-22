// Swift bridge: implements cstitch.h on top of the C/C++ runtime (languages/cpp/runtime).
// Built with the runtime into libstitch_swift_rt.a (ASan, no coverage instrumentation).
//
// The runtime calls endpoints through plain function pointers (Invoker, decoder fn), so each
// Swift endpoint / decoder gets a trampoline by index. Swift reports a bail or a failed check by
// return code; the trampoline turns it into the runtime's bail / check, so no C++ exception ever
// crosses Swift frames.
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cstitch.h"
#include "stitch.h"

using namespace stitch;
using namespace stitch::detail;

namespace {
int32_t (*g_invoke)(int32_t, void*) = nullptr;
void (*g_decode)(int32_t, void*, void*) = nullptr;
void (*g_release)(void*) = nullptr;
std::string g_reason;
// per-call storage handed to Swift (decoded buffers, file paths, sources)
std::vector<std::vector<uint8_t>> g_bufs;
std::vector<std::string> g_paths;
std::vector<std::unique_ptr<Source>> g_sources;
std::string g_attr_str;
std::vector<uint8_t> g_draw;

void free_box(void* storage) {
    if (g_release && *(void**)storage) g_release(*(void**)storage);
    delete (void**)storage;
}

void end_call() {
    g_bufs.clear();
    g_paths.clear();
    g_sources.clear();
}

template <int I>
void invoke(Ctx& c) {
    int rc = g_invoke ? g_invoke(I, &c) : 0;
    end_call();
    if (rc == 1) bail(g_reason.c_str());
    if (rc == 2) check(false, g_reason.c_str());
}

template <int I>
std::vector<uint8_t> decode(void* src) {
    std::vector<uint8_t> out;
    if (g_decode) g_decode(I, src, &out);
    return out;
}

constexpr int kMaxEndpoints = 512;
constexpr int kMaxDecoders = 128;

template <size_t... I>
constexpr std::array<Invoker, sizeof...(I)> invokers(std::index_sequence<I...>) { return {&invoke<(int)I>...}; }
template <size_t... I>
constexpr std::array<std::vector<uint8_t> (*)(void*), sizeof...(I)> decoders(std::index_sequence<I...>) {
    return {&decode<(int)I>...};
}
const auto kInvokers = invokers(std::make_index_sequence<kMaxEndpoints>{});
const auto kDecoders = decoders(std::make_index_sequence<kMaxDecoders>{});
int g_n_endpoints = 0;
int g_n_decoders = 0;

Ctx& C(void* ctx) { return *static_cast<Ctx*>(ctx); }
Source& S(void* s) { return *static_cast<Source*>(s); }
const char* keep(const char* s) { return s ? strdup(s) : nullptr; }
int slot_of(void* obj) { return slot_for_ptr(obj); }
}  // namespace

// the C++ reproducer driver (main_repro.cpp, compiled with -Dmain=stitch_repro_main)
int stitch_repro_main(int argc, char** argv);
// weak: reproducer images (and `stitch cov`) link without the engine library
extern "C" int stitch_engine_main(int argc, char** argv) __attribute__((weak));

extern "C" {

void stitch_c_callbacks(int32_t (*invoke)(int32_t, void*), void (*decode)(int32_t, void*, void*), void (*release)(void*)) {
    g_invoke = invoke;
    g_decode = decode;
    g_release = release;
}

int32_t stitch_c_object(const char* name) {
    auto* ops = new TypeOps{keep(name), sizeof(void*), true, &free_box, nullptr};
    return register_object(ops);
}

int32_t stitch_c_datatype(const char* name, int32_t has_decoder) {
    int id = register_datatype(keep(name));
    if (has_decoder) {
        if (g_n_decoders >= kMaxDecoders) {
            fprintf(stderr, "[stitch] error: more than %d decoders\n", kMaxDecoders);
            abort();
        }
        register_decoder(keep(name), kDecoders[g_n_decoders]);
        return g_n_decoders++;
    }
    return -1;
}

int32_t stitch_c_endpoint(const char* name, const char* file, int32_t n_args, const uint8_t* arg_is_obj,
                          const int32_t* in_types, const int32_t* in_take, int32_t n_out, const int32_t* out_types,
                          const int32_t* out_opt, const stitch_c_param* params, const char* const* arg_names) {
    if (g_n_endpoints >= kMaxEndpoints) {
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
            it.push_back(in_types[ni]);
            take.push_back(in_take[ni] != 0);
            ni++;
        } else {
            const stitch_c_param& p = params[np++];
            ParamDesc d{};
            d.kind = (PKind)p.kind;
            d.size = p.size;
            d.is_signed = p.is_signed != 0;
            d.lo = p.lo;
            d.hi = p.hi;
            d.values.assign(p.values, p.values + p.n_values);
            d.datatype = keep(p.datatype);
            d.ctype = keep(p.ctype);
            ps.push_back(d);
        }
    }
    for (int i = 0; i < n_out; i++) {
        ot.push_back(out_types[i]);
        opt.push_back(out_opt[i] != 0);
    }
    const char* ename = keep(name);
    register_endpoint(ename, keep(file), kInvokers[g_n_endpoints], it, take, ot, opt, ps, order);
    if (arg_names) {
        // argument names (objects included, in order), for traces and `stitch write`
        std::vector<std::string> all;
        for (int i = 0; i < n_args; i++) all.push_back(arg_names[i] ? arg_names[i] : "");
        set_param_names(ename, all);
    }
    return g_n_endpoints++;
}

void stitch_c_init(void (*fn)(void)) { register_init(fn); }

void* stitch_c_in(void* ctx, int32_t k) { return *(void**)C(ctx).in[k]; }

const uint8_t* stitch_c_raw(void* ctx, int32_t k, size_t* len) {
    *len = C(ctx).plen[k];
    return C(ctx).pbuf[k];
}

const uint8_t* stitch_c_decoded(void* ctx, int32_t k, const char* datatype, size_t max, size_t* len) {
    Ctx& c = C(ctx);
    g_bufs.push_back(decode_param(datatype_id(datatype), c.pbuf[k], c.plen[k], c.pform[k], max));
    *len = g_bufs.back().size();
    return g_bufs.back().data();
}

const char* stitch_c_file(void* ctx, int32_t k, const char* datatype, size_t max) {
    size_t n = 0;
    const uint8_t* b = stitch_c_decoded(ctx, k, datatype, max, &n);
    g_paths.push_back(write_temp_file(b, n));
    return g_paths.back().c_str();
}

void* stitch_c_source(void* ctx, int32_t k, size_t max) {
    Ctx& c = C(ctx);
    size_t n = c.plen[k] < max ? c.plen[k] : max;
    g_sources.push_back(std::make_unique<Source>(c.pbuf[k], n, max));
    return g_sources.back().get();
}

void stitch_c_out(void* ctx, int32_t k, void* obj) { add_output(C(ctx), k, new void*(obj), nullptr); }
void stitch_c_consume(void* obj) { consume_slot(slot_of(obj)); }
void stitch_c_reason(const char* s) { g_reason = s ? s : ""; }
void stitch_c_log(const char* s) { stitch::log("%s", s); }
int32_t stitch_c_tracing(void) { return detail::tracing() ? 1 : 0; }

void stitch_c_attr_set_int(void* obj, const char* key, int64_t v) {
    int s = slot_of(obj);
    if (s >= 0) attr_set_int(s, key, v);
    else pending_set_int(obj, key, v);
}
void stitch_c_attr_set_str(void* obj, const char* key, const char* v, size_t len) {
    int s = slot_of(obj);
    if (s >= 0) attr_set_str(s, key, std::string(v, len));
    else pending_set_str(obj, key, std::string(v, len));
}
int32_t stitch_c_attr_get_int(void* obj, const char* key, int64_t* out) { return attr_get_int(slot_of(obj), key, out) ? 1 : 0; }
const char* stitch_c_attr_get_str(void* obj, const char* key, size_t* len) {
    if (!attr_get_str(slot_of(obj), key, &g_attr_str)) return nullptr;
    *len = g_attr_str.size();
    return g_attr_str.data();
}
int32_t stitch_c_attr_has(void* obj, const char* key) { return attr_has(slot_of(obj), key) ? 1 : 0; }
void stitch_c_attr_del(void* obj, const char* key) { attr_del(slot_of(obj), key); }
void stitch_c_global_set_int(const char* key, int64_t v) { global_set(key, v); }
int32_t stitch_c_global_get_int(const char* key, int64_t* out) {
    if (!global_has(key)) return 0;
    *out = global_get(key, (int64_t)0);
    return 1;
}
void stitch_c_note_missing(const char* key) { note_missing(key); }

uint64_t stitch_c_src_uint(void* s, int32_t bytes) {
    switch (bytes) {
        case 1: return S(s).u8();
        case 2: return S(s).u16();
        case 4: return S(s).u32();
        default: return S(s).u64();
    }
}
int64_t stitch_c_src_range(void* s, int64_t lo, int64_t hi) { return S(s).range(lo, hi); }
int32_t stitch_c_src_coin(void* s, double p) { return S(s).coin(p) ? 1 : 0; }
int32_t stitch_c_src_more(void* s) { return S(s).more() ? 1 : 0; }
const uint8_t* stitch_c_src_bytes(void* s, size_t max, size_t* len) {
    g_draw = S(s).bytes(max);
    *len = g_draw.size();
    return g_draw.data();
}
const uint8_t* stitch_c_src_str(void* s, size_t max, size_t* len) {
    std::string v = S(s).str(max);
    g_draw.assign(v.begin(), v.end());
    *len = g_draw.size();
    return g_draw.data();
}
const uint8_t* stitch_c_src_token(void* s, size_t* len) {
    std::string v = S(s).token();
    g_draw.assign(v.begin(), v.end());
    *len = g_draw.size();
    return g_draw.data();
}
double stitch_c_src_f64(void* s) { return S(s).f64(); }
double stitch_c_src_f64_in(void* s, double lo, double hi) { return S(s).f64(lo, hi); }
float stitch_c_src_f32_in(void* s, float lo, float hi) { return S(s).f32(lo, hi); }
int32_t stitch_c_src_more_at(void* s, uint32_t site, uintptr_t frame) { return source_more_at(S(s), site, frame) ? 1 : 0; }
const uint8_t* stitch_c_src_fixed(void* s, size_t n, size_t* len) {
    g_draw = S(s).fixed(n);
    *len = g_draw.size();
    return g_draw.data();
}
const uint8_t* stitch_c_src_rest(void* s, size_t* len) {
    g_draw = S(s).rest();
    *len = g_draw.size();
    return g_draw.data();
}
// a Val parameter's choices as a Source (its decoder's draws are not stub draws in plans)
void* stitch_c_value_source(void* ctx, int32_t k, size_t max) {
    QuietDraws q;
    Ctx& c = C(ctx);
    size_t n = std::min(c.plen[k], max);
    return new Source(c.pbuf[k], n, max);
}
size_t stitch_c_src_position(void* s) { return S(s).position(); }
size_t stitch_c_src_remaining(void* s) { return S(s).remaining(); }
int32_t stitch_c_src_exhausted(void* s) { return S(s).exhausted() ? 1 : 0; }
void* stitch_c_source_new(const uint8_t* p, size_t n, size_t cap) { return new Source(p, n, cap); }
void stitch_c_source_free(void* s) { delete static_cast<Source*>(s); }
void stitch_c_append(void* out, const uint8_t* p, size_t n) {
    auto* v = static_cast<std::vector<uint8_t>*>(out);
    v->insert(v->end(), p, p + n);
}

// STITCH_SWIFT_ENGINE=1 (the fuzzer script): the fuzzing engine, in this process; it re-executes
// this binary (fuzz) and runs it for `exec` / triage with the variable inherited. Otherwise the
// reproducer driver.
int stitch_c_main(int argc, char** argv) {
    const char* e = getenv("STITCH_SWIFT_ENGINE");
    if (e && *e && strcmp(e, "0") != 0) {
        if (!stitch_engine_main) {
            fprintf(stderr, "[stitch] this binary was built without the engine (reproducer build)\n");
            return 2;
        }
        return stitch_engine_main(argc, argv);
    }
    return stitch_repro_main(argc, argv);
}
}
