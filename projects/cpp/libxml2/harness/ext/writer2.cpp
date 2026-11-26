// xmlTextWriter: every writer kind (memory, output buffer, file, document, push parser) driven
// through a legal sequence of calls, covering the Start/End pairs, the Format / VFormat variants,
// namespaces, raw / Base64 / BinHex content and the DTD writers. Its output must be well-formed.
#include "../objects.hpp"
#include <cstdarg>
#include <set>
#include <unistd.h>
#include <libxml/xmlwriter.h>

namespace {
int vf(int (*f)(xmlTextWriterPtr, const char*, va_list), xmlTextWriterPtr w, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = f(w, fmt, ap);
    va_end(ap);
    return r;
}
int vfn(int (*f)(xmlTextWriterPtr, const xmlChar*, const char*, va_list), xmlTextWriterPtr w, const xmlChar* n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = f(w, n, fmt, ap);
    va_end(ap);
    return r;
}
int vfns(int (*f)(xmlTextWriterPtr, const xmlChar*, const xmlChar*, const xmlChar*, const char*, va_list), xmlTextWriterPtr w,
         const xmlChar* p, const xmlChar* n, const xmlChar* u, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = f(w, p, n, u, fmt, ap);
    va_end(ap);
    return r;
}
int write_cb(void* ctx, const char* buf, int len) {
    ((std::string*)ctx)->append(buf, (size_t)len);
    return len;
}
int close_cb(void*) { return 0; }
const xmlChar* X(const char* s) { return (const xmlChar*)s; }
// text that is safe everywhere: XML characters, no markup or quote characters
std::string safe(stitch::Source& s, size_t n) {
    std::string t = lx::clean(s.str(n)), o;
    for (char c : t)
        if (c != '<' && c != '>' && c != '&' && c != '"' && c != '\'' && c != '-' && c != '?' && c != ']' && c != '%') o += c;
    return o;
}
}  // namespace

// sink: 0 memory buffer, 1 output buffer with callbacks, 2 file, 3 new document, 4 push parser
void text_writer_session(Src<256> s, Range<0, 4> sink, Bool indent, Bool single_quotes, Bool with_dtd) {
    lx::silence();
    std::string out;
    xmlBufferPtr buf = nullptr;
    xmlDocPtr doc = nullptr;
    xmlParserCtxtPtr push = nullptr;
    std::string path = "/tmp/stitch_lx_writer_" + std::to_string(getpid()) + ".xml";
    xmlTextWriterPtr w = nullptr;
    switch ((int)sink) {
        case 0: buf = xmlBufferCreate(); w = xmlNewTextWriterMemory(buf, 0); break;
        case 1: w = xmlNewTextWriter(xmlOutputBufferCreateIO(write_cb, close_cb, &out, nullptr)); break;
        case 2: w = xmlNewTextWriterFilename(path.c_str(), 0); break;
        case 3: w = xmlNewTextWriterDoc(&doc, 0); break;
        default:
            push = xmlCreatePushParserCtxt(nullptr, nullptr, nullptr, 0, "push.xml");
            if (push) xmlCtxtUseOptions(push, lx::kQuiet);
            w = push ? xmlNewTextWriterPushParser(push, 0) : nullptr;
            break;
    }
    if (!w) {
        if (buf) xmlBufferFree(buf);
        if (push) xmlFreeParserCtxt(push);
        stitch::bail("no writer");
    }
    int failed = 0;
    auto ok = [&](int r) { if (r < 0) failed++; };
    ok(xmlTextWriterSetIndent(w, indent ? 1 : 0));
    if (indent) ok(xmlTextWriterSetIndentString(w, X(s->pick({" ", "\t", "  "}))));
    ok(xmlTextWriterSetQuoteChar(w, single_quotes ? '\'' : '"'));
    ok(xmlTextWriterStartDocument(w, nullptr, s->coin() ? "UTF-8" : nullptr, s->coin() ? "yes" : nullptr));
    if (with_dtd) {
        switch (s->choice(3)) {
            case 0:
                ok(xmlTextWriterWriteDTD(w, X("root"), nullptr, nullptr, X("<!ELEMENT root ANY>")));
                break;
            case 1:
                ok(vfns([](xmlTextWriterPtr w, const xmlChar* n, const xmlChar* p, const xmlChar* sy, const char* f, va_list ap) {
                            return xmlTextWriterWriteVFormatDTD(w, n, p, sy, f, ap);
                        }, w, X("root"), nullptr, X("r.dtd"), "<!ELEMENT root %s>", "ANY"));
                break;
            default:
                ok(xmlTextWriterStartDTD(w, X("root"), X("-//X//EN"), X("r.dtd")));
                ok(xmlTextWriterStartDTDElement(w, X("root")));
                ok(xmlTextWriterWriteString(w, X("ANY")));
                ok(xmlTextWriterEndDTDElement(w));
                ok(xmlTextWriterWriteFormatDTDElement(w, X("e1"), "(#PCDATA%s)", ""));
                ok(vfn([](xmlTextWriterPtr w, const xmlChar* n, const char* f, va_list ap) { return xmlTextWriterWriteVFormatDTDElement(w, n, f, ap); },
                       w, X("v"), "%s", "EMPTY"));
                ok(xmlTextWriterStartDTDAttlist(w, X("root")));
                ok(xmlTextWriterWriteString(w, X("x CDATA #IMPLIED")));
                ok(xmlTextWriterEndDTDAttlist(w));
                ok(xmlTextWriterWriteFormatDTDAttlist(w, X("root"), "y%d CDATA \"%d\"", 1, 2));
                ok(vfn([](xmlTextWriterPtr w, const xmlChar* n, const char* f, va_list ap) { return xmlTextWriterWriteVFormatDTDAttlist(w, n, f, ap); },
                       w, X("root"), "z CDATA %s", "#IMPLIED"));
                ok(xmlTextWriterStartDTDEntity(w, 0, X("ent")));
                ok(xmlTextWriterWriteString(w, X(safe(*s, 8).c_str())));
                ok(xmlTextWriterEndDTDEntity(w));
                ok(xmlTextWriterWriteFormatDTDInternalEntity(w, 0, X("num"), "%d", (int)s->range(0, 99)));
                ok(xmlTextWriterWriteDTDInternalEntity(w, 1, X("pe"), X("x")));
                ok(xmlTextWriterWriteDTDEntity(w, 0, X("ext"), nullptr, X("ext.xml"), nullptr, nullptr));
                ok(xmlTextWriterWriteDTDNotation(w, X("gif"), X("-//GIF//EN"), nullptr));
                ok(xmlTextWriterWriteDTDExternalEntity(w, 0, X("pic"), nullptr, X("p.gif"), X("gif")));
                ok(xmlTextWriterStartDTDEntity(w, 0, X("ext2")));
                ok(xmlTextWriterWriteDTDExternalEntityContents(w, X("-//E//EN"), X("e2.xml"), nullptr));
                ok(xmlTextWriterEndDTDEntity(w));
                ok(xmlTextWriterEndDTD(w));
        }
    }
    // the document element, then a scripted body
    static const char* names[] = {"a", "b", "item", "c"};
    static const char* prefixes[] = {"p", "q"};
    static const char* uris[] = {"urn:p", "urn:q"};
    std::vector<std::set<std::string>> attrs(1);  // attribute names on each open start tag
    int depth = 1;
    bool tag_open = true;
    ok(xmlTextWriterStartElement(w, X("root")));
    auto fresh_attr = [&](const std::string& qn) {
        if (!tag_open || attrs.back().count(qn)) return false;
        attrs.back().insert(qn);
        return true;
    };
    int ops = 0;
    while (s->more() && ops++ < 60 && failed == 0) {
        std::string t = safe(*s, 12);
        int k = (int)s->choice(24);
        int pi = (int)s->choice(2);
        const xmlChar* P = X(prefixes[pi]);
        const xmlChar* U = X(uris[pi]);
        const char* nm = names[s->choice(4)];
        switch (k) {
            case 0: ok(xmlTextWriterStartElement(w, X(nm))); depth++; attrs.emplace_back(); tag_open = true; break;
            case 1:
                ok(xmlTextWriterStartElementNS(w, P, X(nm), U));
                depth++; attrs.emplace_back(); attrs.back().insert(std::string("xmlns:") + prefixes[pi]); tag_open = true;
                break;
            case 2: case 3:
                if (depth > 1) { ok(k == 2 ? xmlTextWriterEndElement(w) : xmlTextWriterFullEndElement(w)); depth--; attrs.pop_back(); tag_open = false; }
                break;
            case 4: if (fresh_attr(nm)) ok(xmlTextWriterWriteAttribute(w, X(nm), X(t.c_str()))); break;
            case 5: if (fresh_attr(nm)) ok(xmlTextWriterWriteFormatAttribute(w, X(nm), "%s-%d", t.c_str(), (int)s->range(0, 9))); break;
            case 6:
                if (fresh_attr(nm))
                    ok(vfn([](xmlTextWriterPtr w, const xmlChar* n, const char* f, va_list ap) { return xmlTextWriterWriteVFormatAttribute(w, n, f, ap); },
                           w, X(nm), "%x", (unsigned)s->u32()));
                break;
            case 7:
                if (fresh_attr(std::string(prefixes[pi]) + ":" + nm)) {
                    attrs.back().insert(std::string("xmlns:") + prefixes[pi]);
                    switch (s->choice(3)) {
                        case 0: ok(xmlTextWriterWriteAttributeNS(w, P, X(nm), U, X(t.c_str()))); break;
                        case 1: ok(xmlTextWriterWriteFormatAttributeNS(w, P, X(nm), U, "%d", (int)s->range(0, 99))); break;
                        default:
                            ok(vfns([](xmlTextWriterPtr w, const xmlChar* p, const xmlChar* n, const xmlChar* u, const char* f, va_list ap) {
                                        return xmlTextWriterWriteVFormatAttributeNS(w, p, n, u, f, ap);
                                    }, w, P, X(nm), U, "%s", t.c_str()));
                    }
                }
                break;
            case 8:
                if (fresh_attr(nm)) {
                    ok(xmlTextWriterStartAttribute(w, X(nm)));
                    ok(xmlTextWriterWriteString(w, X(t.c_str())));
                    ok(xmlTextWriterWriteFormatString(w, "%d", (int)s->range(0, 9)));
                    ok(xmlTextWriterEndAttribute(w));
                }
                break;
            case 9:
                if (fresh_attr(std::string(prefixes[pi]) + ":" + nm)) {
                    attrs.back().insert(std::string("xmlns:") + prefixes[pi]);
                    ok(xmlTextWriterStartAttributeNS(w, P, X(nm), U));
                    ok(xmlTextWriterWriteString(w, X(t.c_str())));
                    ok(xmlTextWriterEndAttribute(w));
                }
                break;
            case 10: ok(xmlTextWriterWriteElement(w, X(nm), X(t.c_str()))); tag_open = false; break;
            case 11: ok(xmlTextWriterWriteElementNS(w, P, X(nm), U, X(t.c_str()))); tag_open = false; break;
            case 12: ok(xmlTextWriterWriteFormatElement(w, X(nm), "%d", (int)s->range(-9, 9))); tag_open = false; break;
            case 13:
                ok(vfn([](xmlTextWriterPtr w, const xmlChar* n, const char* f, va_list ap) { return xmlTextWriterWriteVFormatElement(w, n, f, ap); },
                       w, X(nm), "%s", t.c_str()));
                tag_open = false;
                break;
            case 14:
                if (s->coin()) ok(xmlTextWriterWriteFormatElementNS(w, P, X(nm), U, "%s", t.c_str()));
                else ok(vfns([](xmlTextWriterPtr w, const xmlChar* p, const xmlChar* n, const xmlChar* u, const char* f, va_list ap) {
                                 return xmlTextWriterWriteVFormatElementNS(w, p, n, u, f, ap);
                             }, w, P, X(nm), U, "%d", 5));
                tag_open = false;
                break;
            case 15:
                switch (s->choice(3)) {
                    case 0: ok(xmlTextWriterStartComment(w)); ok(xmlTextWriterWriteString(w, X(t.c_str()))); ok(xmlTextWriterEndComment(w)); break;
                    case 1: ok(xmlTextWriterWriteFormatComment(w, "c%s", t.c_str())); break;
                    default: ok(vf(xmlTextWriterWriteVFormatComment, w, "%d", 3)); break;
                }
                tag_open = false;
                break;
            case 16:
                switch (s->choice(3)) {
                    case 0: ok(xmlTextWriterStartPI(w, X("pi"))); ok(xmlTextWriterWriteString(w, X(t.c_str()))); ok(xmlTextWriterEndPI(w)); break;
                    case 1: ok(xmlTextWriterWriteFormatPI(w, X("pi"), "%s", t.c_str())); break;
                    default:
                        ok(vfn([](xmlTextWriterPtr w, const xmlChar* n, const char* f, va_list ap) { return xmlTextWriterWriteVFormatPI(w, n, f, ap); },
                               w, X("pi"), "%d", 7));
                }
                tag_open = false;
                break;
            case 17:
                switch (s->choice(3)) {
                    case 0: ok(xmlTextWriterStartCDATA(w)); ok(xmlTextWriterWriteString(w, X(t.c_str()))); ok(xmlTextWriterEndCDATA(w)); break;
                    case 1: ok(xmlTextWriterWriteFormatCDATA(w, "<%s>", t.c_str())); break;
                    default: ok(vf(xmlTextWriterWriteVFormatCDATA, w, "&%d", 1)); break;
                }
                tag_open = false;
                break;
            case 18: ok(vf(xmlTextWriterWriteVFormatString, w, "%s&%d", t.c_str(), 1)); tag_open = false; break;
            case 19:
                switch (s->choice(3)) {
                    case 0: ok(xmlTextWriterWriteRaw(w, X(t.c_str()))); break;
                    case 1: ok(xmlTextWriterWriteRawLen(w, X(t.c_str()), (int)t.size())); break;
                    default: ok(xmlTextWriterWriteFormatRaw(w, "&amp;%d", 2)); break;
                }
                if (s->coin()) ok(vf(xmlTextWriterWriteVFormatRaw, w, "%s", "&lt;"));
                tag_open = false;
                break;
            case 20: {
                std::string bin = s->str(16);
                int start = bin.empty() ? 0 : (int)s->choice(bin.size());
                ok(xmlTextWriterWriteBase64(w, bin.data(), start, (int)bin.size() - start));
                tag_open = false;
                break;
            }
            case 21: {
                std::string bin = s->str(16);
                int start = bin.empty() ? 0 : (int)s->choice(bin.size());
                ok(xmlTextWriterWriteBinHex(w, bin.data(), start, (int)bin.size() - start));
                tag_open = false;
                break;
            }
            case 22: ok(xmlTextWriterWriteString(w, X(t.c_str()))); tag_open = false; break;
            default: ok(xmlTextWriterFlush(w)); break;
        }
    }
    ok(xmlTextWriterEndDocument(w));
    if ((int)sink == 1) ok(-xmlTextWriterClose(w));
    // the push parser writer owns the parser context: closing the output ends the parse
    bool push_ok = false;
    if ((int)sink == 4) {
        xmlTextWriterClose(w);
        push_ok = push->wellFormed && push->myDoc;
    }
    xmlFreeTextWriter(w);
    stitch::log("failed calls: %d", failed);
    switch ((int)sink) {
        case 0: out.assign((const char*)xmlBufferContent(buf), (size_t)xmlBufferLength(buf)); xmlBufferFree(buf); break;
        case 2: {
            FILE* f = fopen(path.c_str(), "rb");
            if (f) {
                char tmp[4096];
                size_t k;
                while ((k = fread(tmp, 1, sizeof tmp, f)) > 0) out.append(tmp, k);
                fclose(f);
            }
            unlink(path.c_str());
            break;
        }
        default: break;
    }
    if ((int)sink == 3) {
        if (failed == 0) stitch::check(doc && xmlDocGetRootElement(doc), "the writer built a document");
        xmlFreeDoc(doc);
        return;
    }
    if ((int)sink == 4) {
        if (failed == 0) stitch::check(push_ok, "the push parser accepted the writer's output");
        return;
    }
    stitch::log("written: %s", out.substr(0, 600).c_str());
    if (failed) return;
    xmlDocPtr d = xmlReadMemory(out.data(), (int)out.size(), "w.xml", nullptr, lx::kQuiet);
    stitch::check(d != nullptr, "the text writer's output parses");
    xmlFreeDoc(d);
}
STITCH_ENDPOINT(text_writer_session);
