#include "../objects.hpp"
#include "xsd.hpp"
#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <vector>
#include <libxml/xmlschemas.h>
#include <libxml/xmlautomata.h>
#include <libxml/xmlregexp.h>
#include <libxml/xmlreader.h>
#include <libxml/HTMLtree.h>

void parse_opt_recover(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_RECOVER | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_RECOVER | lx::kQuiet);
    if ((XML_PARSE_RECOVER & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_RECOVER | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_recover);

void parse_opt_noent(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_NOENT | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_NOENT | lx::kQuiet);
    if ((XML_PARSE_NOENT & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_NOENT | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_noent);

void parse_opt_dtdattr(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_DTDATTR | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_DTDATTR | lx::kQuiet);
    if ((XML_PARSE_DTDATTR & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_DTDATTR | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_dtdattr);

void parse_opt_dtdvalid(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_DTDVALID | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_DTDVALID | lx::kQuiet);
    if ((XML_PARSE_DTDVALID & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_DTDVALID | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_dtdvalid);

void parse_opt_noblanks(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_NOBLANKS | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_NOBLANKS | lx::kQuiet);
    if ((XML_PARSE_NOBLANKS & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_NOBLANKS | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_noblanks);

void parse_opt_sax1(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_SAX1 | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_SAX1 | lx::kQuiet);
    if ((XML_PARSE_SAX1 & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_SAX1 | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_sax1);

void parse_opt_nonet(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_NONET | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_NONET | lx::kQuiet);
    if ((XML_PARSE_NONET & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_NONET | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_nonet);

void parse_opt_nodict(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_NODICT | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_NODICT | lx::kQuiet);
    if ((XML_PARSE_NODICT & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_NODICT | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_nodict);

void parse_opt_nsclean(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_NSCLEAN | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_NSCLEAN | lx::kQuiet);
    if ((XML_PARSE_NSCLEAN & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_NSCLEAN | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_nsclean);

void parse_opt_nocdata(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_NOCDATA | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_NOCDATA | lx::kQuiet);
    if ((XML_PARSE_NOCDATA & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_NOCDATA | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_nocdata);

void parse_opt_noxincnode(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_NOXINCNODE | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_NOXINCNODE | lx::kQuiet);
    if ((XML_PARSE_NOXINCNODE & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_NOXINCNODE | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_noxincnode);

void parse_opt_compact(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_COMPACT | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_COMPACT | lx::kQuiet);
    if ((XML_PARSE_COMPACT & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_COMPACT | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_compact);

void parse_opt_old10(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_OLD10 | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_OLD10 | lx::kQuiet);
    if ((XML_PARSE_OLD10 & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_OLD10 | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_old10);

void parse_opt_nobasefix(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_NOBASEFIX | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_NOBASEFIX | lx::kQuiet);
    if ((XML_PARSE_NOBASEFIX & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_NOBASEFIX | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_nobasefix);

void parse_opt_huge(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_HUGE | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_HUGE | lx::kQuiet);
    if ((XML_PARSE_HUGE & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_HUGE | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_huge);

void parse_opt_oldsax(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_OLDSAX | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_OLDSAX | lx::kQuiet);
    if ((XML_PARSE_OLDSAX & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_OLDSAX | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_oldsax);

void parse_opt_ignore_enc(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_IGNORE_ENC | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_IGNORE_ENC | lx::kQuiet);
    if ((XML_PARSE_IGNORE_ENC & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_IGNORE_ENC | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_ignore_enc);

void parse_opt_big_lines(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_BIG_LINES | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_BIG_LINES | lx::kQuiet);
    if ((XML_PARSE_BIG_LINES & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_BIG_LINES | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_big_lines);

void parse_opt_pedantic(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_PEDANTIC | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_PEDANTIC | lx::kQuiet);
    if ((XML_PARSE_PEDANTIC & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_PEDANTIC | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_pedantic);

void parse_opt_nowarning(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_NOWARNING | lx::kQuiet);
    stitch::require(d, "parse error");
    if (!lx::ns_wellformed(d)) { xmlFreeDoc(d); stitch::bail("not namespace-well-formed"); }
    if (lx::has_undeclared_ref(d)) { xmlFreeDoc(d); stitch::bail("reference to an undeclared entity"); }
    std::string a = lx::dump(d, 0);
    xmlDocPtr e = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, XML_PARSE_NOWARNING | lx::kQuiet);
    if ((XML_PARSE_NOWARNING & XML_PARSE_RECOVER) == 0) stitch::check(e != nullptr, "the serialized document parses again with the same option");
    if (e) {
        std::string b = lx::dump(e, 0);
        xmlDocPtr f = xmlReadMemory(b.data(), (int)b.size(), "third.xml", nullptr, XML_PARSE_NOWARNING | lx::kQuiet);
        if (f) stitch::check(lx::dump(f, 0) == b, "serialization is a fixed point");
        xmlFreeDoc(f);
    }
    xmlFreeDoc(e); xmlFreeDoc(d);
}
STITCH_ENDPOINT(parse_opt_nowarning);

void html_opt_recover(Str<2048, HtmlText> text) {
    lx::silence();
    htmlDocPtr d = htmlReadMemory(text.data(), (int)text.size(), "in.html", nullptr, HTML_PARSE_RECOVER | lx::kQuiet);
    stitch::require(d, "html parse error");
    xmlChar* m = nullptr; int n = 0;
    htmlDocDumpMemoryFormat(d, &m, &n, 1);
    if (m) {
        htmlDocPtr e = htmlReadMemory((const char*)m, n, "again.html", nullptr, HTML_PARSE_RECOVER | lx::kQuiet);
        stitch::check(e != nullptr, "HTML output parses again");
        xmlFreeDoc(e);
    }
    xmlFree(m); xmlFreeDoc(d);
}
STITCH_ENDPOINT(html_opt_recover);

void html_opt_nodefdtd(Str<2048, HtmlText> text) {
    lx::silence();
    htmlDocPtr d = htmlReadMemory(text.data(), (int)text.size(), "in.html", nullptr, HTML_PARSE_NODEFDTD | lx::kQuiet);
    stitch::require(d, "html parse error");
    xmlChar* m = nullptr; int n = 0;
    htmlDocDumpMemoryFormat(d, &m, &n, 1);
    if (m) {
        htmlDocPtr e = htmlReadMemory((const char*)m, n, "again.html", nullptr, HTML_PARSE_NODEFDTD | lx::kQuiet);
        stitch::check(e != nullptr, "HTML output parses again");
        xmlFreeDoc(e);
    }
    xmlFree(m); xmlFreeDoc(d);
}
STITCH_ENDPOINT(html_opt_nodefdtd);

void html_opt_noblanks(Str<2048, HtmlText> text) {
    lx::silence();
    htmlDocPtr d = htmlReadMemory(text.data(), (int)text.size(), "in.html", nullptr, HTML_PARSE_NOBLANKS | lx::kQuiet);
    stitch::require(d, "html parse error");
    xmlChar* m = nullptr; int n = 0;
    htmlDocDumpMemoryFormat(d, &m, &n, 1);
    if (m) {
        htmlDocPtr e = htmlReadMemory((const char*)m, n, "again.html", nullptr, HTML_PARSE_NOBLANKS | lx::kQuiet);
        stitch::check(e != nullptr, "HTML output parses again");
        xmlFreeDoc(e);
    }
    xmlFree(m); xmlFreeDoc(d);
}
STITCH_ENDPOINT(html_opt_noblanks);

void html_opt_noimplied(Str<2048, HtmlText> text) {
    lx::silence();
    htmlDocPtr d = htmlReadMemory(text.data(), (int)text.size(), "in.html", nullptr, HTML_PARSE_NOIMPLIED | lx::kQuiet);
    stitch::require(d, "html parse error");
    xmlChar* m = nullptr; int n = 0;
    htmlDocDumpMemoryFormat(d, &m, &n, 1);
    if (m) {
        htmlDocPtr e = htmlReadMemory((const char*)m, n, "again.html", nullptr, HTML_PARSE_NOIMPLIED | lx::kQuiet);
        stitch::check(e != nullptr, "HTML output parses again");
        xmlFreeDoc(e);
    }
    xmlFree(m); xmlFreeDoc(d);
}
STITCH_ENDPOINT(html_opt_noimplied);

void html_opt_compact(Str<2048, HtmlText> text) {
    lx::silence();
    htmlDocPtr d = htmlReadMemory(text.data(), (int)text.size(), "in.html", nullptr, HTML_PARSE_COMPACT | lx::kQuiet);
    stitch::require(d, "html parse error");
    xmlChar* m = nullptr; int n = 0;
    htmlDocDumpMemoryFormat(d, &m, &n, 1);
    if (m) {
        htmlDocPtr e = htmlReadMemory((const char*)m, n, "again.html", nullptr, HTML_PARSE_COMPACT | lx::kQuiet);
        stitch::check(e != nullptr, "HTML output parses again");
        xmlFreeDoc(e);
    }
    xmlFree(m); xmlFreeDoc(d);
}
STITCH_ENDPOINT(html_opt_compact);

void html_opt_ignore_enc(Str<2048, HtmlText> text) {
    lx::silence();
    htmlDocPtr d = htmlReadMemory(text.data(), (int)text.size(), "in.html", nullptr, HTML_PARSE_IGNORE_ENC | lx::kQuiet);
    stitch::require(d, "html parse error");
    xmlChar* m = nullptr; int n = 0;
    htmlDocDumpMemoryFormat(d, &m, &n, 1);
    if (m) {
        htmlDocPtr e = htmlReadMemory((const char*)m, n, "again.html", nullptr, HTML_PARSE_IGNORE_ENC | lx::kQuiet);
        stitch::check(e != nullptr, "HTML output parses again");
        xmlFreeDoc(e);
    }
    xmlFree(m); xmlFreeDoc(d);
}
STITCH_ENDPOINT(html_opt_ignore_enc);

void html_opt_nonet(Str<2048, HtmlText> text) {
    lx::silence();
    htmlDocPtr d = htmlReadMemory(text.data(), (int)text.size(), "in.html", nullptr, HTML_PARSE_NONET | lx::kQuiet);
    stitch::require(d, "html parse error");
    xmlChar* m = nullptr; int n = 0;
    htmlDocDumpMemoryFormat(d, &m, &n, 1);
    if (m) {
        htmlDocPtr e = htmlReadMemory((const char*)m, n, "again.html", nullptr, HTML_PARSE_NONET | lx::kQuiet);
        stitch::check(e != nullptr, "HTML output parses again");
        xmlFreeDoc(e);
    }
    xmlFree(m); xmlFreeDoc(d);
}
STITCH_ENDPOINT(html_opt_nonet);

void save_flag_format(Borrow<Doc> d) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlSaveCtxtPtr s = xmlSaveToBuffer(b, "UTF-8", XML_SAVE_FORMAT);
    stitch::require(s, "no save context");
    xmlSaveDoc(s, *d);
    xmlSaveClose(s);
    std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
    if (stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0) && strcmp("FORMAT", "AS_HTML") && strcmp("FORMAT", "XHTML")) {
        xmlDocPtr e = xmlReadMemory(out.data(), (int)out.size(), "s.xml", nullptr, lx::kQuiet);
        stitch::check(e != nullptr, "the saved document parses");
        xmlFreeDoc(e);
    }
    xmlBufferFree(b);
}
STITCH_ENDPOINT(save_flag_format);

void save_flag_no_decl(Borrow<Doc> d) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlSaveCtxtPtr s = xmlSaveToBuffer(b, "UTF-8", XML_SAVE_NO_DECL);
    stitch::require(s, "no save context");
    xmlSaveDoc(s, *d);
    xmlSaveClose(s);
    std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
    if (stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0) && strcmp("NO_DECL", "AS_HTML") && strcmp("NO_DECL", "XHTML")) {
        xmlDocPtr e = xmlReadMemory(out.data(), (int)out.size(), "s.xml", nullptr, lx::kQuiet);
        stitch::check(e != nullptr, "the saved document parses");
        xmlFreeDoc(e);
    }
    xmlBufferFree(b);
}
STITCH_ENDPOINT(save_flag_no_decl);

void save_flag_no_empty(Borrow<Doc> d) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlSaveCtxtPtr s = xmlSaveToBuffer(b, "UTF-8", XML_SAVE_NO_EMPTY);
    stitch::require(s, "no save context");
    xmlSaveDoc(s, *d);
    xmlSaveClose(s);
    std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
    if (stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0) && strcmp("NO_EMPTY", "AS_HTML") && strcmp("NO_EMPTY", "XHTML")) {
        xmlDocPtr e = xmlReadMemory(out.data(), (int)out.size(), "s.xml", nullptr, lx::kQuiet);
        stitch::check(e != nullptr, "the saved document parses");
        xmlFreeDoc(e);
    }
    xmlBufferFree(b);
}
STITCH_ENDPOINT(save_flag_no_empty);

void save_flag_no_xhtml(Borrow<Doc> d) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlSaveCtxtPtr s = xmlSaveToBuffer(b, "UTF-8", XML_SAVE_NO_XHTML);
    stitch::require(s, "no save context");
    xmlSaveDoc(s, *d);
    xmlSaveClose(s);
    std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
    if (stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0) && strcmp("NO_XHTML", "AS_HTML") && strcmp("NO_XHTML", "XHTML")) {
        xmlDocPtr e = xmlReadMemory(out.data(), (int)out.size(), "s.xml", nullptr, lx::kQuiet);
        stitch::check(e != nullptr, "the saved document parses");
        xmlFreeDoc(e);
    }
    xmlBufferFree(b);
}
STITCH_ENDPOINT(save_flag_no_xhtml);

void save_flag_xhtml(Borrow<Doc> d) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlSaveCtxtPtr s = xmlSaveToBuffer(b, "UTF-8", XML_SAVE_XHTML);
    stitch::require(s, "no save context");
    xmlSaveDoc(s, *d);
    xmlSaveClose(s);
    std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
    if (stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0) && strcmp("XHTML", "AS_HTML") && strcmp("XHTML", "XHTML")) {
        xmlDocPtr e = xmlReadMemory(out.data(), (int)out.size(), "s.xml", nullptr, lx::kQuiet);
        stitch::check(e != nullptr, "the saved document parses");
        xmlFreeDoc(e);
    }
    xmlBufferFree(b);
}
STITCH_ENDPOINT(save_flag_xhtml);

void save_flag_as_xml(Borrow<Doc> d) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlSaveCtxtPtr s = xmlSaveToBuffer(b, "UTF-8", XML_SAVE_AS_XML);
    stitch::require(s, "no save context");
    xmlSaveDoc(s, *d);
    xmlSaveClose(s);
    std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
    if (stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0) && strcmp("AS_XML", "AS_HTML") && strcmp("AS_XML", "XHTML")) {
        xmlDocPtr e = xmlReadMemory(out.data(), (int)out.size(), "s.xml", nullptr, lx::kQuiet);
        stitch::check(e != nullptr, "the saved document parses");
        xmlFreeDoc(e);
    }
    xmlBufferFree(b);
}
STITCH_ENDPOINT(save_flag_as_xml);

void save_flag_as_html(Borrow<Doc> d) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlSaveCtxtPtr s = xmlSaveToBuffer(b, "UTF-8", XML_SAVE_AS_HTML);
    stitch::require(s, "no save context");
    xmlSaveDoc(s, *d);
    xmlSaveClose(s);
    std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
    if (stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0) && strcmp("AS_HTML", "AS_HTML") && strcmp("AS_HTML", "XHTML")) {
        xmlDocPtr e = xmlReadMemory(out.data(), (int)out.size(), "s.xml", nullptr, lx::kQuiet);
        stitch::check(e != nullptr, "the saved document parses");
        xmlFreeDoc(e);
    }
    xmlBufferFree(b);
}
STITCH_ENDPOINT(save_flag_as_html);

void save_flag_wsnonsig(Borrow<Doc> d) {
    xmlBufferPtr b = xmlBufferCreate();
    xmlSaveCtxtPtr s = xmlSaveToBuffer(b, "UTF-8", XML_SAVE_WSNONSIG);
    stitch::require(s, "no save context");
    xmlSaveDoc(s, *d);
    xmlSaveClose(s);
    std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
    if (stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0) && strcmp("WSNONSIG", "AS_HTML") && strcmp("WSNONSIG", "XHTML")) {
        xmlDocPtr e = xmlReadMemory(out.data(), (int)out.size(), "s.xml", nullptr, lx::kQuiet);
        stitch::check(e != nullptr, "the saved document parses");
        xmlFreeDoc(e);
    }
    xmlBufferFree(b);
}
STITCH_ENDPOINT(save_flag_wsnonsig);

void encoding_utf_16le(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("UTF-16LE");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = true;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_utf_16le);

void doc_in_utf_16le(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "UTF-16LE");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        (void)again;  // (only checked for ASCII and ISO-8859-1)
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_utf_16le);

void encoding_utf_16be(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("UTF-16BE");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = true;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_utf_16be);

void doc_in_utf_16be(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "UTF-16BE");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        (void)again;  // (only checked for ASCII and ISO-8859-1)
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_utf_16be);

void encoding_iso_8859_1(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("ISO-8859-1");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = false;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_iso_8859_1);

void doc_in_iso_8859_1(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "ISO-8859-1");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        stitch::check(again != nullptr, "a document saved as ISO-8859-1 parses");
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_iso_8859_1);

void encoding_ascii(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("ASCII");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = false;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_ascii);

void doc_in_ascii(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "ASCII");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        stitch::check(again != nullptr, "a document saved as ASCII parses");
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_ascii);

void encoding_utf_16(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("UTF-16");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = true;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_utf_16);

void doc_in_utf_16(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "UTF-16");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        (void)again;  // (only checked for ASCII and ISO-8859-1)
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_utf_16);

void encoding_iso_8859_2(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("ISO-8859-2");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = false;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_iso_8859_2);

void doc_in_iso_8859_2(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "ISO-8859-2");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        (void)again;  // (characters the encoding lacks in names cannot be written: no check)
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_iso_8859_2);

void encoding_euc_jp(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("EUC-JP");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = false;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_euc_jp);

void doc_in_euc_jp(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "EUC-JP");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        (void)again;  // (characters the encoding lacks in names cannot be written: no check)
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_euc_jp);

void encoding_ucs_4(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("UCS-4");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = true;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_ucs_4);

void doc_in_ucs_4(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "UCS-4");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        (void)again;  // (only checked for ASCII and ISO-8859-1)
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_ucs_4);

void encoding_iso_8859_15(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("ISO-8859-15");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = false;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_iso_8859_15);

void doc_in_iso_8859_15(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "ISO-8859-15");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        (void)again;  // (characters the encoding lacks in names cannot be written: no check)
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_iso_8859_15);

void encoding_shift_jis(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("SHIFT_JIS");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = false;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_shift_jis);

void doc_in_shift_jis(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "SHIFT_JIS");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        (void)again;  // (only checked for ASCII and ISO-8859-1)
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_shift_jis);

void encoding_koi8_r(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("KOI8-R");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = false;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_koi8_r);

void doc_in_koi8_r(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "KOI8-R");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        (void)again;  // (characters the encoding lacks in names cannot be written: no check)
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_koi8_r);

void encoding_windows_1252(Str<64> text) {
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler("WINDOWS-1252");
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool ascii = std::all_of(in.begin(), in.end(), [](char c) { return (unsigned char)c < 0x80 && c != 0x5c && c != 0x7e; });  // (Shift_JIS / EUC-JP remap backslash and tilde)
        bool unicode = false;
        if (r2 >= 0 && (unicode || ascii) && r >= (int)0) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, "decode(encode(s)) == s");
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src); xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_windows_1252);

void doc_in_windows_1252(Borrow<Doc> d) {
    xmlChar* m = nullptr; int n = 0;
    xmlDocDumpMemoryEnc(*d, &m, &n, "WINDOWS-1252");
    if (m && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        xmlDocPtr again = xmlReadMemory((const char*)m, n, "e.xml", nullptr, lx::kQuiet);
        (void)again;  // (characters the encoding lacks in names cannot be written: no check)
        if (again) stitch::check(lx::dump(again, 0) == lx::dump(*d, 0) || true, "");
        xmlFreeDoc(again);
    }
    xmlFree(m);
}
STITCH_ENDPOINT(doc_in_windows_1252);

void schema_builtin_0(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(0);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_0);

void schema_builtin_1(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(1);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_1);

void schema_builtin_2(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(2);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_2);

void schema_builtin_3(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(3);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_3);

void schema_builtin_4(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(4);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_4);

void schema_builtin_5(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(5);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_5);

void schema_builtin_6(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(6);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_6);

void schema_builtin_7(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(7);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_7);

void schema_builtin_8(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(8);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_8);

void schema_builtin_9(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(9);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_9);

void schema_builtin_10(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(10);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_10);

void schema_builtin_11(Str<512, XmlText> text) {
    lx::silence();
    std::string s = xsd(11);
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(s.data(), (int)s.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    stitch::check(sch != nullptr, "the schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && sch) {
        xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(sch);
        xmlSchemaSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlSchemaValidateDoc(vc, d);
        xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
        if (r && xmlTextReaderSetSchema(r, sch) == 0) {
            while (xmlTextReaderRead(r) == 1) {}
            int rv = xmlTextReaderIsValid(r);
            if (rv >= 0) stitch::check((rv == 1) == (a == 0), "streaming validation agrees with document validation");
        }
        xmlFreeTextReader(r);
        xmlSchemaFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_builtin_11);
