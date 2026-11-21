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

void reader_constlocalname(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderConstLocalName(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_constlocalname);

void reader_constprefix(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderConstPrefix(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_constprefix);

void reader_constnamespaceuri(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderConstNamespaceUri(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_constnamespaceuri);

void reader_constbaseuri(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderConstBaseUri(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_constbaseuri);

void reader_constxmllang(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderConstXmlLang(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_constxmllang);

void reader_readinnerxml(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) { xmlChar* x = xmlTextReaderReadInnerXml(r); xmlFree(x); }
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_readinnerxml);

void reader_readouterxml(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) { xmlChar* x = xmlTextReaderReadOuterXml(r); xmlFree(x); }
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_readouterxml);

void reader_readstring(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) { xmlChar* x = xmlTextReaderReadString(r); xmlFree(x); }
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_readstring);

void reader_nodetype(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderNodeType(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_nodetype);

void reader_isemptyelement(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderIsEmptyElement(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_isemptyelement);

void reader_attributecount(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderAttributeCount(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_attributecount);

void reader_getparserlinenumber(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderGetParserLineNumber(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_getparserlinenumber);

void reader_quotechar(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderQuoteChar(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_quotechar);

void reader_isnamespacedecl(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderIsNamespaceDecl(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_isnamespacedecl);

void reader_preserve(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderPreserve(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_preserve);

void reader_currentnode(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderCurrentNode(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_currentnode);

void reader_next(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderNext(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_next);

void reader_nextsibling(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderNextSibling(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_nextsibling);

void reader_readattributevalue(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderReadAttributeValue(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_readattributevalue);

void reader_movetoelement(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderMoveToElement(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_movetoelement);

void reader_getremainder(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) { xmlParserInputBufferPtr rb = xmlTextReaderGetRemainder(r); xmlFreeParserInputBuffer(rb); break; }
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_getremainder);

void reader_readstate(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderReadState(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_readstate);

void reader_isvalid(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderIsValid(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_isvalid);

void reader_standalone(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderStandalone(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_standalone);

void reader_constxmlversion(Str<2048, XmlText> text, lx::ParseOpts opts, Range<0, 50> every) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    int k = 0, depth = 0;
    while (k++ < 5000) {
        int ret = xmlTextReaderRead(r);
        if (ret != 1) break;
        int dd = xmlTextReaderDepth(r);
        
        stitch::check(dd >= 0, "depth >= 0");
        depth = dd;
        if (every.v == 0 || k % (every.v + 1) == 0) (void)xmlTextReaderConstXmlVersion(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_constxmlversion);

void reader_walker(Borrow<Doc> d) {
    xmlTextReaderPtr r = xmlReaderWalker(*d);
    stitch::require(r, "no walker");
    int n = 0;
    while (xmlTextReaderRead(r) == 1 && n < 10000) { n++; xmlTextReaderConstName(r); xmlTextReaderConstValue(r); if (xmlTextReaderHasAttributes(r)) while (xmlTextReaderMoveToNextAttribute(r) == 1) xmlTextReaderConstValue(r); }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader_walker);

void reader_vs_dom(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    stitch::require(d && r, "not well-formed");
    int elems = 0;
    
    elems = lx::count_elements(d);
    int seen = 0, ret;
    while ((ret = xmlTextReaderRead(r)) == 1) if (xmlTextReaderNodeType(r) == XML_READER_TYPE_ELEMENT) seen++;
    stitch::check(ret == 0, "the reader reads a well-formed document to the end");
    stitch::check(seen == elems, "the reader sees as many elements as the DOM has");
    xmlFreeTextReader(r); xmlFreeDoc(d);
}
STITCH_ENDPOINT(reader_vs_dom);
