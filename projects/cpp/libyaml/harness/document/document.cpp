// Document API. Node ids are 1-based indexes into the document; the library asserts on invalid
// ids and node types, so stubs pick valid ones (that is the documented contract).
#include "../objects.hpp"

New<Document> document_new(Bool with_version, Range<0, 2> major, Range<0, 3> minor, Bool start_implicit, Bool end_implicit) {
    yaml_version_directive_t v{(int)major, (int)minor};
    auto* d = new yaml_document_t();
    if (!yaml_document_initialize(d, with_version ? &v : nullptr, nullptr, nullptr, start_implicit, end_implicit)) {
        delete d;
        stitch::bail("document init failed");
    }
    return d;
}
STITCH_ENDPOINT(document_new);

void add_scalar(Borrow<Document> d, Bool has_tag, Str<64, YamlTag> tag, Bytes<512, YamlText> value, yh::ScalarStyle style) {
    int id = yaml_document_add_scalar(*d, yh::opt(tag.s, has_tag), yh::nonnull(value), (int)value.size(), (yaml_scalar_style_t)(int)style);
    stitch::require(id > 0, "add_scalar failed");
    stitch::check(id == yh::nodes(*d), "new node id is the last index");
}
STITCH_ENDPOINT(add_scalar);

void add_sequence(Borrow<Document> d, Bool has_tag, Str<64, YamlTag> tag, yh::SeqStyle style) {
    stitch::require(yaml_document_add_sequence(*d, yh::opt(tag.s, has_tag), (yaml_sequence_style_t)(int)style) > 0, "add failed");
}
STITCH_ENDPOINT(add_sequence);

void add_mapping(Borrow<Document> d, Bool has_tag, Str<64, YamlTag> tag, yh::MapStyle style) {
    stitch::require(yaml_document_add_mapping(*d, yh::opt(tag.s, has_tag), (yaml_mapping_style_t)(int)style) > 0, "add failed");
}
STITCH_ENDPOINT(add_mapping);

// pick a node id (1-based) of the given type, or any type if type == YAML_NO_NODE
static int pick_node(yaml_document_t* d, stitch::Source& s, yaml_node_type_t type) {
    std::vector<int> ids;
    for (int i = 1; i <= yh::nodes(d); i++)
        if (type == YAML_NO_NODE || yaml_document_get_node(d, i)->type == type) ids.push_back(i);
    stitch::require(!ids.empty(), "no node of the required type");
    return ids[s.choice(ids.size())];
}

void append_sequence_item(Borrow<Document> d, Src<16> s) {
    int seq = pick_node(*d, *s, YAML_SEQUENCE_NODE);
    int item = pick_node(*d, *s, YAML_NO_NODE);
    stitch::require(yaml_document_append_sequence_item(*d, seq, item), "append failed");
}
STITCH_ENDPOINT(append_sequence_item);

void append_mapping_pair(Borrow<Document> d, Src<16> s) {
    int map = pick_node(*d, *s, YAML_MAPPING_NODE);
    int key = pick_node(*d, *s, YAML_NO_NODE);
    int value = pick_node(*d, *s, YAML_NO_NODE);
    stitch::require(yaml_document_append_mapping_pair(*d, map, key, value), "append failed");
}
STITCH_ENDPOINT(append_mapping_pair);

void inspect_document(Borrow<Document> d, Src<16> s, Range<-3, 64> raw_index) {
    yaml_node_t* root = yaml_document_get_root_node(*d);
    stitch::check((root == nullptr) == (yh::nodes(*d) == 0), "root exists iff nodes exist");
    yaml_node_t* n = yaml_document_get_node(*d, (int)raw_index);  // out-of-range must return NULL
    stitch::check(n == nullptr || ((int)raw_index >= 1 && (int)raw_index <= yh::nodes(*d)), "get_node bounds");
    if (yh::nodes(*d)) {
        yaml_node_t* x = yaml_document_get_node(*d, (int)s->range(1, yh::nodes(*d)));
        if (x->type == YAML_SCALAR_NODE) stitch::check(x->data.scalar.value != nullptr, "scalar node has a value");
    }
}
STITCH_ENDPOINT(inspect_document);

void destroy_document(Take<Document> d) {
    yaml_document_delete(*d);
    delete *d;
}
STITCH_ENDPOINT(destroy_document);
