// STITCH C++ runtime: registry, program execution, attributes, Source, coverage hooks.
// Compiled WITHOUT SanitizerCoverage (only the harness stubs and the library are instrumented).
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <exception>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "stitch.h"
#include "stitch_abi.h"

namespace stitch {
namespace detail {

// ============================================================================================
// Registry
// ============================================================================================
struct EndpointInfo {
    std::string name, file;
    Invoker inv;
    std::vector<int> in_types;
    std::vector<bool> in_take;
    std::vector<int> out_types;
    std::vector<bool> out_opt;
    std::vector<ParamDesc> params;
    std::vector<uint8_t> arg_is_obj;
    std::vector<std::string> arg_names;
};

struct DatatypeInfo {
    std::string name;
    std::vector<uint8_t> (*decoder)(void*) = nullptr;
    std::vector<std::string> dict;
};

struct Registry {
    std::vector<TypeOps*> types;
    std::vector<EndpointInfo> endpoints;
    std::unordered_map<std::string, int> ep_by_name;
    std::vector<DatatypeInfo> datatypes;
    std::vector<void (*)()> inits;
    std::unordered_map<std::string, std::vector<std::string>> pending_names;
};

static Registry& reg() {
    static Registry* r = new Registry();
    return *r;
}

int register_object(TypeOps* ops) {
    auto& r = reg();
    for (size_t i = 0; i < r.types.size(); i++)
        if (std::string(r.types[i]->name) == ops->name) return (int)i;
    r.types.push_back(ops);
    return (int)r.types.size() - 1;
}

void set_drop(int type, void (*drop)(void*)) { reg().types[type]->drop = drop; }

int register_datatype(const char* name) {
    auto& r = reg();
    for (size_t i = 0; i < r.datatypes.size(); i++)
        if (r.datatypes[i].name == name) return (int)i;
    r.datatypes.push_back(DatatypeInfo{name});
    return (int)r.datatypes.size() - 1;
}

int datatype_id(const char* name) {
    if (!name) return -1;
    return register_datatype(name);
}

int register_decoder(const char* name, std::vector<uint8_t> (*fn)(void*)) {
    int id = register_datatype(name);
    reg().datatypes[id].decoder = fn;
    return id;
}

int register_init(void (*fn)()) {
    reg().inits.push_back(fn);
    return 0;
}

int register_endpoint(const char* name, const char* file, Invoker inv, std::vector<int> in_types,
                      std::vector<bool> in_take, std::vector<int> out_types, std::vector<bool> out_opt,
                      std::vector<ParamDesc> params, std::vector<uint8_t> arg_is_obj) {
    auto& r = reg();
    if (r.ep_by_name.count(name)) {
        fprintf(stderr, "[stitch] error: duplicate endpoint name '%s' (%s and %s)\n", name,
                r.endpoints[r.ep_by_name[name]].file.c_str(), file);
        abort();
    }
    if (out_types.size() > 16 || in_types.size() > 32) {
        fprintf(stderr, "[stitch] error: endpoint '%s' has too many inputs/outputs\n", name);
        abort();
    }
    EndpointInfo e{name, file, inv, in_types, in_take, out_types, out_opt, params, arg_is_obj, {}};
    auto it = r.pending_names.find(name);
    if (it != r.pending_names.end()) e.arg_names = it->second;
    r.endpoints.push_back(std::move(e));
    r.ep_by_name[name] = (int)r.endpoints.size() - 1;
    return (int)r.endpoints.size() - 1;
}

void set_param_names(const char* endpoint, std::initializer_list<const char*> names) {
    set_param_names(endpoint, std::vector<std::string>(names.begin(), names.end()));
}

void set_param_names(const char* endpoint, const std::vector<std::string>& v) {
    auto& r = reg();
    auto it = r.ep_by_name.find(endpoint);
    if (it != r.ep_by_name.end()) r.endpoints[it->second].arg_names = v;
    else r.pending_names[endpoint] = v;
}

// ============================================================================================
// Shared memory
// ============================================================================================
static void* shared_alloc(size_t n) {
    void* p = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        perror("[stitch] mmap");
        abort();
    }
    return p;
}

static uint8_t* g_edges = nullptr;
static uint32_t g_edges_n = 0;
static StitchResults* g_results = nullptr;
static StitchCmplog* g_cmplog = nullptr;

static StitchResults* results() {
    if (!g_results) g_results = (StitchResults*)shared_alloc(sizeof(StitchResults));
    return g_results;
}
static StitchCmplog* cmplog() {
    if (!g_cmplog) g_cmplog = (StitchCmplog*)shared_alloc(sizeof(StitchCmplog));
    return g_cmplog;
}

static uint32_t fnv1a(const void* data, size_t n) {
    uint32_t h = 2166136261u;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h ? h : 1;
}

static std::unordered_set<uint32_t> g_strings_seen;
static uint32_t intern(const std::string& s) {
    uint32_t h = fnv1a(s.data(), s.size());
    if (g_strings_seen.count(h)) return h;
    StitchResults* r = results();
    size_t len = std::min<size_t>(s.size(), 512);
    if (r->str_used + 6 + len > STITCH_STRINGS_SIZE) return h;
    uint8_t* p = r->strings + r->str_used;
    memcpy(p, &h, 4);
    uint16_t l16 = (uint16_t)len;
    memcpy(p + 4, &l16, 2);
    memcpy(p + 6, s.data(), len);
    r->str_used += 6 + (uint32_t)len;
    g_strings_seen.insert(h);
    return h;
}

// ============================================================================================
// Execution state
// ============================================================================================
struct Value {
    int kind;  // 0 int, 1 str, 2 obj
    int64_t i;
    std::string s;
    ObjRef o;
};
struct AttrMap {
    std::map<std::string, Value> m;
};

struct Slot {
    int type;
    void* storage;
    bool alive;
    uint32_t gen;
    int node, out;
    AttrMap attrs;
};

static std::vector<Slot> g_pool;
static std::map<std::pair<int, int>, int> g_obj_of;  // (node, output) -> slot
static AttrMap g_globals;
static int g_cur_node = -1;
static bool g_in_exec = false;
static bool g_trace = false;
static uint32_t g_cur_consumed = 0;
static std::vector<int> g_cur_in_slots;
static uint32_t g_last_miss = 0;
static std::vector<std::pair<const void*, std::pair<std::string, Value>>> g_pending;
static std::vector<std::string> g_tmp_files;
static uint32_t g_gen_counter = 1;
static uint64_t g_cmp_seen[1024];  // per-run dedupe of comparison records
// plan mode (`repro --plan FILE`): concrete values of every call, for the standalone C++ writer
static FILE* g_plan = nullptr;
static int g_plan_src_next = 0;      // Src params of the current call, in declaration order
static bool g_plan_quiet = false;    // decoders / plan emission: do not log Source draws
static void plan_draw(int src, const char* op, const std::string& val, bool is_bytes);

std::shared_ptr<AttrMap> new_attr_map() { return std::make_shared<AttrMap>(); }

static void note_miss(const char* key) {
    if (g_in_exec) g_last_miss = intern(key);
}
void note_missing(const char* key) { note_miss(key); }
bool tracing() { return g_trace; }

static Slot* live_slot(int slot) {
    if (slot < 0 || slot >= (int)g_pool.size() || !g_pool[slot].alive) return nullptr;
    return &g_pool[slot];
}

// A pointer identifies a pooled object if it is the object's pointer handle, or the address of
// its handle storage (what get_obj<T>() returns, also for struct handles).
int slot_for_ptr(const void* p) {
    if (!p) return -1;
    for (int i = (int)g_pool.size() - 1; i >= 0; i--) {
        Slot& s = g_pool[i];
        if (!s.alive) continue;
        if (s.storage == p) return i;
        if (reg().types[s.type]->handle_is_ptr && *(void**)s.storage == p) return i;
    }
    return -1;
}

static Value vint(int64_t v) { return Value{0, v, {}, {}}; }
static Value vstr(const std::string& v) { return Value{1, 0, v, {}}; }
static Value vobj(int target) {
    Value x{2, 0, {}, {-1, 0}};
    if (Slot* s = live_slot(target)) x.o = ObjRef{target, s->gen};
    return x;
}

static void attr_put(int slot, const char* key, Value v) {
    if (Slot* s = live_slot(slot)) s->attrs.m[key] = std::move(v);
}
static const Value* attr_find(int slot, const char* key) {
    Slot* s = live_slot(slot);
    if (!s) return nullptr;
    auto it = s->attrs.m.find(key);
    if (it == s->attrs.m.end()) return nullptr;
    return &it->second;
}

void attr_set_int(int slot, const char* key, int64_t v) { attr_put(slot, key, vint(v)); }
void attr_set_str(int slot, const char* key, const std::string& v) { attr_put(slot, key, vstr(v)); }
void attr_set_obj(int slot, const char* key, int target) { attr_put(slot, key, vobj(target)); }
bool attr_get_int(int slot, const char* key, int64_t* out) {
    const Value* v = attr_find(slot, key);
    if (!v || v->kind != 0) return false;
    *out = v->i;
    return true;
}
bool attr_get_str(int slot, const char* key, std::string* out) {
    const Value* v = attr_find(slot, key);
    if (!v || v->kind != 1) return false;
    *out = v->s;
    return true;
}
void* attr_get_obj(int slot, const char* key, int type) {
    const Value* v = attr_find(slot, key);
    if (!v || v->kind != 2) return nullptr;
    Slot* t = live_slot(v->o.slot);
    if (!t || t->gen != v->o.gen || t->type != type) return nullptr;
    return t->storage;
}
bool attr_has(int slot, const char* key) {
    Slot* s = live_slot(slot);
    return s && s->attrs.m.count(key);
}
void attr_del(int slot, const char* key) {
    if (Slot* s = live_slot(slot)) s->attrs.m.erase(key);
}
void pending_set_int(const void* p, const char* key, int64_t v) { g_pending.push_back({p, {key, vint(v)}}); }
void pending_set_str(const void* p, const char* key, const std::string& v) { g_pending.push_back({p, {key, vstr(v)}}); }
void pending_set_obj(const void* p, const char* key, int target) { g_pending.push_back({p, {key, vobj(target)}}); }
void map_set_int(AttrMap& m, const char* key, int64_t v) { m.m[key] = vint(v); }
void map_set_str(AttrMap& m, const char* key, const std::string& v) { m.m[key] = vstr(v); }
void map_set_obj(AttrMap& m, const char* key, int target) { m.m[key] = vobj(target); }
bool map_get_int(const AttrMap& m, const char* key, int64_t* v) {
    auto it = m.m.find(key);
    if (it == m.m.end() || it->second.kind != 0) return false;
    *v = it->second.i;
    return true;
}
bool map_get_str(const AttrMap& m, const char* key, std::string* v) {
    auto it = m.m.find(key);
    if (it == m.m.end() || it->second.kind != 1) return false;
    *v = it->second.s;
    return true;
}

void consume_slot(int slot) {
    for (size_t i = 0; i < g_cur_in_slots.size(); i++)
        if (g_cur_in_slots[i] == slot) g_cur_consumed |= (1u << i);
}

Ctx* g_ctx = nullptr;
void add_output(Ctx& c, int idx, void* storage, std::shared_ptr<AttrMap> attrs) {
    if (idx < 0 || idx >= 16) return;
    c.out_storage[idx] = storage;
    c.out_present[idx] = true;
    c.out_attrs[idx] = std::move(attrs);
}

std::string write_temp_file(const uint8_t* data, size_t len) {
    static unsigned counter = 0;
    const char* dir = getenv("STITCH_TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    char path[512];
    snprintf(path, sizeof(path), "%s/stitch-%d-%u", dir, (int)getpid(), counter++);
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd >= 0) {
        size_t off = 0;
        while (off < len) {
            ssize_t w = write(fd, data + off, len - off);
            if (w <= 0) break;
            off += (size_t)w;
        }
        close(fd);
    }
    g_tmp_files.push_back(path);
    return path;
}

struct BailSignal {
    const char* reason;
};

}  // namespace detail

using namespace detail;

[[noreturn]] void bail(const char* reason) { throw BailSignal{reason ? reason : "bail"}; }

namespace detail {
// While fuzzing (the engine's `fuzz` command sets STITCH_FUZZING), a failed check or an uncaught
// exception ends the program as a finding without killing the worker: re-forking an ASan
// process costs milliseconds. exec / triage still abort with the full report.
struct FindingSignal {};
static bool fuzz_mode() {
    static int v = -1;
    if (v < 0) v = getenv("STITCH_FUZZING") != nullptr;
    return v == 1;
}
}  // namespace detail

void check(bool cond, const char* what) {
    if (cond) return;
    results()->crash_pc = (1ull << 63) | fnv1a(what ? what : "?", what ? strlen(what) : 1);
    if (fuzz_mode() && !g_trace && g_in_exec) throw FindingSignal{};
    fprintf(stderr, "\n[stitch] CHECK FAILED: %s (node %d)\n", what ? what : "?", g_cur_node);
    fflush(stderr);
    abort();
}

void log(const char* fmt, ...) {
    if (!g_trace) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[stitch]     log: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void global_set(const char* key, int64_t v) { g_globals.m[key] = vint(v); }
void global_set_str(const char* key, const std::string& v) { g_globals.m[key] = vstr(v); }
int64_t global_get(const char* key, int64_t def) {
    auto it = g_globals.m.find(key);
    return it == g_globals.m.end() || it->second.kind != 0 ? def : it->second.i;
}
int64_t global_get(const char* key) {
    if (!global_has(key)) note_miss(key);
    return global_get(key, 0);
}
std::string global_get_str(const char* key, const std::string& def) {
    auto it = g_globals.m.find(key);
    return it == g_globals.m.end() || it->second.kind != 1 ? def : it->second.s;
}
std::string global_get_str(const char* key) {
    if (!global_has(key)) note_miss(key);
    return global_get_str(key, "");
}
bool global_has(const char* key) { return g_globals.m.count(key) > 0; }

// ============================================================================================
// Source
// ============================================================================================
struct Source::Impl {
    std::vector<uint8_t> buf;
    size_t pos = 0;
    size_t cap = 0;
    bool gen = false;
    std::mt19937_64 rng;
    const std::vector<std::string>* dict = nullptr;
    bool exhausted = false;
    std::vector<uint32_t>* spans = nullptr;  // (prefix offset, prefix width, content length) per str draw
    int log_id = -1;  // plan mode: index of the Src parameter this source belongs to
    int depth = 0;    // nesting of public draw calls (only the outermost one is logged)

    uint64_t rnd() { return rng(); }
    double unit() { return (rnd() >> 11) * (1.0 / 9007199254740992.0); }

    // Read n bytes; in generation mode, synthesize them with `synth` when the buffer ends.
    template <class Synth>
    void draw(uint8_t* out, size_t n, Synth synth) {
        if (pos + n <= buf.size()) {
            memcpy(out, buf.data() + pos, n);
            pos += n;
            return;
        }
        if (gen && buf.size() + n <= cap) {
            buf.resize(pos);
            std::vector<uint8_t> fresh(n, 0);
            synth(fresh.data());
            buf.insert(buf.end(), fresh.begin(), fresh.end());
            memcpy(out, fresh.data(), n);
            pos += n;
            return;
        }
        memset(out, 0, n);
        if (pos < buf.size()) memcpy(out, buf.data() + pos, buf.size() - pos);
        pos = buf.size();
        exhausted = true;
    }

    uint64_t interesting(size_t width) {
        static const uint64_t vals[] = {0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 512, 1000, 1024, 4096, 0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, 0x80000000ull, 0xffffffffull};
        uint64_t mask = width >= 8 ? ~0ull : ((1ull << (8 * width)) - 1);
        uint64_t r = rnd() % 4;
        if (r == 0) return mask;  // -1
        if (r == 1) return (mask >> 1);  // max signed
        return vals[rnd() % (sizeof(vals) / sizeof(vals[0]))] & mask;
    }
    uint64_t uint(size_t width) {
        uint64_t v = 0;
        draw((uint8_t*)&v, width, [&](uint8_t* o) {
            uint64_t x = unit() < 0.3 ? interesting(width) : rnd();
            memcpy(o, &x, width);
        });
        return v;
    }
    size_t len_prefix(size_t max) { return max < 256 ? 1 : max < 65536 ? 2 : 4; }
    size_t synth_len(size_t max) {
        double u = unit();
        size_t l = u < 0.7 ? rnd() % 17 : u < 0.95 ? rnd() % 65 : rnd() % (max + 1);
        return std::min(l, max);
    }
    std::vector<uint8_t> blob(size_t max, bool use_dict) {
        size_t w = len_prefix(max);
        size_t p0 = pos;
        auto out = blob_inner(max, use_dict, w);
        if (spans && p0 + w <= buf.size()) {
            spans->push_back((uint32_t)p0);
            spans->push_back((uint32_t)w);
            spans->push_back((uint32_t)out.size());
        }
        return out;
    }
    std::vector<uint8_t> blob_inner(size_t max, bool use_dict, size_t w) {
        uint64_t raw = 0;
        std::vector<uint8_t> content;
        bool synthesized = false;
        draw((uint8_t*)&raw, w, [&](uint8_t* o) {
            synthesized = true;
            if (use_dict && dict && !dict->empty() && unit() < 0.5) {
                const std::string& t = (*dict)[rnd() % dict->size()];
                content.assign(t.begin(), t.end());
                if (content.size() > max) content.resize(max);
            } else {
                size_t l = synth_len(max);
                bool printable = unit() < 0.7;
                content.resize(l);
                for (auto& c : content) c = printable ? (uint8_t)(32 + rnd() % 95) : (uint8_t)rnd();
            }
            uint64_t l = content.size();
            memcpy(o, &l, w);
        });
        size_t len = (size_t)raw;
        if (len > max) len = len % (max + 1);
        std::vector<uint8_t> out(len);
        if (synthesized) {
            // content was generated together with the prefix; append it verbatim
            if (buf.size() + len <= cap) {
                buf.insert(buf.end(), content.begin(), content.begin() + len);
                memcpy(out.data(), content.data(), len);
                pos += len;
                return out;
            }
            out.clear();
            exhausted = true;
            return out;
        }
        size_t avail = buf.size() > pos ? buf.size() - pos : 0;
        size_t n = std::min(len, avail);
        memcpy(out.data(), buf.data() + pos, n);
        pos += n;
        if (n < len) {
            out.resize(n);
            exhausted = true;
        }
        return out;
    }
};

Source::Source(const uint8_t* b, size_t len, size_t cap) : impl_(new Impl()) {
    impl_->buf.assign(b, b + len);
    impl_->cap = std::max(cap, len);
    if (g_plan && g_in_exec && !g_plan_quiet && g_cur_node >= 0) impl_->log_id = g_plan_src_next++;
}
Source::~Source() { delete impl_; }
Source::Source(Source&& o) noexcept : impl_(o.impl_) { o.impl_ = nullptr; }

namespace {
struct DrawGuard {
    Source::Impl* i;
    explicit DrawGuard(Source::Impl* p) : i(p) { i->depth++; }
    ~DrawGuard() { i->depth--; }
    bool top() const { return i->log_id >= 0 && i->depth == 1 && g_plan; }
};
template <class T>
std::string num_s(T v) {
    if constexpr (std::is_floating_point<T>::value) {
        char b[64];
        snprintf(b, sizeof b, "%.17g", (double)v);
        return b;
    } else return std::to_string(v);
}
}  // namespace
#define STITCH_DRAW(op, expr)                                          \
    DrawGuard g_(impl_);                                               \
    auto v_ = (expr);                                                  \
    if (g_.top()) plan_draw(impl_->log_id, (op).c_str(), num_s(v_), false); \
    return v_

uint8_t Source::u8() { STITCH_DRAW(std::string("u8()"), (uint8_t)impl_->uint(1)); }
uint16_t Source::u16() { STITCH_DRAW(std::string("u16()"), (uint16_t)impl_->uint(2)); }
uint32_t Source::u32() { STITCH_DRAW(std::string("u32()"), (uint32_t)impl_->uint(4)); }
uint64_t Source::u64() { STITCH_DRAW(std::string("u64()"), impl_->uint(8)); }
float Source::f32() {
    DrawGuard g_(impl_);
    uint32_t b = (uint32_t)impl_->uint(4);
    float f;
    memcpy(&f, &b, 4);
    if (g_.top()) plan_draw(impl_->log_id, "f32()", num_s(f), false);
    return f;
}
double Source::f64() {
    DrawGuard g_(impl_);
    uint64_t b = impl_->uint(8);
    double f;
    memcpy(&f, &b, 8);
    if (g_.top()) plan_draw(impl_->log_id, "f64()", num_s(f), false);
    return f;
}
static int64_t range_impl(Source::Impl* impl_, int64_t lo, int64_t hi) {
    if (hi < lo) std::swap(lo, hi);
    int64_t raw = 0;
    impl_->draw((uint8_t*)&raw, 8, [&](uint8_t* o) {
        uint64_t span = (uint64_t)hi - (uint64_t)lo + 1;
        double u = impl_->unit();
        int64_t v = u < 0.05 ? lo : u < 0.1 ? hi : (int64_t)((uint64_t)lo + (span ? impl_->rnd() % span : impl_->rnd()));
        memcpy(o, &v, 8);
    });
    if (raw >= lo && raw <= hi) return raw;
    uint64_t span = (uint64_t)hi - (uint64_t)lo + 1;
    uint64_t off = (uint64_t)raw - (uint64_t)lo;
    return (int64_t)((uint64_t)lo + (span ? off % span : off));
}
int64_t Source::range(int64_t lo, int64_t hi) {
    STITCH_DRAW("range(" + std::to_string(lo) + ", " + std::to_string(hi) + ")", range_impl(impl_, lo, hi));
}
size_t Source::choice(size_t n) {
    STITCH_DRAW("choice(" + std::to_string(n) + ")", (size_t)(n == 0 ? 0 : (size_t)range_impl(impl_, 0, (int64_t)n - 1)));
}
bool Source::coin(double p) {
    DrawGuard g_(impl_);
    uint8_t b = 0;
    double thr = p * 256.0;
    impl_->draw(&b, 1, [&](uint8_t* o) {
        bool yes = impl_->unit() < p;
        uint64_t r = impl_->rnd();
        if (yes) *o = thr >= 1 ? (uint8_t)(r % (uint64_t)std::min(256.0, thr)) : 0;
        else *o = (uint8_t)std::min(255.0, thr + (double)(r % (uint64_t)std::max(1.0, 256.0 - thr)));
    });
    // a full or exhausted buffer ends coin-driven loops, like more()
    bool v = (double)b < thr && !impl_->exhausted;
    if (g_.top()) plan_draw(impl_->log_id, ("coin(" + num_s(p) + ")").c_str(), v ? "true" : "false", false);
    return v;
}
bool Source::more() {
    DrawGuard g_(impl_);
    bool v;
    if (impl_->exhausted && !impl_->gen) v = false;
    else {
        uint8_t b = 0;
        impl_->draw(&b, 1, [&](uint8_t* o) {
            double fill = impl_->cap ? (double)impl_->buf.size() / impl_->cap : 1.0;
            double p = fill > 0.5 ? 0.15 : 0.65;
            *o = (uint8_t)((impl_->rnd() & 0xfe) | (impl_->unit() < p ? 1 : 0));
        });
        v = (b & 1) && !impl_->exhausted;
    }
    if (g_.top()) plan_draw(impl_->log_id, "more()", v ? "true" : "false", false);
    return v;
}
std::string Source::str(size_t max) {
    DrawGuard g_(impl_);
    auto v = impl_->blob(max, false);
    std::string r(v.begin(), v.end());
    if (g_.top()) plan_draw(impl_->log_id, ("str(" + std::to_string(max) + ")").c_str(), r, true);
    return r;
}
std::vector<uint8_t> Source::bytes(size_t max) {
    DrawGuard g_(impl_);
    auto v = impl_->blob(max, false);
    if (g_.top()) plan_draw(impl_->log_id, ("bytes(" + std::to_string(max) + ")").c_str(), std::string(v.begin(), v.end()), true);
    return v;
}
std::string Source::token() {
    DrawGuard g_(impl_);
    auto v = impl_->blob(255, true);
    std::string r(v.begin(), v.end());
    if (g_.top()) plan_draw(impl_->log_id, "token()", r, true);
    return r;
}
#undef STITCH_DRAW
size_t Source::position() const { return impl_->pos; }
size_t Source::remaining() const { return impl_->buf.size() > impl_->pos ? impl_->buf.size() - impl_->pos : 0; }
bool Source::exhausted() const { return impl_->exhausted; }

namespace detail {
std::vector<uint8_t> decode_param(int dt, const uint8_t* buf, size_t len, uint8_t form, size_t max) {
    auto& r = reg();
    if (form == 1 && dt >= 0 && dt < (int)r.datatypes.size() && r.datatypes[dt].decoder) {
        bool quiet_saved = g_plan_quiet;
        g_plan_quiet = true;
        Source s(buf, len, std::max(len, max));
        std::vector<uint8_t> out;
        bool saved = g_in_exec;
        try {
            out = r.datatypes[dt].decoder(&s);
        } catch (BailSignal&) {
            out.clear();
        }
        g_in_exec = saved;
        g_plan_quiet = quiet_saved;
        if (out.size() > max) out.resize(max);
        return out;
    }
    size_t n = std::min(len, max);
    return std::vector<uint8_t>(buf, buf + n);
}
}  // namespace detail

// ============================================================================================
// Manifest
// ============================================================================================
static void json_str(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else o += (char)c;
        }
    }
    o += '"';
}

static const char* kind_name(PKind k) {
    switch (k) {
        case PKind::Int: return "int";
        case PKind::Float: return "float";
        case PKind::Bool: return "bool";
        case PKind::Range: return "range";
        case PKind::OneOf: return "oneof";
        case PKind::Flags: return "flags";
        case PKind::Bytes: return "bytes";
        case PKind::Str: return "str";
        case PKind::File: return "file";
        case PKind::Raw: return "raw";
        case PKind::Source: return "source";
    }
    return "?";
}

static std::string build_manifest() {
    auto& r = reg();
    std::string o = "{\"version\":2,\"objects\":[";
    for (size_t i = 0; i < r.types.size(); i++) {
        if (i) o += ',';
        o += "{\"name\":";
        json_str(o, r.types[i]->name);
        o += ",\"drop\":";
        o += r.types[i]->drop ? "true" : "false";
        o += '}';
    }
    o += "],\"datatypes\":[";
    for (size_t i = 0; i < r.datatypes.size(); i++) {
        if (i) o += ',';
        o += "{\"name\":";
        json_str(o, r.datatypes[i].name);
        o += ",\"decoder\":";
        o += r.datatypes[i].decoder ? "true" : "false";
        o += '}';
    }
    o += "],\"endpoints\":[";
    for (size_t i = 0; i < r.endpoints.size(); i++) {
        auto& e = r.endpoints[i];
        if (i) o += ',';
        o += "{\"name\":";
        json_str(o, e.name);
        o += ",\"file\":";
        json_str(o, e.file);
        // arguments in declaration order, with names when known
        size_t oi = 0, pi = 0;
        o += ",\"inputs\":[";
        std::string params = "[";
        for (size_t a = 0; a < e.arg_is_obj.size(); a++) {
            std::string nm = a < e.arg_names.size() ? e.arg_names[a] : "";
            if (e.arg_is_obj[a]) {
                if (oi) o += ',';
                o += "{\"type\":";
                json_str(o, r.types[e.in_types[oi]]->name);
                o += ",\"mode\":";
                o += e.in_take[oi] ? "\"take\"" : "\"borrow\"";
                o += ",\"name\":";
                json_str(o, nm.empty() ? "in" + std::to_string(oi) : nm);
                o += '}';
                oi++;
            } else {
                auto& p = e.params[pi];
                if (pi) params += ',';
                params += "{\"kind\":\"";
                params += kind_name(p.kind);
                params += "\",\"type\":";
                json_str(params, p.ctype ? p.ctype : "?");
                params += ",\"size\":" + std::to_string(p.size);
                params += ",\"signed\":";
                params += p.is_signed ? "true" : "false";
                if (p.kind == PKind::Range) params += ",\"lo\":" + std::to_string(p.lo) + ",\"hi\":" + std::to_string(p.hi);
                if (!p.values.empty()) {
                    params += ",\"values\":[";
                    for (size_t k = 0; k < p.values.size(); k++) {
                        if (k) params += ',';
                        params += std::to_string(p.values[k]);
                    }
                    params += ']';
                }
                params += ",\"datatype\":";
                if (p.datatype) json_str(params, p.datatype);
                else params += "null";
                params += ",\"name\":";
                json_str(params, nm.empty() ? "p" + std::to_string(pi) : nm);
                params += '}';
                pi++;
            }
        }
        o += "],\"params\":" + params + "]";
        o += ",\"outputs\":[";
        for (size_t k = 0; k < e.out_types.size(); k++) {
            if (k) o += ',';
            o += "{\"type\":";
            json_str(o, r.types[e.out_types[k]]->name);
            o += ",\"optional\":";
            o += e.out_opt[k] ? "true" : "false";
            o += '}';
        }
        o += "]}";
    }
    o += "]}";
    return o;
}

// ============================================================================================
// Program execution
// ============================================================================================
struct PNode {
    int ep;  // -1 unknown
    std::string ep_name;
    std::vector<std::pair<uint32_t, uint8_t>> inputs;
    std::vector<uint8_t> forms;
    std::vector<std::vector<uint8_t>> params;
};

struct Reader {
    const uint8_t* p;
    size_t n, off = 0;
    bool ok = true;
    template <class T>
    T get() {
        T v{};
        if (off + sizeof(T) > n) {
            ok = false;
            return v;
        }
        memcpy(&v, p + off, sizeof(T));
        off += sizeof(T);
        return v;
    }
    std::vector<uint8_t> bytes(size_t len) {
        if (off + len > n) {
            ok = false;
            return {};
        }
        std::vector<uint8_t> v(p + off, p + off + len);
        off += len;
        return v;
    }
};

static bool parse_program(const uint8_t* data, size_t len, std::vector<PNode>& nodes) {
    Reader r{data, len};
    auto magic = r.bytes(4);
    if (!r.ok || memcmp(magic.data(), "STCH", 4) != 0) return false;
    uint32_t nn = r.get<uint32_t>();
    if (nn > 100000) return false;
    std::vector<std::string> names;
    for (uint32_t i = 0; i < nn && r.ok; i++) {
        uint16_t l = r.get<uint16_t>();
        auto b = r.bytes(l);
        names.emplace_back(b.begin(), b.end());
    }
    uint32_t n = r.get<uint32_t>();
    if (!r.ok || n > STITCH_MAX_NODES) return false;
    auto& rg = reg();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        PNode nd;
        uint32_t ni = r.get<uint32_t>();
        nd.ep_name = ni < names.size() ? names[ni] : "?";
        auto it = rg.ep_by_name.find(nd.ep_name);
        nd.ep = it == rg.ep_by_name.end() ? -1 : it->second;
        uint8_t nin = r.get<uint8_t>();
        for (int k = 0; k < nin; k++) {
            uint32_t src = r.get<uint32_t>();
            uint8_t out = r.get<uint8_t>();
            nd.inputs.push_back({src, out});
        }
        uint8_t np = r.get<uint8_t>();
        for (int k = 0; k < np; k++) {
            uint8_t form = r.get<uint8_t>();
            uint32_t l = r.get<uint32_t>();
            if (l > (16u << 20)) {
                r.ok = false;
                break;
            }
            nd.forms.push_back(form);
            nd.params.push_back(r.bytes(l));
        }
        nodes.push_back(std::move(nd));
    }
    return r.ok;
}

static std::string esc_preview(const std::vector<uint8_t>& v, size_t maxlen = 160) {
    std::string o = "\"";
    for (size_t i = 0; i < v.size() && i < maxlen; i++) {
        unsigned char c = v[i];
        if (c == '"' || c == '\\') {
            o += '\\';
            o += (char)c;
        } else if (c >= 32 && c < 127) o += (char)c;
        else if (c == '\n') o += "\\n";
        else {
            char b[8];
            snprintf(b, sizeof b, "\\x%02x", c);
            o += b;
        }
    }
    o += '"';
    if (v.size() > maxlen) o += "...";
    return o;
}

static std::string describe_param(const ParamDesc& p, const std::vector<uint8_t>& raw, uint8_t form) {
    char b[128];
    switch (p.kind) {
        case PKind::Int:
        case PKind::Float:
        case PKind::Bool:
        case PKind::Range:
        case PKind::OneOf:
        case PKind::Flags: {
            int64_t v = 0;
            memcpy(&v, raw.data(), std::min<size_t>(raw.size(), p.kind == PKind::Bool ? 1 : std::min<size_t>(8, p.size)));
            if (p.kind == PKind::Bool) return (v & 1) ? "true" : "false";
            if (p.kind == PKind::Float) {
                double d = 0;
                if (p.size == 4) {
                    float f;
                    memcpy(&f, &v, 4);
                    d = f;
                } else memcpy(&d, &v, 8);
                snprintf(b, sizeof b, "%g", d);
                return b;
            }
            if (p.kind == PKind::Range) {
                if (v < p.lo || v > p.hi) {
                    uint64_t span = (uint64_t)p.hi - (uint64_t)p.lo + 1;
                    v = (int64_t)((uint64_t)p.lo + (span ? ((uint64_t)v - (uint64_t)p.lo) % span : 0));
                }
            } else if (p.kind == PKind::OneOf) {
                bool found = false;
                for (auto x : p.values) found |= (x == v);
                if (!found && !p.values.empty()) v = p.values[(uint64_t)v % p.values.size()];
            } else if (p.kind == PKind::Flags) {
                int64_t m = 0;
                for (auto x : p.values) m |= x;
                v &= m;
            } else if (p.size < 8) {
                uint64_t mask = (1ull << (8 * p.size)) - 1;
                v = (int64_t)((uint64_t)v & mask);
                if (p.is_signed && (v >> (8 * p.size - 1)) & 1) v |= ~(int64_t)mask;
            }
            snprintf(b, sizeof b, "%lld", (long long)v);
            return b;
        }
        case PKind::Bytes:
        case PKind::Str:
        case PKind::File: {
            auto val = decode_param(datatype_id(p.datatype), raw.data(), raw.size(), form, p.size);
            std::string pre = p.kind == PKind::Bytes ? "Bytes" : p.kind == PKind::Str ? "Str" : "File";
            return pre + "[" + std::to_string(val.size()) + (form == 1 ? ",decoded" : "") + "]" + esc_preview(val);
        }
        case PKind::Raw:
        case PKind::Source: {
            std::string o = p.kind == PKind::Raw ? "Raw[" : "Src[";
            o += std::to_string(raw.size()) + "]";
            for (size_t i = 0; i < raw.size() && i < 48; i++) {
                snprintf(b, sizeof b, "%02x", raw[i]);
                o += b;
            }
            if (raw.size() > 48) o += "...";
            return o;
        }
    }
    return "?";
}


// ------------------------------------------------------------------------------ plan mode
static std::string hex_of(const uint8_t* d, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string o;
    o.reserve(2 * n);
    for (size_t i = 0; i < n; i++) {
        o += H[d[i] >> 4];
        o += H[d[i] & 15];
    }
    return o;
}

static void plan_line(const std::string& l) {
    if (!g_plan) return;
    fputs(l.c_str(), g_plan);
    fputc('\n', g_plan);
    fflush(g_plan);
}

namespace detail {
static void plan_draw(int src, const char* op, const std::string& val, bool is_bytes) {
    std::string l = "{\"t\":\"draw\",\"node\":" + std::to_string(g_cur_node) + ",\"src\":" + std::to_string(src) + ",\"op\":";
    json_str(l, op);
    if (is_bytes) l += ",\"hex\":\"" + hex_of((const uint8_t*)val.data(), val.size()) + "\"}";
    else {
        l += ",\"v\":";
        json_str(l, val);
        l += "}";
    }
    plan_line(l);
}
}  // namespace detail

// Concrete value of one parameter as the endpoint sees it (same decoding as the Num/Range/... types).
static std::string plan_param(const ParamDesc& p, const std::vector<uint8_t>& raw, uint8_t form) {
    std::string o = "{\"kind\":\"";
    o += kind_name(p.kind);
    o += "\",\"size\":" + std::to_string(p.size);
    switch (p.kind) {
        case PKind::Int:
        case PKind::Range:
        case PKind::OneOf:
        case PKind::Flags:
        case PKind::Bool: {
            std::string d = describe_param(p, raw, form);  // decoded integer / bool as text
            o += ",\"signed\":" + std::string(p.is_signed ? "true" : "false") + ",\"v\":";
            json_str(o, d);
            if (!p.values.empty()) {
                o += ",\"values\":[";
                for (size_t k = 0; k < p.values.size(); k++) o += (k ? ",\"" : "\"") + std::to_string(p.values[k]) + "\"";
                o += "]";
            }
            break;
        }
        case PKind::Float: {
            uint64_t v = 0;
            memcpy(&v, raw.data(), std::min<size_t>(raw.size(), p.size));
            char b[64], c[64];
            if (p.size == 4) {
                float f;
                memcpy(&f, &v, 4);
                snprintf(b, sizeof b, "%a", (double)f);
                snprintf(c, sizeof c, "%.9g", (double)f);
            } else {
                double f;
                memcpy(&f, &v, 8);
                snprintf(b, sizeof b, "%a", f);
                snprintf(c, sizeof c, "%.17g", f);
            }
            o += ",\"v\":";
            json_str(o, b);
            o += ",\"d\":";
            json_str(o, c);
            break;
        }
        case PKind::Bytes:
        case PKind::Str:
        case PKind::File: {
            auto val = decode_param(datatype_id(p.datatype), raw.data(), raw.size(), form, p.size);
            o += ",\"decoded\":" + std::string(form == 1 ? "true" : "false") + ",\"hex\":\"" + hex_of(val.data(), val.size()) + "\"";
            break;
        }
        case PKind::Raw:
        case PKind::Source: {
            size_t n = std::min<size_t>(raw.size(), p.size);
            o += ",\"hex\":\"" + hex_of(raw.data(), n) + "\"";
            break;
        }
    }
    if (p.datatype) {
        o += ",\"datatype\":";
        json_str(o, p.datatype);
    }
    return o + "}";
}

static void plan_call(size_t i, const EndpointInfo& e, const PNode& nd) {
    std::string l = "{\"t\":\"call\",\"node\":" + std::to_string(i) + ",\"ep\":";
    json_str(l, e.name);
    l += ",\"args\":[";
    size_t oi = 0, pi = 0;
    bool quiet = g_plan_quiet;
    g_plan_quiet = true;
    for (size_t a = 0; a < e.arg_is_obj.size(); a++) {
        if (a) l += ",";
        if (e.arg_is_obj[a]) {
            auto& src = nd.inputs[oi];
            l += "{\"obj\":[" + std::to_string(src.first) + "," + std::to_string(src.second) + "],\"type\":";
            json_str(l, reg().types[e.in_types[oi]]->name);
            l += ",\"take\":" + std::string(e.in_take[oi] ? "true" : "false") + "}";
            oi++;
        } else {
            l += plan_param(e.params[pi], nd.params[pi], nd.forms[pi]);
            pi++;
        }
    }
    g_plan_quiet = quiet;
    l += "],\"outs\":[";
    for (size_t k = 0; k < e.out_types.size(); k++) {
        if (k) l += ",";
        json_str(l, reg().types[e.out_types[k]]->name);
    }
    l += "]}";
    plan_line(l);
}

static void reset_exec_state() {
    for (auto& s : g_pool)
        if (s.storage) reg().types[s.type]->free_storage(s.storage);
    g_pool.clear();
    g_obj_of.clear();
    g_globals.m.clear();
    g_strings_seen.clear();
}

/// A finding while fuzzing (see FindingSignal): the program's objects are not dropped (their state
/// may be inconsistent); the next run frees only their slots (reset_exec_state).
static int finding_exit() {
    g_in_exec = false;
    return STITCH_EXEC_FINDING;
}

static int run_program(const std::vector<PNode>& nodes, bool trace) {
    auto& rg = reg();
    // a persistent worker runs many programs: per-run cmplog dedupe must start empty
    if (g_cmplog && g_cmplog->enabled) memset(g_cmp_seen, 0, sizeof g_cmp_seen);
    StitchResults* R = results();
    R->magic = 0x53544348;  // "STCH"
    R->n_nodes = (uint32_t)nodes.size();
    R->str_used = 0;
    R->crash_pc = 0;
    R->current = -1;
    memset(R->nodes, 0, sizeof(R->nodes));
    reset_exec_state();
    g_trace = trace;
    g_in_exec = true;
    const char* exc_policy = getenv("STITCH_EXCEPTIONS");
    bool exc_bail = exc_policy && std::string(exc_policy) == "bail";

    for (size_t i = 0; i < nodes.size(); i++) {
        const PNode& nd = nodes[i];
        StitchNodeResult& res = R->nodes[i];
        R->current = (int32_t)i;
        g_cur_node = (int)i;
        if (nd.ep < 0) {
            res.status = STITCH_SKIP_INVALID;
            plan_line("{\"t\":\"skip\",\"node\":" + std::to_string(i) + ",\"why\":\"unknown endpoint\"}");
            if (trace) fprintf(stderr, "[stitch] #%zu %s: unknown endpoint, skipped\n", i, nd.ep_name.c_str());
            continue;
        }
        const EndpointInfo& e = rg.endpoints[nd.ep];
        res.n_out = (uint8_t)e.out_types.size();
        if (nd.inputs.size() != e.in_types.size() || nd.params.size() != e.params.size()) {
            res.status = STITCH_SKIP_INVALID;
            plan_line("{\"t\":\"skip\",\"node\":" + std::to_string(i) + ",\"why\":\"signature mismatch\"}");
            if (trace) fprintf(stderr, "[stitch] #%zu %s: signature mismatch, skipped\n", i, e.name.c_str());
            continue;
        }
        // resolve inputs
        std::vector<void*> in(e.in_types.size());
        std::vector<int> in_slot(e.in_types.size());
        bool missing = false;
        for (size_t k = 0; k < nd.inputs.size(); k++) {
            auto it = g_obj_of.find({(int)nd.inputs[k].first, (int)nd.inputs[k].second});
            Slot* s = it == g_obj_of.end() ? nullptr : live_slot(it->second);
            if (!s || s->type != e.in_types[k]) {
                missing = true;
                break;
            }
            for (size_t j = 0; j < k; j++)
                if (in_slot[j] == it->second) missing = true;
            in[k] = s->storage;
            in_slot[k] = it->second;
        }
        if (missing) {
            res.status = STITCH_SKIP_MISSING;
            plan_line("{\"t\":\"skip\",\"node\":" + std::to_string(i) + ",\"why\":\"input not available\"}");
            if (trace) fprintf(stderr, "[stitch] #%zu %s: input not available, skipped\n", i, e.name.c_str());
            continue;
        }
        std::vector<const uint8_t*> pbuf(nd.params.size());
        std::vector<size_t> plen(nd.params.size());
        for (size_t k = 0; k < nd.params.size(); k++) {
            pbuf[k] = nd.params[k].data();
            plen[k] = nd.params[k].size();
        }
        if (trace) {
            std::string line = "[stitch] #" + std::to_string(i) + " " + e.name + "(";
            size_t oi = 0, pi = 0;
            for (size_t a = 0; a < e.arg_is_obj.size(); a++) {
                if (a) line += ", ";
                std::string nm = a < e.arg_names.size() ? e.arg_names[a] + "=" : "";
                if (e.arg_is_obj[a]) {
                    auto& src = nd.inputs[oi];
                    line += nm + (e.in_take[oi] ? "take " : "") + "o" + std::to_string(src.first) + "." + std::to_string(src.second);
                    oi++;
                } else {
                    line += nm + describe_param(e.params[pi], nd.params[pi], nd.forms[pi]);
                    pi++;
                }
            }
            line += ")";
            fprintf(stderr, "%s\n", line.c_str());
        }
        // taken objects stay readable (attributes) during the call and leave the pool after it
        Ctx c{};
        c.node = (int)i;
        c.in = in.data();
        c.in_slot = in_slot.data();
        c.pbuf = pbuf.data();
        c.plen = plen.data();
        c.pform = nd.forms.data();
        c.n_out = (int)e.out_types.size();
        g_cur_consumed = 0;
        g_cur_in_slots = in_slot;
        g_last_miss = 0;
        g_pending.clear();
        size_t tmp_before = g_tmp_files.size();
        const char* bail_reason = nullptr;
        if (g_plan) {
            plan_call(i, e, nd);
            g_plan_src_next = 0;
        }
        try {
            e.inv(c);
            res.status = STITCH_OK;
        } catch (BailSignal& b) {
            res.status = STITCH_BAIL;
            bail_reason = b.reason;
        } catch (FindingSignal&) {
            return finding_exit();
        } catch (std::exception& ex) {
            if (!exc_bail) {
                R->crash_pc = (1ull << 63) | fnv1a(ex.what(), strlen(ex.what())) | ((uint64_t)nd.ep << 32);
                if (fuzz_mode() && !trace) return finding_exit();
                fprintf(stderr, "\n[stitch] UNCAUGHT EXCEPTION in %s: %s\n", e.name.c_str(), ex.what());
                fflush(stderr);
                abort();
            }
            res.status = STITCH_BAIL;
            bail_reason = "exception";
        } catch (...) {
            if (!exc_bail) {
                R->crash_pc = (1ull << 63) | fnv1a("non-std", 7) | ((uint64_t)nd.ep << 32);
                if (fuzz_mode() && !trace) return finding_exit();
                fprintf(stderr, "\n[stitch] UNCAUGHT EXCEPTION in %s (non-std)\n", e.name.c_str());
                fflush(stderr);
                abort();
            }
            res.status = STITCH_BAIL;
            bail_reason = "exception";
        }
        // free taken / consumed inputs
        for (size_t k = 0; k < in_slot.size(); k++) {
            Slot& s = g_pool[in_slot[k]];
            bool gone = e.in_take[k] || (g_cur_consumed & (1u << k));
            if (gone) {
                s.alive = false;
                if (s.storage) rg.types[s.type]->free_storage(s.storage);
                s.storage = nullptr;
                if (!e.in_take[k]) res.consumed |= (1u << k);
            }
        }
        // register outputs
        if (res.status == STITCH_OK) {
            for (int k = 0; k < c.n_out; k++) {
                if (!c.out_present[k]) continue;
                TypeOps* ops = rg.types[e.out_types[k]];
                if (ops->handle_is_ptr && *(void**)c.out_storage[k] == nullptr) {
                    // objects are never null: treat as a bail of this call
                    ops->free_storage(c.out_storage[k]);
                    c.out_present[k] = false;
                    continue;
                }
                Slot s{e.out_types[k], c.out_storage[k], true, g_gen_counter++, (int)i, k, {}};
                if (c.out_attrs[k]) s.attrs = *c.out_attrs[k];
                if (ops->handle_is_ptr) {
                    void* p = *(void**)c.out_storage[k];
                    for (auto& pa : g_pending)
                        if (pa.first == p) s.attrs.m[pa.second.first] = pa.second.second;
                }
                g_pool.push_back(std::move(s));
                g_obj_of[{(int)i, k}] = (int)g_pool.size() - 1;
                res.out_mask |= (uint16_t)(1u << k);
            }
            bool any_required_missing = false;
            for (int k = 0; k < c.n_out; k++)
                if (!e.out_opt[k] && !(res.out_mask & (1u << k))) any_required_missing = true;
            if (any_required_missing) {
                // a call that returns a null required object failed; objects it did produce stay
                res.status = STITCH_BAIL;
                bail_reason = "returned null object";
            }
        } else {
            for (int k = 0; k < c.n_out; k++)
                if (c.out_present[k]) rg.types[e.out_types[k]]->free_storage(c.out_storage[k]);
        }
        if (res.status == STITCH_BAIL) {
            res.reason = intern(bail_reason ? bail_reason : "bail");
            res.miss_key = g_last_miss;
        }
        if (g_plan) {
            std::string l = "{\"t\":\"done\",\"node\":" + std::to_string(i) + ",\"status\":\"" +
                            (res.status == STITCH_OK ? "ok" : "bail") + "\"";
            if (res.status == STITCH_BAIL) {
                l += ",\"reason\":";
                json_str(l, bail_reason ? bail_reason : "bail");
            }
            l += ",\"outs\":[";
            bool first = true;
            for (int k = 0; k < c.n_out; k++)
                if (res.out_mask & (1u << k)) {
                    l += (first ? "" : ",") + std::to_string(k);
                    first = false;
                }
            l += "],\"consumed\":[";
            first = true;
            for (size_t k = 0; k < in_slot.size(); k++)
                if (res.consumed & (1u << k)) {
                    l += (first ? "" : ",") + std::to_string(k);
                    first = false;
                }
            plan_line(l + "]}");
        }
        if (trace) {
            if (res.status == STITCH_OK) {
                std::string outs;
                for (int k = 0; k < c.n_out; k++)
                    if (res.out_mask & (1u << k)) outs += " o" + std::to_string(i) + "." + std::to_string(k);
                fprintf(stderr, "[stitch]     -> ok%s%s\n", outs.empty() ? "" : " produced", outs.c_str());
            } else {
                fprintf(stderr, "[stitch]     -> bail \"%s\"\n", bail_reason ? bail_reason : "bail");
            }
        }
        for (size_t t = tmp_before; t < g_tmp_files.size(); t++) unlink(g_tmp_files[t].c_str());
        g_tmp_files.resize(tmp_before);
    }
    // finalize leftovers with STITCH_DROP
    R->current = (int32_t)nodes.size();
    g_cur_node = (int)nodes.size();
    // reverse creation order: dependents (values) go before what they depend on (contexts)
    if (g_plan) {
        std::string l = "{\"t\":\"drops\",\"objs\":[";
        bool first = true;
        for (auto it = g_pool.rbegin(); it != g_pool.rend(); ++it) {
            if (!it->alive || !it->storage || !rg.types[it->type]->drop) continue;
            l += std::string(first ? "" : ",") + "[" + std::to_string(it->node) + "," + std::to_string(it->out) + "]";
            first = false;
        }
        plan_line(l + "]}");
    }
    for (auto it = g_pool.rbegin(); it != g_pool.rend(); ++it) {
        Slot& s = *it;
        if (!s.alive || !s.storage) continue;
        TypeOps* ops = rg.types[s.type];
        if (ops->drop) {
            try {
                ops->drop(s.storage);
            } catch (...) {
            }
        }
    }
    g_in_exec = false;
    g_cur_node = -1;
    R->current = -1;
    plan_line("{\"t\":\"end\"}");
    return 0;
}

}  // namespace stitch

// ============================================================================================
// C ABI
// ============================================================================================
using namespace stitch;
using namespace stitch::detail;

extern "C" {

const char* stitch_manifest(void) {
    static std::string m;
    m = build_manifest();
    return m.c_str();
}

void stitch_init_once(void) {
    static bool done = false;
    if (done) return;
    done = true;
    results();
    cmplog();
    for (auto fn : reg().inits) fn();
}

int stitch_exec(const uint8_t* prog, size_t len, uint32_t flags) {
    std::vector<PNode> nodes;
    if (!parse_program(prog, len, nodes)) {
        results()->n_nodes = 0;
        if (flags & 1) fprintf(stderr, "[stitch] invalid program\n");
        return -1;
    }
    return run_program(nodes, flags & 1);
}

void stitch_set_plan(const char* path) {
    if (g_plan) fclose(g_plan);
    g_plan = path ? fopen(path, "w") : nullptr;
    if (path && !g_plan) fprintf(stderr, "[stitch] cannot write %s\n", path);
}

int stitch_run_file(const char* path, uint32_t flags) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[stitch] cannot open %s\n", path);
        return -1;
    }
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(f);
    return stitch_exec(data.data(), data.size(), flags);
}

uint8_t* stitch_edges(size_t* n_edges) {
    if (!g_edges) g_edges = (uint8_t*)shared_alloc(STITCH_EDGES_SIZE);
    if (n_edges) *n_edges = g_edges_n ? g_edges_n : 1;
    return g_edges;
}
StitchResults* stitch_results(void) { return results(); }
StitchCmplog* stitch_cmplog(void) { return cmplog(); }

int stitch_datatype_count(void) { return (int)reg().datatypes.size(); }
int stitch_has_decoder(int dt) { return dt >= 0 && dt < (int)reg().datatypes.size() && reg().datatypes[dt].decoder; }

size_t stitch_decode(int dt, const uint8_t* choices, size_t n, uint8_t* out, size_t cap) {
    auto v = decode_param(dt, choices, n, 1, cap);
    memcpy(out, v.data(), v.size());
    return v.size();
}

size_t stitch_generate(int dt, uint64_t seed, const uint8_t* prefix, size_t plen, uint8_t* out, size_t cap) {
    auto& r = reg();
    if (dt < 0 || dt >= (int)r.datatypes.size() || !r.datatypes[dt].decoder) return 0;
    Source s(prefix, std::min(plen, cap), cap);
    s.impl()->gen = true;
    s.impl()->rng.seed(seed);
    s.impl()->dict = &r.datatypes[dt].dict;
    try {
        r.datatypes[dt].decoder(&s);
    } catch (...) {
    }
    auto& b = s.impl()->buf;
    size_t n = std::min(b.size(), cap);
    memcpy(out, b.data(), n);
    return n;
}

size_t stitch_decode_spans(int dt, const uint8_t* choices, size_t n, uint32_t* out, size_t max_spans) {
    auto& r = reg();
    if (dt < 0 || dt >= (int)r.datatypes.size() || !r.datatypes[dt].decoder) return 0;
    Source s(choices, n, n);
    std::vector<uint32_t> spans;
    s.impl()->spans = &spans;
    try {
        r.datatypes[dt].decoder(&s);
    } catch (...) {
    }
    size_t k = std::min(spans.size() / 3, max_spans);
    memcpy(out, spans.data(), k * 3 * sizeof(uint32_t));
    return k;
}

void stitch_dict_add(int dt, const uint8_t* tok, size_t len) {
    auto& r = reg();
    if (dt < 0 || dt >= (int)r.datatypes.size()) return;
    auto& d = r.datatypes[dt].dict;
    std::string t((const char*)tok, len);
    if (d.size() < 4096 && std::find(d.begin(), d.end(), t) == d.end()) d.push_back(t);
}

// ------------------------------------------------------------------------------------------
// SanitizerCoverage: edges (hit counts) and comparisons (cmplog)
// ------------------------------------------------------------------------------------------
void __sanitizer_cov_trace_pc_guard_init(uint32_t* start, uint32_t* stop) {
    if (start == stop || *start) return;
    stitch_edges(nullptr);
    for (uint32_t* x = start; x < stop; x++) {
        g_edges_n++;
        *x = g_edges_n % STITCH_EDGES_SIZE;
    }
}

void __sanitizer_cov_trace_pc_guard(uint32_t* guard) {
    uint8_t& c = g_edges[*guard];
    c = (uint8_t)(c + 1) ? (uint8_t)(c + 1) : 1;
}

static inline bool cmplog_on() { return g_in_exec && g_cmplog && g_cmplog->enabled && g_cur_node >= 0; }

static void cmp_record_int(uint64_t a, uint64_t b, uint8_t size, bool a_const) {
    if (a == b) return;
    uint64_t h = (a * 0x9E3779B97F4A7C15ull) ^ (b + size) ^ ((uint64_t)g_cur_node << 48);
    uint64_t bit = h % 65536;
    if (g_cmp_seen[bit / 64] & (1ull << (bit % 64))) return;
    g_cmp_seen[bit / 64] |= 1ull << (bit % 64);
    StitchCmplog* L = g_cmplog;
    if (L->count >= STITCH_CMPLOG_RECORDS) return;
    StitchCmpRecord& r = L->records[L->count++];
    r.node = (uint16_t)g_cur_node;
    r.kind = 0;
    r.size = size;
    r.a_const = a_const;
    r.len_a = r.len_b = size;
    memcpy(r.a, &a, 8);
    memcpy(r.b, &b, 8);
}

static void cmp_record_bytes(const void* a, size_t la, const void* b, size_t lb) {
    if (!a || !b) return;
    la = std::min<size_t>(la, 32);
    lb = std::min<size_t>(lb, 32);
    if (la == 0 || lb == 0) return;
    uint32_t h = fnv1a(a, la) ^ (fnv1a(b, lb) * 31u) ^ ((uint32_t)g_cur_node << 20);
    uint32_t bit = h % 65536;
    if (g_cmp_seen[bit / 64] & (1ull << (bit % 64))) return;
    g_cmp_seen[bit / 64] |= 1ull << (bit % 64);
    StitchCmplog* L = g_cmplog;
    if (L->count >= STITCH_CMPLOG_RECORDS) return;
    StitchCmpRecord& r = L->records[L->count++];
    r.node = (uint16_t)g_cur_node;
    r.kind = 1;
    r.size = 0;
    r.a_const = 0;
    r.len_a = (uint8_t)la;
    r.len_b = (uint8_t)lb;
    memcpy(r.a, a, la);
    memcpy(r.b, b, lb);
}

void __sanitizer_cov_trace_cmp1(uint8_t a, uint8_t b) { if (cmplog_on()) cmp_record_int(a, b, 1, false); }
void __sanitizer_cov_trace_cmp2(uint16_t a, uint16_t b) { if (cmplog_on()) cmp_record_int(a, b, 2, false); }
void __sanitizer_cov_trace_cmp4(uint32_t a, uint32_t b) { if (cmplog_on()) cmp_record_int(a, b, 4, false); }
void __sanitizer_cov_trace_cmp8(uint64_t a, uint64_t b) { if (cmplog_on()) cmp_record_int(a, b, 8, false); }
void __sanitizer_cov_trace_const_cmp1(uint8_t a, uint8_t b) { if (cmplog_on()) cmp_record_int(a, b, 1, true); }
void __sanitizer_cov_trace_const_cmp2(uint16_t a, uint16_t b) { if (cmplog_on()) cmp_record_int(a, b, 2, true); }
void __sanitizer_cov_trace_const_cmp4(uint32_t a, uint32_t b) { if (cmplog_on()) cmp_record_int(a, b, 4, true); }
void __sanitizer_cov_trace_const_cmp8(uint64_t a, uint64_t b) { if (cmplog_on()) cmp_record_int(a, b, 8, true); }
void __sanitizer_cov_trace_switch(uint64_t val, uint64_t* cases) {
    if (!cmplog_on()) return;
    uint64_t n = cases[0], bits = cases[1];
    uint8_t size = (uint8_t)(bits / 8);
    for (uint64_t i = 0; i < n && i < 64; i++) cmp_record_int(cases[2 + i], val, size ? size : 8, true);
}
void __sanitizer_cov_trace_div4(uint32_t) {}
void __sanitizer_cov_trace_div8(uint64_t) {}
void __sanitizer_cov_trace_gep(uintptr_t) {}
void __sanitizer_cov_trace_pc_indir(uintptr_t) {}

void __sanitizer_weak_hook_memcmp(void*, const void* s1, const void* s2, size_t n, int result) {
    if (cmplog_on() && result != 0) cmp_record_bytes(s1, n, s2, n);
}
void __sanitizer_weak_hook_strncmp(void*, const char* s1, const char* s2, size_t n, int result) {
    if (cmplog_on() && result != 0) cmp_record_bytes(s1, strnlen(s1, n), s2, strnlen(s2, n));
}
void __sanitizer_weak_hook_strcmp(void*, const char* s1, const char* s2, int result) {
    if (cmplog_on() && result != 0) cmp_record_bytes(s1, strlen(s1), s2, strlen(s2));
}
void __sanitizer_weak_hook_strncasecmp(void*, const char* s1, const char* s2, size_t n, int result) {
    if (cmplog_on() && result != 0) cmp_record_bytes(s1, strnlen(s1, n), s2, strnlen(s2, n));
}
void __sanitizer_weak_hook_strcasecmp(void*, const char* s1, const char* s2, int result) {
    if (cmplog_on() && result != 0) cmp_record_bytes(s1, strlen(s1), s2, strlen(s2));
}
void __sanitizer_weak_hook_strstr(void*, const char* s1, const char* s2, char* result) {
    if (cmplog_on() && !result) cmp_record_bytes(s1, strlen(s1), s2, strlen(s2));
}
void __sanitizer_weak_hook_strcasestr(void*, const char* s1, const char* s2, char* result) {
    if (cmplog_on() && !result) cmp_record_bytes(s1, strlen(s1), s2, strlen(s2));
}
void __sanitizer_weak_hook_memmem(void*, const void* s1, size_t l1, const void* s2, size_t l2, void* result) {
    if (cmplog_on() && !result) cmp_record_bytes(s1, l1, s2, l2);
}

// Reset per-exec dedupe of comparison records (called by the engine before a tracing run).
void stitch_cmplog_reset(void) {
    memset(g_cmp_seen, 0, sizeof g_cmp_seen);
    cmplog()->count = 0;
}

// ASan error hook: remember where the bug is so the engine can keep distinct crashes apart.
uintptr_t __asan_get_report_pc() __attribute__((weak));
void __asan_on_error() {
    if (g_results && __asan_get_report_pc) g_results->crash_pc = (uint64_t)__asan_get_report_pc();
}

// Sanitizer defaults: crash on the first error with SIGABRT so the fork executor sees it.
const char* __asan_default_options() {
    return "abort_on_error=1:detect_leaks=0:symbolize=1:allocator_may_return_null=1:handle_abort=1:"
           "detect_stack_use_after_return=0:malloc_context_size=10";
}
const char* __ubsan_default_options() { return "print_stacktrace=1:halt_on_error=1:abort_on_error=1"; }

}  // extern "C"
