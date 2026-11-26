// XMLAttribute value accessors, 64-bit unsigned attributes / text and the QueryAttribute overloads.
#include "../objects.hpp"

static const XMLAttribute* pick_attr(XMLElement* e, stitch::Source& s) {
    int cnt = 0;
    for (const XMLAttribute* a = e->FirstAttribute(); a; a = a->Next()) cnt++;
    stitch::require(cnt > 0, "no attributes");
    const XMLAttribute* a = e->FirstAttribute();
    for (size_t i = s.choice(cnt); i > 0; i--) a = a->Next();
    return a;
}

// The XMLAttribute getters agree with the XMLElement ones for the same name.
void attr_values(Borrow<Doc> d, Borrow<Node> n, Src<8> s) {
    XMLElement* e = need_elem(d, n);
    const XMLAttribute* a = pick_attr(e, *s);
    const char* name = a->Name();
    stitch::check(e->FindAttribute(name) == a, "FindAttribute finds the first attribute of that name");
    stitch::check(e->Attribute(name, a->Value()) == a->Value(), "Attribute(name, value) matches");
    stitch::check(a->GetLineNum() >= 0, "attribute line is not negative");
    int i = 0, ei = 0; unsigned u = 0, eu = 0; int64_t l = 0, el = 0; uint64_t ul = 0, eul = 0;
    bool b = false, eb = false; double dd = 0, edd = 0; float f = 0, ef = 0;
    XMLError ri = a->QueryIntValue(&i), rei = e->QueryIntAttribute(name, &ei);
    XMLError ru = a->QueryUnsignedValue(&u), reu = e->QueryUnsignedAttribute(name, &eu);
    XMLError rl = a->QueryInt64Value(&l), rel = e->QueryInt64Attribute(name, &el);
    XMLError rul = a->QueryUnsigned64Value(&ul), reul = e->QueryUnsigned64Attribute(name, &eul);
    XMLError rb = a->QueryBoolValue(&b), reb = e->QueryBoolAttribute(name, &eb);
    XMLError rd = a->QueryDoubleValue(&dd), red = e->QueryDoubleAttribute(name, &edd);
    XMLError rf = a->QueryFloatValue(&f), ref = e->QueryFloatAttribute(name, &ef);
    stitch::check(ri == rei && ru == reu && rl == rel && rul == reul && rb == reb && rd == red && rf == ref,
                  "attribute and element queries agree");
    if (ri == XML_SUCCESS) stitch::check(i == ei && a->IntValue() == i, "IntValue");
    if (ru == XML_SUCCESS) stitch::check(u == eu && a->UnsignedValue() == u, "UnsignedValue");
    if (rl == XML_SUCCESS) stitch::check(l == el && a->Int64Value() == l, "Int64Value");
    if (rul == XML_SUCCESS) stitch::check(ul == eul && a->Unsigned64Value() == ul, "Unsigned64Value");
    if (rb == XML_SUCCESS) stitch::check(b == eb && a->BoolValue() == b, "BoolValue");
    if (rd == XML_SUCCESS && dd == dd) stitch::check(dd == edd && a->DoubleValue() == dd, "DoubleValue");
    if (rf == XML_SUCCESS && f == f) stitch::check(f == ef && a->FloatValue() == f, "FloatValue");
    // the QueryAttribute overloads forward to the typed queries
    int qi = 0; unsigned qu = 0; int64_t ql = 0; uint64_t qul = 0; bool qb = false; const char* qs = nullptr;
    stitch::check(e->QueryAttribute(name, &qi) == ri && (ri != XML_SUCCESS || qi == i), "QueryAttribute(int)");
    stitch::check(e->QueryAttribute(name, &qu) == ru && (ru != XML_SUCCESS || qu == u), "QueryAttribute(unsigned)");
    stitch::check(e->QueryAttribute(name, &ql) == rl && (rl != XML_SUCCESS || ql == l), "QueryAttribute(int64)");
    stitch::check(e->QueryAttribute(name, &qul) == rul && (rul != XML_SUCCESS || qul == ul), "QueryAttribute(uint64)");
    stitch::check(e->QueryAttribute(name, &qb) == rb && (rb != XML_SUCCESS || qb == b), "QueryAttribute(bool)");
    stitch::check(e->QueryAttribute(name, &qs) == XML_SUCCESS && qs == a->Value(), "QueryAttribute(string)");
    double qd = 0; float qf = 0;
    stitch::check(e->QueryAttribute(name, &qd) == rd && e->QueryAttribute(name, &qf) == rf, "QueryAttribute(double/float)");
    stitch::check((rul == XML_SUCCESS ? ul : 3u) == e->Unsigned64Attribute(name, 3), "Unsigned64Attribute default");
}
STITCH_ENDPOINT(attr_values);

void set_attr_uint64(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name, U64 v) {
    stitch::require(valid_name(name.c_str()), "bad name");
    XMLElement* e = need_elem(d, n);
    e->SetAttribute(name.c_str(), (uint64_t)v);
    uint64_t got = 0;
    stitch::check(e->QueryUnsigned64Attribute(name.c_str(), &got) == XML_SUCCESS && got == v, "uint64 attribute round-trips");
    stitch::check(e->Unsigned64Attribute(name.c_str(), 1) == v, "Unsigned64Attribute");
    stitch::check(e->FindAttribute(name.c_str())->Unsigned64Value() == v, "Unsigned64Value");
}
STITCH_ENDPOINT(set_attr_uint64);

void set_text_uint64(Borrow<Doc> d, Borrow<Node> n, U64 v) {
    XMLElement* e = need_elem(d, n);
    e->SetText((uint64_t)v);
    uint64_t got = 0;
    stitch::check(e->QueryUnsigned64Text(&got) == XML_SUCCESS && got == v, "uint64 text round-trips");
    stitch::check(e->Unsigned64Text(1) == v, "Unsigned64Text");
}
STITCH_ENDPOINT(set_text_uint64);

void query_text_uint64(Borrow<Doc> d, Borrow<Node> n) {
    XMLElement* e = need_elem(d, n);
    uint64_t got = 0;
    XMLError r = e->QueryUnsigned64Text(&got);
    if (!e->GetText()) stitch::check(r == XML_NO_TEXT_NODE, "no text node");
    stitch::check((r == XML_SUCCESS ? got : 5u) == e->Unsigned64Text(5), "Unsigned64Text default");
}
STITCH_ENDPOINT(query_text_uint64);
