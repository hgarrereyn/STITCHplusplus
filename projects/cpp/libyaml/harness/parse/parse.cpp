#include "../objects.hpp"

New<Parser> parser_new(Str<8192, YamlText> text, yh::Encoding enc, Bool set_encoding) {
    auto* p = new ParserT();
    if (!yaml_parser_initialize(&p->p)) {
        delete p;
        stitch::bail("parser init failed");
    }
    p->input = text.s;
    yaml_parser_set_input_string(&p->p, (const unsigned char*)p->input.data(), p->input.size());
    if (set_encoding) yaml_parser_set_encoding(&p->p, (yaml_encoding_t)(int)enc);
    return p;
}
STITCH_ENDPOINT(parser_new);

static void use_mode(Borrow<Parser>& p, const char* mode) {
    std::string m = stitch::get_str(p, "mode", "");
    stitch::require(m.empty() || m == mode, "parser already used in another mode");
    stitch::set(p, "mode", mode);
}

std::optional<New<Event>> parse_event(Borrow<Parser> p) {
    use_mode(p, "parse");
    auto* ev = new yaml_event_t();
    if (!yaml_parser_parse(&p->p, ev)) {
        delete ev;
        stitch::check(p->p.error != YAML_NO_ERROR, "failure sets an error");
        stitch::bail("parse error");
    }
    if (ev->type == YAML_NO_EVENT) {
        delete ev;
        return std::nullopt;
    }
    return New<Event>(ev);
}
STITCH_ENDPOINT(parse_event);

// Parse to the end; checks the event stream is balanced.
void parse_all(Borrow<Parser> p) {
    use_mode(p, "parse");
    int depth = 0;
    for (int i = 0; i < 100000; i++) {
        yaml_event_t ev;
        if (!yaml_parser_parse(&p->p, &ev)) stitch::bail("parse error");
        yaml_event_type_t t = ev.type;
        if (t == YAML_SEQUENCE_START_EVENT || t == YAML_MAPPING_START_EVENT) depth++;
        if (t == YAML_SEQUENCE_END_EVENT || t == YAML_MAPPING_END_EVENT) depth--;
        stitch::check(depth >= 0, "event nesting never goes negative");
        yaml_event_delete(&ev);
        if (t == YAML_STREAM_END_EVENT || t == YAML_NO_EVENT) break;
    }
}
STITCH_ENDPOINT(parse_all);

std::optional<New<Token>> scan_token(Borrow<Parser> p) {
    use_mode(p, "scan");
    auto* t = new yaml_token_t();
    if (!yaml_parser_scan(&p->p, t)) {
        delete t;
        stitch::bail("scan error");
    }
    if (t->type == YAML_NO_TOKEN) {
        delete t;
        return std::nullopt;
    }
    return New<Token>(t);
}
STITCH_ENDPOINT(scan_token);

void scan_all(Borrow<Parser> p) {
    use_mode(p, "scan");
    for (int i = 0; i < 100000; i++) {
        yaml_token_t t;
        if (!yaml_parser_scan(&p->p, &t)) stitch::bail("scan error");
        yaml_token_type_t ty = t.type;
        if (ty == YAML_SCALAR_TOKEN) stitch::check(t.data.scalar.value != nullptr, "scalar token has a value");
        yaml_token_delete(&t);
        if (ty == YAML_STREAM_END_TOKEN || ty == YAML_NO_TOKEN) break;
    }
}
STITCH_ENDPOINT(scan_all);

std::optional<New<Document>> load_document(Borrow<Parser> p) {
    use_mode(p, "load");
    // after a failed load the parser returns empty events and yaml_parser_load asserts
    // (questionable library behavior, recorded in notes.md): do not reuse it
    stitch::require(stitch::get(p, "load_failed", 0) == 0, "parser failed earlier");
    auto* d = new yaml_document_t();
    if (!yaml_parser_load(&p->p, d)) {
        delete d;
        stitch::set(p, "load_failed", 1);
        stitch::bail("load error");
    }
    if (!yaml_document_get_root_node(d)) {  // end of stream: an empty document
        yaml_document_delete(d);
        delete d;
        return std::nullopt;
    }
    return New<Document>(d);
}
STITCH_ENDPOINT(load_document);

void inspect_event(Borrow<Event> e) {
    if (e->type == YAML_SCALAR_EVENT) {
        stitch::check(e->data.scalar.value != nullptr, "scalar event has a value");
        stitch::check(strlen((char*)e->data.scalar.value) <= e->data.scalar.length, "scalar length >= strlen");
    }
    stitch::check(e->start_mark.index <= e->end_mark.index, "event start <= end");
}
STITCH_ENDPOINT(inspect_event);

void destroy_event(Take<Event> e) {
    yaml_event_delete(*e);
    delete *e;
}
STITCH_ENDPOINT(destroy_event);

void destroy_token(Take<Token> t) {
    yaml_token_delete(*t);
    delete *t;
}
STITCH_ENDPOINT(destroy_token);

void set_max_nest_level(Range<0, 2000> max) { yaml_set_max_nest_level((int)max); }
STITCH_ENDPOINT(set_max_nest_level);
