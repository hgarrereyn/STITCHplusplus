// xmlTextReader: every way to create or reuse a reader reads the same node sequence; the
// attribute API agrees with itself; reader state, properties, error handlers and validation.
#include "../objects.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <libxml/relaxng.h>
#include <libxml/xmlschemas.h>

namespace {
// the node sequence a reader produces: type, depth, name, value
std::string events(xmlTextReaderPtr r, int* status) {
    std::string out;
    int ret = 0, n = 0;
    while (n++ < 20000 && (ret = xmlTextReaderRead(r)) == 1) {
        out += std::to_string(xmlTextReaderNodeType(r)) + "/" + std::to_string(xmlTextReaderDepth(r)) + "/" +
               lx::str(xmlTextReaderConstName(r)) + "=" + lx::str(xmlTextReaderConstValue(r)) + "\n";
    }
    *status = ret;
    return out;
}
}  // namespace

// Readers from memory, a string, a file descriptor, callbacks, a file, an input buffer, and
// readers reused with xmlReaderNew* all produce the same node sequence.
void reader_sources(Str<4096, XmlText> text, Range<0, 12> how, Flags<XML_PARSE_NOENT, XML_PARSE_NOBLANKS, XML_PARSE_NOCDATA, XML_PARSE_NSCLEAN> opts) {
    lx::silence();
    std::string t = text.s.substr(0, strlen(text.c_str()));  // the string variants stop at NUL
    int o = (int)opts | lx::kQuiet;
    bool no_opts = (int)how == 5 || (int)how == 11;  // readers made without options
    xmlTextReaderPtr ref = xmlReaderForMemory(t.data(), (int)t.size(), "in.xml", nullptr, no_opts ? lx::kQuiet : o);
    stitch::require(ref, "no reader");
    int ref_status = 0;
    std::string want = events(ref, &ref_status);
    xmlFreeTextReader(ref);
    std::string path = lx::tmp_path("reader");
    lx::StrIO io{t};
    xmlTextReaderPtr r = nullptr;
    int fd = -1;
    bool reuse = (int)how >= 6 && (int)how != 11;
    if (reuse || (int)how == 2 || (int)how == 4 || (int)how == 11) stitch::require(lx::write_file(path, t), "cannot write a file");
    if (reuse) {
        r = xmlReaderForMemory("<old/>", 6, "old.xml", nullptr, 0);
        stitch::require(r, "no reader");
        while (xmlTextReaderRead(r) == 1) {}
    }
    int rc = 0;
    switch ((int)how) {
        case 0: r = xmlReaderForMemory(t.data(), (int)t.size(), "in.xml", nullptr, o); break;
        case 1: r = xmlReaderForDoc((const xmlChar*)t.c_str(), "in.xml", nullptr, o); break;
        case 2: fd = open(path.c_str(), O_RDONLY); r = fd >= 0 ? xmlReaderForFd(fd, "in.xml", nullptr, o) : nullptr; break;
        case 3: r = xmlReaderForIO(lx::io_read, lx::io_close, &io, "in.xml", nullptr, o); break;
        case 4: r = xmlReaderForFile(path.c_str(), nullptr, o); break;
        case 5: {
            xmlParserInputBufferPtr in = xmlParserInputBufferCreateStatic(t.data(), (int)t.size(), XML_CHAR_ENCODING_NONE);
            r = in ? xmlNewTextReader(in, "in.xml") : nullptr;  // (the reader does not own `in`)
            if (!r && in) xmlFreeParserInputBuffer(in);
            if (r) {
                int st = 0;
                std::string got = events(r, &st);
                xmlFreeTextReader(r);
                xmlFreeParserInputBuffer(in);
                if (ref_status == 0) stitch::check(st == 0 && got == want, "same node sequence from an input buffer");
                return;
            }
            break;
        }
        case 6: rc = xmlReaderNewMemory(r, t.data(), (int)t.size(), "in.xml", nullptr, o); break;
        case 7: rc = xmlReaderNewDoc(r, (const xmlChar*)t.c_str(), "in.xml", nullptr, o); break;
        case 8: fd = open(path.c_str(), O_RDONLY); rc = fd >= 0 ? xmlReaderNewFd(r, fd, "in.xml", nullptr, o) : -1; break;
        case 9: rc = xmlReaderNewIO(r, lx::io_read, lx::io_close, &io, "in.xml", nullptr, o); break;
        case 10: rc = xmlReaderNewFile(r, path.c_str(), nullptr, o); break;
        case 11: r = xmlNewTextReaderFilename(path.c_str()); break;
        default: rc = xmlTextReaderSetup(r, xmlParserInputBufferCreateMem(t.data(), (int)t.size(), XML_CHAR_ENCODING_NONE), "in.xml", nullptr, o); break;
    }
    if (!r || rc != 0) {
        if (r) xmlFreeTextReader(r);
        if (fd >= 0) close(fd);
        unlink(path.c_str());
        stitch::bail("reader creation failed");
    }
    int status = 0;
    std::string got = events(r, &status);
    xmlFreeTextReader(r);
    if (fd >= 0) close(fd);
    unlink(path.c_str());
    // (the URL differs for the file readers: base URIs are not part of the events; malformed
    // input may fail at different points depending on how the input arrives)
    if (ref_status == 0) {
        stitch::check(status == 0, "same final status");
        stitch::check(got == want, "same node sequence");
    }
}
STITCH_ENDPOINT(reader_sources);

// On every element of a namespace-well-formed document, the attribute accessors agree:
// by index, by name, by namespace, by moving, and the allocating vs constant getters.
void reader_attributes(Str<4096, XmlText> text, Range<0, 200> stop) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    stitch::require(r, "no reader");
    int n = 0;
    auto same = [](xmlChar* owned, const xmlChar* c) {
        bool eq = xmlStrEqual(owned, c);
        xmlFree(owned);
        return eq;
    };
    while (n++ < 5000 && xmlTextReaderRead(r) == 1) {
        stitch::check(same(xmlTextReaderName(r), xmlTextReaderConstName(r)), "Name == ConstName");
        stitch::check(same(xmlTextReaderLocalName(r), xmlTextReaderConstLocalName(r)), "LocalName == ConstLocalName");
        stitch::check(same(xmlTextReaderPrefix(r), xmlTextReaderConstPrefix(r)), "Prefix == ConstPrefix");
        stitch::check(same(xmlTextReaderNamespaceUri(r), xmlTextReaderConstNamespaceUri(r)), "NamespaceUri == ConstNamespaceUri");
        stitch::check(same(xmlTextReaderBaseUri(r), xmlTextReaderConstBaseUri(r)), "BaseUri == ConstBaseUri");
        stitch::check(same(xmlTextReaderXmlLang(r), xmlTextReaderConstXmlLang(r)), "XmlLang == ConstXmlLang");
        stitch::check(same(xmlTextReaderValue(r), xmlTextReaderConstValue(r)), "Value == ConstValue");
        stitch::check(xmlTextReaderHasValue(r) == 0 || xmlTextReaderHasValue(r) == 1, "HasValue is a boolean");
        stitch::check(xmlStrEqual(xmlTextReaderConstString(r, (const xmlChar*)"abc"), (const xmlChar*)"abc"), "ConstString interns");
        if (n == (int)stop) xmlTextReaderNormalization(r);
        if (xmlTextReaderNodeType(r) != XML_READER_TYPE_ELEMENT) continue;
        xmlDocPtr doc = xmlTextReaderCurrentDoc(r);
        bool plain = doc && !doc->intSubset;  // (DTD defaults and entities make values differ by source)
        if (const xmlChar* pfx = xmlTextReaderConstPrefix(r)) {
            xmlChar* uri = xmlTextReaderLookupNamespace(r, pfx);
            stitch::check(xmlStrEqual(uri, xmlTextReaderConstNamespaceUri(r)), "LookupNamespace(prefix) is the element's namespace");
            xmlFree(uri);
        }
        int count = xmlTextReaderAttributeCount(r);
        stitch::check((count > 0) == (xmlTextReaderHasAttributes(r) == 1), "HasAttributes iff AttributeCount > 0");
        std::vector<std::string> names, values, locals, uris;
        std::vector<bool> undeclared;
        for (int i = 0; i < count; i++) {
            xmlChar* byno = xmlTextReaderGetAttributeNo(r, i);
            stitch::check(xmlTextReaderMoveToAttributeNo(r, i) == 1, "MoveToAttributeNo");
            stitch::check(xmlTextReaderNodeType(r) == XML_READER_TYPE_ATTRIBUTE, "on an attribute");
            if (plain) stitch::check(xmlStrEqual(byno, xmlTextReaderConstValue(r)), "GetAttributeNo == value after MoveToAttributeNo");
            names.push_back(lx::str(xmlTextReaderConstName(r)));
            values.push_back(lx::str(xmlTextReaderConstValue(r)));
            locals.push_back(lx::str(xmlTextReaderConstLocalName(r)));
            bool decl = xmlTextReaderIsNamespaceDecl(r) == 1;
            uris.push_back(decl ? "" : lx::str(xmlTextReaderConstNamespaceUri(r)));
            // a prefixed name without a namespace: the prefix is undeclared (namespace error)
            undeclared.push_back(!decl && names.back().find(':') != std::string::npos && uris.back().empty());
            xmlTextReaderIsDefault(r);
            xmlFree(byno);
            stitch::check(xmlTextReaderMoveToElement(r) == 1 && xmlTextReaderNodeType(r) == XML_READER_TYPE_ELEMENT, "MoveToElement");
        }
        stitch::check(xmlTextReaderGetAttributeNo(r, count) == nullptr && xmlTextReaderMoveToAttributeNo(r, count) == 0, "index past the end");
        if (count > 0) {
            stitch::check(xmlTextReaderMoveToFirstAttribute(r) == 1 && lx::str(xmlTextReaderConstName(r)) == names[0], "MoveToFirstAttribute");
            int k = 1;
            while (xmlTextReaderMoveToNextAttribute(r) == 1) k++;
            stitch::check(k == count, "MoveToNextAttribute visits every attribute");
            xmlTextReaderMoveToElement(r);
        }
        if (!plain) continue;
        for (int i = 0; i < count; i++) {
            if (undeclared[i]) continue;
            xmlChar* v = xmlTextReaderGetAttribute(r, (const xmlChar*)names[i].c_str());
            stitch::check(v && values[i] == (const char*)v, "GetAttribute(name)");
            xmlFree(v);
            stitch::check(xmlTextReaderMoveToAttribute(r, (const xmlChar*)names[i].c_str()) == 1 &&
                              lx::str(xmlTextReaderConstValue(r)) == values[i], "MoveToAttribute(name)");
            xmlTextReaderMoveToElement(r);
            if (!uris[i].empty()) {
                v = xmlTextReaderGetAttributeNs(r, (const xmlChar*)locals[i].c_str(), (const xmlChar*)uris[i].c_str());
                stitch::check(v && values[i] == (const char*)v, "GetAttributeNs(local, uri)");
                xmlFree(v);
                stitch::check(xmlTextReaderMoveToAttributeNs(r, (const xmlChar*)locals[i].c_str(), (const xmlChar*)uris[i].c_str()) == 1,
                              "MoveToAttributeNs");
                xmlTextReaderMoveToElement(r);
            }
        }
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_attributes);

static void reader_err(void* arg, const char*, xmlParserSeverities, xmlTextReaderLocatorPtr loc) {
    ++*(int*)arg;
    stitch::check(xmlTextReaderLocatorLineNumber(loc) >= -1, "locator line");
    xmlFree(xmlTextReaderLocatorBaseURI(loc));
}
static void reader_serr(void* arg, const xmlError*) { ++*(int*)arg; }

// Reader state: parser properties, positions, byte counts, preserved subtrees, error handlers,
// the last error, closing.
void reader_state(Str<4096, XmlText> text, Range<0, 3> prop, Bool value, Bool structured, Str<32, XPathExpr> pattern) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_NONET);
    stitch::require(r, "no reader");
    int errors = 0;
    if (structured) xmlTextReaderSetStructuredErrorHandler(r, reader_serr, &errors);
    else {
        xmlTextReaderSetErrorHandler(r, reader_err, &errors);
        xmlTextReaderErrorFunc f = nullptr;
        void* arg = nullptr;
        xmlTextReaderGetErrorHandler(r, &f, &arg);
        stitch::check(f == reader_err && arg == &errors, "GetErrorHandler returns the handler");
    }
    xmlTextReaderSetMaxAmplification(r, 20);
    xmlTextReaderSetResourceLoader(r, lx::mem_loader, nullptr);
    static const int props[] = {XML_PARSER_LOADDTD, XML_PARSER_DEFAULTATTRS, XML_PARSER_VALIDATE, XML_PARSER_SUBST_ENTITIES};
    int set = xmlTextReaderSetParserProp(r, props[(int)prop], value ? 1 : 0);
    if (set == 0) stitch::check(xmlTextReaderGetParserProp(r, props[(int)prop]) == (value ? 1 : 0), "GetParserProp after SetParserProp");
    const xmlChar* ns[] = {nullptr};
    int pat = xmlTextReaderPreservePattern(r, (const xmlChar*)pattern.c_str(), ns);
    long last = -1;
    int ret, n = 0;
    while (n++ < 5000 && (ret = xmlTextReaderRead(r)) == 1) {
        stitch::check(xmlTextReaderGetParserLineNumber(r) >= 0 && xmlTextReaderGetParserColumnNumber(r) >= 0, "position");
        long b = xmlTextReaderByteConsumed(r);
        stitch::check(b >= last || b < 0, "ByteConsumed does not go back");
        if (b >= 0) last = b;
        int mode = xmlTextReaderReadState(r);
        stitch::check(mode == XML_TEXTREADER_MODE_INTERACTIVE || mode == XML_TEXTREADER_MODE_EOF, "interactive while reading");
        if (n == 3 && xmlTextReaderNodeType(r) == XML_READER_TYPE_ELEMENT) xmlTextReaderPreserve(r);
    }
    if (ret < 0) {
        const xmlError* e = xmlTextReaderGetLastError(r);
        stitch::check(e != nullptr && e->code != 0, "a failed read leaves an error");
    }
    stitch::log("pattern %d, errors %d", pat, errors);
    if (xmlDocPtr d = xmlTextReaderCurrentDoc(r)) {  // the reader gives up the document
        std::string s = lx::dump(d, 0);
        xmlFreeDoc(d);
    }
    stitch::check(xmlTextReaderClose(r) == 0, "Close");
    stitch::check(xmlTextReaderReadState(r) == XML_TEXTREADER_MODE_CLOSED, "closed");
    stitch::check(xmlTextReaderRead(r) != 1, "no reads after Close");
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_state);

// Validating while reading, with a schema given as a file or a validation context.
void reader_validate(Str<2048, XmlText> text, Str<2048, XmlText> schema, Range<0, 3> how) {
    lx::silence();
    std::string path = lx::tmp_path("schema");
    bool rng = (int)how % 2 == 0;
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    stitch::require(r, "no reader");
    int rc = -1;
    xmlRelaxNGPtr rs = nullptr;
    xmlRelaxNGValidCtxtPtr rv = nullptr;
    xmlSchemaPtr xs = nullptr;
    xmlSchemaValidCtxtPtr xv = nullptr;
    if ((int)how < 2) {
        if (lx::write_file(path, schema.s))
            rc = rng ? xmlTextReaderRelaxNGValidate(r, path.c_str()) : xmlTextReaderSchemaValidate(r, path.c_str());
    } else if (rng) {
        xmlRelaxNGParserCtxtPtr p = xmlRelaxNGNewMemParserCtxt(schema.data(), (int)schema.size());
        rs = p ? xmlRelaxNGParse(p) : nullptr;
        xmlRelaxNGFreeParserCtxt(p);
        if (rs && (rv = xmlRelaxNGNewValidCtxt(rs))) rc = xmlTextReaderRelaxNGValidateCtxt(r, rv, 0);
    } else {
        xmlSchemaParserCtxtPtr p = xmlSchemaNewMemParserCtxt(schema.data(), (int)schema.size());
        xs = p ? xmlSchemaParse(p) : nullptr;
        xmlSchemaFreeParserCtxt(p);
        if (xs && (xv = xmlSchemaNewValidCtxt(xs))) rc = xmlTextReaderSchemaValidateCtxt(r, xv, 0);
    }
    unlink(path.c_str());
    if (rc == 0) {
        int n = 0;
        while (n++ < 5000 && xmlTextReaderRead(r) == 1) {}
        int v = xmlTextReaderIsValid(r);
        stitch::check(v >= -1 && v <= 1, "IsValid");
    }
    xmlFreeTextReader(r);
    if (rv) xmlRelaxNGFreeValidCtxt(rv);
    if (rs) xmlRelaxNGFree(rs);
    if (xv) xmlSchemaFreeValidCtxt(xv);
    if (xs) xmlSchemaFree(xs);
    stitch::require(rc == 0, "schema not usable");
}
STITCH_ENDPOINT(reader_validate);
