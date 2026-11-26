// Output: xmlSaveCtxt sinks (fd, file name, callbacks) and xmlSaveTree; the xmlIO buffers;
// the HTML serializers (HTMLtree.h); canonical XML to a buffer / file / with a visibility
// callback; the debugXML dumpers; streaming patterns; local XML catalogs.
#include "../objects.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <libxml/xmlsave.h>
#include <libxml/xmlIO.h>
#include <libxml/HTMLtree.h>
#include <libxml/debugXML.h>
#include <libxml/pattern.h>
#include <libxml/catalog.h>
#include <functional>

namespace {
}  // namespace

// The save context writes the same bytes to a buffer, a descriptor, a file and callbacks;
// xmlSaveTree of the root writes what xmlSaveDoc writes for the root.
void save_sinks(Borrow<Doc> d, Flags<XML_SAVE_FORMAT, XML_SAVE_NO_DECL, XML_SAVE_NO_EMPTY, XML_SAVE_AS_XML> opts, Range<0, 2> sink,
                Bool indent_tab) {
    stitch::require(!stitch::get(d, "html", 0), "HTML document");
    int o = (int)opts;
    auto setup = [&](xmlSaveCtxtPtr s) {
        if (s && indent_tab) xmlSaveSetIndentString(s, "\t");
        return s;
    };
    xmlBufferPtr b = xmlBufferCreate();
    xmlSaveCtxtPtr ref = setup(xmlSaveToBuffer(b, "UTF-8", o));
    stitch::require(ref, "no save context");
    long r0 = xmlSaveDoc(ref, *d);
    xmlSaveFlush(ref);
    xmlSaveClose(ref);
    std::string want((const char*)xmlBufferContent(b), (size_t)xmlBufferLength(b));
    xmlBufferFree(b);
    std::string got, path = lx::tmp_path("savectx");
    xmlSaveCtxtPtr s = nullptr;
    int fd = -1;
    switch ((int)sink) {
        case 0: fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600); s = fd >= 0 ? setup(xmlSaveToFd(fd, "UTF-8", o)) : nullptr; break;
        case 1: s = setup(xmlSaveToFilename(path.c_str(), "UTF-8", o)); break;
        default: s = setup(xmlSaveToIO(lx::str_write, lx::str_close, &got, "UTF-8", o)); break;
    }
    if (!s) {
        if (fd >= 0) close(fd);
        unlink(path.c_str());
        stitch::bail("no save context");
    }
    long r1 = xmlSaveDoc(s, *d);
    xmlParserErrors fin = xmlSaveFinish(s);
    if (fd >= 0) close(fd);
    if ((int)sink < 2) got = lx::read_file(path);
    unlink(path.c_str());
    if (r0 >= 0 && r1 >= 0 && fin == XML_ERR_OK) stitch::check(got == want, "every sink gets the same bytes");
    // a subtree
    if (xmlNodePtr root = xmlDocGetRootElement(*d)) {
        xmlBufferPtr t = xmlBufferCreate();
        xmlSaveCtxtPtr st = setup(xmlSaveToBuffer(t, "UTF-8", o));
        long n = xmlSaveTree(st, root);
        xmlSaveClose(st);
        xmlBufferPtr u = xmlBufferCreate();
        int m = xmlNodeDump(u, *d, root, 0, (o & XML_SAVE_FORMAT) ? 1 : 0);
        xmlDtdPtr dtd = xmlGetIntSubset(*d);
        bool xhtml = dtd && xmlIsXHTML(dtd->SystemID, dtd->ExternalID);  // (XHTML documents are saved by XHTML rules)
        if (n >= 0 && m >= 0 && !(o & XML_SAVE_NO_EMPTY) && !indent_tab && !xhtml)
            stitch::check(std::string((const char*)xmlBufferContent(t), (size_t)xmlBufferLength(t)) ==
                              std::string((const char*)xmlBufferContent(u), (size_t)xmlBufferLength(u)),
                          "xmlSaveTree(root) == xmlNodeDump(root)");
        xmlBufferFree(t);
        xmlBufferFree(u);
    }
}
STITCH_ENDPOINT(save_sinks);

// xmlIO input buffers from every source read the same bytes; output buffers write them.
void io_buffers(Bytes<2048> data, Range<0, 6> in_kind, Range<0, 4> out_kind) {
    std::string bytes((const char*)data.data(), data.size());
    std::string path = lx::tmp_path("io");
    FILE* f = fopen(path.c_str(), "wb");
    stitch::require(f, "cannot write a file");
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    // input
    xmlParserInputBufferPtr in = nullptr;
    int fd = -1;
    FILE* fp = nullptr;
    switch ((int)in_kind) {
        case 0: in = xmlParserInputBufferCreateMem(bytes.data(), (int)bytes.size(), XML_CHAR_ENCODING_NONE); break;
        case 1: in = xmlParserInputBufferCreateStatic(bytes.data(), (int)bytes.size(), XML_CHAR_ENCODING_NONE); break;
        case 2: in = xmlParserInputBufferCreateFilename(path.c_str(), XML_CHAR_ENCODING_NONE); break;
        case 3: fd = open(path.c_str(), O_RDONLY); in = fd >= 0 ? xmlParserInputBufferCreateFd(fd, XML_CHAR_ENCODING_NONE) : nullptr; break;
        case 4: fp = fopen(path.c_str(), "rb"); in = fp ? xmlParserInputBufferCreateFile(fp, XML_CHAR_ENCODING_NONE) : nullptr; break;
        case 5: {
            fp = fopen(path.c_str(), "rb");
            in = fp ? xmlParserInputBufferCreateIO([](void* c, char* b, int l) { return (int)fread(b, 1, (size_t)l, (FILE*)c); },
                                                    [](void*) { return 0; }, fp, XML_CHAR_ENCODING_NONE)
                    : nullptr;
            break;
        }
        default: in = xmlAllocParserInputBuffer(XML_CHAR_ENCODING_NONE); break;
    }
    if (in && (int)in_kind != 6) {
        int guard = 0;
        while (xmlParserInputBufferRead(in, 512) > 0 && guard++ < 100) {}
        std::string got((const char*)xmlBufContent(in->buffer), xmlBufUse(in->buffer));
        stitch::check(got == bytes, "the input buffer read every byte");
    }
    if (in) xmlFreeParserInputBuffer(in);
    if (fd >= 0) close(fd);
    if (fp) fclose(fp);
    // output
    std::string text = bytes.substr(0, strlen(bytes.c_str()));
    xmlOutputBufferPtr out = nullptr;
    std::string sink;
    xmlBufferPtr xb = nullptr;
    fd = -1;
    fp = nullptr;
    switch ((int)out_kind) {
        case 0: out = xmlOutputBufferCreateFilename(path.c_str(), nullptr, 0); break;
        case 1: fp = fopen(path.c_str(), "wb"); out = fp ? xmlOutputBufferCreateFile(fp, nullptr) : nullptr; break;
        case 2: fd = open(path.c_str(), O_WRONLY | O_TRUNC); out = fd >= 0 ? xmlOutputBufferCreateFd(fd, nullptr) : nullptr; break;
        case 3: xb = xmlBufferCreate(); out = xmlOutputBufferCreateBuffer(xb, nullptr); break;
        default: out = xmlOutputBufferCreateIO(lx::str_write, lx::str_close, &sink, nullptr); break;
    }
    if (out) {
        std::string want;
        xmlOutputBufferWrite(out, (int)bytes.size(), bytes.data());
        want += bytes;
        xmlOutputBufferWriteString(out, text.c_str());
        want += text;
        xmlOutputBufferWriteEscape(out, (const xmlChar*)"<&>", nullptr);
        want += "&lt;&amp;&gt;";
        if ((int)out_kind == 4 || (int)out_kind == 3) {
            const xmlChar* c = xmlOutputBufferGetContent(out);
            size_t n = xmlOutputBufferGetSize(out);
            (void)c;
            (void)n;
        }
        int r = xmlOutputBufferClose(out);
        std::string got = (int)out_kind == 4 ? sink : (int)out_kind == 3 ? std::string((const char*)xmlBufferContent(xb), (size_t)xmlBufferLength(xb)) : "";
        if (fp) fclose(fp);
        if (fd >= 0) close(fd);
        if ((int)out_kind < 3) got = lx::read_file(path);
        if (r >= 0) stitch::check(got == want, "the output buffer wrote every byte");
    } else {
        if (fp) fclose(fp);
        if (fd >= 0) close(fd);
    }
    if (xb) xmlBufferFree(xb);
    char* dir = xmlParserGetDirectory(path.c_str());
    stitch::check(dir && std::string(dir) == "/tmp", "xmlParserGetDirectory");
    xmlFree(dir);
    unlink(path.c_str());
}
STITCH_ENDPOINT(io_buffers);

static int match_mem(const char* uri) { return uri && strncmp(uri, "mem:", 4) == 0; }
static void* open_mem(const char*) { return new size_t(0); }
static int read_mem(void* c, char* buf, int len) {
    static const char doc[] = "<mem>from a callback</mem>";
    size_t* off = (size_t*)c;
    size_t n = std::min((size_t)len, sizeof doc - 1 - *off);
    memcpy(buf, doc + *off, n);
    *off += n;
    return (int)n;
}
static int close_mem(void* c) { delete (size_t*)c; return 0; }
static int write_null(void*, const char*, int len) { return len; }
static void* open_out(const char*) { return (void*)1; }
static int close_out(void*) { return 0; }

// Registering input / output callbacks (global: restored before returning).
void io_callbacks(Bool cleanup) {
    lx::silence();
    int k = xmlRegisterInputCallbacks(match_mem, open_mem, read_mem, close_mem);
    stitch::require(k >= 0, "cannot register");
    xmlParserInputBufferPtr in = xmlParserInputBufferCreateFilename("mem:doc", XML_CHAR_ENCODING_NONE);
    stitch::check(in != nullptr, "the registered callback opens mem: URIs");
    if (in) xmlFreeParserInputBuffer(in);
    stitch::check(xmlPopInputCallbacks() == k, "PopInputCallbacks removes the last registration");
    int o = xmlRegisterOutputCallbacks(match_mem, open_out, write_null, close_out);
    xmlOutputBufferPtr out = xmlOutputBufferCreateFilename("mem:out", nullptr, 0);
    stitch::check(out != nullptr, "the registered output callback opens mem: URIs");
    if (out) xmlOutputBufferClose(out);
    stitch::check(xmlPopOutputCallbacks() == o, "PopOutputCallbacks removes the last registration");
    if (cleanup) {
        xmlCleanupInputCallbacks();
        xmlRegisterDefaultInputCallbacks();
        xmlCleanupOutputCallbacks();
        xmlRegisterDefaultOutputCallbacks();
    }
    // the default handlers still open files
    xmlParserInputBufferPtr again = xmlParserInputBufferCreateFilename("/dev/null", XML_CHAR_ENCODING_NONE);
    stitch::check(again != nullptr, "default input callbacks are back");
    xmlFreeParserInputBuffer(again);
}
STITCH_ENDPOINT(io_callbacks);

// Every HTML serializer writes the same bytes as htmlDocDumpMemoryFormat. (Output buffers made
// here have no encoder, while the HTML serializers pick one, which writes non-ASCII characters
// as references by default: those outputs are compared when they are ASCII.)
void html_serializers(Borrow<Doc> d, Bool format) {
    stitch::require(stitch::get(d, "html", 0), "not HTML");
    xmlDocPtr doc = *d;
    int fmt = format ? 1 : 0;
    auto mem = [&](int f) {
        xmlChar* m = nullptr;
        int n = 0;
        htmlDocDumpMemoryFormat(doc, &m, &n, f);
        std::string s = m ? std::string((const char*)m, (size_t)n) : "";
        xmlFree(m);
        return s;
    };
    std::string want = mem(fmt), want1 = mem(1);
    xmlChar* m = nullptr;
    int n = 0;
    htmlDocDumpMemory(doc, &m, &n);
    stitch::check(m && std::string((const char*)m, (size_t)n) == want1, "htmlDocDumpMemory == htmlDocDumpMemoryFormat(1)");
    xmlFree(m);
    bool plain = doc->encoding == nullptr;
    std::string path = lx::tmp_path("html");
    int r = fmt ? htmlSaveFileFormat(path.c_str(), doc, nullptr, 1) : htmlSaveFile(path.c_str(), doc);
    if (r >= 0 && plain) stitch::check(lx::read_file(path) == want1, "htmlSaveFile(Format) == htmlDocDumpMemoryFormat");
    htmlSaveFileEnc(path.c_str(), doc, "UTF-8");
    if (FILE* f = fopen(path.c_str(), "wb")) {
        r = htmlDocDump(f, doc);
        fclose(f);
        if (r >= 0) stitch::check(lx::read_file(path) == want1, "htmlDocDump == htmlDocDumpMemory");
    }
    std::string out;
    xmlOutputBufferPtr ob = xmlOutputBufferCreateIO(lx::str_write, lx::str_close, &out, nullptr);
    if (fmt) htmlDocContentDumpFormatOutput(ob, doc, nullptr, 1);
    else htmlDocContentDumpOutput(ob, doc, nullptr);
    xmlOutputBufferClose(ob);
    auto ascii = [](const std::string& x) { for (unsigned char ch : x) if (ch >= 0x80) return false; return true; };
    if (plain && ascii(out)) stitch::check(out == want1, "htmlDocContentDump(Format)Output == htmlDocDumpMemoryFormat");
    if (xmlNodePtr root = xmlDocGetRootElement(doc)) {
        xmlBufferPtr b = xmlBufferCreate();
        int len = htmlNodeDump(b, doc, root);
        std::string nd = len >= 0 ? std::string((const char*)xmlBufferContent(b), (size_t)xmlBufferLength(b)) : "";
        xmlBufferFree(b);
        std::string o2;
        xmlOutputBufferPtr ob2 = xmlOutputBufferCreateIO(lx::str_write, lx::str_close, &o2, nullptr);
        htmlNodeDumpFormatOutput(ob2, doc, root, nullptr, 1);
        xmlOutputBufferClose(ob2);
        std::string o3;
        xmlOutputBufferPtr ob3 = xmlOutputBufferCreateIO(lx::str_write, lx::str_close, &o3, nullptr);
        htmlNodeDumpOutput(ob3, doc, root, nullptr);
        xmlOutputBufferClose(ob3);
        if (len >= 0) stitch::check(o2 == nd, "htmlNodeDumpFormatOutput == htmlNodeDump");
        if (FILE* f = fopen(path.c_str(), "wb")) {
            htmlNodeDumpFile(f, doc, root);
            fclose(f);
            if (ascii(o2)) stitch::check(lx::read_file(path) == o2, "htmlNodeDumpFile == htmlNodeDumpFormatOutput");
        }
        if (FILE* f = fopen(path.c_str(), "wb")) {
            int k = htmlNodeDumpFileFormat(f, doc, root, nullptr, fmt);
            fclose(f);
            if (k >= 0 && fmt && ascii(o2)) stitch::check(lx::read_file(path) == o2, "htmlNodeDumpFileFormat == htmlNodeDumpFormatOutput");
        }
        (void)o3;
    }
    unlink(path.c_str());
}
STITCH_ENDPOINT(html_serializers);

// New HTML documents with and without a DOCTYPE serialize and parse back as HTML.
New<Doc> html_new_doc(Bool with_dtd, Str<32> title) {
    xmlDocPtr d = with_dtd ? htmlNewDoc(nullptr, nullptr) : htmlNewDocNoDtD(nullptr, nullptr);
    stitch::require(d, "no document");
    stitch::check(with_dtd ? d->intSubset != nullptr : d->intSubset == nullptr, "DOCTYPE as requested");
    xmlNodePtr html = xmlNewDocNode(d, nullptr, (const xmlChar*)"html", nullptr);
    xmlDocSetRootElement(d, html);
    xmlNodePtr head = xmlNewChild(html, nullptr, (const xmlChar*)"head", nullptr);
    xmlNewTextChild(head, nullptr, (const xmlChar*)"title", (const xmlChar*)lx::clean(title.s).c_str());
    htmlSetMetaEncoding(d, (const xmlChar*)"UTF-8");
    New<Doc> out(d);
    stitch::set(out, "html", 1);
    return out;
}
STITCH_ENDPOINT(html_new_doc);

static int visible_all(void*, xmlNodePtr, xmlNodePtr) { return 1; }

// Canonical XML through an output buffer, a file and xmlC14NExecute (everything visible)
// equals xmlC14NDocDumpMemory.
void c14n_outputs(Borrow<Doc> d, Range<0, 2> mode, Bool comments) {
    stitch::require(!stitch::get(d, "html", 0) && xmlDocGetRootElement(*d) && lx::ns_wellformed(*d), "not canonicalizable");
    xmlChar* m = nullptr;
    int n = xmlC14NDocDumpMemory(*d, nullptr, (int)mode, nullptr, comments ? 1 : 0, &m);
    stitch::require(n >= 0 && m, "c14n failed");
    std::string want((const char*)m, (size_t)n);
    xmlFree(m);
    std::string got;
    xmlOutputBufferPtr ob = xmlOutputBufferCreateIO(lx::str_write, lx::str_close, &got, nullptr);
    int r = xmlC14NDocSaveTo(*d, nullptr, (int)mode, nullptr, comments ? 1 : 0, ob);
    xmlOutputBufferClose(ob);
    if (r >= 0) stitch::check(got == want, "xmlC14NDocSaveTo == xmlC14NDocDumpMemory");
    std::string ex;
    ob = xmlOutputBufferCreateIO(lx::str_write, lx::str_close, &ex, nullptr);
    r = xmlC14NExecute(*d, visible_all, nullptr, (int)mode, nullptr, comments ? 1 : 0, ob);
    xmlOutputBufferClose(ob);
    if (r >= 0) stitch::check(ex == want, "xmlC14NExecute(all visible) == xmlC14NDocDumpMemory");
    std::string path = lx::tmp_path("c14n");
    r = xmlC14NDocSave(*d, nullptr, (int)mode, nullptr, comments ? 1 : 0, path.c_str(), 0);
    if (r >= 0) stitch::check(lx::read_file(path) == want, "xmlC14NDocSave == xmlC14NDocDumpMemory");
    unlink(path.c_str());
}
STITCH_ENDPOINT(c14n_outputs);

// The debug dumpers walk every node kind.
void debug_dumpers(Borrow<Doc> d, Str<32> s) {
    FILE* f = fopen("/dev/null", "w");
    stitch::require(f, "no /dev/null");
    xmlDebugDumpString(f, (const xmlChar*)s.c_str());
    xmlDebugDumpDocumentHead(f, *d);
    xmlDebugDumpEntities(f, *d);
    if ((*d)->intSubset) xmlDebugDumpDTD(f, (*d)->intSubset);
    if (xmlNodePtr root = xmlDocGetRootElement(*d)) {
        xmlDebugDumpOneNode(f, root, 0);
        xmlDebugDumpNodeList(f, root->children, 1);
        if (root->properties) {
            xmlDebugDumpAttr(f, root->properties, 1);
            xmlDebugDumpAttrList(f, root->properties, 1);
        }
    }
    fclose(f);
}
STITCH_ENDPOINT(debug_dumpers);

// Streaming patterns: compiled safely, pushed with elements and attributes of a document.
void pattern_stream(Borrow<Doc> d, Str<64, XPathExpr> pat) {
    const xmlChar* ns[] = {(const xmlChar*)"urn:a", (const xmlChar*)"a", nullptr};
    xmlPatternPtr p = nullptr;
    int rc = xmlPatternCompileSafe((const xmlChar*)pat.c_str(), nullptr, XML_PATTERN_XPATH, ns, &p);
    stitch::require(rc == 0 && p, "not a pattern");
    xmlPatternFromRoot(p);
    xmlPatternMaxDepth(p);
    xmlPatternMinDepth(p);
    xmlStreamCtxtPtr st = xmlPatternGetStreamCtxt(p);
    if (st) {
        xmlStreamWantsAnyNode(st);
        std::vector<std::pair<xmlNodePtr, int>> stack;
        if (xmlNodePtr root = xmlDocGetRootElement(*d)) stack.push_back({root, 0});
        int steps = 0;
        // depth-first walk with push / pop
        std::function<void(xmlNodePtr)> walk = [&](xmlNodePtr n) {
            if (steps++ > 2000) return;
            if (n->type == XML_ELEMENT_NODE) {
                xmlStreamPush(st, n->name, n->ns ? n->ns->href : nullptr);
                for (xmlAttrPtr a = n->properties; a; a = a->next) {
                    xmlStreamPushAttr(st, a->name, a->ns ? a->ns->href : nullptr);
                    xmlStreamPop(st);
                }
                for (xmlNodePtr c = n->children; c; c = c->next) walk(c);
                xmlStreamPop(st);
            } else if (n->type == XML_TEXT_NODE || n->type == XML_COMMENT_NODE || n->type == XML_PI_NODE) {
                xmlStreamPushNode(st, nullptr, nullptr, n->type);
                xmlStreamPop(st);
            }
        };
        if (!stack.empty()) walk(stack[0].first);
        xmlFreeStreamCtxt(st);
    }
    xmlFreePatternList(p);
}
STITCH_ENDPOINT(pattern_stream);

// A local catalog (per parse) loaded from a file resolves the entries it lists.
void local_catalog(Str<64, UriText> sys, Str<64, UriText> uri) {
    lx::silence();
    std::string path = lx::tmp_path("catalog");
    std::string cat =
        "<catalog xmlns='urn:oasis:names:tc:entity:xmlns:xml:catalog'>"
        "<system systemId='http://example.com/a.dtd' uri='file:///a.dtd'/>"
        "<public publicId='-//X//DTD X//EN' uri='file:///x.dtd'/>"
        "<uri name='http://example.com/u' uri='file:///u'/>"
        "<rewriteSystem systemIdStartString='http://r/' rewritePrefix='file:///r/'/>"
        "</catalog>";
    FILE* f = fopen(path.c_str(), "wb");
    stitch::require(f, "cannot write a file");
    fwrite(cat.data(), 1, cat.size(), f);
    fclose(f);
    xmlCatalogAllow old = xmlCatalogGetDefaults();
    xmlCatalogSetDefaults(XML_CATA_ALLOW_DOCUMENT);
    xmlCatalogSetDebug(0);
    void* local = xmlCatalogAddLocal(nullptr, (const xmlChar*)path.c_str());
    if (local) {
        xmlChar* r = xmlCatalogLocalResolve(local, nullptr, (const xmlChar*)"http://example.com/a.dtd");
        stitch::check(r && xmlStrEqual(r, (const xmlChar*)"file:///a.dtd"), "system entry resolves");
        xmlFree(r);
        r = xmlCatalogLocalResolve(local, (const xmlChar*)"-//X//DTD X//EN", nullptr);
        stitch::check(r && xmlStrEqual(r, (const xmlChar*)"file:///x.dtd"), "public entry resolves");
        xmlFree(r);
        r = xmlCatalogLocalResolveURI(local, (const xmlChar*)"http://example.com/u");
        stitch::check(r && xmlStrEqual(r, (const xmlChar*)"file:///u"), "uri entry resolves");
        xmlFree(r);
        xmlFree(xmlCatalogLocalResolve(local, nullptr, (const xmlChar*)sys.c_str()));
        xmlFree(xmlCatalogLocalResolveURI(local, (const xmlChar*)uri.c_str()));
        xmlCatalogFreeLocal(local);
    }
    xmlCatalogSetDefaults(old);
    unlink(path.c_str());
}
STITCH_ENDPOINT(local_catalog);
