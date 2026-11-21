#include "../objects.hpp"

// A parsed URI saves to text that parses to the same URI again.
void uri(Str<256, UriText> text) {
    lx::silence();
    std::string t = text.c_str();
    for (auto& ch : t) ch = (char)tolower((unsigned char)ch);
    stitch::require(t.find("%00") == std::string::npos, "escaped NUL (C strings truncate)");
    xmlURIPtr u = xmlParseURI(text.c_str());
    stitch::require(u, "invalid URI");
    xmlChar* s = xmlSaveUri(u);
    xmlFreeURI(u);
    stitch::require(s, "save failed");
    xmlURIPtr again = xmlParseURI((const char*)s);
    stitch::log("saved: %s", (const char*)s);
    stitch::check(again != nullptr, "a saved URI parses");
    xmlChar* s2 = xmlSaveUri(again);
    stitch::check(s2 && !xmlStrcmp(s, s2), "xmlSaveUri(xmlParseURI(saved)) == saved");
    xmlFree(s2);
    xmlFreeURI(again);
    xmlFree(s);
}
STITCH_ENDPOINT(uri);

void build_uri(Str<128, UriText> rel, Str<128, UriText> base) {
    xmlChar* r = xmlBuildURI((const xmlChar*)rel.c_str(), (const xmlChar*)base.c_str());
    xmlFree(r);
    xmlChar* r2 = xmlBuildRelativeURI((const xmlChar*)rel.c_str(), (const xmlChar*)base.c_str());
    xmlFree(r2);
    xmlChar* e = xmlURIEscapeStr((const xmlChar*)rel.c_str(), (const xmlChar*)":/");
    if (e) {
        char* un = xmlURIUnescapeString((const char*)e, 0, nullptr);
        stitch::check(un && std::string(rel.c_str()) == un, "unescape(escape(s)) == s");
        xmlFree(un);
    }
    xmlFree(e);
}
STITCH_ENDPOINT(build_uri);

// XML Schema regular expressions (the automata in xmlregexp.c)
void regexp(Str<64, RegexText> pattern, Str<64> input) {
    xmlRegexpPtr re = xmlRegexpCompile((const xmlChar*)pattern.c_str());
    stitch::require(re, "invalid regexp");
    xmlRegexpExec(re, (const xmlChar*)input.c_str());
    xmlRegexpIsDeterminist(re);
    xmlRegFreeRegexp(re);
}
STITCH_ENDPOINT(regexp);

// libxml2's UTF-8 helpers agree with a strict decoder.
void utf8(Bytes<16> b) {
    std::string s((const char*)b.data(), b.size());
    s = s.c_str();  // (C string APIs)
    const unsigned char* p = (const unsigned char*)s.c_str();
    size_t n = s.size();
    bool valid = true;
    int chars = 0;
    for (size_t i = 0; i < n;) {
        int len = 0;
        if (lx::utf8_next(p + i, n - i, &len) < 0) { valid = false; break; }
        i += (size_t)len;
        chars++;
    }
    int len = (int)n;
    int c = n ? xmlGetUTF8Char(p, &len) : 0;
    int ref_len = 0;
    int ref = n ? lx::utf8_next(p, n, &ref_len) : 0;
    stitch::log("first: libxml2 %d (len %d), strict %d (len %d); valid %d", c, len, ref, ref_len, valid);
    stitch::check((c < 0) == (ref < 0), "xmlGetUTF8Char accepts exactly the valid UTF-8 sequences");
    if (c >= 0) stitch::check(c == ref && len == ref_len, "xmlGetUTF8Char decodes like a strict decoder");
    stitch::check((xmlCheckUTF8(p) != 0) == valid, "xmlCheckUTF8 accepts exactly valid UTF-8");
    if (valid) stitch::check(xmlUTF8Strlen(p) == chars, "xmlUTF8Strlen counts characters");
}
STITCH_ENDPOINT(utf8);
