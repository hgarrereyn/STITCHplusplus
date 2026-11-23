/* A tiny key-value store with planted bugs (STITCH++ example). */
#ifndef KV_H
#define KV_H
#include <stddef.h>

typedef struct kv_store kv_store;

kv_store *kv_new(void);
void kv_free(kv_store *st);
/* Keys and values are single lines; keys are not empty. 0 on success, -1 otherwise. */
int kv_put(kv_store *st, const char *key, const char *value);
const char *kv_get(const kv_store *st, const char *key);
void kv_remove(kv_store *st, const char *key);
size_t kv_len(const kv_store *st);
/* "key=value\n" lines in insertion order. The caller frees the result. */
char *kv_dump(const kv_store *st);
/* Parses kv_dump's format. NULL for a line without '='. */
kv_store *kv_load(const char *text);

#endif
