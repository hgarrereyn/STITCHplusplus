// cJSON harness: object types, data types and shared helpers.
#pragma once
#include <stitch.h>
#include <cJSON.h>
#include <cJSON_Utils.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// A standalone JSON value that owns its subtree (parsed, created, detached or duplicated).
// Attributes: "ver" (bumped on every structural change), "refs" (live reference nodes pointing
// into it: while > 0 it must not be modified, moved into another tree, or deleted).
STITCH_OBJECT(Json, cJSON*);
STITCH_DROP(Json, [](cJSON* j) { cJSON_Delete(j); });

// A node inside some Json tree (not owned). Attributes: relation "root", "ver" of the root when the
// view was taken; the view is only used while the root is alive and unchanged.
struct ItemRef {
    cJSON* item;
    cJSON* parent;  // container holding the item (for *ViaPointer APIs)
};
STITCH_OBJECT(Item, ItemRef);

// A reference node (cJSON_Create*Reference): does not own what it points to. `str` is the backing
// storage of string references (owned here). Attribute: relation "target".
struct RefT {
    cJSON* ref;
    char* str;
};
STITCH_OBJECT(JRef, RefT);
STITCH_DROP(JRef, [](RefT r) {
    cJSON_Delete(r.ref);
    free(r.str);
});

STITCH_DATATYPE(JsonText);     // whole JSON documents
STITCH_DATATYPE(JsonKey);      // object member names
STITCH_DATATYPE(JsonPointer);  // RFC 6901 pointers ("/a/0/b")

namespace cj {

inline void touch(const Borrow<Json>& j) { stitch::set(j, "ver", stitch::get(j, "ver", 0) + 1); }

// Structural changes are not allowed while reference nodes point into this value.
inline void require_unreferenced(const Borrow<Json>& j) {
    stitch::require(stitch::get(j, "refs", 0) == 0, "value is the target of a live reference");
}

// A detached node that is a reference shares its children with the referenced value: it is not
// an owning Json. Free just the wrapper and bail.
inline cJSON* owning(cJSON* d) {
    if (d && (d->type & cJSON_IsReference)) {
        cJSON_Delete(d);
        stitch::bail("detached a reference node");
    }
    return d;
}

// Resolve an item view; bails if its root is gone or changed since the lookup. Views below a
// reference node are read-only (writes would modify the referenced value behind its back).
inline cJSON* live_item(const Borrow<Item>& it, bool for_write = false) {
    if (for_write) stitch::require(stitch::get(it, "readonly", 0) == 0, "item is inside a reference");
    cJSON** root = stitch::get_obj<Json>(it, "root");
    stitch::require(root != nullptr, "item's root is gone");
    stitch::require(stitch::get(*root, "ver", 0) == stitch::get(it, "ver", 0), "item's root changed since lookup");
    if (for_write) stitch::require(stitch::get(*root, "refs", 0) == 0, "item's root is the target of a live reference");
    return it->item;
}

inline void bump_root(const Borrow<Item>& it) {
    cJSON** root = stitch::get_obj<Json>(it, "root");
    if (root) stitch::set(*root, "ver", stitch::get(*root, "ver", 0) + 1);
}

// New item view of `item` inside the tree rooted at `root` (a Json object or another view's root).
template <class R>
New<Item> view(const R& root, cJSON* item, cJSON* parent, bool readonly = false) {
    New<Item> v(ItemRef{item, parent});
    stitch::set(v, "root", root);
    stitch::set(v, "ver", stitch::get(root, "ver", 0));
    readonly = readonly || (item->type & cJSON_IsReference) || (parent && (parent->type & cJSON_IsReference));
    if (readonly) stitch::set(v, "readonly", 1);
    return v;
}

// true if cJSON_Compare / print -> parse cannot be expected to round-trip this tree: raw text,
// non-finite or huge numbers, object members without a name (e.g. object references to bare
// values) or duplicate member names (Compare looks members up by name).
inline bool has_lossy(const cJSON* j, bool in_object = false) {
    for (const cJSON* a = j; a; a = a->next) {
        if (cJSON_IsRaw(a) || cJSON_IsInvalid(a)) return true;
        if (cJSON_IsNumber(a) && (!isfinite(a->valuedouble) || fabs(a->valuedouble) > 1e300)) return true;
        if (cJSON_IsString(a) && a->valuestring == nullptr) return true;
        if (in_object && a->string == nullptr) return true;
        if (in_object && a->string)
            for (const cJSON* b = a->next; b; b = b->next)
                if (b->string && strcasecmp(a->string, b->string) == 0) return true;
        if (a->child && has_lossy(a->child, cJSON_IsObject(a))) return true;
    }
    return false;
}
inline bool has_null_value(const cJSON* j) {
    for (; j; j = j->next) {
        if (cJSON_IsNull(j)) return true;
        if (j->child && has_null_value(j->child)) return true;
    }
    return false;
}

// Mostly an existing member name of `obj` (so lookups hit), sometimes a fresh key.
inline std::string pick_key(const cJSON* obj, stitch::Source& s, const char* fresh) {
    int n = 0;
    for (const cJSON* c = obj->child; c; c = c->next) n += c->string != nullptr;
    if (n > 0 && s.coin(0.8)) {
        int k = (int)s.choice((size_t)n);
        for (const cJSON* c = obj->child; c; c = c->next)
            if (c->string && k-- == 0) return s.coin(0.9) ? std::string(c->string) : std::string(c->string) + "x";
    }
    return fresh;
}

inline int pick_index(stitch::Source& s, int n) {
    // mostly valid indices, sometimes out of range / negative
    if (s.coin(0.85)) return n > 0 ? (int)s.choice((size_t)n) : 0;
    return (int)s.range(-2, n + 2);
}

}  // namespace cj
