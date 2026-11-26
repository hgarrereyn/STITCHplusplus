// Parser entry points (parser.h, parserInternals.h, HTMLparser.h): every way to feed the same
// bytes to the XML or HTML parser builds the same document; parser-context accessors; parsing
// with an in-memory resource loader (external DTDs, entities, XInclude) and DTD parsing.
#include "../objects.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <libxml/parserInternals.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <libxml/xinclude.h>
#include <libxml/valid.h>

namespace {
std::string dump_any(xmlDocPtr d, bool html) {
    if (!d) return "<none>";
    if (!html) return lx::dump(d, 0);
    xmlChar* m = nullptr;
    int n = 0;
    htmlDocDumpMemoryFormat(d, &m, &n, 0);
    std::string s = m ? std::string((const char*)m, (size_t)n) : "";
    xmlFree(m);
    return s;
}
void count_err(void* data, const xmlError*) { ++*(int*)data; }
}  // namespace

// Initialize the library once (some functions, e.g. the XPath casts, use globals it sets up).
STITCH_INIT { xmlInitParser(); }

// The XML parser builds the same document from memory, a string, a file, a descriptor and
// callbacks, through the plain and the context functions.
void read_variants(Str<4096, XmlText> text, lx::ParseOpts opts, Range<0, 15> how) {
    lx::silence();
    std::string t = text.s.substr(0, strlen(text.c_str()));
    int o = ((int)opts | lx::kQuiet) & ~XML_PARSE_OLDSAX;
    xmlDocPtr ref = xmlReadMemory(t.data(), (int)t.size(), "in.xml", nullptr, o);
    std::string path = lx::tmp_path("read");
    stitch::require(lx::write_file(path, t), "cannot write a file");
    lx::StrIO io{t};
    int fd = open(path.c_str(), O_RDONLY);
    xmlParserCtxtPtr c = xmlNewParserCtxt();
    xmlDocPtr d = nullptr;
    bool uses_opts = true;
    switch ((int)how) {
        case 0: d = xmlReadDoc((const xmlChar*)t.c_str(), "in.xml", nullptr, o); break;
        case 1: d = xmlReadFile(path.c_str(), nullptr, o); break;
        case 2: d = fd >= 0 ? xmlReadFd(fd, "in.xml", nullptr, o) : nullptr; break;
        case 3: d = xmlReadIO(lx::io_read, lx::io_close, &io, "in.xml", nullptr, o); break;
        case 4: d = xmlCtxtReadMemory(c, t.data(), (int)t.size(), "in.xml", nullptr, o); break;
        case 5: d = xmlCtxtReadDoc(c, (const xmlChar*)t.c_str(), "in.xml", nullptr, o); break;
        case 6: d = xmlCtxtReadFile(c, path.c_str(), nullptr, o); break;
        case 7: d = fd >= 0 ? xmlCtxtReadFd(c, fd, "in.xml", nullptr, o) : nullptr; break;
        case 8: d = xmlCtxtReadIO(c, lx::io_read, lx::io_close, &io, "in.xml", nullptr, o); break;
        case 9: xmlCtxtUseOptions(c, o); d = xmlCtxtParseDocument(c, xmlNewInputFromMemory("in.xml", t.data(), t.size(), XML_INPUT_BUF_STATIC)); break;
        case 10: xmlCtxtUseOptions(c, o); d = xmlCtxtParseDocument(c, xmlNewInputFromString("in.xml", t.c_str(), XML_INPUT_BUF_STATIC)); break;
        case 11: xmlCtxtSetOptions(c, o); d = fd >= 0 ? xmlCtxtParseDocument(c, xmlNewInputFromFd("in.xml", fd, (xmlParserInputFlags)0)) : nullptr; break;
        case 12: xmlCtxtSetOptions(c, o); d = xmlCtxtParseDocument(c, xmlNewInputFromIO("in.xml", lx::io_read, lx::io_close, &io, (xmlParserInputFlags)0)); break;
        case 13: {
            xmlParserInputPtr in = nullptr;
            xmlCtxtSetOptions(c, o);
            if (xmlNewInputFromUrl(path.c_str(), (xmlParserInputFlags)0, &in) == XML_ERR_OK) d = xmlCtxtParseDocument(c, in);
            break;
        }
        case 14: {
            // the old style: a context made for the input, then xmlParseDocument
            xmlParserCtxtPtr m = xmlCreateMemoryParserCtxt(t.data(), (int)t.size());
            if (m) {
                xmlCtxtUseOptions(m, o);
                xmlParseDocument(m);
                d = m->wellFormed || (o & XML_PARSE_RECOVER) ? m->myDoc : nullptr;
                if (!d) xmlFreeDoc(m->myDoc);
                m->myDoc = nullptr;
                xmlFreeParserCtxt(m);
            }
            break;
        }
        default: {
            xmlParserCtxtPtr m = xmlCreateIOParserCtxt(nullptr, nullptr, lx::io_read, lx::io_close, &io, XML_CHAR_ENCODING_NONE);
            if (m) {
                xmlCtxtUseOptions(m, o);
                xmlParseDocument(m);
                d = m->wellFormed || (o & XML_PARSE_RECOVER) ? m->myDoc : nullptr;
                if (!d) xmlFreeDoc(m->myDoc);
                m->myDoc = nullptr;
                xmlFreeParserCtxt(m);
            }
            break;
        }
    }
    (void)uses_opts;
    if (fd >= 0) close(fd);
    unlink(path.c_str());
    std::string a = dump_any(ref, false), b = dump_any(d, false);
    stitch::log("ref %d, variant %d", ref != nullptr, d != nullptr);
    if (ref || (int)how < 14) stitch::check((ref != nullptr) == (d != nullptr), "accepted by both or neither");
    if (ref && d) stitch::check(a == b, "the same document");
    xmlFreeDoc(ref);
    xmlFreeDoc(d);
    xmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(read_variants);

// The same for the HTML parser.
void html_read_variants(Str<4096, HtmlText> text, lx::HtmlOpts opts, Range<0, 12> how) {
    lx::silence();
    std::string t = text.s.substr(0, strlen(text.c_str()));
    int o = (int)opts | lx::kQuiet;
    htmlDocPtr ref = htmlReadMemory(t.data(), (int)t.size(), "in.html", nullptr, o);
    std::string path = lx::tmp_path("hread");
    stitch::require(lx::write_file(path, t), "cannot write a file");
    lx::StrIO io{t};
    int fd = open(path.c_str(), O_RDONLY);
    htmlParserCtxtPtr c = htmlNewParserCtxt();
    htmlDocPtr d = nullptr;
    bool made = true;  // (the old-style constructors refuse some inputs, e.g. an empty buffer)
    switch ((int)how) {
        case 0: d = htmlReadDoc((const xmlChar*)t.c_str(), "in.html", nullptr, o); break;
        case 1: d = htmlReadFile(path.c_str(), nullptr, o); break;
        case 2: d = fd >= 0 ? htmlReadFd(fd, "in.html", nullptr, o) : nullptr; break;
        case 3: d = htmlReadIO(lx::io_read, lx::io_close, &io, "in.html", nullptr, o); break;
        case 4: d = htmlCtxtReadMemory(c, t.data(), (int)t.size(), "in.html", nullptr, o); break;
        case 5: d = htmlCtxtReadDoc(c, (const xmlChar*)t.c_str(), "in.html", nullptr, o); break;
        case 6: d = htmlCtxtReadFile(c, path.c_str(), nullptr, o); break;
        case 7: d = fd >= 0 ? htmlCtxtReadFd(c, fd, "in.html", nullptr, o) : nullptr; break;
        case 8: d = htmlCtxtReadIO(c, lx::io_read, lx::io_close, &io, "in.html", nullptr, o); break;
        case 9:
            htmlCtxtSetOptions(c, o);
            d = htmlCtxtParseDocument(c, xmlNewInputFromMemory("in.html", t.data(), t.size(), XML_INPUT_BUF_STATIC));
            break;
        case 10: {
            htmlParserCtxtPtr m = htmlCreateMemoryParserCtxt(t.data(), (int)t.size());
            made = m != nullptr;
            if (m) {
                htmlCtxtUseOptions(m, o);
                htmlParseDocument(m);
                d = m->myDoc;
                m->myDoc = nullptr;
                htmlFreeParserCtxt(m);
            }
            break;
        }
        case 11: {
            htmlParserCtxtPtr m = htmlCreateFileParserCtxt(path.c_str(), nullptr);
            made = m != nullptr;
            if (m) {
                htmlCtxtUseOptions(m, o);
                htmlParseDocument(m);
                d = m->myDoc;
                m->myDoc = nullptr;
                htmlFreeParserCtxt(m);
            }
            break;
        }
        default: {
            htmlParserCtxtPtr m = htmlNewSAXParserCtxt(nullptr, nullptr);
            if (m) {
                d = htmlCtxtReadMemory(m, t.data(), (int)t.size(), "in.html", nullptr, o);
                htmlCtxtReset(m);
                htmlDocPtr again = htmlCtxtReadMemory(m, t.data(), (int)t.size(), "in.html", nullptr, o);
                stitch::check(dump_any(again, true) == dump_any(d, true), "a reset context parses the same again");
                xmlFreeDoc(again);
                htmlFreeParserCtxt(m);
            }
            break;
        }
    }
    if (fd >= 0) close(fd);
    unlink(path.c_str());
    if (made) stitch::check((ref != nullptr) == (d != nullptr), "accepted by both or neither");
    if (ref && d) stitch::check(dump_any(ref, true) == dump_any(d, true), "the same HTML document");
    xmlFreeDoc(ref);
    xmlFreeDoc(d);
    htmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(html_read_variants);

// What a parser context reports after a parse agrees with the document it built.
void parser_ctxt_info(Str<4096, XmlText> text, lx::ParseOpts opts, Bool reuse) {
    lx::silence();
    xmlParserCtxtPtr c = xmlNewParserCtxt();
    stitch::require(c, "no context");
    int errors = 0;
    xmlCtxtSetErrorHandler(c, count_err, &errors);
    xmlCtxtSetResourceLoader(c, lx::mem_loader, nullptr);
    xmlCtxtSetMaxAmplification(c, 20);
    int o = ((int)opts | XML_PARSE_NONET) & ~XML_PARSE_OLDSAX;
    stitch::check(xmlCtxtSetOptions(c, o) == 0 && xmlCtxtGetOptions(c) == o, "GetOptions after SetOptions");
    int priv = 0;
    xmlCtxtSetPrivate(c, &priv);
    stitch::check(xmlCtxtGetPrivate(c) == &priv, "GetPrivate");
    xmlCtxtSetCatalogs(c, nullptr);
    stitch::check(xmlCtxtGetCatalogs(c) == nullptr, "GetCatalogs");
    xmlDictPtr dict = xmlDictCreate();
    xmlCtxtSetDict(c, dict);
    stitch::check(xmlCtxtGetDict(c) == dict, "GetDict after SetDict");
    xmlDictFree(dict);  // the context holds a reference
    xmlSAXHandler sax;
    memcpy(&sax, xmlCtxtGetSaxHandler(c), sizeof sax);
    stitch::check(xmlCtxtSetSaxHandler(c, &sax) == 0, "SetSaxHandler with the default handler");
    stitch::check(xmlCtxtIsHtml(c) == 0, "not an HTML context");
    if (reuse) {
        xmlDocPtr first = xmlCtxtReadMemory(c, "<x/>", 4, "first.xml", nullptr, o);
        xmlFreeDoc(first);
        xmlCtxtReset(c);
        xmlCtxtResetLastError(c);
        stitch::check(xmlCtxtGetLastError(c) == nullptr || xmlCtxtGetLastError(c)->code == 0, "ResetLastError");
    }
    xmlParserInputPtr in = xmlNewInputFromMemory("in.xml", text.data(), text.size(), XML_INPUT_BUF_STATIC);
    stitch::require(in, "no input");
    xmlDocPtr d = xmlCtxtParseDocument(c, in);
    int st = xmlCtxtGetStatus(c);
    stitch::log("doc %d, status %d, errors %d", d != nullptr, st, errors);
    if (d && !(o & XML_PARSE_RECOVER)) stitch::check(!(st & XML_STATUS_NOT_WELL_FORMED), "a returned document is well-formed");
    if (!d) stitch::check(st != 0 || errors > 0 || xmlCtxtGetLastError(c), "a failure is reported");
    if (st & XML_STATUS_NOT_WELL_FORMED) {
        const xmlError* e = xmlCtxtGetLastError(c);
        stitch::check(e && e->code != 0, "an error is recorded");
        xmlError copy;
        memset(&copy, 0, sizeof copy);
        stitch::check(xmlCopyError(e, &copy) == 0 && copy.code == e->code, "xmlCopyError");
        xmlFormatError(&copy, lx::quiet_generic, nullptr);
        xmlResetError(&copy);
    }
    if (d) {
        if (xmlCtxtGetVersion(c) && d->version) stitch::check(xmlStrEqual(xmlCtxtGetVersion(c), d->version), "GetVersion is the document's version");
        stitch::check(xmlCtxtGetStandalone(c) == d->standalone || d->standalone < 0, "GetStandalone");
        const xmlChar *name = nullptr, *sys = nullptr, *pub = nullptr;
        int has = xmlCtxtGetDocTypeDecl(c, &name, &sys, &pub);
        (void)has;
        if (d->intSubset && name) stitch::check(xmlStrEqual(name, d->intSubset->name), "GetDocTypeDecl name");
        xmlCtxtGetDeclaredEncoding(c);
    }
    xmlCtxtIsStopped(c);
    xmlCtxtIsInSubset(c);
    xmlCtxtGetValidCtxt(c);
    xmlCtxtGetUserData(c);
    xmlCtxtGetNode(c);
    xmlCtxtGetDocument(c);
    const char* file = nullptr;
    int line = 0, col = 0;
    unsigned long pos = 0;
    xmlCtxtGetInputPosition(c, 0, &file, &line, &col, &pos);
    const xmlChar* start = nullptr;
    int size = 80, off = 0;
    xmlCtxtGetInputWindow(c, 0, &start, &size, &off);
    xmlFreeDoc(d);
    xmlFreeParserCtxt(c);
    xmlResetLastError();
}
STITCH_ENDPOINT(parser_ctxt_info);

// Parsing with external DTDs, external entities and XIncludes served from memory, then DTD
// validation and XInclude processing in all their variants.
void parse_with_resources(Str<4096, XmlText> text, Flags<XML_PARSE_DTDLOAD, XML_PARSE_DTDATTR, XML_PARSE_NOENT, XML_PARSE_DTDVALID,
                                                          XML_PARSE_XINCLUDE, XML_PARSE_NOXINCNODE, XML_PARSE_NOBASEFIX> opts,
                          Range<0, 6> xinclude) {
    lx::silence();
    xmlParserCtxtPtr c = xmlNewParserCtxt();
    stitch::require(c, "no context");
    xmlCtxtSetResourceLoader(c, lx::mem_loader, nullptr);
    xmlCtxtSetMaxAmplification(c, 20);
    xmlDocPtr d = xmlCtxtReadMemory(c, text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    if (!d) {
        xmlFreeParserCtxt(c);
        stitch::bail("parse error");
    }
    if (d->intSubset || d->extSubset) {
        int v = xmlCtxtValidateDocument(c, d);
        stitch::check(v == 0 || v == 1, "ValidateDocument result");
        if (d->extSubset) {
            int v2 = xmlCtxtValidateDtd(c, d, d->extSubset);
            stitch::check(v2 == 0 || v2 == 1, "ValidateDtd result");
        }
        if (xmlNodePtr root = xmlDocGetRootElement(d)) {
            xmlValidCtxtPtr vc = xmlNewValidCtxt();
            xmlValidateElement(vc, d, root);
            xmlFreeValidCtxt(vc);
        }
    }
    // XInclude processing on a copy, with the same in-memory loader
    xmlDocPtr x = xmlCopyDoc(d, 1);
    int r = 0;
    switch ((int)xinclude) {
        case 0: r = xmlXIncludeProcess(x); break;
        case 1: r = xmlXIncludeProcessFlags(x, lx::kQuiet); break;
        case 2: r = xmlXIncludeProcessFlagsData(x, lx::kQuiet, nullptr); break;
        case 3: if (xmlDocGetRootElement(x)) r = xmlXIncludeProcessTree(xmlDocGetRootElement(x)); break;
        case 4: if (xmlDocGetRootElement(x)) r = xmlXIncludeProcessTreeFlags(xmlDocGetRootElement(x), lx::kQuiet); break;
        case 5: if (xmlDocGetRootElement(x)) r = xmlXIncludeProcessTreeFlagsData(xmlDocGetRootElement(x), lx::kQuiet, nullptr); break;
        default: {
            xmlXIncludeCtxtPtr xc = xmlXIncludeNewContext(x);
            if (xc) {
                int errors = 0;
                xmlXIncludeSetFlags(xc, lx::kQuiet);
                xmlXIncludeSetErrorHandler(xc, count_err, &errors);
                xmlXIncludeSetResourceLoader(xc, lx::mem_loader, nullptr);
                if (xmlNodePtr root = xmlDocGetRootElement(x)) r = xmlXIncludeProcessNode(xc, root);
                if (r < 0) stitch::check(xmlXIncludeGetLastError(xc) != 0 || errors > 0, "a failed XInclude reports an error");
                xmlXIncludeFreeContext(xc);
            }
        }
    }
    stitch::log("xinclude %d", r);
    if (r >= 0) {
        std::string s = lx::dump(x, 0);
        (void)s;
    }
    xmlFreeDoc(x);
    xmlFreeDoc(d);
    xmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(parse_with_resources);

// A DTD parsed on its own (from a string, an input buffer, a file) and used to validate a
// document; the declarations it holds are found by the lookup functions.
void parse_dtd_alone(Str<2048> dtd, Str<2048, XmlText> doc, Range<0, 2> how, Str<16, XmlName> name) {
    lx::silence();
    xmlDtdPtr dt = nullptr;
    std::string path = lx::tmp_path("dtd");
    switch ((int)how) {
        case 0: {
            xmlParserCtxtPtr c = xmlNewParserCtxt();
            xmlCtxtSetOptions(c, lx::kQuiet);
            xmlParserInputPtr in = xmlNewInputFromMemory("in.dtd", dtd.data(), dtd.size(), XML_INPUT_BUF_STATIC);
            dt = in ? xmlCtxtParseDtd(c, in, nullptr, (const xmlChar*)"in.dtd") : nullptr;
            xmlFreeParserCtxt(c);
            break;
        }
        case 1: {
            xmlParserInputBufferPtr in = xmlParserInputBufferCreateMem(dtd.data(), (int)dtd.size(), XML_CHAR_ENCODING_NONE);
            dt = in ? xmlIOParseDTD(nullptr, in, XML_CHAR_ENCODING_NONE) : nullptr;  // (consumes `in`)
            break;
        }
        default:
            if (lx::write_file(path, dtd.s)) dt = xmlParseDTD(nullptr, (const xmlChar*)path.c_str());
            unlink(path.c_str());
            break;
    }
    stitch::require(dt, "DTD parse error");
    const xmlChar* n = (const xmlChar*)name.c_str();
    xmlElementPtr el = xmlGetDtdElementDesc(dt, n);
    xmlElementPtr qel = xmlGetDtdQElementDesc(dt, n, nullptr);
    stitch::check(el == qel || strchr(name.c_str(), ':'), "GetDtdElementDesc == GetDtdQElementDesc without prefix");
    if (el) {
        stitch::check(xmlStrEqual(el->name, n), "element declaration has the name");
        const xmlChar* names[64];
        int len = 0;
        xmlValidGetPotentialChildren(el->content, names, &len, 64);
        for (xmlAttributePtr a = el->attributes; a; a = a->nexth) {
            xmlAttributePtr ad = xmlGetDtdAttrDesc(dt, n, a->name);
            xmlAttributePtr qad = xmlGetDtdQAttrDesc(dt, n, a->name, a->prefix);
            stitch::check(ad != nullptr || a->prefix, "attribute declaration found");
            stitch::check(qad != nullptr, "qualified attribute declaration found");
        }
    }
    xmlGetDtdNotationDesc(dt, n);
    xmlEntityPtr e = xmlGetDtdEntity(nullptr, n);
    (void)e;
    // validate a document against it
    xmlDocPtr d = xmlReadMemory(doc.data(), (int)doc.size(), "doc.xml", nullptr, lx::kQuiet);
    if (d) {
        xmlValidCtxtPtr vc = xmlNewValidCtxt();
        xmlValidateDtd(vc, d, dt);
        xmlFreeValidCtxt(vc);
        xmlParserCtxtPtr c = xmlNewParserCtxt();
        int v = xmlCtxtValidateDtd(c, d, dt);
        stitch::check(v == 0 || v == 1, "CtxtValidateDtd result");
        xmlFreeParserCtxt(c);
        xmlFreeDoc(d);
    }
    xmlFreeDtd(dt);
}
STITCH_ENDPOINT(parse_dtd_alone);

// Chunks parsed in the context of a document; content parsed in the context of a node.
void parse_fragments(Borrow<Node> n, Str<1024, XmlText> chunk, Bool recover) {
    lx::silence();
    xmlNodePtr e = *n;
    stitch::require(e->doc && e->type == XML_ELEMENT_NODE, "not an element");
    xmlNodePtr list = nullptr;
    int r = xmlParseBalancedChunkMemoryRecover(e->doc, nullptr, nullptr, 0, (const xmlChar*)chunk.c_str(), &list, recover ? 1 : 0);
    stitch::log("balanced chunk %d", r);
    xmlFreeNodeList(list);
    xmlParserCtxtPtr c = xmlNewParserCtxt();
    xmlCtxtSetOptions(c, lx::kQuiet);
    xmlParserInputPtr in = xmlNewInputFromMemory(nullptr, chunk.data(), chunk.size(), XML_INPUT_BUF_STATIC);
    xmlNodePtr content = in ? xmlCtxtParseContent(c, in, e, 0) : nullptr;
    xmlFreeNodeList(content);
    xmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(parse_fragments);

// The push parser: reset a context with a first chunk, feed the rest, stop it midway.
void push_reset(Str<4096, XmlText> text, Range<1, 256> first, Bool stop) {
    lx::silence();
    xmlParserCtxtPtr c = xmlCreatePushParserCtxt(nullptr, nullptr, nullptr, 0, "push.xml");
    stitch::require(c, "no push parser");
    xmlCtxtUseOptions(c, lx::kQuiet);
    xmlParseChunk(c, "<x>", 3, 0);
    size_t k = std::min((size_t)(int)first, text.size());
    stitch::check(xmlCtxtResetPush(c, text.data(), (int)k, "push.xml", nullptr) == 0, "ResetPush");
    xmlFreeDoc(c->myDoc);
    c->myDoc = nullptr;
    if (stop) xmlStopParser(c);
    xmlParseChunk(c, text.data() + k, (int)(text.size() - k), 1);
    if (stop) stitch::check(xmlCtxtIsStopped(c) == 1, "a stopped parser stays stopped");
    xmlFreeDoc(c->myDoc);
    xmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(push_reset);

// Library features and a few global helpers.
void parser_globals(Range<0, 40> feature, Str<32, XmlName> qname) {
    lx::silence();
    xmlInitParser();
    int h = xmlHasFeature((xmlFeature)(int)feature);
    stitch::check(h == 0 || h == 1, "HasFeature is a boolean");
    if ((int)feature == XML_WITH_XPATH) stitch::check(h == 1, "XPath is compiled in");
    int old = xmlKeepBlanksDefault(1);
    xmlKeepBlanksDefault(old);
    xmlParserCtxtPtr c = xmlNewParserCtxt();
    xmlChar* prefix = nullptr;
    xmlChar* local = xmlSplitQName(c, (const xmlChar*)qname.c_str(), &prefix);
    if (local && prefix) {
        std::string q = std::string((const char*)prefix) + ":" + (const char*)local;
        stitch::check(q == qname.c_str(), "prefix:local is the QName");
    }
    xmlFree(local);
    xmlFree(prefix);
    xmlExternalEntityLoader loader = xmlGetExternalEntityLoader();
    stitch::check(loader != nullptr, "an entity loader is set");
    xmlParserInputPtr in = xmlLoadExternalEntity("missing.ent", nullptr, c);
    if (in) xmlFreeInputStream(in);
    xmlParserInputPtr s = xmlNewStringInputStream(c, (const xmlChar*)"<a/>");
    if (s) {
        stitch::check(xmlCtxtPushInput(c, s) >= 0, "PushInput");
        xmlParserInputPtr back = xmlCtxtPopInput(c);
        stitch::check(back == s, "PopInput returns the pushed input");
        xmlFreeInputStream(back);
    }
    xmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(parser_globals);

// Switching the input encoding of a context before parsing.
void switch_encoding(Bytes<1024> data, OneOf<0, 1, 2, 3, 4> which, Bool by_name) {
    lx::silence();
    static const char* names[] = {"UTF-16LE", "ISO-8859-1", "UTF-8", "US-ASCII", "UTF-16BE"};
    static const xmlCharEncoding encs[] = {XML_CHAR_ENCODING_UTF16LE, XML_CHAR_ENCODING_8859_1, XML_CHAR_ENCODING_UTF8,
                                           XML_CHAR_ENCODING_ASCII, XML_CHAR_ENCODING_UTF16BE};
    xmlParserCtxtPtr c = xmlCreateMemoryParserCtxt((const char*)data.data(), (int)data.size());
    stitch::require(c, "no context");
    xmlCtxtUseOptions(c, lx::kQuiet);
    switch (by_name ? 0 : (int)which % 2 + 1) {
        case 0: xmlSwitchEncodingName(c, names[(int)which]); break;
        case 1: xmlSwitchEncoding(c, encs[(int)which]); break;
        default: {
            xmlCharEncodingHandlerPtr h = nullptr;
            if (xmlLookupCharEncodingHandler(encs[(int)which], &h) == 0 && h) xmlSwitchToEncoding(c, h);
            break;
        }
    }
    xmlParseDocument(c);
    xmlFreeDoc(c->myDoc);
    c->myDoc = nullptr;
    xmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(switch_encoding);
