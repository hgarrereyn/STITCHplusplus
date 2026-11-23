// stitch.h - STITCH++ harness API (C++17).
//
// A harness is a set of endpoint functions. The function signature is the spec:
//
//   STITCH_OBJECT(Json, cJSON*);
//   STITCH_DATATYPE(JsonText);
//
//   New<Json> parse(Str<4096, JsonText> text) {
//       cJSON* root = cJSON_Parse(text.c_str());
//       stitch::require(root, "parse failed");
//       return root;
//   }
//   STITCH_ENDPOINT(parse);
//
// Reference: `stitch guide api` (skill/stitch/reference/harness-api.md).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <sstream>
#include <ostream>

namespace stitch {

// ---------------------------------------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------------------------------------

// Stop the current call: the call failed for a reason that is not a bug (bad input, unmet
// precondition). Objects this call would have produced are not created; the rest of the
// program keeps running. Give every bail a short, specific reason: it keys the statistics
// that `stitch probe` reports.
[[noreturn]] void bail(const char* reason = "bail");
inline void require(bool cond, const char* reason) {
    if (!cond) bail(reason);
}
// Semantic oracle: a failed check is reported as a crash ("check failed: <what>").
void check(bool cond, const char* what);
// Print a line in trace mode (`stitch exec --trace`); no-op while fuzzing.
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

namespace detail {

// ----------------------------------- registry descriptors -----------------------------------
enum class PKind : uint8_t { Int, Float, Bool, Range, OneOf, Flags, Bytes, Str, File, Raw, Source, Value };

struct ParamDesc {
    PKind kind;
    uint32_t size;  // byte size for fixed kinds, max length for Bytes/Str/File/Source
    bool is_signed;
    int64_t lo, hi;
    std::vector<int64_t> values;  // OneOf / Flags
    const char* datatype;         // tag name or nullptr
    const char* ctype;            // human readable type name
};

struct Ctx;
struct DatatypeInfo;
using Invoker = void (*)(Ctx&);

struct TypeOps {
    const char* name;
    size_t handle_size;
    bool handle_is_ptr;
    void (*free_storage)(void*);  // delete the heap copy of the handle
    void (*drop)(void*);          // user STITCH_DROP on the handle (may be null)
};

int register_object(TypeOps* ops);
void set_drop(int type, void (*drop)(void*));
int register_datatype(const char* name);
int register_decoder(const char* name, std::vector<uint8_t> (*fn)(void* source));
int register_init(void (*fn)());
int register_endpoint(const char* name, const char* file, Invoker inv, std::vector<int> in_types,
                      std::vector<bool> in_take, std::vector<int> out_types, std::vector<bool> out_opt,
                      std::vector<ParamDesc> params, std::vector<uint8_t> arg_is_obj);

// ------------------------------------- execution context ------------------------------------
struct AttrMap;
struct PendingAttrs;

struct Ctx {
    int node;
    void** in;                 // storage (heap Handle*) of each object input
    int* in_slot;              // pool slot of each object input
    const uint8_t* const* pbuf;
    const size_t* plen;
    const uint8_t* pform;      // 0 = value bytes, 1 = choice buffer for the datatype decoder
    // outputs written by the invoker
    int n_out;
    void* out_storage[16];
    bool out_present[16];
    std::shared_ptr<AttrMap> out_attrs[16];
};

void add_output(Ctx& c, int idx, void* storage, std::shared_ptr<AttrMap> attrs);
void consume_slot(int slot);
std::vector<uint8_t> decode_param(int datatype_id, const uint8_t* buf, size_t len, uint8_t form, size_t max);
std::string write_temp_file(const uint8_t* data, size_t len);
int datatype_id(const char* name);

template <class T>
struct ObjType {
    using H = typename T::handle;
    static void free_storage(void* p) { delete static_cast<H*>(p); }
    static int id() {
        static TypeOps ops{T::stitch_name(), sizeof(H), std::is_pointer<H>::value, &free_storage, nullptr};
        static int id = register_object(&ops);
        return id;
    }
};

template <class T>
struct DropHolder {
    using H = typename T::handle;
    static inline void (*fn)(H) = nullptr;
    static void call(void* storage) { fn(*static_cast<H*>(storage)); }
};

template <class T, class F>
int set_drop_fn(F f) {
    DropHolder<T>::fn = +f;
    set_drop(ObjType<T>::id(), &DropHolder<T>::call);
    return 0;
}

template <class Tag>
struct TagName {
    static const char* get() { return Tag::stitch_name(); }
};
template <>
struct TagName<void> {
    static const char* get() { return nullptr; }
};

}  // namespace detail

// ---------------------------------------------------------------------------------------------
// Attribute values and the attribute API (declared here, used by the object wrappers)
// ---------------------------------------------------------------------------------------------
namespace detail {
struct ObjRef {
    int slot;
    uint32_t gen;
};
int slot_for_ptr(const void* p);  // live object whose pointer handle equals p, or -1
void attr_set_int(int slot, const char* key, int64_t v);
void attr_set_str(int slot, const char* key, const std::string& v);
void attr_set_obj(int slot, const char* key, int target_slot);
bool attr_get_int(int slot, const char* key, int64_t* out);
bool attr_get_str(int slot, const char* key, std::string* out);
void* attr_get_obj(int slot, const char* key, int type);
bool attr_has(int slot, const char* key);
void attr_del(int slot, const char* key);
void note_missing(const char* key);
bool tracing();  // `stitch exec --trace` is running this program
// A bug that a language layer detected in the current call (Objective-C: an exception that
// escaped the stub). `key` groups the findings of the current stub. While fuzzing the program
// ends as a finding; otherwise `report` (kind line and frames) is printed and the process aborts.
[[noreturn]] void report_finding(uint32_t key, const char* report);
void pending_set_int(const void* p, const char* key, int64_t v);
void pending_set_str(const void* p, const char* key, const std::string& v);
void pending_set_obj(const void* p, const char* key, int target_slot);
std::shared_ptr<AttrMap> new_attr_map();
void map_set_int(AttrMap& m, const char* key, int64_t v);
void map_set_str(AttrMap& m, const char* key, const std::string& v);
void map_set_obj(AttrMap& m, const char* key, int target_slot);
bool map_get_int(const AttrMap& m, const char* key, int64_t* v);
bool map_get_str(const AttrMap& m, const char* key, std::string* v);
}  // namespace detail

// ---------------------------------------------------------------------------------------------
// Object wrappers
// ---------------------------------------------------------------------------------------------

// Borrow<T>: use an object from the pool; it stays in the pool afterwards. `*obj` is a reference
// to the pooled handle, so reassigning it (realloc-style APIs) updates the pool. Call
// `obj.consume()` when the library took ownership (conditional consumption).
template <class T>
class Borrow {
   public:
    using H = typename T::handle;
    Borrow(H* h, int slot) : h_(h), slot_(slot) {}
    static Borrow from_ctx(detail::Ctx& c, int i) { return Borrow(static_cast<H*>(c.in[i]), c.in_slot[i]); }
    H& operator*() const { return *h_; }
    auto operator->() const {
        if constexpr (std::is_pointer<H>::value) return *h_;
        else return h_;
    }
    operator H&() const { return *h_; }
    H& get() const { return *h_; }
    void consume() const { detail::consume_slot(slot_); }
    int slot() const { return slot_; }

   private:
    H* h_;
    int slot_;
};

// Take<T>: always consumes the object. The stub must free it or hand it to the library, also on
// every bail path after the point where ownership moved.
template <class T>
class Take {
   public:
    using H = typename T::handle;
    Take(H* h, int slot) : h_(h), slot_(slot) {}
    static Take from_ctx(detail::Ctx& c, int i) { return Take(static_cast<H*>(c.in[i]), c.in_slot[i]); }
    H& operator*() const { return *h_; }
    auto operator->() const {
        if constexpr (std::is_pointer<H>::value) return *h_;
        else return h_;
    }
    operator H&() const { return *h_; }
    H& get() const { return *h_; }
    int slot() const { return slot_; }

   private:
    H* h_;
    int slot_;
};

// New<T>: a produced object. Return it (or a std::tuple of New / std::optional<New>).
template <class T>
struct New {
    using H = typename T::handle;
    New(H v) : h(v) {}
    H h;
    std::shared_ptr<detail::AttrMap> attrs;
    H& operator*() { return h; }
    operator H&() { return h; }
};

// ---------------------------------------------------------------------------------------------
// Attribute API: free-form state attached to pooled objects (and globals).
// Keys belong to the pool object, not the address: consumed objects lose their attributes,
// and a relation to a consumed object reads as missing.
// ---------------------------------------------------------------------------------------------
namespace detail {
template <class V>
struct AttrValue;
template <class O>
struct SlotOf {
    static constexpr bool ok = false;
};
template <class T>
struct SlotOf<Borrow<T>> {
    static constexpr bool ok = true;
    static int get(const Borrow<T>& b) { return b.slot(); }
};
template <class T>
struct SlotOf<Take<T>> {
    static constexpr bool ok = true;
    static int get(const Take<T>& b) { return b.slot(); }
};
template <class V>
int target_slot(const V& v) {
    if constexpr (SlotOf<V>::ok) return SlotOf<V>::get(v);
    else if constexpr (std::is_pointer<V>::value) return slot_for_ptr(static_cast<const void*>(v));
    else return -1;
}
template <class V>
constexpr int value_kind() {  // 0 int, 1 string, 2 object
    using D = std::decay_t<V>;
    if constexpr (std::is_integral<D>::value || std::is_enum<D>::value) return 0;
    else if constexpr (std::is_same<D, std::string>::value || std::is_same<D, const char*>::value ||
                       std::is_same<D, char*>::value)
        return 1;
    else return 2;
}
template <class V>
std::string as_string(const V& v) {
    return std::string(v);
}
}  // namespace detail

template <class O, class V>
void set(const O& obj, const char* key, const V& v) {
    constexpr int k = detail::value_kind<V>();
    if constexpr (detail::SlotOf<O>::ok) {
        int s = detail::SlotOf<O>::get(obj);
        if constexpr (k == 0) detail::attr_set_int(s, key, (int64_t)v);
        else if constexpr (k == 1) detail::attr_set_str(s, key, detail::as_string(v));
        else detail::attr_set_obj(s, key, detail::target_slot(v));
    } else if constexpr (std::is_pointer<O>::value) {
        const void* p = static_cast<const void*>(obj);
        int s = detail::slot_for_ptr(p);
        if constexpr (k == 0) {
            if (s >= 0) detail::attr_set_int(s, key, (int64_t)v);
            else detail::pending_set_int(p, key, (int64_t)v);
        } else if constexpr (k == 1) {
            if (s >= 0) detail::attr_set_str(s, key, detail::as_string(v));
            else detail::pending_set_str(p, key, detail::as_string(v));
        } else {
            if (s >= 0) detail::attr_set_obj(s, key, detail::target_slot(v));
            else detail::pending_set_obj(p, key, detail::target_slot(v));
        }
    } else {
        static_assert(detail::SlotOf<O>::ok || std::is_pointer<O>::value,
                      "stitch::set target must be Borrow<T>, Take<T>, New<T> or a pointer handle");
    }
}

template <class T, class V>
void set(New<T>& obj, const char* key, const V& v) {
    constexpr int k = detail::value_kind<V>();
    if (!obj.attrs) obj.attrs = detail::new_attr_map();
    if constexpr (k == 0) detail::map_set_int(*obj.attrs, key, (int64_t)v);
    else if constexpr (k == 1) detail::map_set_str(*obj.attrs, key, detail::as_string(v));
    else detail::map_set_obj(*obj.attrs, key, detail::target_slot(v));
}

namespace detail {
template <class O>
int slot_of(const O& obj) {
    if constexpr (SlotOf<O>::ok) return SlotOf<O>::get(obj);
    else if constexpr (std::is_pointer<O>::value) return slot_for_ptr(static_cast<const void*>(obj));
    else {
        static_assert(SlotOf<O>::ok || std::is_pointer<O>::value,
                      "stitch::get target must be Borrow<T>, Take<T>, New<T> or a pointer handle");
        return -1;
    }
}
}  // namespace detail

// Integer attribute. Without a default, a missing key is reported by `stitch probe` ("read
// missing attr"); pass a default when absence is normal (e.g. a counter that starts at 0).
template <class O>
int64_t get(const O& obj, const char* key) {
    int64_t v;
    if (detail::attr_get_int(detail::slot_of(obj), key, &v)) return v;
    detail::note_missing(key);
    return 0;
}
template <class O>
int64_t get(const O& obj, const char* key, int64_t def) {
    int64_t v;
    return detail::attr_get_int(detail::slot_of(obj), key, &v) ? v : def;
}
template <class T>
int64_t get(const New<T>& obj, const char* key, int64_t def = 0) {
    int64_t v;
    return obj.attrs && detail::map_get_int(*obj.attrs, key, &v) ? v : def;
}
template <class O>
std::string get_str(const O& obj, const char* key) {
    std::string v;
    if (detail::attr_get_str(detail::slot_of(obj), key, &v)) return v;
    detail::note_missing(key);
    return "";
}
template <class O>
std::string get_str(const O& obj, const char* key, const std::string& def) {
    std::string v;
    return detail::attr_get_str(detail::slot_of(obj), key, &v) ? v : def;
}
// Related object stored with set(obj, key, other): pointer to its handle, or nullptr if the key is
// missing or the related object is gone (reported as a missing read).
template <class T, class O>
typename T::handle* get_obj(const O& obj, const char* key) {
    auto* p = static_cast<typename T::handle*>(detail::attr_get_obj(detail::slot_of(obj), key, detail::ObjType<T>::id()));
    if (!p) detail::note_missing(key);
    return p;
}
template <class O>
bool has(const O& obj, const char* key) {
    return detail::attr_has(detail::slot_of(obj), key);
}
template <class O>
void del(const O& obj, const char* key) {
    detail::attr_del(detail::slot_of(obj), key);
}

void global_set(const char* key, int64_t v);
void global_set_str(const char* key, const std::string& v);
int64_t global_get(const char* key);                      // missing -> 0, reported
int64_t global_get(const char* key, int64_t def);         // missing -> def, not reported
std::string global_get_str(const char* key);
std::string global_get_str(const char* key, const std::string& def);
bool global_has(const char* key);

// ---------------------------------------------------------------------------------------------
// Source: structured choices for decoders and in-stub generation.
// Encoding is identity-preserving: integers are stored raw little-endian, range/choice values are
// used as-is when valid, strings are stored verbatim with a length prefix. So LibAFL byte
// mutators (havoc, tokens, cmplog I2S) work directly on the choice buffer.
// ---------------------------------------------------------------------------------------------
class Source {
   public:
    Source(const uint8_t* buf, size_t len, size_t cap);
    ~Source();
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
    Source(Source&&) noexcept;

    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    int8_t i8() { return (int8_t)u8(); }
    int16_t i16() { return (int16_t)u16(); }
    int32_t i32() { return (int32_t)u32(); }
    int64_t i64() { return (int64_t)u64(); }
    float f32();
    double f64();
    float f32(float lo, float hi);    // in [lo, hi]
    double f64(double lo, double hi);  // in [lo, hi]
    int64_t range(int64_t lo, int64_t hi);  // inclusive
    size_t choice(size_t n);                // 0..n-1
    bool coin(double p = 0.5);
    bool more();  // loop continuation: `while (s.more()) {...}`; false once the buffer is exhausted
    std::string str(size_t max);
    std::vector<uint8_t> bytes(size_t max);
    std::vector<uint8_t> fixed(size_t n);  // exactly n bytes (zeros past the end of the input)
    std::vector<uint8_t> rest();           // every byte that is left
    std::string token();  // a token from the datatype dictionary (stored verbatim)
    // A value from data type Tag's decoder, drawn from this source: decoders build on each other.
    template <class Tag>
    auto draw();
    template <class T>
    T pick(std::initializer_list<T> xs) {
        size_t i = choice(xs.size());
        return *(xs.begin() + i);
    }
    const char* pick(std::initializer_list<const char*> xs) {
        size_t i = choice(xs.size());
        return *(xs.begin() + i);
    }
    size_t position() const;
    size_t remaining() const;
    bool exhausted() const;

    struct Impl;
    Impl* impl() { return impl_; }

   private:
    Impl* impl_;
};

namespace detail {
// While alive, Source draws are not recorded in plans (decoders behind a parameter).
struct QuietDraws {
    bool saved;
    QuietDraws();
    ~QuietDraws();
};
// `more()` with an explicit loop site and frame (for language bridges; C++ uses the call site).
bool source_more_at(Source& s, uint32_t site, uintptr_t frame);
// Decoders are found by argument-dependent lookup on the data type tag: STITCH_DECODER(Tag, fn)
// defines stitch_decoder_fn(Tag*) next to the tag. This deleted fallback names the missing case.
template <class T>
void stitch_decoder_fn(T*) = delete;
template <class Tag>
using decoded_t = std::decay_t<decltype(stitch_decoder_fn((Tag*)nullptr)(std::declval<::stitch::Source&>()))>;
template <class R, class = void>
struct is_byte_range : std::false_type {};
// Text for a decoded value in traces and `stitch sample`: operator<< if the type has one, else
// numbers, strings, containers, pairs and optionals element by element, else a placeholder.
template <class T, class = void>
struct has_ostream : std::false_type {};
template <class T>
struct has_ostream<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>> : std::true_type {};
template <class T, class = void>
struct is_range_t : std::false_type {};
template <class T>
struct is_range_t<T, std::void_t<decltype(std::declval<const T&>().begin()), decltype(std::declval<const T&>().end())>> : std::true_type {};
template <class T>
struct is_pair_t : std::false_type {};
template <class A, class B>
struct is_pair_t<std::pair<A, B>> : std::true_type {};
template <class T>
struct is_optional_t : std::false_type {};
template <class T>
struct is_optional_t<std::optional<T>> : std::true_type {};
template <class T>
void render_to(std::ostream& o, const T& v) {
    if constexpr (std::is_same<T, std::string>::value) o << '"' << v << '"';
    else if constexpr (std::is_same<T, bool>::value) o << (v ? "true" : "false");
    else if constexpr (std::is_same<T, char>::value || std::is_same<T, signed char>::value || std::is_same<T, unsigned char>::value) o << (int)v;
    else if constexpr (std::is_enum<T>::value) o << (long long)v;
    else if constexpr (has_ostream<T>::value) o << v;
    else if constexpr (is_pair_t<T>::value) {
        o << '(';
        render_to(o, v.first);
        o << ", ";
        render_to(o, v.second);
        o << ')';
    } else if constexpr (is_optional_t<T>::value) {
        if (v) render_to(o, *v);
        else o << "nullopt";
    } else if constexpr (is_range_t<T>::value) {
        o << '[';
        size_t i = 0;
        for (const auto& x : v) {
            if (i) o << ", ";
            if (++i > 32) {
                o << "...";
                break;
            }
            render_to(o, x);
        }
        o << ']';
    } else o << "<value>";
}
template <class T>
std::string render(const T& v) {
    std::ostringstream o;
    render_to(o, v);
    return o.str();
}
template <class R>
struct is_byte_range<R, std::void_t<typename R::value_type, decltype(std::declval<R>().begin())>>
    : std::bool_constant<sizeof(typename R::value_type) == 1 && std::is_integral<typename R::value_type>::value> {};
}  // namespace detail

template <class Tag>
auto Source::draw() {
    using detail::stitch_decoder_fn;
    return stitch_decoder_fn((Tag*)nullptr)(*this);
}

// ---------------------------------------------------------------------------------------------
// Fuzz parameters
// ---------------------------------------------------------------------------------------------
namespace detail {
template <class T>
inline T load_le(const uint8_t* b, size_t n) {
    T v{};
    std::memcpy(&v, b, n < sizeof(T) ? n : sizeof(T));
    return v;
}
inline std::string to_str(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }
}  // namespace detail

template <class T>
struct Num {
    T v;
    operator T() const { return v; }
    T value() const { return v; }
    static Num decode(const uint8_t* b, size_t n, uint8_t) { return Num{detail::load_le<T>(b, n)}; }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = std::is_floating_point<T>::value ? detail::PKind::Float : detail::PKind::Int;
        d.size = sizeof(T);
        d.is_signed = std::is_signed<T>::value;
        d.ctype = std::is_floating_point<T>::value ? (sizeof(T) == 4 ? "F32" : "F64")
                  : std::is_signed<T>::value      ? (sizeof(T) == 1 ? "I8" : sizeof(T) == 2 ? "I16" : sizeof(T) == 4 ? "I32" : "I64")
                                                  : (sizeof(T) == 1 ? "U8" : sizeof(T) == 2 ? "U16" : sizeof(T) == 4 ? "U32" : "U64");
        return d;
    }
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
    bool v;
    operator bool() const { return v; }
    static Bool decode(const uint8_t* b, size_t n, uint8_t) { return Bool{n > 0 && (b[0] & 1)}; }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = detail::PKind::Bool;
        d.size = 1;
        d.ctype = "Bool";
        return d;
    }
};

// Integer in [Lo, Hi] (inclusive). Stored as 8 raw bytes; valid values decode to themselves.
template <long long Lo, long long Hi>
struct Range {
    static_assert(Lo <= Hi, "Range<Lo, Hi> needs Lo <= Hi");
    long long v;
    operator long long() const { return v; }
    long long value() const { return v; }
    static Range decode(const uint8_t* b, size_t n, uint8_t) {
        long long raw = detail::load_le<long long>(b, n);
        if (raw >= Lo && raw <= Hi) return Range{raw};
        unsigned long long span = (unsigned long long)Hi - (unsigned long long)Lo + 1ull;
        unsigned long long off = (unsigned long long)raw - (unsigned long long)Lo;
        return Range{(long long)((unsigned long long)Lo + (span ? off % span : off))};
    }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = detail::PKind::Range;
        d.size = 8;
        d.is_signed = true;
        d.lo = Lo;
        d.hi = Hi;
        d.ctype = "Range";
        return d;
    }
};

// One of the listed constants (enums, modes). Stored as the raw value; unknown values map to an
// entry by index.
template <auto First, auto... Rest>
struct OneOf {
    using T = decltype(First);
    T v;
    operator T() const { return v; }
    T value() const { return v; }
    static OneOf decode(const uint8_t* b, size_t n, uint8_t) {
        const long long vals[] = {(long long)First, (long long)Rest...};
        long long raw = detail::load_le<long long>(b, n);
        for (long long x : vals)
            if (x == raw) return OneOf{(T)raw};
        return OneOf{(T)vals[(unsigned long long)raw % (sizeof...(Rest) + 1)]};
    }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = detail::PKind::OneOf;
        d.size = 8;
        d.values = {(int64_t)First, (int64_t)Rest...};
        d.ctype = "OneOf";
        return d;
    }
};

// Bitwise OR of a subset of the listed flags.
template <auto First, auto... Rest>
struct Flags {
    using T = decltype(First);
    T v;
    operator T() const { return v; }
    T value() const { return v; }
    static Flags decode(const uint8_t* b, size_t n, uint8_t) {
        long long mask = ((long long)First | ... | (long long)Rest);
        return Flags{(T)(detail::load_le<long long>(b, n) & mask)};
    }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = detail::PKind::Flags;
        d.size = 8;
        d.values = {(int64_t)First, (int64_t)Rest...};
        d.ctype = "Flags";
        return d;
    }
};

// Byte buffer of at most Max bytes. With a datatype Tag, values come from that type's pool,
// seeds and decoder.
template <size_t Max, class Tag = void>
struct Bytes {
    std::vector<uint8_t> buf;
    const uint8_t* data() const { return buf.data(); }
    uint8_t* data() { return buf.data(); }
    size_t size() const { return buf.size(); }
    bool empty() const { return buf.empty(); }
    std::string str() const { return detail::to_str(buf); }
    uint8_t operator[](size_t i) const { return buf[i]; }
    static Bytes decode(const uint8_t* b, size_t n, uint8_t form) {
        return Bytes{detail::decode_param(detail::datatype_id(detail::TagName<Tag>::get()), b, n, form, Max)};
    }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = detail::PKind::Bytes;
        d.size = Max;
        d.datatype = detail::TagName<Tag>::get();
        d.ctype = "Bytes";
        return d;
    }
};

// NUL-terminated string of at most Max bytes (may contain embedded NULs; c_str() stops there).
template <size_t Max, class Tag = void>
struct Str {
    std::string s;
    const char* c_str() const { return s.c_str(); }
    const char* data() const { return s.data(); }
    size_t size() const { return s.size(); }
    size_t length() const { return s.size(); }
    bool empty() const { return s.empty(); }
    operator const std::string&() const { return s; }
    operator const char*() const { return s.c_str(); }
    char operator[](size_t i) const { return s[i]; }
    static Str decode(const uint8_t* b, size_t n, uint8_t form) {
        return Str{detail::to_str(detail::decode_param(detail::datatype_id(detail::TagName<Tag>::get()), b, n, form, Max))};
    }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = detail::PKind::Str;
        d.size = Max;
        d.datatype = detail::TagName<Tag>::get();
        d.ctype = "Str";
        return d;
    }
};

// File whose content is the fuzzed bytes. `path()` is valid during the call.
template <size_t Max, class Tag = void>
struct File {
    std::vector<uint8_t> buf;
    std::string p;
    const char* path() const { return p.c_str(); }
    const uint8_t* data() const { return buf.data(); }
    size_t size() const { return buf.size(); }
    static File decode(const uint8_t* b, size_t n, uint8_t form) {
        File f{detail::decode_param(detail::datatype_id(detail::TagName<Tag>::get()), b, n, form, Max), {}};
        f.p = detail::write_temp_file(f.buf.data(), f.buf.size());
        return f;
    }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = detail::PKind::File;
        d.size = Max;
        d.datatype = detail::TagName<Tag>::get();
        d.ctype = "File";
        return d;
    }
};

// sizeof(T) raw bytes. Only for plain data (no pointers; enums get arbitrary values).
template <class T>
struct Raw {
    static_assert(std::is_trivially_copyable<T>::value, "Raw<T> requires a trivially copyable T");
    T v;
    operator T&() { return v; }
    operator const T&() const { return v; }
    T* operator->() { return &v; }
    const T* operator->() const { return &v; }
    T& operator*() { return v; }
    static Raw decode(const uint8_t* b, size_t n, uint8_t) {
        Raw r;
        std::memset(&r.v, 0, sizeof(T));
        std::memcpy(&r.v, b, n < sizeof(T) ? n : sizeof(T));
        return r;
    }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = detail::PKind::Raw;
        d.size = sizeof(T);
        d.ctype = "Raw";
        return d;
    }
};

// A value of data type Tag, built by Tag's decoder from at most Max bytes of choices. The decoder
// can return any type (a struct, a vector, ...): `p->x`, `*p`, or use it where a T is expected. Its
// definition (STITCH_DECODER) must be visible where it is used. Written stitch::Val (not exported
// globally: Val is a common name in harnesses).
template <class Tag, size_t Max = 1024>
struct Val {
    using value_type = detail::decoded_t<Tag>;
    value_type v;
    operator value_type&() { return v; }
    operator const value_type&() const { return v; }
    value_type* operator->() { return &v; }
    const value_type* operator->() const { return &v; }
    value_type& operator*() { return v; }
    const value_type& operator*() const { return v; }
    static Val decode(const uint8_t* b, size_t n, uint8_t) {
        detail::QuietDraws q;  // the decoder's draws are not stub draws in plans
        Source s(b, n < Max ? n : Max, Max);
        return Val{s.draw<Tag>()};
    }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = detail::PKind::Value;
        d.size = Max;
        d.datatype = detail::TagName<Tag>::get();
        d.ctype = "Val";
        return d;
    }
};

// In-stub choice source of at most Max bytes, for values that depend on object state
// (e.g. pick an existing key or index).
template <size_t Max>
struct Src {
    std::shared_ptr<Source> s;
    Source* operator->() const { return s.get(); }
    Source& operator*() const { return *s; }
    operator Source&() const { return *s; }
    static Src decode(const uint8_t* b, size_t n, uint8_t) {
        return Src{std::make_shared<Source>(b, n < Max ? n : Max, Max)};
    }
    static detail::ParamDesc desc() {
        detail::ParamDesc d{};
        d.kind = detail::PKind::Source;
        d.size = Max;
        d.ctype = "Source";
        return d;
    }
};

// ---------------------------------------------------------------------------------------------
// Endpoint reflection
// ---------------------------------------------------------------------------------------------
namespace detail {

template <class A>
struct ArgKind {
    static constexpr bool is_obj = false;
};
template <class T>
struct ArgKind<Borrow<T>> {
    static constexpr bool is_obj = true;
    static constexpr bool take = false;
    using obj = T;
};
template <class T>
struct ArgKind<Take<T>> {
    static constexpr bool is_obj = true;
    static constexpr bool take = true;
    using obj = T;
};

template <class... A>
constexpr std::array<int, sizeof...(A) + 1> ordinals() {
    constexpr bool flags[] = {ArgKind<std::decay_t<A>>::is_obj..., false};
    std::array<int, sizeof...(A) + 1> r{};
    int o = 0, p = 0;
    for (size_t i = 0; i < sizeof...(A); i++) r[i] = flags[i] ? o++ : p++;
    return r;
}

template <class A>
A make_arg(Ctx& c, int ord) {
    if constexpr (ArgKind<A>::is_obj) return A::from_ctx(c, ord);
    else return A::decode(c.pbuf[ord], c.plen[ord], c.pform[ord]);
}

template <class R>
struct OutKind;
template <class T>
struct OutKind<New<T>> {
    static void add(std::vector<int>& types, std::vector<bool>& opt) {
        types.push_back(ObjType<T>::id());
        opt.push_back(false);
    }
    static void store(Ctx& c, int i, New<T>& v) {
        add_output(c, i, new typename T::handle(v.h), v.attrs);
    }
};
template <class T>
struct OutKind<std::optional<New<T>>> {
    static void add(std::vector<int>& types, std::vector<bool>& opt) {
        types.push_back(ObjType<T>::id());
        opt.push_back(true);
    }
    static void store(Ctx& c, int i, std::optional<New<T>>& v) {
        if (v) add_output(c, i, new typename T::handle(v->h), v->attrs);
    }
};

template <class R>
struct RetKind {
    static void add(std::vector<int>& t, std::vector<bool>& o) { OutKind<R>::add(t, o); }
    static void store(Ctx& c, R& r) { OutKind<R>::store(c, 0, r); }
};
template <>
struct RetKind<void> {
    static void add(std::vector<int>&, std::vector<bool>&) {}
};
template <class... E>
struct RetKind<std::tuple<E...>> {
    static void add(std::vector<int>& t, std::vector<bool>& o) { (OutKind<E>::add(t, o), ...); }
    template <size_t... I>
    static void store_impl(Ctx& c, std::tuple<E...>& r, std::index_sequence<I...>) {
        (OutKind<E>::store(c, (int)I, std::get<I>(r)), ...);
    }
    static void store(Ctx& c, std::tuple<E...>& r) { store_impl(c, r, std::index_sequence_for<E...>{}); }
};

template <class A>
void describe_arg(std::vector<int>& in_types, std::vector<bool>& in_take, std::vector<ParamDesc>& params) {
    using D = std::decay_t<A>;
    if constexpr (ArgKind<D>::is_obj) {
        in_types.push_back(ObjType<typename ArgKind<D>::obj>::id());
        in_take.push_back(ArgKind<D>::take);
    } else {
        params.push_back(D::desc());
    }
}

template <auto F>
struct Endpoint;
template <class R, class... A, R (*F)(A...)>
struct Endpoint<F> {
    template <size_t... I>
    static void call(Ctx& c, std::index_sequence<I...>) {
        constexpr auto ord = ordinals<A...>();
        std::tuple<std::decay_t<A>...> args{make_arg<std::decay_t<A>>(c, ord[I])...};
        if constexpr (std::is_void<R>::value) {
            F(std::get<I>(args)...);
        } else {
            R r = F(std::get<I>(args)...);
            RetKind<R>::store(c, r);
        }
    }
    static void invoke(Ctx& c) { call(c, std::index_sequence_for<A...>{}); }
    static int reg(const char* name, const char* file) {
        std::vector<int> in_types, out_types;
        std::vector<bool> in_take, out_opt;
        std::vector<ParamDesc> params;
        std::vector<uint8_t> order{(uint8_t)ArgKind<std::decay_t<A>>::is_obj...};
        (describe_arg<A>(in_types, in_take, params), ...);
        RetKind<R>::add(out_types, out_opt);
        return register_endpoint(name, file, &invoke, in_types, in_take, out_types, out_opt, params, order);
    }
};

template <class Tag>
struct DecoderReg {
    template <class Fn>
    static int reg(Fn fn) {
        static Fn f = fn;
        using R = std::decay_t<decltype(fn(std::declval<Source&>()))>;
        if constexpr (is_byte_range<R>::value) {
            return register_decoder(Tag::stitch_name(), +[](void* src) -> std::vector<uint8_t> {
                auto out = f(*static_cast<Source*>(src));
                return std::vector<uint8_t>(out.begin(), out.end());
            });
        } else {
            // a typed decoder (Val<Tag>): the engine runs it to generate and inspect choices; the
            // bytes it gets back are only a rendering of the value for traces and `stitch sample`
            return register_decoder(Tag::stitch_name(), +[](void* src) -> std::vector<uint8_t> {
                auto t = render(f(*static_cast<Source*>(src)));
                return std::vector<uint8_t>(t.begin(), t.end());
            });
        }
    }
};

void set_param_names(const char* endpoint, std::initializer_list<const char*> names);
void set_param_names(const char* endpoint, const std::vector<std::string>& names);

}  // namespace detail
}  // namespace stitch

#define STITCH_CAT2(a, b) a##b
#define STITCH_CAT(a, b) STITCH_CAT2(a, b)

// Declare an object type: STITCH_OBJECT(Name, HandleType)
#define STITCH_OBJECT(Name, Handle)                                   \
    struct Name {                                                     \
        using handle = Handle;                                        \
        static const char* stitch_name() { return #Name; }            \
    };                                                                \
    inline const int STITCH_CAT(stitch_objreg_, Name) = ::stitch::detail::ObjType<Name>::id()

// Optional finalizer for objects still alive at the end of a run: STITCH_DROP(Name, fn)
#define STITCH_DROP(Name, fn) \
    inline const int STITCH_CAT(stitch_dropreg_, Name) = ::stitch::detail::set_drop_fn<Name>(fn)

// Declare a data type tag: STITCH_DATATYPE(JsonText). Folder: harness/data/JsonText/
#define STITCH_DATATYPE(Name)                                                \
    struct Name {                                                            \
        static const char* stitch_name() { return #Name; }                   \
    };                                                                       \
    inline const int STITCH_CAT(stitch_dtreg_, Name) = ::stitch::detail::register_datatype(#Name)

// Register a decoder for a data type: STITCH_DECODER(JsonText, fn) with fn(stitch::Source&) -> T.
// T = std::string / bytes: values for Str<N, Tag> / Bytes<N, Tag>. Any other T: values for Val<Tag>.
#define STITCH_DECODER(Tag, ...)                                               \
    [[maybe_unused]] inline auto stitch_decoder_fn(Tag*) { return __VA_ARGS__; } \
    static const int STITCH_CAT(stitch_decreg_, Tag) =                \
        ::stitch::detail::DecoderReg<Tag>::reg(stitch_decoder_fn((Tag*)nullptr))

// Register an endpoint function. Qualified names are fine (STITCH_ENDPOINT(json::parse)); the
// endpoint id is the name as written. Do not overload endpoint functions.
#define STITCH_ENDPOINT(fn) \
    static const int STITCH_CAT(stitch_epreg_, __COUNTER__) = ::stitch::detail::Endpoint<&fn>::reg(#fn, __FILE__)

// Run once per process before fuzzing (library-global initialization).
#define STITCH_INIT                                                                                    \
    static void STITCH_CAT(stitch_init_fn_, __LINE__)();                                              \
    static const int STITCH_CAT(stitch_initreg_, __LINE__) =                                          \
        ::stitch::detail::register_init(&STITCH_CAT(stitch_init_fn_, __LINE__));                      \
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
