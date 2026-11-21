#include "../objects.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <vector>
#include <libxml/xmlwriter.h>
#include <libxml/xmlschemas.h>
#include <libxml/xmlschemastypes.h>
#include <libxml/relaxng.h>
#include <libxml/schematron.h>
#include <libxml/pattern.h>
#include <libxml/debugXML.h>
#include <libxml/list.h>
#include <libxml/hash.h>
#include <libxml/HTMLparser.h>
#include <libxml/entities.h>
#include <strings.h>

void str_dup_sub(Str<64> a, Range<0, 70> i, Range<0, 70> n) {
    xmlChar* d = xmlStrdup((const xmlChar*)a.c_str());
    stitch::check(d && std::string((char*)d) == a.c_str(), "xmlStrdup copies");
    int len = xmlStrlen(d);
    stitch::check(len == (int)strlen(a.c_str()), "xmlStrlen");
    
    stitch::require(i.v + n.v <= len, "range beyond the string");  // (xmlStrsub / xmlStrndup read len bytes)
    xmlChar* s = xmlStrsub(d, (int)i.v, (int)n.v);
    if (i.v + n.v <= len) stitch::check(s && std::string((char*)s) == std::string(a.c_str()).substr(i.v, n.v), "xmlStrsub == substr");
    xmlChar* nd = xmlStrndup(d, (int)n.v);
    stitch::check(nd && std::string((char*)nd) == std::string(a.c_str()).substr(0, n.v), "xmlStrndup");
    xmlFree(s); xmlFree(nd); xmlFree(d);
}
STITCH_ENDPOINT(str_dup_sub);

void str_compare(Str<32> a, Str<32> b, Range<0, 40> n) {
    const xmlChar *x = (const xmlChar*)a.c_str(), *y = (const xmlChar*)b.c_str();
    auto sg = [](int v) { return (v > 0) - (v < 0); };
    stitch::check(sg(xmlStrcmp(x, y)) == sg(strcmp(a.c_str(), b.c_str())), "xmlStrcmp == strcmp");
    stitch::check(sg(xmlStrncmp(x, y, (int)n.v)) == sg(strncmp(a.c_str(), b.c_str(), n.v)), "xmlStrncmp == strncmp");
    stitch::check(xmlStrEqual(x, y) == (strcmp(a.c_str(), b.c_str()) == 0), "xmlStrEqual");
    stitch::check((xmlStrcasecmp(x, y) == 0) == (strcasecmp(a.c_str(), b.c_str()) == 0), "xmlStrcasecmp == strcasecmp (zero)");
    stitch::check((xmlStrncasecmp(x, y, (int)n.v) == 0) == (strncasecmp(a.c_str(), b.c_str(), n.v) == 0), "xmlStrncasecmp");
}
STITCH_ENDPOINT(str_compare);

void str_search(Str<48> a, Str<8> b) {
    const xmlChar* f = xmlStrstr((const xmlChar*)a.c_str(), (const xmlChar*)b.c_str());
    const char* r = strstr(a.c_str(), b.c_str());
    stitch::check((f == nullptr) == (r == nullptr || !*b.c_str()) || !*b.c_str(), "xmlStrstr finds like strstr");
    if (f && r && *b.c_str()) stitch::check((const char*)f == r, "xmlStrstr position");
    const xmlChar* c = xmlStrcasestr((const xmlChar*)a.c_str(), (const xmlChar*)b.c_str());
    const char* rc = strcasestr(a.c_str(), b.c_str());
    if (*b.c_str()) stitch::check((c == nullptr) == (rc == nullptr), "xmlStrcasestr finds like strcasestr");
    if (*b.c_str()) stitch::check((xmlStrchr((const xmlChar*)a.c_str(), b.c_str()[0]) == nullptr) == (strchr(a.c_str(), b.c_str()[0]) == nullptr), "xmlStrchr");
}
STITCH_ENDPOINT(str_search);

void str_cat(Str<32> a, Str<32> b, Range<-1, 40> n) {
    xmlChar* d = xmlStrdup((const xmlChar*)a.c_str());
    d = xmlStrcat(d, (const xmlChar*)b.c_str());
    stitch::check(d && std::string((char*)d) == std::string(a.c_str()) + b.c_str(), "xmlStrcat");
    stitch::require(n.v <= (long long)strlen(b.c_str()), "length beyond the string");  // (xmlStrncatNew reads len bytes)

    stitch::require(n.v <= (long long)strlen(b.c_str()), "length beyond the string");  // (xmlStrncatNew reads len bytes)
    xmlChar* e = xmlStrncatNew((const xmlChar*)a.c_str(), (const xmlChar*)b.c_str(), (int)n.v);
    std::string want = std::string(a.c_str()) + (n.v < 0 ? std::string(b.c_str()) : std::string(b.c_str()).substr(0, n.v));
    stitch::check(e && want == (char*)e, "xmlStrncatNew");
    xmlFree(d); xmlFree(e);
}
STITCH_ENDPOINT(str_cat);

void str_qequal(Str<16> pre, Str<16> name, Str<32> str) {
    std::string q = std::string(pre.c_str()) + ":" + name.c_str();
    int r = xmlStrQEqual((const xmlChar*)pre.c_str(), (const xmlChar*)name.c_str(), (const xmlChar*)str.c_str());
    stitch::check((r != 0) == (*pre.c_str() ? q == str.c_str() : std::string(name.c_str()) == str.c_str()), "xmlStrQEqual");
    
    xmlChar* b = xmlBuildQName((const xmlChar*)name.c_str(), *pre.c_str() ? (const xmlChar*)pre.c_str() : nullptr, nullptr, 0);
    if (b) { stitch::check(std::string((char*)b) == (*pre.c_str() ? q : std::string(name.c_str())), "xmlBuildQName"); if (b != (const xmlChar*)name.c_str()) xmlFree(b); }
}
STITCH_ENDPOINT(str_qequal);

void split_qname(Str<32> q) {
    xmlChar* prefix = nullptr;
    xmlChar* local = xmlSplitQName2((const xmlChar*)q.c_str(), &prefix);
    if (local) {
        stitch::check(prefix != nullptr, "a split name has a prefix");
        stitch::check(std::string((char*)prefix) + ":" + (char*)local == q.c_str(), "prefix:local == the name");
    }
    xmlFree(local); xmlFree(prefix);
    int len = 0;
    const xmlChar* l3 = xmlSplitQName3((const xmlChar*)q.c_str(), &len);
    if (l3) stitch::check(len > 0 && q.c_str()[len] == ':' && (const char*)l3 == q.c_str() + len + 1, "xmlSplitQName3");
}
STITCH_ENDPOINT(split_qname);

void utf8_ops(Str<64> a, Range<0, 20> i, Range<0, 20> n) {
    const xmlChar* s = (const xmlChar*)a.c_str();
    std::string t = a.c_str();
    bool ok = xmlCheckUTF8(s);
    size_t pos = 0; int chars = 0; bool strict = true;
    while (pos < t.size()) { int l = 0; if (lx::utf8_next((const unsigned char*)t.data() + pos, t.size() - pos, &l) < 0) { strict = false; break; } pos += l; chars++; }
    stitch::require(ok && strict, "not valid UTF-8");
    stitch::check(xmlUTF8Strlen(s) == chars, "xmlUTF8Strlen counts characters");
    stitch::check(xmlUTF8Strsize(s, chars) == (int)t.size(), "xmlUTF8Strsize of every char is the byte length");
    if (i.v < chars) {
        const xmlChar* p = xmlUTF8Strpos(s, (int)i.v);
        
        // (xmlUTF8Strloc finds the first occurrence of that character)
        int loc = xmlUTF8Strloc(s, p);
        stitch::check(p != nullptr && loc >= 0 && loc <= (int)i.v && xmlUTF8Strsize(xmlUTF8Strpos(s, loc), 1) == xmlUTF8Strsize(p, 1) && !memcmp(xmlUTF8Strpos(s, loc), p, xmlUTF8Strsize(p, 1)), "xmlUTF8Strloc finds the first occurrence of the character at Strpos(i)");
        xmlChar* sub = xmlUTF8Strsub(s, (int)i.v, (int)n.v);
        if (sub) { stitch::check(xmlUTF8Strlen(sub) == (int)std::min<long long>(n.v, chars - i.v), "xmlUTF8Strsub length"); xmlFree(sub); }
        stitch::check(xmlUTF8Size(p) >= 1 && xmlUTF8Size(p) <= 4, "xmlUTF8Size");
    }
    xmlChar* nd = xmlUTF8Strndup(s, (int)n.v);
    if (nd) { stitch::check(xmlUTF8Strlen(nd) == (int)std::min<long long>(n.v, chars), "xmlUTF8Strndup"); xmlFree(nd); }
}
STITCH_ENDPOINT(utf8_ops);

void utf8_strpos_garbage(Bytes<32> b, Range<-2, 40> i) {
    std::string t((const char*)b.data(), b.size());
    const xmlChar* s = (const xmlChar*)t.c_str();
    xmlUTF8Strpos(s, (int)i.v);
    xmlUTF8Strlen(s);
    xmlUTF8Strsize(s, (int)i.v);
    xmlChar* x = xmlUTF8Strndup(s, (int)i.v); xmlFree(x);
    xmlChar* y = xmlUTF8Strsub(s, 0, (int)i.v); xmlFree(y);
    int len = (int)t.size(); xmlGetUTF8Char(s, &len);
}
STITCH_ENDPOINT(utf8_strpos_garbage);

void str_printf(Str<16> a, Range<-1000, 1000> n) {
    xmlChar buf[64];
    int r = xmlStrPrintf(buf, sizeof buf, "%s/%d", a.c_str(), (int)n.v);
    char ref[64]; snprintf(ref, sizeof ref, "%s/%d", a.c_str(), (int)n.v);
    stitch::check(r >= 0 && std::string((char*)buf) == ref, "xmlStrPrintf == snprintf");
}
STITCH_ENDPOINT(str_printf);

void char_strndup(Str<32> a, Range<0, 40> n) {
    xmlChar* d = xmlCharStrndup(a.c_str(), (int)std::min<long long>(n.v, strlen(a.c_str())));
    stitch::check(d && std::string((char*)d) == std::string(a.c_str()).substr(0, n.v), "xmlCharStrndup");
    xmlFree(d);
    xmlChar* e = xmlCharStrdup(a.c_str()); stitch::check(e && std::string((char*)e) == a.c_str(), "xmlCharStrdup"); xmlFree(e);
}
STITCH_ENDPOINT(char_strndup);
