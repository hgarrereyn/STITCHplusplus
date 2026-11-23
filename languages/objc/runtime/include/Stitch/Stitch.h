// Stitch.h - STITCH++ harness API for Objective-C (GNUstep, ARC).
//
// A harness is a set of stub functions. The declaration is the spec:
//
//   #import <Stitch/Stitch.h>
//   #import "KVStore.h"
//
//   STITCH_OBJECT(Store, KVStore *);           // object type Store, handle type KVStore *
//   STITCH_DATATYPE(KvText);                    // data type: harness/data/KvText/
//
//   STITCH_ENDPOINT(New(Store), load, Str(256, KvText) text) {
//       NSError *err = nil;
//       KVStore *st = [KVStore storeWithText:text error:&err];
//       STRequire(st != nil, @"load failed");   // not a bug: invalid input
//       return st;
//   }
//
//   STITCH_ENDPOINT(void, put, Borrow(Store) st, Str(16) key, Str(32) value) {
//       STRequire([st setValue:value forKey:key error:NULL], @"put failed");
//   }
//
// Borrow(T) uses an object, Take(T) consumes it, New(T) (the return type) produces one. Every
// other parameter is fuzzed. An Objective-C exception that escapes a stub is a bug, like
// AddressSanitizer reports and failed checks. `stitch build` reads the STITCH_ENDPOINT
// declarations and registers them.
// Reference: `stitch guide objc`.
#ifndef STITCH_OBJC_H
#define STITCH_OBJC_H

#import <Foundation/Foundation.h>
#include <stddef.h>
#include <stdint.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
NS_ASSUME_NONNULL_BEGIN
#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------------------------

#define STITCH_CAT2(a, b) a##b
#define STITCH_CAT(a, b) STITCH_CAT2(a, b)

// Object type: STITCH_OBJECT(Store, KVStore *). The handle is an Objective-C object pointer
// (a class, `id` or `id<Protocol>`); the pool retains it. Wrap C handles in an object whose
// -dealloc frees them.
#define STITCH_OBJECT(Name, Handle)                                                        \
    typedef Handle stitch_obj_##Name;                                                      \
    __attribute__((constructor)) static void STITCH_CAT(stitch_objreg_, __COUNTER__)(void) { \
        stitch_objc_object(#Name);                                                         \
    }                                                                                      \
    struct stitch_objc_semicolon

// Data type: STITCH_DATATYPE(KvText). Folder harness/data/KvText/ (seeds/, *.dict).
#define STITCH_DATATYPE(Name)                                                             \
    __attribute__((constructor)) static void STITCH_CAT(stitch_dtreg_, __COUNTER__)(void) { \
        stitch_objc_datatype(#Name);                                                      \
    }                                                                                     \
    struct stitch_objc_semicolon

// Decoder of a data type: builds the value from fuzzer choices. The body sees `STSource *s` and
// returns an NSString (values for Str/CStr/File), NSData (values for Bytes) or any other object
// (values for Val; its -description is what traces and `stitch sample` show).
//
//   STITCH_DECODER(KvText) {
//       NSMutableString *out = [NSMutableString string];
//       while ([s more]) [out appendFormat:@"%@=%@\n", [s token], [s str:12]];
//       return out;
//   }
#define STITCH_DECODER(Name)                                                                    \
    static id STITCH_CAT(stitch_decoder_, Name)(STSource * s);                                  \
    __attribute__((constructor)) static void STITCH_CAT(stitch_decreg_, Name)(void) {           \
        stitch_objc_decoder(#Name, STITCH_CAT(stitch_decoder_, Name));                          \
    }                                                                                           \
    static id STITCH_CAT(stitch_decoder_, Name)(STSource * s)

// A stub: STITCH_ENDPOINT(ReturnType, name, parameters...) { body }. The endpoint id is `name`
// (unique in the harness); the function is called stub_<name>.
#define STITCH_ENDPOINT(Ret, name, ...) Ret stub_##name(__VA_ARGS__)

// Runs once per process before any stub (library-global initialization): STITCH_INIT { ... }
#define STITCH_INIT STITCH_INIT_(STITCH_CAT(stitch_init_fn_, __LINE__))
#define STITCH_INIT_(fn)                                                                      \
    static void fn(void);                                                                     \
    __attribute__((constructor)) static void STITCH_CAT(fn, _reg)(void) { stitch_objc_init(fn); } \
    static void fn(void)

// ---------------------------------------------------------------------------------------------
// Signature types (macros: stitch-build reads them from the STITCH_ENDPOINT declaration)
// ---------------------------------------------------------------------------------------------

// objects
#define Borrow(T) stitch_obj_##T      // uses an object; it stays available
#define Take(T) stitch_obj_##T        // consumes the object (it leaves the pool after the call)
#define New(T) stitch_obj_##T         // return type: produces an object (nil is a bail)
#define Maybe(T) stitch_obj_##T       // return type: produces an object or nothing (nil)
#define Outputs(...) NSArray *        // return type: several outputs, @[a, b] (NSNull for none)

// fuzzed parameters (also plain C types: int8_t..uint64_t, int, unsigned, long, NSInteger,
// NSUInteger, size_t, char, unichar, float, double, CGFloat, BOOL, bool)
#define Range(lo, hi) long long       // integer in [lo, hi]
#define OneOf(...) long long          // one of the listed constants
#define Flags(...) long long          // OR of a subset of the listed flags
#define Str(...) NSString *           // Str(max) or Str(max, Tag): UTF-8 text (invalid bytes -> U+FFFD)
#define CStr(...) const char *        // CStr(max) or CStr(max, Tag): NUL-terminated C string
#define Bytes(...) NSData *           // Bytes(max) or Bytes(max, Tag)
#define File(...) NSString *          // File(max) or File(max, Tag): path of a temporary file
#define Raw(T) T                      // the bytes of a plain C struct T
#define Val(...) id                   // Val(Tag) or Val(Tag, max): the value Tag's decoder returns
#define Src(max) STSource *           // choices made inside the stub

// ---------------------------------------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------------------------------------

// Stop this call: it failed for a reason that is not a bug. Objects it would have produced are
// not created; the program continues. Short, fixed reasons: they key `stitch probe` statistics.
__attribute__((noreturn)) void STBail(NSString *reason);
// Bail unless cond holds.
void STRequire(BOOL cond, NSString *reason);
// An oracle: a false condition is a bug, reported as "check failed: what".
void STCheck(BOOL cond, NSString *what);
// Printed by `stitch exec --trace` (and reproducers with STITCH_REPRO_TRACE=1); no-op while fuzzing.
void STLog(NSString *format, ...) NS_FORMAT_FUNCTION(1, 2);
BOOL STTracing(void);
// On a Borrow: the library took ownership of the object in this call; it leaves the pool.
void STConsume(id obj);

// ---------------------------------------------------------------------------------------------
// Attributes: state attached to pooled objects (and program-wide globals). Keys belong to the
// object: a consumed object loses them. Reads of a missing key without a default are reported
// by `stitch probe` ("read missing attr").
// ---------------------------------------------------------------------------------------------
void STSetInt(id obj, NSString *key, int64_t v);
int64_t STGetInt(id obj, NSString *key);                    // missing: 0, reported
int64_t STGetIntOr(id obj, NSString *key, int64_t def);     // missing: def, not reported
void STSetStr(id obj, NSString *key, NSString *v);
NSString *_Nullable STGetStr(id obj, NSString *key);        // missing: nil, reported
NSString *STGetStrOr(id obj, NSString *key, NSString *def);
// Link to another object (an input of the call): STSetObj(item, @"root", root). STGetObj returns
// nil once the linked object was consumed.
void STSetObj(id obj, NSString *key, id target);
#define STGetObj(obj, key, T) ((stitch_obj_##T)stitch_objc_get_obj((obj), (key), #T))
BOOL STHas(id obj, NSString *key);
void STDel(id obj, NSString *key);
void STGlobalSet(NSString *key, int64_t v);                 // reset for every program
int64_t STGlobalGet(NSString *key);                         // missing: 0, reported
int64_t STGlobalGetOr(NSString *key, int64_t def);
void STGlobalSetStr(NSString *key, NSString *v);
NSString *_Nullable STGlobalGetStr(NSString *key);
BOOL STGlobalHas(NSString *key);

// ---------------------------------------------------------------------------------------------
// Source: choices for decoders and Src parameters. Running out of choices is fine: later draws
// return zeros and -more returns NO.
// ---------------------------------------------------------------------------------------------
@interface STSource : NSObject
- (uint8_t)u8;
- (uint16_t)u16;
- (uint32_t)u32;
- (uint64_t)u64;
- (int8_t)i8;
- (int16_t)i16;
- (int32_t)i32;
- (int64_t)i64;
- (float)f32;
- (double)f64;
- (float)f32:(float)lo to:(float)hi;               // finite, in [lo, hi]
- (double)f64:(double)lo to:(double)hi;
- (int64_t)range:(int64_t)lo to:(int64_t)hi;       // inclusive
- (NSUInteger)choice:(NSUInteger)n;                // 0..n-1
- (id)pick:(NSArray *)items;                       // one element
- (BOOL)coin;                                      // about 50%
- (BOOL)coin:(double)p;                            // true with probability about p
- (BOOL)more;                                      // loop condition: while ([s more]) { ... }
- (NSString *)str:(NSUInteger)max;                 // text of at most max bytes
- (NSData *)bytes:(NSUInteger)max;
- (NSData *)fixed:(NSUInteger)n;                   // exactly n bytes
- (NSData *)rest;                                  // every byte that is left
- (NSString *)token;                               // a token from the data type's .dict files
- (nullable id)draw:(NSString *)datatype;          // a value from that data type's decoder
@property(readonly) NSUInteger position;
@property(readonly) NSUInteger remaining;
@property(readonly) BOOL exhausted;
@end

// STDraw(s, Pair): [s draw:@"Pair"]
#define STDraw(s, Tag) [(s) draw:@#Tag]

// ---------------------------------------------------------------------------------------------
// Reproducers (`stitch write`): call stub functions directly, without the fuzzer.
//
//   int main(void) {
//       return STReproMain(^{
//           stitch_obj_Store store0 = STCall(0, "new_store", ^id { return stub_new_store(); });
//           STCallBails(1, "put", ^{ stub_put(store0, @"", @"v"); });
//       });
//   }
// ---------------------------------------------------------------------------------------------
int STReproMain(void (^program)(void));
id _Nullable STCall(int node, const char *name, id _Nullable (^call)(void));  // succeeded in the original run
void STCallBails(int node, const char *name, void (^call)(void));             // bailed in the original run
NSString *STString(const char *bytes, size_t len);           // a Str value from its bytes
const char *STCString(const char *bytes, size_t len);        // a CStr value
NSData *STData(const char *bytes, size_t len);               // a Bytes value
NSString *STTempFile(const char *bytes, size_t len);         // a File value: a temporary file
STSource *STSourceWith(const char *bytes, size_t len, size_t max);           // a Src value
id _Nullable STValue(NSString *datatype, const char *bytes, size_t len, size_t max);  // a Val value

NS_ASSUME_NONNULL_END

// ---------------------------------------------------------------------------------------------
// Internal: used by the declaration macros and the code stitch-build generates
// ---------------------------------------------------------------------------------------------
typedef struct {
    int32_t kind;          // 0 int, 1 float, 2 bool, 3 range, 4 oneof, 5 flags, 6 bytes, 7 str, 8 file,
                           // 9 raw, 10 source, 11 value
    uint32_t size;         // byte size, or max length
    int32_t is_signed;
    int64_t lo, hi;        // range
    int32_t n_values;      // oneof / flags
    const int64_t *values;
    const char *datatype;  // data type name or NULL
    const char *ctype;     // type name shown in traces
} stitch_objc_param;

void stitch_objc_object(const char *name);
void stitch_objc_datatype(const char *name);
void stitch_objc_decoder(const char *name, id _Nullable (*fn)(STSource *s));
void stitch_objc_init(void (*fn)(void));
void stitch_objc_endpoint(const char *name, const char *file, void (*invoke)(void *ctx), int32_t n_args,
                          const uint8_t *arg_is_obj, const char *const *arg_names, const char *const *in_types,
                          const uint8_t *in_take, int32_t n_out, const char *const *out_types, const uint8_t *out_opt,
                          const stitch_objc_param *params);
void *stitch_objc_in(void *ctx, int32_t k);
uint64_t stitch_objc_bits(void *ctx, int32_t k);
double stitch_objc_f64(void *ctx, int32_t k);
float stitch_objc_f32(void *ctx, int32_t k);
long long stitch_objc_range(void *ctx, int32_t k, long long lo, long long hi);
long long stitch_objc_oneof(void *ctx, int32_t k, const long long *values, int32_t n);
long long stitch_objc_flags(void *ctx, int32_t k, long long mask);
NSString *stitch_objc_str(void *ctx, int32_t k, size_t max, const char *_Nullable datatype);
const char *stitch_objc_cstr(void *ctx, int32_t k, size_t max, const char *_Nullable datatype);
NSData *stitch_objc_bytes(void *ctx, int32_t k, size_t max, const char *_Nullable datatype);
NSString *stitch_objc_file(void *ctx, int32_t k, size_t max, const char *_Nullable datatype);
void stitch_objc_raw(void *ctx, int32_t k, void *out, size_t n);
id _Nullable stitch_objc_val(void *ctx, int32_t k, const char *datatype, size_t max);
STSource *stitch_objc_src(void *ctx, int32_t k, size_t max);
void stitch_objc_out(void *ctx, int32_t k, id _Nullable obj, int32_t optional);
void stitch_objc_outs(void *ctx, NSArray *_Nullable outs, int32_t n, const uint8_t *optional);
id _Nullable stitch_objc_get_obj(id obj, NSString *key, const char *type);

#ifdef __cplusplus
}
#endif
#pragma clang diagnostic pop
#endif
