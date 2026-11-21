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

void text_writer(Src<128> s, Bool indent) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlTextWriterPtr w = xmlNewTextWriterMemory(b, 0);
    xmlTextWriterSetIndent(w, indent ? 1 : 0);
    xmlTextWriterStartDocument(w, nullptr, s->coin() ? "UTF-8" : nullptr, nullptr);
    int depth = 0;
    const char* names[] = {"a", "b", "x:y", "item"};
    xmlTextWriterStartElement(w, (const xmlChar*)"root"); depth++;
    while (s->more()) {
        std::string t = lx::clean(s->str(12));
        switch (s->choice(9)) {
            case 0: xmlTextWriterStartElement(w, (const xmlChar*)names[s->choice(4)]); depth++; break;
            case 1: if (depth > 1) { xmlTextWriterEndElement(w); depth--; } break;
            case 2: xmlTextWriterWriteAttribute(w, (const xmlChar*)s->pick({"k", "id", "xml:lang"}), (const xmlChar*)t.c_str()); break;
            case 3: xmlTextWriterWriteString(w, (const xmlChar*)t.c_str()); break;
            case 4: if (t.find("--") == std::string::npos && (t.empty() || t.back() != '-')) xmlTextWriterWriteComment(w, (const xmlChar*)t.c_str()); break;
            case 5: if (t.find("]]>") == std::string::npos) xmlTextWriterWriteCDATA(w, (const xmlChar*)t.c_str()); break;
            case 6: xmlTextWriterWriteElement(w, (const xmlChar*)names[s->choice(4)], (const xmlChar*)t.c_str()); break;
            case 7: if (t.find("?>") == std::string::npos) xmlTextWriterWritePI(w, (const xmlChar*)"pi", (const xmlChar*)t.c_str()); break;
            default: xmlTextWriterWriteFormatString(w, "%d", (int)s->range(-100, 100)); break;
        }
    }
    xmlTextWriterEndDocument(w);
    xmlFreeTextWriter(w);
    std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
    stitch::log("written: %s", out.substr(0, 400).c_str());
    xmlDocPtr d = xmlReadMemory(out.data(), (int)out.size(), "w.xml", nullptr, lx::kQuiet);
    stitch::check(d != nullptr, "the text writer's output parses");
    xmlFreeDoc(d); xmlBufferFree(b);
}
STITCH_ENDPOINT(text_writer);

void text_writer_dtd(Src<64> s) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlTextWriterPtr w = xmlNewTextWriterMemory(b, 0);
    xmlTextWriterStartDocument(w, nullptr, nullptr, nullptr);
    xmlTextWriterStartDTD(w, (const xmlChar*)"r", nullptr, nullptr);
    while (s->more()) {
        switch (s->choice(4)) {
            case 0: xmlTextWriterWriteDTDElement(w, (const xmlChar*)"r", (const xmlChar*)s->pick({"(#PCDATA)", "ANY", "EMPTY", "(a|b)*"})); break;
            case 1: xmlTextWriterWriteDTDAttlist(w, (const xmlChar*)"r", (const xmlChar*)s->pick({"x CDATA #IMPLIED", "id ID #REQUIRED"})); break;
            case 2: { std::string v = lx::clean(s->str(8)); if (v.find('"') == std::string::npos && v.find('%') == std::string::npos && v.find('&') == std::string::npos) xmlTextWriterWriteDTDInternalEntity(w, 0, (const xmlChar*)s->pick({"e", "f"}), (const xmlChar*)v.c_str()); break; }
            default: xmlTextWriterWriteDTDExternalEntity(w, 0, (const xmlChar*)"ext", nullptr, (const xmlChar*)"x.xml", nullptr); break;
        }
    }
    xmlTextWriterEndDTD(w);
    xmlTextWriterWriteElement(w, (const xmlChar*)"r", (const xmlChar*)"t");
    xmlTextWriterEndDocument(w);
    xmlFreeTextWriter(w);
    std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
    stitch::log("written: %s", out.c_str());
    xmlDocPtr d = xmlReadMemory(out.data(), (int)out.size(), "w.xml", nullptr, lx::kQuiet);
    stitch::check(d != nullptr, "a written DTD parses");
    xmlFreeDoc(d); xmlBufferFree(b);
}
STITCH_ENDPOINT(text_writer_dtd);

void text_writer_doc(Borrow<Doc> d) {
    xmlTextWriterPtr w = xmlNewTextWriterTree(*d, xmlDocGetRootElement(*d), 0);
    stitch::require(w, "no writer");
    xmlTextWriterStartElement(w, (const xmlChar*)"added");
    xmlTextWriterWriteString(w, (const xmlChar*)"text & more");
    xmlTextWriterEndElement(w);
    xmlFreeTextWriter(w);
}
STITCH_ENDPOINT(text_writer_doc);
