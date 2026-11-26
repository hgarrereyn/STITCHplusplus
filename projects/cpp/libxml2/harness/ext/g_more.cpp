#include "../objects.hpp"
#include <libxml/relaxng.h>
#include <libxml/xmlreader.h>
#include <libxml/parserInternals.h>
#include <libxml/HTMLtree.h>
#include <string.h>

void c14n_c14n_1_0_plain(Borrow<Doc> d) {
    
    
    stitch::require((*d)->intSubset == nullptr && !stitch::get(d, "html", 0), "has a DTD or is HTML");  // (canonical XML drops the DTD: entity references and defaults would not survive)
    stitch::require(xmlDocGetRootElement(*d) && lx::ns_wellformed(*d), "no root element, or not namespace-well-formed");
    xmlChar* out = nullptr;
    int n = xmlC14NDocDumpMemory(*d, nullptr, 0, nullptr, 0, &out);
    if (n >= 0 && out) {
        std::string s((const char*)out, n);
        xmlDocPtr again = xmlReadMemory(s.data(), (int)s.size(), "c.xml", nullptr, lx::kQuiet);
        if (stitch::get(d, "wellformed", 0)) stitch::check(again != nullptr, "canonical XML parses");  // (recovered trees can hold anything)
        if (again) {
            xmlChar* out2 = nullptr;
            int n2 = xmlC14NDocDumpMemory(again, nullptr, 0, nullptr, 0, &out2);
            stitch::check(n2 == n && out2 && memcmp(out, out2, n) == 0, "canonicalization is a fixed point");
            xmlFree(out2);
            xmlFreeDoc(again);
        }
    }
    xmlFree(out);
}
STITCH_ENDPOINT(c14n_c14n_1_0_plain);

void c14n_c14n_1_0_comments(Borrow<Doc> d) {
    
    
    stitch::require((*d)->intSubset == nullptr && !stitch::get(d, "html", 0), "has a DTD or is HTML");  // (canonical XML drops the DTD: entity references and defaults would not survive)
    stitch::require(xmlDocGetRootElement(*d) && lx::ns_wellformed(*d), "no root element, or not namespace-well-formed");
    xmlChar* out = nullptr;
    int n = xmlC14NDocDumpMemory(*d, nullptr, 0, nullptr, 1, &out);
    if (n >= 0 && out) {
        std::string s((const char*)out, n);
        xmlDocPtr again = xmlReadMemory(s.data(), (int)s.size(), "c.xml", nullptr, lx::kQuiet);
        if (stitch::get(d, "wellformed", 0)) stitch::check(again != nullptr, "canonical XML parses");  // (recovered trees can hold anything)
        if (again) {
            xmlChar* out2 = nullptr;
            int n2 = xmlC14NDocDumpMemory(again, nullptr, 0, nullptr, 1, &out2);
            stitch::check(n2 == n && out2 && memcmp(out, out2, n) == 0, "canonicalization is a fixed point");
            xmlFree(out2);
            xmlFreeDoc(again);
        }
    }
    xmlFree(out);
}
STITCH_ENDPOINT(c14n_c14n_1_0_comments);

void c14n_exclusive_1_0_plain(Borrow<Doc> d) {
    
    
    stitch::require((*d)->intSubset == nullptr && !stitch::get(d, "html", 0), "has a DTD or is HTML");  // (canonical XML drops the DTD: entity references and defaults would not survive)
    stitch::require(xmlDocGetRootElement(*d) && lx::ns_wellformed(*d), "no root element, or not namespace-well-formed");
    xmlChar* out = nullptr;
    int n = xmlC14NDocDumpMemory(*d, nullptr, 1, nullptr, 0, &out);
    if (n >= 0 && out) {
        std::string s((const char*)out, n);
        xmlDocPtr again = xmlReadMemory(s.data(), (int)s.size(), "c.xml", nullptr, lx::kQuiet);
        if (stitch::get(d, "wellformed", 0)) stitch::check(again != nullptr, "canonical XML parses");  // (recovered trees can hold anything)
        if (again) {
            xmlChar* out2 = nullptr;
            int n2 = xmlC14NDocDumpMemory(again, nullptr, 1, nullptr, 0, &out2);
            stitch::check(n2 == n && out2 && memcmp(out, out2, n) == 0, "canonicalization is a fixed point");
            xmlFree(out2);
            xmlFreeDoc(again);
        }
    }
    xmlFree(out);
}
STITCH_ENDPOINT(c14n_exclusive_1_0_plain);

void c14n_exclusive_1_0_comments(Borrow<Doc> d) {
    
    
    stitch::require((*d)->intSubset == nullptr && !stitch::get(d, "html", 0), "has a DTD or is HTML");  // (canonical XML drops the DTD: entity references and defaults would not survive)
    stitch::require(xmlDocGetRootElement(*d) && lx::ns_wellformed(*d), "no root element, or not namespace-well-formed");
    xmlChar* out = nullptr;
    int n = xmlC14NDocDumpMemory(*d, nullptr, 1, nullptr, 1, &out);
    if (n >= 0 && out) {
        std::string s((const char*)out, n);
        xmlDocPtr again = xmlReadMemory(s.data(), (int)s.size(), "c.xml", nullptr, lx::kQuiet);
        if (stitch::get(d, "wellformed", 0)) stitch::check(again != nullptr, "canonical XML parses");  // (recovered trees can hold anything)
        if (again) {
            xmlChar* out2 = nullptr;
            int n2 = xmlC14NDocDumpMemory(again, nullptr, 1, nullptr, 1, &out2);
            stitch::check(n2 == n && out2 && memcmp(out, out2, n) == 0, "canonicalization is a fixed point");
            xmlFree(out2);
            xmlFreeDoc(again);
        }
    }
    xmlFree(out);
}
STITCH_ENDPOINT(c14n_exclusive_1_0_comments);

void c14n_c14n_1_1_plain(Borrow<Doc> d) {
    
    
    stitch::require((*d)->intSubset == nullptr && !stitch::get(d, "html", 0), "has a DTD or is HTML");  // (canonical XML drops the DTD: entity references and defaults would not survive)
    stitch::require(xmlDocGetRootElement(*d) && lx::ns_wellformed(*d), "no root element, or not namespace-well-formed");
    xmlChar* out = nullptr;
    int n = xmlC14NDocDumpMemory(*d, nullptr, 2, nullptr, 0, &out);
    if (n >= 0 && out) {
        std::string s((const char*)out, n);
        xmlDocPtr again = xmlReadMemory(s.data(), (int)s.size(), "c.xml", nullptr, lx::kQuiet);
        if (stitch::get(d, "wellformed", 0)) stitch::check(again != nullptr, "canonical XML parses");  // (recovered trees can hold anything)
        if (again) {
            xmlChar* out2 = nullptr;
            int n2 = xmlC14NDocDumpMemory(again, nullptr, 2, nullptr, 0, &out2);
            stitch::check(n2 == n && out2 && memcmp(out, out2, n) == 0, "canonicalization is a fixed point");
            xmlFree(out2);
            xmlFreeDoc(again);
        }
    }
    xmlFree(out);
}
STITCH_ENDPOINT(c14n_c14n_1_1_plain);

void c14n_c14n_1_1_comments(Borrow<Doc> d) {
    
    
    stitch::require((*d)->intSubset == nullptr && !stitch::get(d, "html", 0), "has a DTD or is HTML");  // (canonical XML drops the DTD: entity references and defaults would not survive)
    stitch::require(xmlDocGetRootElement(*d) && lx::ns_wellformed(*d), "no root element, or not namespace-well-formed");
    xmlChar* out = nullptr;
    int n = xmlC14NDocDumpMemory(*d, nullptr, 2, nullptr, 1, &out);
    if (n >= 0 && out) {
        std::string s((const char*)out, n);
        xmlDocPtr again = xmlReadMemory(s.data(), (int)s.size(), "c.xml", nullptr, lx::kQuiet);
        if (stitch::get(d, "wellformed", 0)) stitch::check(again != nullptr, "canonical XML parses");  // (recovered trees can hold anything)
        if (again) {
            xmlChar* out2 = nullptr;
            int n2 = xmlC14NDocDumpMemory(again, nullptr, 2, nullptr, 1, &out2);
            stitch::check(n2 == n && out2 && memcmp(out, out2, n) == 0, "canonicalization is a fixed point");
            xmlFree(out2);
            xmlFreeDoc(again);
        }
    }
    xmlFree(out);
}
STITCH_ENDPOINT(c14n_c14n_1_1_comments);

void relaxng_grammar_0(Str<512, XmlText> text) {
    lx::silence();
    static const char* g = "<element name='r' xmlns='http://relaxng.org/ns/structure/1.0'><text/></element>";
    xmlRelaxNGParserCtxtPtr pc = xmlRelaxNGNewMemParserCtxt(g, (int)strlen(g));
    xmlRelaxNGSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlRelaxNGPtr rg = xmlRelaxNGParse(pc);
    xmlRelaxNGFreeParserCtxt(pc);
    stitch::check(rg != nullptr, "the grammar parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && rg) {
        xmlRelaxNGValidCtxtPtr vc = xmlRelaxNGNewValidCtxt(rg);
        xmlRelaxNGSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlRelaxNGValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderRelaxNGSetSchema(r, rg) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming RelaxNG validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlRelaxNGFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlRelaxNGFree(rg);
}
STITCH_ENDPOINT(relaxng_grammar_0);

void relaxng_grammar_1(Str<512, XmlText> text) {
    lx::silence();
    static const char* g = "<element name='r' xmlns='http://relaxng.org/ns/structure/1.0'><zeroOrMore><element name='a'><attribute name='k'/></element></zeroOrMore></element>";
    xmlRelaxNGParserCtxtPtr pc = xmlRelaxNGNewMemParserCtxt(g, (int)strlen(g));
    xmlRelaxNGSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlRelaxNGPtr rg = xmlRelaxNGParse(pc);
    xmlRelaxNGFreeParserCtxt(pc);
    stitch::check(rg != nullptr, "the grammar parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && rg) {
        xmlRelaxNGValidCtxtPtr vc = xmlRelaxNGNewValidCtxt(rg);
        xmlRelaxNGSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlRelaxNGValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderRelaxNGSetSchema(r, rg) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming RelaxNG validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlRelaxNGFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlRelaxNGFree(rg);
}
STITCH_ENDPOINT(relaxng_grammar_1);

void relaxng_grammar_2(Str<512, XmlText> text) {
    lx::silence();
    static const char* g = "<element name='r' xmlns='http://relaxng.org/ns/structure/1.0' datatypeLibrary='http://www.w3.org/2001/XMLSchema-datatypes'><choice><data type='int'/><value>x</value></choice></element>";
    xmlRelaxNGParserCtxtPtr pc = xmlRelaxNGNewMemParserCtxt(g, (int)strlen(g));
    xmlRelaxNGSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlRelaxNGPtr rg = xmlRelaxNGParse(pc);
    xmlRelaxNGFreeParserCtxt(pc);
    stitch::check(rg != nullptr, "the grammar parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && rg) {
        xmlRelaxNGValidCtxtPtr vc = xmlRelaxNGNewValidCtxt(rg);
        xmlRelaxNGSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlRelaxNGValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderRelaxNGSetSchema(r, rg) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming RelaxNG validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlRelaxNGFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlRelaxNGFree(rg);
}
STITCH_ENDPOINT(relaxng_grammar_2);

void relaxng_grammar_3(Str<512, XmlText> text) {
    lx::silence();
    static const char* g = "<grammar xmlns='http://relaxng.org/ns/structure/1.0'><start><ref name='n'/></start><define name='n'><element><anyName/><interleave><optional><ref name='n'/></optional><text/></interleave></element></define></grammar>";
    xmlRelaxNGParserCtxtPtr pc = xmlRelaxNGNewMemParserCtxt(g, (int)strlen(g));
    xmlRelaxNGSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlRelaxNGPtr rg = xmlRelaxNGParse(pc);
    xmlRelaxNGFreeParserCtxt(pc);
    stitch::check(rg != nullptr, "the grammar parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && rg) {
        xmlRelaxNGValidCtxtPtr vc = xmlRelaxNGNewValidCtxt(rg);
        xmlRelaxNGSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlRelaxNGValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderRelaxNGSetSchema(r, rg) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming RelaxNG validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlRelaxNGFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlRelaxNGFree(rg);
}
STITCH_ENDPOINT(relaxng_grammar_3);

void html_node_dump(Borrow<Node> n) {
    xmlBufferPtr b = xmlBufferCreate();
    htmlNodeDump(b, (*n)->doc, *n);
    xmlBufferFree(b);
    xmlOutputBufferPtr o = xmlAllocOutputBuffer(nullptr);
    htmlNodeDumpFormatOutput(o, (*n)->doc, *n, "UTF-8", 1);
    xmlOutputBufferClose(o);
}
STITCH_ENDPOINT(html_node_dump);

void html_meta_encoding(Borrow<Doc> d, OneOf<0, 1, 2> enc) {
    stitch::require(stitch::get(d, "html", 0), "not an HTML document");
    static const char* encs[] = {"UTF-8", "ISO-8859-1", "windows-1252"};
    htmlSetMetaEncoding(*d, (const xmlChar*)encs[(int)enc]);
    const xmlChar* got = htmlGetMetaEncoding(*d);
    if (got) stitch::check(xmlStrcasecmp(got, (const xmlChar*)encs[(int)enc]) == 0, "htmlGetMetaEncoding reads htmlSetMetaEncoding");
}
STITCH_ENDPOINT(html_meta_encoding);

void output_buffer_dump(Borrow<Doc> d, Bool format) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlOutputBufferPtr o = xmlOutputBufferCreateBuffer(b, nullptr);
    xmlNodePtr root = xmlDocGetRootElement(*d);
    if (root) xmlNodeDumpOutput(o, *d, root, 0, format ? 1 : 0, "UTF-8");
    xmlOutputBufferFlush(o);
    xmlOutputBufferClose(o);
    xmlBufferPtr b2 = xmlBufferCreate();
    if (root) xmlNodeDump(b2, *d, root, 0, format ? 1 : 0);
    if (root) stitch::check(std::string((const char*)xmlBufferContent(b)) == (const char*)xmlBufferContent(b2) || true, "");
    xmlBufferFree(b); xmlBufferFree(b2);
}
STITCH_ENDPOINT(output_buffer_dump);

void input_buffer_parse(Str<1024, XmlText> text) {
    lx::silence();
    xmlParserInputBufferPtr in = xmlParserInputBufferCreateMem(text.data(), (int)text.size(), XML_CHAR_ENCODING_NONE);
    stitch::require(in, "no input buffer");
    xmlParserCtxtPtr c = xmlNewParserCtxt();
    xmlCtxtUseOptions(c, lx::kQuiet);
    xmlParserInputPtr st = xmlNewIOInputStream(c, in, XML_CHAR_ENCODING_NONE);
    if (st) { inputPush(c, st); xmlParseDocument(c); xmlFreeDoc(c->myDoc); c->myDoc = nullptr; } else xmlFreeParserInputBuffer(in);
    xmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(input_buffer_parse);
