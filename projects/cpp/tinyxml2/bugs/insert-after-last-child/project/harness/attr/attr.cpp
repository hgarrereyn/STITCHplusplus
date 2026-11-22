#include "../objects.hpp"
#include <climits>

#define NEED_NAME(n) stitch::require(valid_name((n).c_str()), "bad name")

static XMLElement* elem(Borrow<Doc>& d, Borrow<Node>& n) {
    stitch::require(live(d, n), "stale node");
    XMLElement* e = ((XMLNode*)n)->ToElement();
    stitch::require(e, "not an element");
    return e;
}

void set_attr_str(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name, Str<64> v) {
    NEED_NAME(name);
    XMLElement* e = elem(d, n);
    e->SetAttribute(name.c_str(), v.c_str());
    const char* got = e->Attribute(name.c_str());
    stitch::check(got && !strcmp(got, v.c_str()), "Attribute() returns what SetAttribute stored");
}
STITCH_ENDPOINT(set_attr_str);

void set_attr_int(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name, I32 v) {
    NEED_NAME(name);
    XMLElement* e = elem(d, n);
    e->SetAttribute(name.c_str(), (int)v);
    int got = 0;
    stitch::check(e->QueryIntAttribute(name.c_str(), &got) == XML_SUCCESS && got == v, "int attribute round-trips");
    stitch::check(e->IntAttribute(name.c_str(), 12345) == v, "IntAttribute");
}
STITCH_ENDPOINT(set_attr_int);

void set_attr_uint(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name, U32 v) {
    NEED_NAME(name);
    XMLElement* e = elem(d, n);
    e->SetAttribute(name.c_str(), (unsigned)v);
    unsigned got = 0;
    stitch::check(e->QueryUnsignedAttribute(name.c_str(), &got) == XML_SUCCESS && got == v, "unsigned attribute round-trips");
}
STITCH_ENDPOINT(set_attr_uint);

void set_attr_int64(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name, I64 v) {
    NEED_NAME(name);
    XMLElement* e = elem(d, n);
    e->SetAttribute(name.c_str(), (int64_t)v);
    int64_t got = 0;
    stitch::check(e->QueryInt64Attribute(name.c_str(), &got) == XML_SUCCESS && got == v, "int64 attribute round-trips");
}
STITCH_ENDPOINT(set_attr_int64);

void set_attr_bool(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name, Bool v) {
    NEED_NAME(name);
    XMLElement* e = elem(d, n);
    e->SetAttribute(name.c_str(), (bool)v);
    bool got = !v;
    stitch::check(e->QueryBoolAttribute(name.c_str(), &got) == XML_SUCCESS && got == (bool)v, "bool attribute round-trips");
}
STITCH_ENDPOINT(set_attr_bool);

void set_attr_double(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name, F64 v) {
    NEED_NAME(name);
    stitch::require(std::isfinite((double)v), "non-finite");
    XMLElement* e = elem(d, n);
    e->SetAttribute(name.c_str(), (double)v);
    double got = 0;
    stitch::check(e->QueryDoubleAttribute(name.c_str(), &got) == XML_SUCCESS, "double attribute queries");
    stitch::check(got == (double)v, "double attribute round-trips exactly");
}
STITCH_ENDPOINT(set_attr_double);

void set_attr_float(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name, F32 v) {
    NEED_NAME(name);
    stitch::require(std::isfinite((float)v), "non-finite");
    XMLElement* e = elem(d, n);
    e->SetAttribute(name.c_str(), (float)v);
    float got = 0;
    stitch::check(e->QueryFloatAttribute(name.c_str(), &got) == XML_SUCCESS, "float attribute queries");
    // 7.0.0 stores floats with 6 significant digits: only check a relative error (see notes.md).
    stitch::check(std::fabs(got - (float)v) <= std::fabs((float)v) * 2e-6f, "float attribute round-trips approximately");
}
STITCH_ENDPOINT(set_attr_float);

void delete_attr(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name) {
    XMLElement* e = elem(d, n);
    e->DeleteAttribute(name.c_str());
    stitch::check(e->Attribute(name.c_str()) == nullptr, "attribute gone after DeleteAttribute");
    stitch::check(e->FindAttribute(name.c_str()) == nullptr, "FindAttribute null after delete");
}
STITCH_ENDPOINT(delete_attr);

// Look up an attribute that exists and cross-check the typed query variants.
void query_existing_attr(Borrow<Doc> d, Borrow<Node> n, Src<8> s) {
    XMLElement* e = elem(d, n);
    int cnt = 0;
    for (const XMLAttribute* a = e->FirstAttribute(); a; a = a->Next()) cnt++;
    stitch::require(cnt > 0, "no attributes");
    const XMLAttribute* a = e->FirstAttribute();
    for (size_t i = s->choice(cnt); i > 0; i--) a = a->Next();
    const char* name = a->Name();
    const char* v = e->Attribute(name);
    stitch::check(v != nullptr, "Attribute(name) finds existing attribute");
    stitch::check(!strcmp(v, a->Value()), "Attribute() == attr->Value()");
    int i = 0; unsigned u = 0; int64_t i64 = 0; bool b = false; double dd = 0; float f = 0;
    XMLError ei = e->QueryIntAttribute(name, &i);
    XMLError eu = e->QueryUnsignedAttribute(name, &u);
    XMLError e64 = e->QueryInt64Attribute(name, &i64);
    XMLError eb = e->QueryBoolAttribute(name, &b);
    XMLError ed = e->QueryDoubleAttribute(name, &dd);
    XMLError ef = e->QueryFloatAttribute(name, &f);
    stitch::check(ei == XML_SUCCESS || ei == XML_WRONG_ATTRIBUTE_TYPE, "int query result");
    stitch::check(eu == XML_SUCCESS || eu == XML_WRONG_ATTRIBUTE_TYPE, "unsigned query result");
    stitch::check(e64 == XML_SUCCESS || e64 == XML_WRONG_ATTRIBUTE_TYPE, "int64 query result");
    stitch::check(eb == XML_SUCCESS || eb == XML_WRONG_ATTRIBUTE_TYPE, "bool query result");
    stitch::check(ed == XML_SUCCESS || ed == XML_WRONG_ATTRIBUTE_TYPE, "double query result");
    stitch::check(ef == XML_SUCCESS || ef == XML_WRONG_ATTRIBUTE_TYPE, "float query result");
    if (ei == XML_SUCCESS && e64 == XML_SUCCESS && i64 >= INT_MIN && i64 <= INT_MAX) stitch::check((int64_t)i == i64, "int and int64 agree");
    if (ei == XML_SUCCESS) stitch::check(e->IntAttribute(name, -99) == i, "IntAttribute agrees with Query");
    // missing attribute
    stitch::check(e->QueryIntAttribute("!no such attr!", &i) == XML_NO_ATTRIBUTE, "missing attribute");
}
STITCH_ENDPOINT(query_existing_attr);

// Query by an arbitrary name (usually missing).
void query_attr_any(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name) {
    XMLElement* e = elem(d, n);
    int i; double dd; bool b; const char* sv;
    XMLError a = e->QueryIntAttribute(name.c_str(), &i);
    XMLError c = e->QueryDoubleAttribute(name.c_str(), &dd);
    XMLError f = e->QueryBoolAttribute(name.c_str(), &b);
    XMLError g = e->QueryStringAttribute(name.c_str(), &sv);
    bool has = e->FindAttribute(name.c_str()) != nullptr;
    stitch::check(has == (g == XML_SUCCESS), "QueryStringAttribute succeeds iff attribute exists");
    if (!has) stitch::check(a == XML_NO_ATTRIBUTE && c == XML_NO_ATTRIBUTE && f == XML_NO_ATTRIBUTE, "missing attr");
}
STITCH_ENDPOINT(query_attr_any);
