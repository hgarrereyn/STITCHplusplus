// stitch_standalone.h - run STITCH++ harness code without the fuzzer.
//
// `stitch write` turns a crashing program into a plain C++ file: every call's harness code is
// pasted into main() with its arguments spelled out. This header provides the harness API that
// code uses (Borrow/Take/New, attributes, require/check, Source, parameter types) with simple,
// single-threaded semantics that match the fuzzing runtime:
//   - objects live in cells; Take<T> and Borrow<T>::consume() retire the object when the call ends,
//     attributes belong to the cell (a consumed object's attributes are gone)
//   - stitch::require/bail inside a call that bailed in the original run is expected
//     (stitch::call_bails); anywhere else it means the reproducer diverged (exit 99)
//   - stitch::check failures abort with the same "[stitch] CHECK FAILED" line as the fuzzer
// Nothing here is needed once a reproducer is minimized down to plain library calls.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <unistd.h>

namespace stitch {

struct Bail {
    const char* reason;
};

namespace detail {
inline int& cur_call() {
    static int n = -1;
    return n;
}
inline const char*& cur_ep() {
    static const char* e = "";
    return e;
}
inline bool trace_on() {
    static int t = -1;
    if (t < 0) t = getenv("STITCH_REPRO_TRACE") != nullptr;
    return t == 1;
}
// STITCH_REPRO_TRACE=1: one line per call with its outcome (used to compare with the fuzzer's run)
inline void trace(int n, const char* ep, const char* status, const char* reason = nullptr) {
    if (!trace_on()) return;
    fprintf(stderr, "[repro] #%d %s %s%s%s\n", n, ep, status, reason ? " " : "", reason ? reason : "");
}
}  // namespace detail

[[noreturn]] inline void bail(const char* reason = "bail") { throw Bail{reason ? reason : "bail"}; }
inline void require(bool cond, const char* reason) {
    if (!cond) bail(reason);
}
inline void check(bool cond, const char* what) {
    if (cond) return;
    fprintf(stderr, "\n[stitch] CHECK FAILED: %s (node %d)\n", what ? what : "?", detail::cur_call());
    fflush(stderr);
    abort();
}
inline void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline void log(const char*, ...) {}

// ---------------------------------------------------------------------------------------------
// Object cells and attributes
// ---------------------------------------------------------------------------------------------
namespace detail {
struct Cell;
struct Value {
    int kind = 0;  // 0 int, 1 str, 2 obj
    int64_t i = 0;
    std::string s;
    std::weak_ptr<Cell> o;
};
struct AttrMap {
    std::map<std::string, Value> m;
};
struct Cell {
    const void* type;
    bool handle_is_ptr;
    void* h;                      // heap copy of the handle
    void (*free_h)(void*);
    bool alive = true;
    bool retire = false;          // consumed / taken: leaves the pool when the current call ends
    AttrMap attrs;
};
inline std::vector<std::shared_ptr<Cell>>& cells() {
    static std::vector<std::shared_ptr<Cell>> v;
    return v;
}
inline std::vector<std::pair<const void*, std::pair<std::string, Value>>>& pending() {
    static std::vector<std::pair<const void*, std::pair<std::string, Value>>> v;
    return v;
}
inline AttrMap& globals() {
    static AttrMap g;
    return g;
}
template <class T>
const void* type_tag() {
    static const char tag = 0;
    return &tag;
}
inline void kill(Cell* c) {
    if (!c->alive) return;
    c->alive = false;
    c->attrs.m.clear();
    c->free_h(c->h);
    c->h = nullptr;
}
// A pointer identifies an object if it is the object's pointer handle or the address of its handle.
inline std::shared_ptr<Cell> cell_for_ptr(const void* p) {
    if (!p) return nullptr;
    auto& v = cells();
    for (auto it = v.rbegin(); it != v.rend(); ++it) {
        Cell& c = **it;
        if (!c.alive) continue;
        if (c.h == p || (c.handle_is_ptr && *(void**)c.h == p)) return *it;
    }
    return nullptr;
}
[[noreturn]] inline void diverged(const std::string& why) {
    fprintf(stderr, "\n[repro] DIVERGED at call #%d %s: %s\n", cur_call(), cur_ep(), why.c_str());
    fflush(stderr);
    exit(99);
}
inline void note_missing(const char*) {}
}  // namespace detail

template <class T>
class Obj;

template <class T>
class Borrow {
   public:
    using H = typename T::handle;
    Borrow(const Obj<T>& o);
    H& operator*() const { return *static_cast<H*>(c_->h); }
    auto operator->() const {
        if constexpr (std::is_pointer<H>::value) return *static_cast<H*>(c_->h);
        else return static_cast<H*>(c_->h);
    }
    operator H&() const { return **this; }
    H& get() const { return **this; }
    void consume() const { c_->retire = true; }
    detail::Cell* cell() const { return c_; }

   private:
    detail::Cell* c_;
};

template <class T>
class Take {
   public:
    using H = typename T::handle;
    Take(const Obj<T>& o);
    H& operator*() const { return *static_cast<H*>(c_->h); }
    auto operator->() const {
        if constexpr (std::is_pointer<H>::value) return *static_cast<H*>(c_->h);
        else return static_cast<H*>(c_->h);
    }
    operator H&() const { return **this; }
    H& get() const { return **this; }
    detail::Cell* cell() const { return c_; }

   private:
    detail::Cell* c_;
};

template <class T>
struct New {
    using H = typename T::handle;
    New(H v) : h(v) {}
    H h;
    std::shared_ptr<detail::AttrMap> attrs;
    H& operator*() { return h; }
    operator H&() { return h; }
};

namespace detail {
template <class T>
struct DropHolder {
    static inline void (*fn)(typename T::handle) = nullptr;
};
template <class T, class F>
int set_drop_fn(F f) {
    DropHolder<T>::fn = +f;
    return 0;
}
}  // namespace detail

// A pooled object in a standalone reproducer.
template <class T>
class Obj {
   public:
    using H = typename T::handle;
    Obj() = default;
    Obj(New<T> n) {
        auto c = std::make_shared<detail::Cell>();
        c->type = detail::type_tag<T>();
        c->handle_is_ptr = std::is_pointer<H>::value;
        c->h = new H(n.h);
        c->free_h = [](void* p) { delete static_cast<H*>(p); };
        if (n.attrs) c->attrs = *n.attrs;
        if constexpr (std::is_pointer<H>::value) {
            for (auto& pa : detail::pending())
                if (pa.first == (const void*)n.h) c->attrs.m[pa.second.first] = pa.second.second;
        }
        detail::cells().push_back(c);
        c_ = c;
    }
    bool alive() const { return c_ && c_->alive; }
    detail::Cell* cell() const { return c_.get(); }
    H& operator*() const { return *static_cast<H*>(c_->h); }
    // STITCH_DROP finalizer (what the fuzzer runs for objects still alive at the end)
    void drop() {
        if (!alive()) return;
        if (detail::DropHolder<T>::fn) detail::DropHolder<T>::fn(**this);
        detail::kill(c_.get());
    }

   private:
    std::shared_ptr<detail::Cell> c_;
};

template <class T>
Borrow<T>::Borrow(const Obj<T>& o) : c_(o.cell()) {
    if (!o.alive()) detail::diverged("an input object is gone (consumed earlier)");
}
template <class T>
Take<T>::Take(const Obj<T>& o) : c_(o.cell()) {
    if (!o.alive()) detail::diverged("an input object is gone (consumed earlier)");
    c_->retire = true;
}

// Output of a call result: New<T> or std::optional<New<T>> (tuples: std::get<k>(r) first).
template <class T>
New<T> unwrap(std::optional<New<T>>& o) {
    if (!o) detail::diverged("an optional output that the original run produced is missing");
    return *o;
}
template <class T>
New<T> unwrap(New<T>& n) {
    return n;
}

// ------------------------------------------------------------------------------- calls
namespace detail {
inline void call_begin(int n, const char* ep) {
    cur_call() = n;
    cur_ep() = ep;
    pending().clear();
}
inline void call_end() {
    for (auto& c : cells())
        if (c->alive && c->retire) kill(c.get());
}
}  // namespace detail

// Run one call (a pasted endpoint body). A bail here means the reproducer diverged.
template <class F>
auto call(int n, const char* ep, F f) -> decltype(f()) {
    detail::call_begin(n, ep);
    try {
        if constexpr (std::is_void<decltype(f())>::value) {
            f();
            detail::call_end();
            detail::trace(n, ep, "ok");
        } else {
            auto r = f();
            detail::call_end();
            detail::trace(n, ep, "ok");
            return r;
        }
    } catch (Bail& b) {
        detail::call_end();
        detail::trace(n, ep, "bail", b.reason);
        detail::diverged(std::string("bailed (\"") + b.reason + "\"), but the original run did not");
    } catch (std::exception& e) {
        fprintf(stderr, "\n[stitch] UNCAUGHT EXCEPTION in %s: %s\n", ep, e.what());
        fflush(stderr);
        abort();
    }
}

// A call that bailed in the original run: its side effects up to the bail still happen.
template <class F>
void call_bails(int n, const char* ep, F f) {
    detail::call_begin(n, ep);
    try {
        f();
        detail::trace(n, ep, "ok");
        fprintf(stderr, "[repro] note: call #%d %s did not bail as in the original run\n", n, ep);
    } catch (Bail& b) {
        detail::trace(n, ep, "bail", b.reason);
    } catch (std::exception& e) {
        fprintf(stderr, "\n[stitch] UNCAUGHT EXCEPTION in %s: %s\n", ep, e.what());
        fflush(stderr);
        abort();
    }
    detail::call_end();
}

// ------------------------------------------------------------------------------- attribute API
namespace detail {
template <class V>
struct IsWrapper : std::false_type {};
template <class T>
struct IsWrapper<Borrow<T>> : std::true_type {};
template <class T>
struct IsWrapper<Take<T>> : std::true_type {};
template <class T>
struct IsWrapper<Obj<T>> : std::true_type {};

template <class O>
std::shared_ptr<Cell> target(const O& o) {
    if constexpr (IsWrapper<O>::value) {
        for (auto& c : cells())
            if (c.get() == o.cell()) return c->alive ? c : nullptr;
        return nullptr;
    } else if constexpr (std::is_pointer<O>::value) {
        return cell_for_ptr(static_cast<const void*>(o));
    } else {
        static_assert(IsWrapper<O>::value || std::is_pointer<O>::value,
                      "attribute target must be Borrow<T>, Take<T>, New<T> or a pointer handle");
        return nullptr;
    }
}
template <class V>
Value make_value(const V& v) {
    using D = std::decay_t<V>;
    Value x;
    if constexpr (std::is_integral<D>::value || std::is_enum<D>::value) {
        x.kind = 0;
        x.i = (int64_t)v;
    } else if constexpr (std::is_same<D, std::string>::value || std::is_same<D, const char*>::value ||
                         std::is_same<D, char*>::value) {
        x.kind = 1;
        x.s = std::string(v);
    } else {
        x.kind = 2;
        x.o = target(v);
    }
    return x;
}
inline const Value* find(const std::shared_ptr<Cell>& c, const char* key) {
    if (!c) return nullptr;
    auto it = c->attrs.m.find(key);
    return it == c->attrs.m.end() ? nullptr : &it->second;
}
}  // namespace detail

template <class O, class V>
void set(const O& obj, const char* key, const V& v) {
    detail::Value x = detail::make_value(v);
    if (auto c = detail::target(obj)) c->attrs.m[key] = x;
    else if constexpr (std::is_pointer<O>::value) detail::pending().push_back({(const void*)obj, {key, x}});
}
template <class T, class V>
void set(New<T>& obj, const char* key, const V& v) {
    if (!obj.attrs) obj.attrs = std::make_shared<detail::AttrMap>();
    obj.attrs->m[key] = detail::make_value(v);
}
template <class O>
int64_t get(const O& obj, const char* key, int64_t def = 0) {
    auto* v = detail::find(detail::target(obj), key);
    return v && v->kind == 0 ? v->i : def;
}
template <class T>
int64_t get(const New<T>& obj, const char* key, int64_t def = 0) {
    if (!obj.attrs) return def;
    auto it = obj.attrs->m.find(key);
    return it != obj.attrs->m.end() && it->second.kind == 0 ? it->second.i : def;
}
template <class O>
std::string get_str(const O& obj, const char* key, const std::string& def = "") {
    auto* v = detail::find(detail::target(obj), key);
    return v && v->kind == 1 ? v->s : def;
}
template <class T, class O>
typename T::handle* get_obj(const O& obj, const char* key) {
    auto* v = detail::find(detail::target(obj), key);
    if (!v || v->kind != 2) return nullptr;
    auto c = v->o.lock();
    if (!c || !c->alive || c->type != detail::type_tag<T>()) return nullptr;
    return static_cast<typename T::handle*>(c->h);
}
template <class O>
bool has(const O& obj, const char* key) {
    return detail::find(detail::target(obj), key) != nullptr;
}
template <class O>
void del(const O& obj, const char* key) {
    if (auto c = detail::target(obj)) c->attrs.m.erase(key);
}

inline void global_set(const char* key, int64_t v) { detail::globals().m[key] = detail::make_value(v); }
inline void global_set_str(const char* key, const std::string& v) { detail::globals().m[key] = detail::make_value(v); }
inline int64_t global_get(const char* key, int64_t def = 0) {
    auto& m = detail::globals().m;
    auto it = m.find(key);
    return it == m.end() || it->second.kind != 0 ? def : it->second.i;
}
inline std::string global_get_str(const char* key, const std::string& def = "") {
    auto& m = detail::globals().m;
    auto it = m.find(key);
    return it == m.end() || it->second.kind != 1 ? def : it->second.s;
}
inline bool global_has(const char* key) { return detail::globals().m.count(key) > 0; }

// ---------------------------------------------------------------------------------------------
// Source: replays the recorded choice bytes with the fuzzer's decoding
// ---------------------------------------------------------------------------------------------
class Source {
   public:
    Source(const char* b, size_t len) : buf_((const uint8_t*)b, (const uint8_t*)b + len) {}
    uint8_t u8() { return (uint8_t)uint(1); }
    uint16_t u16() { return (uint16_t)uint(2); }
    uint32_t u32() { return (uint32_t)uint(4); }
    uint64_t u64() { return uint(8); }
    int8_t i8() { return (int8_t)u8(); }
    int16_t i16() { return (int16_t)u16(); }
    int32_t i32() { return (int32_t)u32(); }
    int64_t i64() { return (int64_t)u64(); }
    float f32() {
        uint32_t b = u32();
        float f;
        memcpy(&f, &b, 4);
        return f;
    }
    double f64() {
        uint64_t b = u64();
        double f;
        memcpy(&f, &b, 8);
        return f;
    }
    int64_t range(int64_t lo, int64_t hi) {
        if (hi < lo) std::swap(lo, hi);
        int64_t raw = (int64_t)uint(8);
        if (raw >= lo && raw <= hi) return raw;
        uint64_t span = (uint64_t)hi - (uint64_t)lo + 1;
        uint64_t off = (uint64_t)raw - (uint64_t)lo;
        return (int64_t)((uint64_t)lo + (span ? off % span : off));
    }
    size_t choice(size_t n) { return n == 0 ? 0 : (size_t)range(0, (int64_t)n - 1); }
    bool coin(double p = 0.5) { return (double)(uint8_t)uint(1) < p * 256.0; }
    bool more() {
        if (exhausted_) return false;
        uint8_t b = (uint8_t)uint(1);
        return (b & 1) && !exhausted_;
    }
    std::string str(size_t max) {
        auto v = blob(max);
        return std::string(v.begin(), v.end());
    }
    std::vector<uint8_t> bytes(size_t max) { return blob(max); }
    std::string token() { return str(255); }
    double f64(double lo, double hi) {
        if (!(lo <= hi)) std::swap(lo, hi);
        uint64_t bits = uint(8);
        double v;
        memcpy(&v, &bits, 8);
        if (std::isfinite(v) && v >= lo && v <= hi) return v;
        double p = (double)(bits >> 11) * (1.0 / 9007199254740992.0);
        double w = hi - lo;
        double r = std::isfinite(w) ? lo + w * p : (p < 0.5 ? lo + (hi / 2 - lo / 2) * (2 * p) : lo / 2 + hi / 2 + (hi / 2 - lo / 2) * (2 * p - 1));
        return std::min(hi, std::max(lo, r));
    }
    float f32(float lo, float hi) {
        if (!(lo <= hi)) std::swap(lo, hi);
        uint32_t bits = (uint32_t)uint(4);
        float v;
        memcpy(&v, &bits, 4);
        if (std::isfinite(v) && v >= lo && v <= hi) return v;
        double p = (double)(bits >> 8) * (1.0 / 16777216.0);
        return (float)std::min<double>(hi, std::max<double>(lo, lo + ((double)hi - lo) * p));
    }
    std::vector<uint8_t> fixed(size_t n) {
        std::vector<uint8_t> out(n, 0);
        size_t k = std::min(n, remaining());
        if (k) memcpy(out.data(), buf_.data() + pos_, k);
        pos_ += k;
        if (k < n) exhausted_ = true;
        return out;
    }
    std::vector<uint8_t> rest() {
        std::vector<uint8_t> out(buf_.begin() + std::min(pos_, buf_.size()), buf_.end());
        pos_ = buf_.size();
        return out;
    }
    template <class Tag>
    auto draw();
    template <class T>
    T pick(std::initializer_list<T> xs) {
        return *(xs.begin() + choice(xs.size()));
    }
    const char* pick(std::initializer_list<const char*> xs) { return *(xs.begin() + choice(xs.size())); }
    size_t position() const { return pos_; }
    size_t remaining() const { return buf_.size() > pos_ ? buf_.size() - pos_ : 0; }
    bool exhausted() const { return exhausted_; }

   private:
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
    bool exhausted_ = false;
    uint64_t uint(size_t w) {
        uint64_t v = 0;
        size_t n = std::min(w, remaining());
        memcpy(&v, buf_.data() + pos_, n);
        pos_ += n;
        if (n < w) exhausted_ = true;
        return v;
    }
    std::vector<uint8_t> blob(size_t max) {
        size_t w = max < 256 ? 1 : max < 65536 ? 2 : 4;
        size_t len = (size_t)uint(w);
        if (len > max) len = len % (max + 1);
        size_t n = std::min(len, remaining());
        std::vector<uint8_t> out(buf_.begin() + pos_, buf_.begin() + pos_ + n);
        pos_ += n;
        if (n < len) exhausted_ = true;
        return out;
    }
};

// ---------------------------------------------------------------------------------------------
// Parameter types, constructed from the concrete values of the original run.
// Constructors are explicit (like the fuzzer's aggregate types, they do not convert implicitly)
// and pass values through opaque(): in the fuzzer every value arrives at run time, so the
// compiler must not constant-fold them here either (folding undefined behavior such as
// (int64_t)1e308 lets it miscompile the surrounding code).
// ---------------------------------------------------------------------------------------------
template <class T>
inline T opaque(T x) {
    asm volatile("" : : "r"(&x) : "memory");
    return x;
}

template <class T>
struct Num {
    T v{};
    Num() = default;
    explicit Num(T x) : v(opaque(x)) {}
    operator T() const { return v; }
    T value() const { return v; }
};
using U8 = Num<uint8_t>;
using U16 = Num<uint16_t>;
using U32 = Num<uint32_t>;
using U64 = Num<uint64_t>;
using I8 = Num<int8_t>;
using I16 = Num<int16_t>;
using I32 = Num<int32_t>;
using I64 = Num<int64_t>;
using F32 = Num<float>;
using F64 = Num<double>;

struct Bool {
    bool v = false;
    Bool() = default;
    explicit Bool(bool x) : v(opaque(x)) {}
    operator bool() const { return v; }
};

template <long long Lo, long long Hi>
struct Range {
    long long v = Lo;
    Range() = default;
    explicit Range(long long x) : v(opaque(x)) {}
    operator long long() const { return v; }
    long long value() const { return v; }
};

template <auto First, auto... Rest>
struct OneOf {
    using T = decltype(First);
    T v = First;
    OneOf() = default;
    explicit OneOf(T x) : v(opaque(x)) {}
    template <class I, class = std::enable_if_t<std::is_integral<I>::value && !std::is_same<I, T>::value>>
    explicit OneOf(I x) : v((T)opaque(x)) {}
    operator T() const { return v; }
    T value() const { return v; }
};

template <auto First, auto... Rest>
struct Flags {
    using T = decltype(First);
    T v{};
    Flags() = default;
    explicit Flags(long long x) : v((T)opaque(x)) {}
    operator T() const { return v; }
    T value() const { return v; }
};

template <size_t Max, class Tag = void>
struct Bytes {
    std::vector<uint8_t> buf;
    Bytes() = default;
    explicit Bytes(const char* p, size_t n) : buf((const uint8_t*)p, (const uint8_t*)p + n) {}
    const uint8_t* data() const { return buf.data(); }
    uint8_t* data() { return buf.data(); }
    size_t size() const { return buf.size(); }
    bool empty() const { return buf.empty(); }
    std::string str() const { return std::string(buf.begin(), buf.end()); }
    uint8_t operator[](size_t i) const { return buf[i]; }
};

template <size_t Max, class Tag = void>
struct Str {
    std::string s;
    Str() = default;
    explicit Str(const char* p) : s(p) {}
    explicit Str(const char* p, size_t n) : s(p, n) {}
    const char* c_str() const { return s.c_str(); }
    const char* data() const { return s.data(); }
    size_t size() const { return s.size(); }
    size_t length() const { return s.size(); }
    bool empty() const { return s.empty(); }
    operator const std::string&() const { return s; }
    operator const char*() const { return s.c_str(); }
    char operator[](size_t i) const { return s[i]; }
};

namespace detail {
struct TmpFile {
    std::string path;
    explicit TmpFile(const std::vector<uint8_t>& d) {
        char t[] = "/tmp/stitch-repro-XXXXXX";
        int fd = mkstemp(t);
        if (fd >= 0) {
            if (!d.empty() && write(fd, d.data(), d.size()) < 0) perror("write");
            close(fd);
        }
        path = t;
    }
    ~TmpFile() { unlink(path.c_str()); }
};
}  // namespace detail

template <size_t Max, class Tag = void>
struct File {
    std::vector<uint8_t> buf;
    std::shared_ptr<detail::TmpFile> f;
    File(const char* p, size_t n) : buf((const uint8_t*)p, (const uint8_t*)p + n), f(std::make_shared<detail::TmpFile>(buf)) {}
    const char* path() const { return f->path.c_str(); }
    const uint8_t* data() const { return buf.data(); }
    size_t size() const { return buf.size(); }
};

template <class T>
struct Raw {
    T v;
    Raw(const char* p, size_t n) {
        memset(&v, 0, sizeof(T));
        memcpy(&v, p, n < sizeof(T) ? n : sizeof(T));
    }
    operator T&() { return v; }
    operator const T&() const { return v; }
    T* operator->() { return &v; }
    const T* operator->() const { return &v; }
    T& operator*() { return v; }
};

namespace detail {
template <class T>
void stitch_decoder_fn(T*) = delete;
template <class Tag>
using decoded_t = std::decay_t<decltype(stitch_decoder_fn((Tag*)nullptr)(std::declval<::stitch::Source&>()))>;
}  // namespace detail
template <class Tag>
auto Source::draw() {
    using detail::stitch_decoder_fn;
    return stitch_decoder_fn((Tag*)nullptr)(*this);
}

// A value of data type Tag, decoded again from the recorded choices by the harness's decoder.
template <class Tag, size_t Max = 1024>
struct Val {
    using value_type = detail::decoded_t<Tag>;
    value_type v;
    Val(const char* p, size_t n) : v(decode(p, n)) {}
    static value_type decode(const char* p, size_t n) {
        Source s(p, n);
        return s.draw<Tag>();
    }
    operator value_type&() { return v; }
    operator const value_type&() const { return v; }
    value_type* operator->() { return &v; }
    const value_type* operator->() const { return &v; }
    value_type& operator*() { return v; }
    const value_type& operator*() const { return v; }
};

template <size_t Max>
struct Src {
    std::shared_ptr<Source> s;
    Src(const char* p, size_t n) : s(std::make_shared<Source>(p, n)) {}
    Source* operator->() const { return s.get(); }
    Source& operator*() const { return *s; }
    operator Source&() const { return *s; }
};

// ------------------------------------------------------------------------------- init
namespace detail {
inline std::vector<void (*)()>& inits() {
    static std::vector<void (*)()> v;
    return v;
}
inline int register_init(void (*fn)()) {
    inits().push_back(fn);
    return 0;
}
}  // namespace detail
// Library-global initialization (the harness's STITCH_INIT blocks).
inline void init() {
    for (auto fn : detail::inits()) fn();
}

}  // namespace stitch

#define STITCH_CAT2(a, b) a##b
#define STITCH_CAT(a, b) STITCH_CAT2(a, b)
#define STITCH_OBJECT(Name, Handle)                        \
    struct Name {                                          \
        using handle = Handle;                             \
        static const char* stitch_name() { return #Name; } \
    }
#define STITCH_DROP(Name, fn) \
    inline const int STITCH_CAT(stitch_dropreg_, Name) = ::stitch::detail::set_drop_fn<Name>(fn)
#define STITCH_DATATYPE(Name)                              \
    struct Name {                                          \
        static const char* stitch_name() { return #Name; } \
    }
#define STITCH_DECODER(Tag, ...) \
    [[maybe_unused]] inline auto stitch_decoder_fn(Tag*) { return __VA_ARGS__; }
#define STITCH_ENDPOINT(fn) static_assert(true, "")
#define STITCH_INIT                                                                                         \
    static void STITCH_CAT(stitch_init_fn_, __LINE__)();                                                   \
    static const int STITCH_CAT(stitch_initreg_, __LINE__) =                                               \
        ::stitch::detail::register_init(&STITCH_CAT(stitch_init_fn_, __LINE__));                           \
    static void STITCH_CAT(stitch_init_fn_, __LINE__)()

using stitch::Bool;
using stitch::Borrow;
using stitch::Bytes;
using stitch::F32;
using stitch::F64;
using stitch::File;
using stitch::Flags;
using stitch::I16;
using stitch::I32;
using stitch::I64;
using stitch::I8;
using stitch::New;
using stitch::Obj;
using stitch::OneOf;
using stitch::Range;
using stitch::Raw;
using stitch::Src;
using stitch::Str;
using stitch::Take;
using stitch::U16;
using stitch::U32;
using stitch::U64;
using stitch::U8;
