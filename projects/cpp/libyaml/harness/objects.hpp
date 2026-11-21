// libyaml harness.
#pragma once
#include <stitch.h>
#include <yaml.h>
#include <stdlib.h>
#include <string.h>
#include <string>

// Parser + the input it reads (yaml_parser_set_input_string does not copy).
// Attribute "mode": "scan" | "parse" | "load" (the API forbids mixing them on one parser).
struct ParserT {
    yaml_parser_t p;
    std::string input;
};
STITCH_OBJECT(Parser, ParserT*);
STITCH_DROP(Parser, [](ParserT* p) { yaml_parser_delete(&p->p); delete p; });

// Emitter writing into a growable string.
struct EmitterT {
    yaml_emitter_t e;
    std::string out;
};
STITCH_OBJECT(Emitter, EmitterT*);
STITCH_DROP(Emitter, [](EmitterT* e) { yaml_emitter_delete(&e->e); delete e; });

STITCH_OBJECT(Event, yaml_event_t*);
STITCH_DROP(Event, [](yaml_event_t* e) { yaml_event_delete(e); delete e; });
STITCH_OBJECT(Token, yaml_token_t*);
STITCH_DROP(Token, [](yaml_token_t* t) { yaml_token_delete(t); delete t; });
STITCH_OBJECT(Document, yaml_document_t*);
STITCH_DROP(Document, [](yaml_document_t* d) { yaml_document_delete(d); delete d; });

STITCH_DATATYPE(YamlText);
STITCH_DATATYPE(YamlAnchor);  // anchors / aliases
STITCH_DATATYPE(YamlTag);     // tags ("!", "tag:yaml.org,2002:str", "!local", ...)

namespace yh {
using Encoding = OneOf<YAML_ANY_ENCODING, YAML_UTF8_ENCODING, YAML_UTF16LE_ENCODING, YAML_UTF16BE_ENCODING>;
using ScalarStyle = OneOf<YAML_ANY_SCALAR_STYLE, YAML_PLAIN_SCALAR_STYLE, YAML_SINGLE_QUOTED_SCALAR_STYLE,
                          YAML_DOUBLE_QUOTED_SCALAR_STYLE, YAML_LITERAL_SCALAR_STYLE, YAML_FOLDED_SCALAR_STYLE>;
using SeqStyle = OneOf<YAML_ANY_SEQUENCE_STYLE, YAML_BLOCK_SEQUENCE_STYLE, YAML_FLOW_SEQUENCE_STYLE>;
using MapStyle = OneOf<YAML_ANY_MAPPING_STYLE, YAML_BLOCK_MAPPING_STYLE, YAML_FLOW_MAPPING_STYLE>;

inline const yaml_char_t* opt(const std::string& s, bool present) { return present ? (const yaml_char_t*)s.c_str() : nullptr; }

inline int write_handler(void* data, unsigned char* buffer, size_t size) {
    auto* e = static_cast<EmitterT*>(data);
    if (e->out.size() + size > (8u << 20)) return 0;  // cap output
    e->out.append((const char*)buffer, size);
    return 1;
}

// libyaml asserts on NULL even for zero-length values
template <class B>
inline const yaml_char_t* nonnull(const B& b) { return b.size() ? (const yaml_char_t*)b.data() : (const yaml_char_t*)""; }

inline int nodes(const yaml_document_t* d) { return (int)(d->nodes.top - d->nodes.start); }
}  // namespace yh
