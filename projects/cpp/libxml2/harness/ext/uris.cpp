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

void uri_escape_unescape(Str<64, UriText> text, Str<4> keep) {
    xmlChar* e = xmlURIEscapeStr((const xmlChar*)text.c_str(), (const xmlChar*)keep.c_str());
    stitch::require(e, "no result");
    char* u = xmlURIUnescapeString((const char*)e, 0, nullptr);
    stitch::check(u && std::string(u) == text.c_str(), "xmlURIUnescapeString(xmlURIEscapeStr(s)) == s");
    xmlFree(u); xmlFree(e);
    xmlChar* e2 = xmlURIEscape((const xmlChar*)text.c_str()); xmlFree(e2);
}
STITCH_ENDPOINT(uri_escape_unescape);

void uri_normalize_path(Str<64, UriText> text) {
    std::string p = text.c_str();
    std::vector<char> buf(p.begin(), p.end()); buf.push_back(0);
    xmlNormalizeURIPath(buf.data());
    std::string once = buf.data();
    std::vector<char> buf2(once.begin(), once.end()); buf2.push_back(0);
    xmlNormalizeURIPath(buf2.data());
    stitch::check(once == buf2.data(), "xmlNormalizeURIPath is idempotent (" + once + ")");
}
STITCH_ENDPOINT(uri_normalize_path);

void uri_canonic_path(Str<64, UriText> text) {
    xmlChar* c = xmlCanonicPath((const xmlChar*)text.c_str()); xmlFree(c);
    xmlChar* p = xmlPathToURI((const xmlChar*)text.c_str()); xmlFree(p);
}
STITCH_ENDPOINT(uri_canonic_path);

void uri_relative(Str<64, UriText> uri, Str<64, UriText> base) {
    xmlChar* rel = xmlBuildRelativeURI((const xmlChar*)uri.c_str(), (const xmlChar*)base.c_str());
    if (rel) {
        xmlChar* abs = xmlBuildURI(rel, (const xmlChar*)base.c_str());
        xmlChar* abs2 = xmlBuildURI((const xmlChar*)uri.c_str(), (const xmlChar*)base.c_str());
        xmlURIPtr bu = xmlParseURI(base.c_str());
        xmlURIPtr uu = xmlParseURI(uri.c_str());
        if (abs && abs2 && bu && uu && bu->scheme && uu->scheme && !strcmp(bu->scheme, uu->scheme) && !strchr(uri.c_str(), '%') && !strchr(base.c_str(), '%'))
            stitch::check(xmlStrEqual(abs, abs2), std::string("resolving the relative URI gives the URI back: ") + (char*)rel);
        xmlFreeURI(bu); xmlFreeURI(uu); xmlFree(abs); xmlFree(abs2);
    }
    xmlFree(rel);
}
STITCH_ENDPOINT(uri_relative);

void uri_fields(Str<128, UriText> text) {
    xmlURIPtr u = xmlCreateURI();
    int r = xmlParseURIReference(u, text.c_str());
    if (r == 0) {
        xmlChar* s = xmlSaveUri(u); xmlFree(s);
        FILE* f = fopen("/dev/null", "w"); xmlPrintURI(f, u); fclose(f);
    }
    xmlFreeURI(u);
    xmlURIPtr raw = xmlParseURIRaw(text.c_str(), 1); xmlFreeURI(raw);
}
STITCH_ENDPOINT(uri_fields);
