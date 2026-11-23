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
    stitch::check(std::fabs(got - (float)v) <= std::fabs((float)v) * 2e-6f, "float text round-trips approximately");
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
    if (v) stitch::require(!strstr(t->Value(), "]]>"), "cdata with ]]>");
    t->SetCData(v);
    stitch::check(t->CData() == (bool)v, "CData flag");
}
STITCH_ENDPOINT(set_cdata);

void set_text_uint(Borrow<Doc> d, Borrow<Node> n, U32 v) {
    XMLElement* e = elem(d, n);
    e->SetText((unsigned)v);
    unsigned got = 0;
    stitch::check(e->QueryUnsignedText(&got) == XML_SUCCESS && got == v, "unsigned text round-trips");
    stitch::check(e->UnsignedText(1) == v, "UnsignedText");
}
STITCH_ENDPOINT(set_text_uint);

// The default-value getters must agree with the Query* variants.
void typed_defaults(Borrow<Doc> d, Borrow<Node> n, Src<8> s) {
    XMLElement* e = elem(d, n);
    int64_t i64 = 0; bool b = false; double dd = 0; float f = 0; unsigned u = 0;
    const char* t = e->GetText();
    stitch::require(t, "no text");
    stitch::check((e->QueryInt64Text(&i64) == XML_SUCCESS ? i64 : 77) == e->Int64Text(77), "Int64Text default");
    stitch::check((e->QueryBoolText(&b) == XML_SUCCESS ? b : true) == e->BoolText(true), "BoolText default");
    stitch::check((e->QueryUnsignedText(&u) == XML_SUCCESS ? u : 7u) == e->UnsignedText(7), "UnsignedText default");
    if (e->QueryDoubleText(&dd) == XMLError::XML_SUCCESS) stitch::check(dd == e->DoubleText(1.5) || std::isnan(dd), "DoubleText default");
    if (e->QueryFloatText(&f) == XMLError::XML_SUCCESS) stitch::check(f == e->FloatText(1.5f) || std::isnan(f), "FloatText default");
    // attribute variants on the first attribute, if any
    const XMLAttribute* a = e->FirstAttribute();
    stitch::require(a, "no attributes");
    const char* nm = a->Name();
    stitch::check((e->QueryInt64Attribute(nm, &i64) == XML_SUCCESS ? i64 : 5) == e->Int64Attribute(nm, 5), "Int64Attribute default");
    stitch::check((e->QueryBoolAttribute(nm, &b) == XML_SUCCESS ? b : true) == e->BoolAttribute(nm, true), "BoolAttribute default");
    stitch::check((e->QueryUnsignedAttribute(nm, &u) == XML_SUCCESS ? u : 9u) == e->UnsignedAttribute(nm, 9), "UnsignedAttribute default");
    if (e->QueryDoubleAttribute(nm, &dd) == XML_SUCCESS) stitch::check(dd == e->DoubleAttribute(nm, 2.5) || std::isnan(dd), "DoubleAttribute default");
    if (e->QueryFloatAttribute(nm, &f) == XML_SUCCESS) stitch::check(f == e->FloatAttribute(nm, 2.5f) || std::isnan(f), "FloatAttribute default");
}
STITCH_ENDPOINT(typed_defaults);
