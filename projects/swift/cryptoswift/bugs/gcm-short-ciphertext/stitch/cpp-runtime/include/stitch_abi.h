/* stitch_abi.h - the C ABI between a STITCH runtime (any harness language) and the engine.
 *
 * A runtime registers endpoints, executes serialized programs, provides SanitizerCoverage
 * edge/comparison hooks, and runs datatype decoders. The engine (Rust/LibAFL) only talks to
 * the harness through these functions and the shared-memory regions below.
 *
 * Program file format (".stp", little endian):
 *   "STCH" u32 n_names { u16 len, bytes }*        endpoint name table
 *   u32 n_nodes, per node:
 *     u32 name_index
 *     u8 n_inputs   { u32 src_node, u8 src_output }*
 *     u8 n_params   { u8 form, u32 len, bytes }*  form 0 = value bytes, 1 = decoder choices
 */
#ifndef STITCH_ABI_H
#define STITCH_ABI_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define STITCH_MAX_NODES 256
#define STITCH_STRINGS_SIZE 65536
#define STITCH_CMPLOG_RECORDS 16384
#define STITCH_EDGES_SIZE (1u << 21)

enum StitchStatus {
  STITCH_NOT_RUN = 0,
  STITCH_OK = 1,
  STITCH_BAIL = 2,
  STITCH_SKIP_MISSING = 3, /* an input object was not available (producer bailed / consumed) */
  STITCH_SKIP_INVALID = 4, /* unknown endpoint or signature mismatch */
  STITCH_CRASHED = 5,      /* set by the engine for the node that was running at crash time */
};

typedef struct {
  uint8_t status;
  uint8_t n_out;
  uint16_t out_mask; /* outputs actually produced */
  uint32_t reason;   /* hash of bail reason string */
  uint32_t miss_key; /* hash of the last attribute key read that was missing (0 = none) */
  uint32_t consumed; /* bitmask of borrowed inputs consumed */
} StitchNodeResult;

typedef struct {
  uint32_t magic;
  uint32_t n_nodes;
  int32_t current; /* node running now; n_nodes while dropping leftovers; -1 idle */
  uint32_t str_used;
  uint64_t crash_pc;   /* ASan report pc, or a hash of the check/exception message (bit 63 set) */
  StitchNodeResult nodes[STITCH_MAX_NODES];
  /* string table: { u32 hash, u16 len, bytes }* */
  uint8_t strings[STITCH_STRINGS_SIZE];
} StitchResults;

typedef struct {
  uint16_t node;
  uint8_t kind; /* 0 = integer compare, 1 = bytes compare */
  uint8_t size; /* integer width in bytes (1,2,4,8) */
  uint8_t len_a, len_b;
  uint8_t a_const;
  uint8_t pad;
  uint8_t a[32];
  uint8_t b[32];
} StitchCmpRecord;

typedef struct {
  uint32_t enabled;
  uint32_t count;
  StitchCmpRecord records[STITCH_CMPLOG_RECORDS];
} StitchCmplog;

/* registry */
const char* stitch_manifest(void);   /* JSON */
void stitch_init_once(void);          /* runs STITCH_INIT blocks */

/* execution (call in a forked child while fuzzing) */
int stitch_exec(const uint8_t* prog, size_t len, uint32_t flags); /* flags: 1 = trace to stderr */
/* stitch_exec() returns 0 (ran), -1 (invalid program) or STITCH_EXEC_FINDING: a failed check or a
 * caught bug while fuzzing; the runtime can keep running programs (the fork server keeps the worker) */
#define STITCH_EXEC_FINDING 2

/* shared memory (MAP_SHARED, visible to the parent after the child exits) */
uint8_t* stitch_edges(size_t* n_edges);
StitchResults* stitch_results(void);
StitchCmplog* stitch_cmplog(void);

/* datatypes */
int stitch_datatype_count(void);
int stitch_has_decoder(int dt);
/* decode a choice buffer into a value; returns value length (<= cap) */
size_t stitch_decode(int dt, const uint8_t* choices, size_t n, uint8_t* out, size_t cap);
/* run the decoder in generation mode: keeps `prefix`, extends it with fresh random choices,
 * writes the resulting choice buffer to out; returns its length (<= cap) */
size_t stitch_generate(int dt, uint64_t seed, const uint8_t* prefix, size_t plen, uint8_t* out, size_t cap);
void stitch_dict_add(int dt, const uint8_t* tok, size_t len);
/* where the decoder's str()/bytes()/token() draws live in a choice buffer:
 * triples (prefix offset, prefix width, content length); returns the number of triples */
size_t stitch_decode_spans(int dt, const uint8_t* choices, size_t n, uint32_t* out, size_t max_spans);
void stitch_cmplog_reset(void); /* clear comparison records and per-run dedupe */

/* misc */
int stitch_run_file(const char* path, uint32_t flags); /* load + exec a .stp file */
/* Plan mode: write every executed call with its concrete arguments as JSON lines (see cli/stitch_fuzz/standalone.py). */
void stitch_set_plan(const char* path);

#ifdef __cplusplus
}
#endif
#endif
