// C interface between the Swift harness API (Sources/Stitch) and the C/C++ runtime
// (languages/cpp/runtime, built with bridge/bridge.cpp into libstitch_swift_rt.a).
// Swift stubs are registered with descriptors; the runtime runs programs and calls back into
// Swift by endpoint index. Objects are retained Swift boxes stored as opaque pointers.
#ifndef CSTITCH_H
#define CSTITCH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t kind;          // 0 int, 1 float, 2 bool, 3 range, 4 oneof, 5 flags, 6 bytes, 7 str, 8 file, 9 raw, 10 source
    uint32_t size;         // byte size, or max length for bytes/str/file/source
    int32_t is_signed;
    int64_t lo, hi;        // range
    int32_t n_values;      // oneof / flags
    const int64_t* values;
    const char* datatype;  // data type name or NULL
    const char* ctype;     // type name shown in traces
} stitch_c_param;

// callbacks into Swift: run endpoint `ep` (returns 0 ok, 1 bail, 2 failed check; the reason via
// stitch_c_reason), run the decoder of data type `dt` (append bytes with stitch_c_append),
// release a retained object box
void stitch_c_callbacks(int32_t (*invoke)(int32_t ep, void* ctx), void (*decode)(int32_t dt, void* src, void* out),
                        void (*release)(void* obj));

int32_t stitch_c_object(const char* name);
int32_t stitch_c_datatype(const char* name, int32_t has_decoder);
int32_t stitch_c_endpoint(const char* name, const char* file, int32_t n_args, const uint8_t* arg_is_obj,
                          const int32_t* in_types, const int32_t* in_take, int32_t n_out, const int32_t* out_types,
                          const int32_t* out_opt, const stitch_c_param* params, const char* const* arg_names);
void stitch_c_init(void (*fn)(void));

// during a call
void* stitch_c_in(void* ctx, int32_t k);  // object input k: its box (unretained)
const uint8_t* stitch_c_raw(void* ctx, int32_t k, size_t* len);
const uint8_t* stitch_c_decoded(void* ctx, int32_t k, const char* datatype, size_t max, size_t* len);
const char* stitch_c_file(void* ctx, int32_t k, const char* datatype, size_t max);
void* stitch_c_source(void* ctx, int32_t k, size_t max);
void stitch_c_out(void* ctx, int32_t k, void* retained_obj);
void stitch_c_consume(void* obj);
void stitch_c_reason(const char* s);
void stitch_c_log(const char* s);
int32_t stitch_c_tracing(void);

// attributes (objects by box pointer) and globals
void stitch_c_attr_set_int(void* obj, const char* key, int64_t v);
void stitch_c_attr_set_str(void* obj, const char* key, const char* v, size_t len);
int32_t stitch_c_attr_get_int(void* obj, const char* key, int64_t* out);
const char* stitch_c_attr_get_str(void* obj, const char* key, size_t* len);  // valid until the next call
int32_t stitch_c_attr_has(void* obj, const char* key);
void stitch_c_attr_del(void* obj, const char* key);
void stitch_c_global_set_int(const char* key, int64_t v);
int32_t stitch_c_global_get_int(const char* key, int64_t* out);
void stitch_c_note_missing(const char* key);

// Source (decoders, Src parameters)
uint64_t stitch_c_src_uint(void* s, int32_t bytes);
int64_t stitch_c_src_range(void* s, int64_t lo, int64_t hi);
int32_t stitch_c_src_coin(void* s, double p);
int32_t stitch_c_src_more(void* s);
const uint8_t* stitch_c_src_bytes(void* s, size_t max, size_t* len);  // valid until the next draw
const uint8_t* stitch_c_src_str(void* s, size_t max, size_t* len);
const uint8_t* stitch_c_src_token(void* s, size_t* len);
double stitch_c_src_f64(void* s);
double stitch_c_src_f64_in(void* s, double lo, double hi);
float stitch_c_src_f32_in(void* s, float lo, float hi);
int32_t stitch_c_src_more_at(void* s, uint32_t site, uintptr_t frame);  // more() with the loop's site and frame
const uint8_t* stitch_c_src_fixed(void* s, size_t n, size_t* len);
const uint8_t* stitch_c_src_rest(void* s, size_t* len);
void* stitch_c_value_source(void* ctx, int32_t k, size_t max);  // owned by the caller (stitch_c_source_free)
size_t stitch_c_src_position(void* s);
size_t stitch_c_src_remaining(void* s);
int32_t stitch_c_src_exhausted(void* s);
void stitch_c_append(void* out, const uint8_t* p, size_t n);
void* stitch_c_source_new(const uint8_t* p, size_t n, size_t cap);  // reproducers
void stitch_c_source_free(void* s);

// STITCH_SWIFT_ENGINE=1: the fuzzing engine (fuzz, exec, triage, ...); otherwise the reproducer
int stitch_c_main(int argc, char** argv);

#ifdef __cplusplus
}
#endif
#endif
