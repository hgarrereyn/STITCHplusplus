#include "../objects.hpp"

New<Emitter> emitter_new(yh::Encoding enc, Bool canonical, Range<-1, 12> indent, Range<-1, 200> width, Bool unicode,
                         OneOf<YAML_ANY_BREAK, YAML_CR_BREAK, YAML_LN_BREAK, YAML_CRLN_BREAK> brk) {
    auto* e = new EmitterT();
    if (!yaml_emitter_initialize(&e->e)) {
        delete e;
        stitch::bail("emitter init failed");
    }
    yaml_emitter_set_output(&e->e, yh::write_handler, e);
    yaml_emitter_set_encoding(&e->e, (yaml_encoding_t)(int)enc);
    yaml_emitter_set_canonical(&e->e, canonical);
    yaml_emitter_set_indent(&e->e, (int)indent);
    yaml_emitter_set_width(&e->e, (int)width);
    yaml_emitter_set_unicode(&e->e, unicode);
    yaml_emitter_set_break(&e->e, (yaml_break_t)(int)brk);
    return e;
}
STITCH_ENDPOINT(emitter_new);

// The emitter takes the event (deleted even on failure).
void emit(Borrow<Emitter> e, Take<Event> ev) {
    int ok = yaml_emitter_emit(&e->e, *ev);
    delete *ev;
    stitch::require(ok, "emit rejected the event");
}
STITCH_ENDPOINT(emit);

// The emitter takes the document (deleted even on failure).
void dump(Borrow<Emitter> e, Take<Document> d) {
    int ok = yaml_emitter_dump(&e->e, *d);
    delete *d;
    stitch::require(ok, "dump failed");
}
STITCH_ENDPOINT(dump);

// Preconditions (asserted by libyaml): open once, close only after open, flush only after the
// output encoding is known (set by the STREAM-START event or yaml_emitter_open).
void emitter_control(Borrow<Emitter> e, OneOf<0, 1, 2> op) {
    if ((int)op == 0) stitch::require(!e->e.opened, "emitter already opened");
    if ((int)op == 1) stitch::require(e->e.opened, "emitter not opened");
    if ((int)op == 2) stitch::require(e->e.encoding != YAML_ANY_ENCODING, "output encoding not set yet");
    int ok = (int)op == 0 ? yaml_emitter_open(&e->e) : (int)op == 1 ? yaml_emitter_close(&e->e) : yaml_emitter_flush(&e->e);
    stitch::require(ok, "emitter rejected open/close/flush");
}
STITCH_ENDPOINT(emitter_control);

// Re-parse what the emitter produced (a fresh parser over its output).
New<Parser> parse_emitted(Borrow<Emitter> e) {
    if (e->e.encoding != YAML_ANY_ENCODING) yaml_emitter_flush(&e->e);
    auto* p = new ParserT();
    yaml_parser_initialize(&p->p);
    p->input = e->out;
    yaml_parser_set_input_string(&p->p, (const unsigned char*)p->input.data(), p->input.size());
    return p;
}
STITCH_ENDPOINT(parse_emitted);

// ---- event constructors
static New<Event> make(int ok, yaml_event_t* ev) {
    if (!ok) {
        delete ev;
        stitch::bail("event initialize rejected arguments");
    }
    return ev;
}

New<Event> ev_stream_start(yh::Encoding enc) {
    auto* ev = new yaml_event_t();
    return make(yaml_stream_start_event_initialize(ev, (yaml_encoding_t)(int)enc), ev);
}
STITCH_ENDPOINT(ev_stream_start);

New<Event> ev_stream_end() {
    auto* ev = new yaml_event_t();
    return make(yaml_stream_end_event_initialize(ev), ev);
}
STITCH_ENDPOINT(ev_stream_end);

New<Event> ev_document_start(Bool with_version, Range<0, 2> major, Range<0, 3> minor, Src<128> tags, Bool implicit) {
    yaml_version_directive_t v{(int)major, (int)minor};
    std::vector<std::string> store;
    while (store.size() < 8 && tags->more()) {
        store.push_back(tags->coin() ? "!" + tags->str(6) + "!" : "!!");
        store.push_back(tags->coin() ? "tag:example.com,2000:" + tags->str(8) : tags->str(10));
    }
    std::vector<yaml_tag_directive_t> td;
    for (size_t i = 0; i + 1 < store.size(); i += 2) td.push_back({(yaml_char_t*)store[i].c_str(), (yaml_char_t*)store[i + 1].c_str()});
    auto* ev = new yaml_event_t();
    return make(yaml_document_start_event_initialize(ev, with_version ? &v : nullptr, td.empty() ? nullptr : td.data(),
                                                     td.empty() ? nullptr : td.data() + td.size(), implicit),
                ev);
}
STITCH_ENDPOINT(ev_document_start);

New<Event> ev_document_end(Bool implicit) {
    auto* ev = new yaml_event_t();
    return make(yaml_document_end_event_initialize(ev, implicit), ev);
}
STITCH_ENDPOINT(ev_document_end);

New<Event> ev_alias(Str<32, YamlAnchor> anchor) {
    auto* ev = new yaml_event_t();
    return make(yaml_alias_event_initialize(ev, (const yaml_char_t*)anchor.c_str()), ev);
}
STITCH_ENDPOINT(ev_alias);

New<Event> ev_scalar(Bool has_anchor, Str<32, YamlAnchor> anchor, Bool has_tag, Str<64, YamlTag> tag, Bytes<1024, YamlText> value,
                     Bool plain_implicit, Bool quoted_implicit, yh::ScalarStyle style) {
    auto* ev = new yaml_event_t();
    return make(yaml_scalar_event_initialize(ev, yh::opt(anchor.s, has_anchor), yh::opt(tag.s, has_tag), yh::nonnull(value),
                                             (int)value.size(), plain_implicit, quoted_implicit, (yaml_scalar_style_t)(int)style),
                ev);
}
STITCH_ENDPOINT(ev_scalar);

New<Event> ev_sequence_start(Bool has_anchor, Str<32, YamlAnchor> anchor, Bool has_tag, Str<64, YamlTag> tag, Bool implicit,
                             yh::SeqStyle style) {
    auto* ev = new yaml_event_t();
    return make(yaml_sequence_start_event_initialize(ev, yh::opt(anchor.s, has_anchor), yh::opt(tag.s, has_tag), implicit,
                                                     (yaml_sequence_style_t)(int)style),
                ev);
}
STITCH_ENDPOINT(ev_sequence_start);

New<Event> ev_sequence_end() {
    auto* ev = new yaml_event_t();
    return make(yaml_sequence_end_event_initialize(ev), ev);
}
STITCH_ENDPOINT(ev_sequence_end);

New<Event> ev_mapping_start(Bool has_anchor, Str<32, YamlAnchor> anchor, Bool has_tag, Str<64, YamlTag> tag, Bool implicit,
                            yh::MapStyle style) {
    auto* ev = new yaml_event_t();
    return make(yaml_mapping_start_event_initialize(ev, yh::opt(anchor.s, has_anchor), yh::opt(tag.s, has_tag), implicit,
                                                    (yaml_mapping_style_t)(int)style),
                ev);
}
STITCH_ENDPOINT(ev_mapping_start);

New<Event> ev_mapping_end() {
    auto* ev = new yaml_event_t();
    return make(yaml_mapping_end_event_initialize(ev), ev);
}
STITCH_ENDPOINT(ev_mapping_end);
