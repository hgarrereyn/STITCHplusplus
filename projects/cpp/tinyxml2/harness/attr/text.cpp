#include "../objects.hpp"

static XMLElement* elem(Borrow<Doc>& d, Borrow<Node>& n) {
    stitch::require(live(d, n), "stale node");
    XMLElement* e = ((XMLNode*)n)->ToElement();
    stitch::require(e, "not an element");
    return e;
}

void set_text_str(Borrow<Doc> d, Borrow<Node> n, Str<64> v) {
    XMLElement* e = elem(d, n);
    e->SetText(v.c_str());
    const char* got = e->GetText();
    stitch::check(got != nullptr, "GetText after SetText");
    stitch::check(!strcmp(got, v.c_str()), "GetText returns what SetText stored");
}
STITCH_ENDPOINT(set_text_str);

void set_text_int(Borrow<Doc> d, Borrow<Node> n, I32 v) {
    XMLElement* e = elem(d, n);
    e->SetText((int)v);
    int got = 0;
    stitch::check(e->QueryIntText(&got) == XML_SUCCESS && got == v, "int text round-trips");
    stitch::check(e->IntText(-1) == v, "IntText");
}
STITCH_ENDPOINT(set_text_int);

void set_text_int64(Borrow<Doc> d, Borrow<Node> n, I64 v) {
    XMLElement* e = elem(d, n);
    e->SetText((int64_t)v);
    int64_t got = 0;
    stitch::check(e->QueryInt64Text(&got) == XML_SUCCESS && got == v, "int64 text round-trips");
}
STITCH_ENDPOINT(set_text_int64);

void set_text_bool(Borrow<Doc> d, Borrow<Node> n, Bool v) {
    XMLElement* e = elem(d, n);
    e->SetText((bool)v);
    bool got = !v;
    stitch::check(e->QueryBoolText(&got) == XML_SUCCESS && got == (bool)v, "bool text round-trips");
}
STITCH_ENDPOINT(set_text_bool);

void set_text_double(Borrow<Doc> d, Borrow<Node> n, F64 v) {
    stitch::require(std::isfinite((double)v), "non-finite");
    XMLElement* e = elem(d, n);
    e->SetText((double)v);
    double got = 0;
    stitch::check(e->QueryDoubleText(&got) == XML_SUCCESS, "double text queries");
    stitch::check(got == (double)v, "double text round-trips exactly");
}
STITCH_ENDPOINT(set_text_double);

void set_text_float(Borrow<Doc> d, Borrow<Node> n, F32 v) {
    stitch::require(std::isfinite((float)v), "non-finite");
    XMLElement* e = elem(d, n);
    e->SetText((float)v);
    float got = 0;
    stitch::check(e->QueryFloatText(&got) == XML_SUCCESS, "float text queries");
    stitch::check(got == (float)v, "float text round-trips exactly");
}
STITCH_ENDPOINT(set_text_float);

// GetText / typed text queries on whatever the element holds.
void query_text(Borrow<Doc> d, Borrow<Node> n) {
    XMLElement* e = elem(d, n);
    const char* t = e->GetText();
    int i; unsigned u; int64_t i64; bool b; double dd; float f;
    XMLError a = e->QueryIntText(&i);
    XMLError a2 = e->QueryUnsignedText(&u);
    XMLError a3 = e->QueryInt64Text(&i64);
    XMLError a4 = e->QueryBoolText(&b);
    XMLError a5 = e->QueryDoubleText(&dd);
    XMLError a6 = e->QueryFloatText(&f);
    if (!t) {
        stitch::check(a == XML_NO_TEXT_NODE && a2 == XML_NO_TEXT_NODE && a3 == XML_NO_TEXT_NODE &&
                      a4 == XML_NO_TEXT_NODE && a5 == XML_NO_TEXT_NODE && a6 == XML_NO_TEXT_NODE, "no text node");
    }
}
STITCH_ENDPOINT(query_text);

// Set the CDATA flag on a text node.
void set_cdata(Borrow<Doc> d, Borrow<Node> n, Bool v) {
    stitch::require(live(d, n), "stale node");
    XMLText* t = ((XMLNode*)n)->ToText();
    stitch::require(t, "not a text node");
    t->SetCData(v);
    stitch::check(t->CData() == (bool)v, "CData flag");
}
STITCH_ENDPOINT(set_cdata);
