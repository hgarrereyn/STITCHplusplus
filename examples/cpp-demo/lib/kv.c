/* A tiny key-value store with planted bugs (STITCH++ example). */
#define _POSIX_C_SOURCE 200809L
#include "kv.h"
#include <stdlib.h>
#include <string.h>

typedef struct { char *key, *value; } entry;

struct kv_store {
    entry **data;   /* lookup table */
    size_t n;
    entry **order;  /* insertion order */
    size_t n_order;
};

kv_store *kv_new(void) { return calloc(1, sizeof(kv_store)); }

static entry *find(const kv_store *st, const char *key, size_t *at) {
    for (size_t i = 0; i < st->n; i++)
        if (strcmp(st->data[i]->key, key) == 0) {
            if (at) *at = i;
            return st->data[i];
        }
    return NULL;
}

int kv_put(kv_store *st, const char *key, const char *value) {
    if (!*key || strchr(key, '\n') || strchr(value, '\n')) return -1;
    entry *e = find(st, key, NULL);
    if (e) {
        free(e->value);
        e->value = strdup(value);
        return 0;
    }
    e = malloc(sizeof(entry));
    e->key = strdup(key);
    e->value = strdup(value);
    st->data = realloc(st->data, (st->n + 1) * sizeof(entry *));
    st->data[st->n++] = e;
    st->order = realloc(st->order, (st->n_order + 1) * sizeof(entry *));
    st->order[st->n_order++] = e;
    return 0;
}

const char *kv_get(const kv_store *st, const char *key) {
    entry *e = find(st, key, NULL);
    return e ? e->value : NULL;
}

void kv_remove(kv_store *st, const char *key) {
    size_t at;
    entry *e = find(st, key, &at);
    if (!e) return;
    st->data[at] = st->data[--st->n];
    if (strncmp(key, "tmp", 3) != 0) { /* BUG: temporary keys stay in the order list */
        for (size_t i = 0; i < st->n_order; i++)
            if (st->order[i] == e) {
                memmove(&st->order[i], &st->order[i + 1], (--st->n_order - i) * sizeof(entry *));
                break;
            }
    }
    free(e->key);
    free(e->value);
    free(e);
}

size_t kv_len(const kv_store *st) { return st->n; }

void kv_merge(kv_store *st, const kv_store *other) {
    for (size_t i = 0; i < other->n_order; i++) /* use-after-free after removing a tmp key from other */
        kv_put(st, other->order[i]->key, other->order[i]->value);
}

char *kv_dump(const kv_store *st) {
    size_t len = 1;
    for (size_t i = 0; i < st->n_order; i++) /* use-after-free after removing a tmp key */
        len += strlen(st->order[i]->key) + strlen(st->order[i]->value) + 2;
    char *out = malloc(len), *p = out;
    for (size_t i = 0; i < st->n_order; i++) {
        size_t k = strlen(st->order[i]->key), v = strlen(st->order[i]->value);
        memcpy(p, st->order[i]->key, k);
        p[k] = '=';
        memcpy(p + k + 1, st->order[i]->value, v);
        p[k + 1 + v] = '\n';
        p += k + v + 2;
    }
    *p = 0;
    return out;
}

kv_store *kv_load(const char *text) {
    kv_store *st = kv_new();
    char *copy = strdup(text), *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(line, '='); /* BUG: keys containing '=' do not round-trip */
        if (!eq) goto fail;
        *eq = 0;
        char *v = eq + 1;
        if (strncmp(v, "#!", 2) == 0) {
            v = strchr(v + 2, ' ') + 1; /* BUG: reads address 1 on "#!x" (no space) */
            v[strcspn(v, " ")] = 0;
        }
        if (kv_put(st, line, v) != 0) goto fail;
    }
    free(copy);
    return st;
fail:
    free(copy);
    kv_free(st);
    return NULL;
}

void kv_free(kv_store *st) {
    for (size_t i = 0; i < st->n; i++) {
        free(st->data[i]->key);
        free(st->data[i]->value);
        free(st->data[i]);
    }
    free(st->data);
    free(st->order);
    free(st);
}
