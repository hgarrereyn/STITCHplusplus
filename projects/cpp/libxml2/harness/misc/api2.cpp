// hash.h (two and three keys, scans, copies), list.h (the rest of the list API), dict.h limits,
// encoding handlers, string helpers, the *Safe URI functions, and DTD declarations, entities
// and IDs built through the API (valid.h, entities.h).
#include "../objects.hpp"
#include <algorithm>
#include <cstdarg>
#include <map>
#include <tuple>
#include <libxml/hash.h>
#include <libxml/list.h>
#include <libxml/entities.h>
#include <libxml/encoding.h>

namespace {
using Key3 = std::tuple<std::string, std::string, std::string>;
void count_scan(void*, void* data, const xmlChar*) { ++*(int*)data; }
void count_scan_full(void*, void* data, const xmlChar*, const xmlChar*, const xmlChar*) { ++*(int*)data; }
void* copy_str(void* p, const xmlChar*) { return xmlStrdup((const xmlChar*)p); }
}  // namespace

// Three-key hash tables (with a dictionary, owning their values) against a std::map model.
void hash3_model(Src<96> s, Bool with_dict) {
    xmlDictPtr dict = with_dict ? xmlDictCreate() : nullptr;
    xmlHashTablePtr h = dict ? xmlHashCreateDict((int)s->range(0, 8), dict) : xmlHashCreate(0);
    stitch::require(h, "no hash table");
    std::map<Key3, std::string> model;
    static const char* ks[] = {"a", "b", "p:c", "", "d"};
    while (s->more() && model.size() < 64) {
        std::string k1 = ks[s->choice(4)], k2 = ks[s->choice(5)], k3 = ks[s->choice(5)];
        const xmlChar *K1 = (const xmlChar*)k1.c_str(), *K2 = k2.empty() ? nullptr : (const xmlChar*)k2.c_str(),
                      *K3 = k3.empty() ? nullptr : (const xmlChar*)k3.c_str();
        Key3 key{k1, k2, k3};
        std::string v = std::to_string(s->range(0, 999));
        bool had = model.count(key);
        switch (s->choice(6)) {
            case 0: {
                xmlChar* val = xmlStrdup((const xmlChar*)v.c_str());
                int r = xmlHashAdd3(h, K1, K2, K3, val);
                stitch::check(r == (had ? 0 : 1), "xmlHashAdd3 adds only new keys");
                if (r == 1) model[key] = v;
                else xmlFree(val);
                break;
            }
            case 1: {
                xmlChar* val = xmlStrdup((const xmlChar*)v.c_str());
                int r = xmlHashAddEntry3(h, K1, K2, K3, val);
                stitch::check((r == 0) == !had, "xmlHashAddEntry3 fails for an existing key");
                if (r == 0) model[key] = v;
                else xmlFree(val);
                break;
            }
            case 2:
                stitch::check(xmlHashUpdateEntry3(h, K1, K2, K3, xmlStrdup((const xmlChar*)v.c_str()), xmlHashDefaultDeallocator) == 0, "UpdateEntry3");
                model[key] = v;
                break;
            case 3: {
                int r = xmlHashRemoveEntry3(h, K1, K2, K3, xmlHashDefaultDeallocator);
                stitch::check((r == 0) == had, "RemoveEntry3 removes existing keys");
                model.erase(key);
                break;
            }
            case 4:
                if (!K3) {
                    xmlChar* val = xmlStrdup((const xmlChar*)v.c_str());
                    int r = xmlHashAdd2(h, K1, K2, val);
                    if (r == 1) model[key] = v;
                    else xmlFree(val);
                    stitch::check(r == (had ? 0 : 1), "xmlHashAdd2 adds only new keys");
                    if (r == 0) {
                        xmlHashUpdateEntry2(h, K1, K2, xmlStrdup((const xmlChar*)v.c_str()), xmlHashDefaultDeallocator);
                        model[key] = v;
                    }
                }
                break;
            default:
                if (!K2 && !K3) {
                    xmlChar* val = xmlStrdup((const xmlChar*)v.c_str());
                    int r = xmlHashAdd(h, K1, val);
                    stitch::check(r == (had ? 0 : 1), "xmlHashAdd adds only new keys");
                    if (r == 1) model[key] = v;
                    else xmlFree(val);
                }
                break;
        }
    }
    for (auto& e : model) {
        const xmlChar *K1 = (const xmlChar*)std::get<0>(e.first).c_str();
        const xmlChar* K2 = std::get<1>(e.first).empty() ? nullptr : (const xmlChar*)std::get<1>(e.first).c_str();
        const xmlChar* K3 = std::get<2>(e.first).empty() ? nullptr : (const xmlChar*)std::get<2>(e.first).c_str();
        stitch::check(lx::str((const xmlChar*)xmlHashLookup3(h, K1, K2, K3)) == e.second, "Lookup3 finds the value");
        if (!K3) stitch::check(xmlHashLookup2(h, K1, K2) == xmlHashLookup3(h, K1, K2, nullptr), "Lookup2 == Lookup3 with no third key");
        if (!K2 && !K3) stitch::check(xmlHashLookup(h, K1) == xmlHashLookup3(h, K1, nullptr, nullptr), "Lookup == Lookup3");
        // QLookup splits nothing when the prefix is NULL
        stitch::check(xmlHashQLookup3(h, nullptr, K1, nullptr, K2, nullptr, K3) == xmlHashLookup3(h, K1, K2, K3), "QLookup3 without prefixes");
        if (!K3) stitch::check(xmlHashQLookup2(h, nullptr, K1, nullptr, K2) == xmlHashLookup2(h, K1, K2), "QLookup2 without prefixes");
        if (!K2 && !K3) stitch::check(xmlHashQLookup(h, nullptr, K1) == xmlHashLookup(h, K1), "QLookup without prefix");
    }
    stitch::check(xmlHashSize(h) == (int)model.size(), "size");
    int n = 0, m = 0, f = 0;
    xmlHashScan3(h, nullptr, nullptr, nullptr, count_scan, &n);
    xmlHashScanFull(h, count_scan_full, &m);
    xmlHashScanFull3(h, (const xmlChar*)"a", nullptr, nullptr, count_scan_full, &f);
    int fa = 0;
    for (auto& e : model) fa += std::get<0>(e.first) == "a";
    stitch::check(n == (int)model.size() && m == (int)model.size(), "scans visit every entry");
    stitch::check(f == fa, "ScanFull3 with a first key visits the matching entries");
    xmlHashTablePtr c = xmlHashCopySafe(h, copy_str, xmlHashDefaultDeallocator);
    stitch::check(c && xmlHashSize(c) == xmlHashSize(h), "CopySafe copies everything");
    xmlHashFree(c, xmlHashDefaultDeallocator);
    xmlHashFree(h, xmlHashDefaultDeallocator);
    if (dict) xmlDictFree(dict);
}
STITCH_ENDPOINT(hash3_model);

namespace {
int cmp_int(const void* a, const void* b) {
    intptr_t x = (intptr_t)a, y = (intptr_t)b;
    return (x > y) - (x < y);
}
int collect(const void* data, void* user) {
    ((std::vector<intptr_t>*)user)->push_back((intptr_t)data);
    return 1;
}
std::vector<intptr_t> items(xmlListPtr l) {
    std::vector<intptr_t> v;
    xmlListWalk(l, collect, &v);
    return v;
}
}  // namespace

// The rest of the list API against a std::vector model. The list is kept sorted: xmlListInsert
// inserts in order, and the search / remove functions assume an ordered list.
void list_model2(Src<96> s) {
    xmlListPtr l = xmlListCreate(nullptr, cmp_int);
    std::vector<intptr_t> model;
    while (s->more() && model.size() < 64) {
        intptr_t v = (intptr_t)s->range(1, 12);
        switch (s->choice(10)) {
            case 0: xmlListInsert(l, (void*)v); model.insert(std::lower_bound(model.begin(), model.end(), v), v); break;
            case 1:
                if (model.empty() || v >= model.back()) { xmlListPushBack(l, (void*)v); model.push_back(v); }
                else if (v <= model.front()) { xmlListPushFront(l, (void*)v); model.insert(model.begin(), v); }
                break;
            case 2: {
                int r = xmlListRemoveLast(l, (void*)v);
                auto it = std::find(model.rbegin(), model.rend(), v);
                stitch::check((r == 1) == (it != model.rend()), "RemoveLast");
                if (it != model.rend()) model.erase(std::next(it).base());
                break;
            }
            case 3: {
                int r = xmlListRemoveAll(l, (void*)v);
                int c = (int)std::count(model.begin(), model.end(), v);
                stitch::check(r == c, "RemoveAll returns the count");
                model.erase(std::remove(model.begin(), model.end(), v), model.end());
                break;
            }
            case 4: {
                bool in = std::count(model.begin(), model.end(), v) > 0;
                stitch::check((intptr_t)xmlListSearch(l, (void*)v) == (in ? v : 0), "Search");
                stitch::check((intptr_t)xmlListReverseSearch(l, (void*)v) == (in ? v : 0), "ReverseSearch");
                break;
            }
            case 5: if (!model.empty()) { xmlListPopBack(l); model.pop_back(); } break;
            case 6: if (!model.empty()) { xmlListPopFront(l); model.erase(model.begin()); } break;
            case 7: {
                xmlListPtr d = xmlListDup(l);
                stitch::check(d && items(d) == model, "Dup copies the list");
                xmlListPtr e = xmlListCreate(nullptr, cmp_int);
                stitch::check(xmlListCopy(e, l) == 0 && items(e) == model, "Copy inserts every element");
                xmlListMerge(d, e);  // inserts e's elements into d, empties e
                std::vector<intptr_t> both = model;
                both.insert(both.end(), model.begin(), model.end());
                std::sort(both.begin(), both.end());
                stitch::check(xmlListEmpty(e) == 1 && items(d) == both, "Merge");
                xmlListDelete(d);
                xmlListDelete(e);
                break;
            }
            case 8: {
                std::vector<intptr_t> back;
                xmlListReverseWalk(l, collect, &back);
                stitch::check(std::vector<intptr_t>(back.rbegin(), back.rend()) == model, "ReverseWalk");
                break;
            }
            default: xmlListClear(l); model.clear(); break;
        }
        stitch::check(items(l) == model, "list matches the model");
        stitch::check(xmlListEmpty(l) == (model.empty() ? 1 : 0), "Empty");
        if (!model.empty()) {
            stitch::check((intptr_t)xmlLinkGetData(xmlListFront(l)) == model.front(), "Front");
            stitch::check((intptr_t)xmlLinkGetData(xmlListEnd(l)) == model.back(), "End");
        }
    }
    xmlListDelete(l);
}
STITCH_ENDPOINT(list_model2);

// Dictionary limits and reference counting.
void dict_limits(Range<0, 64> limit, Src<32> s) {
    xmlDictPtr d = xmlDictCreate();
    xmlDictSetLimit(d, (size_t)(int)limit);
    stitch::check(xmlDictReference(d) == 0, "Reference");
    size_t used = 0;
    while (s->more()) {
        std::string k = s->str(12);
        const xmlChar* p = xmlDictLookup(d, (const xmlChar*)k.c_str(), -1);
        if (p) stitch::check(xmlStrEqual(p, (const xmlChar*)k.c_str()), "lookup returns the string");
        size_t u = xmlDictGetUsage(d);
        stitch::check(u >= used, "usage does not shrink");
        used = u;
    }
    xmlDictFree(d);  // the extra reference
    xmlDictFree(d);
}
STITCH_ENDPOINT(dict_limits);

// Encoding handlers by enum and by name, ISO-8859-1 <-> UTF-8 conversions.
void encoding_api(Bytes<128> data, Range<1, 22> enc, OneOf<0, 1, 2, 3> name) {
    static const char* names[] = {"ISO-8859-1", "UTF-16", "ASCII", "EUC-JP"};
    xmlCharEncodingHandlerPtr h = nullptr;
    if (xmlLookupCharEncodingHandler((xmlCharEncoding)(int)enc, &h) == XML_ERR_OK) xmlCharEncCloseFunc(h);
    xmlCharEncodingHandlerPtr g = xmlGetCharEncodingHandler((xmlCharEncoding)(int)enc);
    if (g) xmlCharEncCloseFunc(g);
    for (int out = 0; out < 2; out++) {
        xmlCharEncodingHandlerPtr o = nullptr;
        if (xmlOpenCharEncodingHandler(names[(int)name], out, &o) == XML_ERR_OK && o) xmlCharEncCloseFunc(o);
    }
    xmlCharEncodingHandlerPtr c = nullptr;
    if (xmlCreateCharEncodingHandler(names[(int)name], (xmlCharEncFlags)(XML_ENC_INPUT | XML_ENC_OUTPUT), nullptr, nullptr, &c) == XML_ERR_OK && c)
        xmlCharEncCloseFunc(c);
    // Latin-1 -> UTF-8 -> Latin-1 gives the bytes back
    std::vector<unsigned char> utf8(data.size() * 2 + 1), back(data.size() + 1);
    int outlen = (int)utf8.size(), inlen = (int)data.size();
    int r = xmlIsolat1ToUTF8(utf8.data(), &outlen, (const unsigned char*)data.data(), &inlen);
    stitch::require(r >= 0, "conversion failed");
    stitch::check(inlen == (int)data.size(), "all Latin-1 input converted");
    int blen = (int)back.size(), ilen = outlen;
    r = xmlUTF8ToIsolat1(back.data(), &blen, utf8.data(), &ilen);
    stitch::check(r >= 0 && blen == (int)data.size() && !memcmp(back.data(), data.data(), data.size()), "UTF-8 -> Latin-1 round trip");
}
STITCH_ENDPOINT(encoding_api);

static int vprint(xmlChar* buf, int len, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = xmlStrVPrintf(buf, len, fmt, ap);
    va_end(ap);
    return r;
}

// String helpers and the *Safe URI functions (same results as the plain ones).
void strings_uri_safe(Str<32> a, Str<32> b, Range<-1, 40> n, Str<64, UriText> uri, Str<64, UriText> base) {
    int k = std::min((int)n, (int)strlen(b.c_str()));  // (len is the length of `add`: at most its size)
    xmlChar* cat = xmlStrncat(xmlStrdup((const xmlChar*)a.c_str()), (const xmlChar*)b.c_str(), k);
    std::string want = std::string(a.c_str()) + std::string(b.c_str()).substr(0, k < 0 ? std::string::npos : (size_t)k);
    if (k >= 0) stitch::check(lx::str(cat) == want, "xmlStrncat");
    xmlFree(cat);
    xmlChar buf[64];
    int len = vprint(buf, sizeof buf, "%s|%d", a.c_str(), (int)n);
    stitch::check(len >= 0 && len < (int)sizeof buf && xmlStrlen(buf) == len, "xmlStrVPrintf");
    std::string ca = lx::clean(a.s), cb = lx::clean(b.s);
    if (!ca.empty() && !cb.empty()) {
        int cmp = xmlUTF8Charcmp((const xmlChar*)ca.c_str(), (const xmlChar*)cb.c_str());
        int l1 = xmlUTF8Size((const xmlChar*)ca.c_str()), l2 = xmlUTF8Size((const xmlChar*)cb.c_str());
        if (l1 == l2) stitch::check((cmp == 0) == (ca.compare(0, (size_t)l1, cb, 0, (size_t)l2) == 0), "xmlUTF8Charcmp");
    }
    stitch::check(xmlCharInRange(0x100, &xmlIsBaseCharGroup) == 1, "U+0100 is a base char");
    stitch::check(xmlCharInRange((unsigned)(unsigned char)a.c_str()[0] | 0x3000u, &xmlIsBaseCharGroup) == xmlIsBaseChar((unsigned)(unsigned char)a.c_str()[0] | 0x3000u),
                  "xmlCharInRange == xmlIsBaseChar");
    const xmlChar* U = (const xmlChar*)uri.c_str();
    const xmlChar* B = (const xmlChar*)base.c_str();
    xmlChar* s1 = nullptr;
    int r1 = xmlBuildURISafe(U, B, &s1);
    xmlChar* p1 = xmlBuildURI(U, B);
    stitch::check(r1 != 0 || xmlStrEqual(s1, p1), "xmlBuildURISafe == xmlBuildURI");
    xmlFree(s1);
    xmlFree(p1);
    xmlChar* s2 = nullptr;
    int r2 = xmlBuildRelativeURISafe(U, B, &s2);
    xmlChar* p2 = xmlBuildRelativeURI(U, B);
    stitch::check(r2 != 0 || xmlStrEqual(s2, p2), "xmlBuildRelativeURISafe == xmlBuildRelativeURI");
    xmlFree(s2);
    xmlFree(p2);
    xmlURIPtr u = nullptr;
    int r3 = xmlParseURISafe(uri.c_str(), &u);
    xmlURIPtr v = xmlParseURI(uri.c_str());
    stitch::check((r3 == 0) == (v != nullptr), "xmlParseURISafe accepts what xmlParseURI does");
    xmlFreeURI(u);
    xmlFreeURI(v);
}
STITCH_ENDPOINT(strings_uri_safe);

// DTD declarations, entities and IDs added through the API are found again, and a document
// using them validates.
void dtd_api(Src<64> s, Bool internal) {
    lx::silence();
    xmlDocPtr d = xmlNewDoc((const xmlChar*)"1.0");
    xmlDtdPtr dtd = internal ? xmlCreateIntSubset(d, (const xmlChar*)"root", nullptr, nullptr) : xmlNewDtd(d, (const xmlChar*)"root", nullptr, nullptr);
    stitch::require(dtd, "no DTD");
    xmlValidCtxtPtr vc = xmlNewValidCtxt();
    vc->error = nullptr;
    vc->warning = nullptr;
    xmlElementContentPtr any = xmlNewDocElementContent(d, (const xmlChar*)"item", XML_ELEMENT_CONTENT_ELEMENT);
    any->ocur = XML_ELEMENT_CONTENT_MULT;
    xmlElementPtr el = xmlAddElementDecl(vc, dtd, (const xmlChar*)"root", XML_ELEMENT_TYPE_ELEMENT, any);
    xmlFreeDocElementContent(d, any);
    stitch::check(el && xmlGetDtdElementDesc(dtd, (const xmlChar*)"root") == el, "element declaration found");
    xmlElementPtr it = xmlAddElementDecl(vc, dtd, (const xmlChar*)"item", XML_ELEMENT_TYPE_ANY, nullptr);
    stitch::check(it != nullptr, "ANY element declared");
    xmlEnumerationPtr en = xmlCreateEnumeration((const xmlChar*)"x");
    en->next = xmlCreateEnumeration((const xmlChar*)"y");
    xmlAttributePtr at = xmlAddAttributeDecl(vc, dtd, (const xmlChar*)"item", (const xmlChar*)"k", nullptr, XML_ATTRIBUTE_ENUMERATION,
                                             XML_ATTRIBUTE_IMPLIED, nullptr, en);
    xmlFreeEnumeration(xmlCreateEnumeration((const xmlChar*)"unused"));  // (the declaration owns `en`)
    xmlAttributePtr idd = xmlAddAttributeDecl(vc, dtd, (const xmlChar*)"item", (const xmlChar*)"id", nullptr, XML_ATTRIBUTE_ID,
                                              XML_ATTRIBUTE_IMPLIED, nullptr, nullptr);
    stitch::check(at && idd && xmlGetDtdAttrDesc(dtd, (const xmlChar*)"item", (const xmlChar*)"k") == at, "attribute declaration found");
    xmlNotationPtr no = xmlAddNotationDecl(vc, dtd, (const xmlChar*)"gif", (const xmlChar*)"-//GIF//EN", nullptr);
    stitch::check(no && xmlGetDtdNotationDesc(dtd, (const xmlChar*)"gif") == no, "notation found");
    xmlBufferPtr b = xmlBufferCreate();
    xmlDumpNotationTable(b, (xmlNotationTablePtr)dtd->notations);
    stitch::check(xmlBufferLength(b) > 0, "notation table dumped");
    xmlBufferFree(b);
    // entities: general and parameter, in the subset the DTD is
    std::string val = lx::clean(s->str(16));
    for (char& c : val) if (c == '&' || c == '%' || c == '<') c = '_';
    xmlEntityPtr ge = internal ? xmlAddDocEntity(d, (const xmlChar*)"g", XML_INTERNAL_GENERAL_ENTITY, nullptr, nullptr, (const xmlChar*)val.c_str())
                               : xmlAddDtdEntity(d, (const xmlChar*)"g", XML_INTERNAL_GENERAL_ENTITY, nullptr, nullptr, (const xmlChar*)val.c_str());
    stitch::check(ge != nullptr, "entity added");
    stitch::check(xmlGetDocEntity(d, (const xmlChar*)"g") == ge, "GetDocEntity finds it");
    if (!internal) stitch::check(xmlGetDtdEntity(d, (const xmlChar*)"g") == ge, "GetDtdEntity finds it");
    int r = xmlAddEntity(d, internal ? 0 : 1, (const xmlChar*)"pe", XML_INTERNAL_PARAMETER_ENTITY, nullptr, nullptr, (const xmlChar*)"x", nullptr);
    stitch::check(r == 0 && xmlGetParameterEntity(d, (const xmlChar*)"pe") != nullptr, "parameter entity added");
    // without a document, xmlNewEntity returns an unlinked entity the caller frees
    xmlEntityPtr loose = xmlNewEntity(nullptr, (const xmlChar*)"loose", XML_INTERNAL_GENERAL_ENTITY, nullptr, nullptr, (const xmlChar*)"v");
    stitch::check(loose && xmlStrEqual(loose->name, (const xmlChar*)"loose"), "xmlNewEntity");
    xmlFreeEntity(loose);
    // a document that uses the declarations
    xmlNodePtr root = xmlNewDocNode(d, nullptr, (const xmlChar*)"root", nullptr);
    xmlDocSetRootElement(d, root);
    std::vector<xmlAttrPtr> ids;
    while (s->more() && ids.size() < 8) {
        xmlNodePtr item = xmlNewChild(root, nullptr, (const xmlChar*)"item", nullptr);
        if (s->coin()) xmlNewProp(item, (const xmlChar*)"k", (const xmlChar*)s->pick({"x", "y"}));
        std::string id = "i" + std::to_string(ids.size());
        xmlAttrPtr a = xmlNewProp(item, (const xmlChar*)"id", (const xmlChar*)id.c_str());
        // (xmlNewProp already registered the declared ID attribute)
        stitch::check(xmlGetID(d, (const xmlChar*)id.c_str()) == a && xmlIsID(d, item, a) == 1, "the ID is found");
        if (s->coin()) stitch::check(xmlAddIDSafe(a, (const xmlChar*)id.c_str()) == 0, "AddIDSafe: the ID exists");
        else stitch::check(xmlAddID(vc, d, (const xmlChar*)id.c_str(), a) == nullptr, "AddID refuses an existing ID");
        ids.push_back(a);
    }
    if (!ids.empty() && s->coin()) {
        xmlAttrPtr a = ids.back();
        xmlChar* id = xmlNodeListGetString(d, a->children, 1);
        stitch::check(xmlRemoveID(d, a) == 0 && xmlGetID(d, id) == nullptr, "RemoveID");
        xmlFree(id);
    }
    int v = xmlValidateElement(vc, d, root);
    stitch::check(v == 1, "a document built from the declarations is valid");
    xmlFreeValidCtxt(vc);
    xmlFreeDoc(d);
}
STITCH_ENDPOINT(dtd_api);
