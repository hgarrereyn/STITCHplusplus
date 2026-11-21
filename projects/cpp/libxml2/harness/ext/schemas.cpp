#include "../objects.hpp"
#include "xsd.hpp"
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




void schema_parse_fuzzed(Str<1024, XmlText> text) {
    lx::silence();
    xmlSchemaParserCtxtPtr pc = xmlSchemaNewMemParserCtxt(text.data(), (int)text.size());
    xmlSchemaSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlSchemaPtr sch = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    xmlSchemaFree(sch);
}
STITCH_ENDPOINT(schema_parse_fuzzed);

void schema_value_types(Range<0, 20> t, Str<32> v) {
    static const char* types[] = {"string", "boolean", "decimal", "float", "double", "duration", "dateTime", "time", "date", "gYearMonth", "gYear", "gMonthDay",
                                  "gDay", "gMonth", "hexBinary", "base64Binary", "anyURI", "QName", "integer", "nonNegativeInteger", "language"};
    xmlSchemaTypePtr ty = xmlSchemaGetBuiltInType((xmlSchemaValType)0);
    (void)ty;
    xmlSchemaTypePtr bt = xmlSchemaGetPredefinedType((const xmlChar*)types[t.v], (const xmlChar*)"http://www.w3.org/2001/XMLSchema");
    stitch::require(bt, "no such type");
    xmlSchemaValPtr val = nullptr;
    int r = xmlSchemaValPredefTypeNode(bt, (const xmlChar*)v.c_str(), &val, nullptr);
    if (r == 0 && val) {
        const xmlChar* canon = nullptr;
        if (xmlSchemaGetCanonValue(val, &canon) == 0 && canon) {
            xmlSchemaValPtr v2 = nullptr;
            int r2 = xmlSchemaValPredefTypeNode(bt, canon, &v2, nullptr);
            stitch::check(r2 == 0, std::string("the canonical value is valid: ") + (char*)canon);
            if (r2 == 0 && v2) stitch::check(xmlSchemaCompareValues(val, v2) == 0, std::string("the canonical value compares equal: ") + (char*)canon);
            xmlSchemaFreeValue(v2);
        }
        xmlFree((void*)canon);
        xmlSchemaFreeValue(val);
    }
}
STITCH_ENDPOINT(schema_value_types);

void relaxng_validate(Range<0, 3> which, Str<512, XmlText> text) {
    lx::silence();
    static const char* rngs[] = {
        "<element name='r' xmlns='http://relaxng.org/ns/structure/1.0'><text/></element>",
        "<element name='r' xmlns='http://relaxng.org/ns/structure/1.0'><zeroOrMore><element name='a'><attribute name='k'/></element></zeroOrMore></element>",
        "<element name='r' xmlns='http://relaxng.org/ns/structure/1.0' datatypeLibrary='http://www.w3.org/2001/XMLSchema-datatypes'><choice><data type='int'/><value>x</value></choice></element>",
        "<grammar xmlns='http://relaxng.org/ns/structure/1.0'><start><ref name='n'/></start><define name='n'><element><anyName/><interleave><optional><ref name='n'/></optional><text/></interleave></element></define></grammar>"};
    xmlRelaxNGParserCtxtPtr pc = xmlRelaxNGNewMemParserCtxt(rngs[which.v], (int)strlen(rngs[which.v]));
    xmlRelaxNGSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlRelaxNGPtr g = xmlRelaxNGParse(pc);
    xmlRelaxNGFreeParserCtxt(pc);
    stitch::check(g != nullptr, "the built-in grammar parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && g) {
        xmlRelaxNGValidCtxtPtr vc = xmlRelaxNGNewValidCtxt(g);
        xmlRelaxNGSetValidStructuredErrors(vc, [](void*, LX_ERR) {}, nullptr);
        int a = xmlRelaxNGValidateDoc(vc, d);
        int b = xmlRelaxNGValidateDoc(vc, d);
        stitch::check((a == 0) == (b == 0), "validating twice gives the same verdict");
        xmlRelaxNGFreeValidCtxt(vc);
    }
    xmlFreeDoc(d); xmlRelaxNGFree(g);
}
STITCH_ENDPOINT(relaxng_validate);

void relaxng_parse_fuzzed(Str<1024, XmlText> text) {
    lx::silence();
    xmlRelaxNGParserCtxtPtr pc = xmlRelaxNGNewMemParserCtxt(text.data(), (int)text.size());
    xmlRelaxNGSetParserStructuredErrors(pc, [](void*, LX_ERR) {}, nullptr);
    xmlRelaxNGPtr g = xmlRelaxNGParse(pc);
    xmlRelaxNGFreeParserCtxt(pc);
    xmlRelaxNGFree(g);
}
STITCH_ENDPOINT(relaxng_parse_fuzzed);

void schematron_validate(Str<512, XmlText> text) {
    lx::silence();
    static const char* sch = "<schema xmlns='http://purl.oclc.org/dsdl/schematron'><pattern><rule context='r'><assert test='count(*) &lt; 3'>too many</assert><report test='@x'>has x</report></rule></pattern></schema>";
    xmlSchematronParserCtxtPtr pc = xmlSchematronNewMemParserCtxt(sch, (int)strlen(sch));
    xmlSchematronPtr s = xmlSchematronParse(pc);
    xmlSchematronFreeParserCtxt(pc);
    stitch::check(s != nullptr, "the schematron schema parses");
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d && s) { xmlSchematronValidCtxtPtr vc = xmlSchematronNewValidCtxt(s, XML_SCHEMATRON_OUT_QUIET); xmlSchematronValidateDoc(vc, d); xmlSchematronFreeValidCtxt(vc); }
    xmlFreeDoc(d); xmlSchematronFree(s);
}
STITCH_ENDPOINT(schematron_validate);

void validate_name_value(Str<24, XmlName> v) {
    int r = xmlValidateNameValue((const xmlChar*)v.c_str());
    stitch::check(r == (xmlValidateName((const xmlChar*)v.c_str(), 0) == 0), "xmlValidateNameValue agrees with xmlValidateName");
}
STITCH_ENDPOINT(validate_name_value);

void validate_names_value(Str<24, XmlName> v) {
    int r = xmlValidateNamesValue((const xmlChar*)v.c_str());
    (void)r;
}
STITCH_ENDPOINT(validate_names_value);

void validate_nmtoken_value(Str<24, XmlName> v) {
    int r = xmlValidateNmtokenValue((const xmlChar*)v.c_str());
    stitch::check(r == (xmlValidateNMToken((const xmlChar*)v.c_str(), 0) == 0), "xmlValidateNmtokenValue agrees with xmlValidateNMToken");
}
STITCH_ENDPOINT(validate_nmtoken_value);

void validate_nmtokens_value(Str<24, XmlName> v) {
    int r = xmlValidateNmtokensValue((const xmlChar*)v.c_str());
    (void)r;
}
STITCH_ENDPOINT(validate_nmtokens_value);

void validate_qname_ncname(Str<24, XmlName> v) {
    int nc = xmlValidateNCName((const xmlChar*)v.c_str(), 0), q = xmlValidateQName((const xmlChar*)v.c_str(), 0), n = xmlValidateName((const xmlChar*)v.c_str(), 0);
    if (nc == 0) stitch::check(q == 0 && n == 0, "an NCName is a QName and a Name");
    if (q == 0) stitch::check(n == 0, "a QName is a Name");
}
STITCH_ENDPOINT(validate_qname_ncname);

void dtd_validate_doc(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, XML_PARSE_DTDATTR | lx::kQuiet);
    stitch::require(d && d->intSubset, "no DTD");
    xmlValidCtxtPtr v = xmlNewValidCtxt(); v->error = nullptr; v->warning = nullptr;
    xmlValidateDtd(v, d, d->intSubset);
    xmlValidateDocumentFinal(v, d);
    xmlValidateRoot(v, d);
    xmlElementPtr* e = nullptr; (void)e;
    xmlValidGetValidElements(nullptr, nullptr, nullptr, 0);
    xmlFreeValidCtxt(v);
    xmlDtdPtr copy = xmlCopyDtd(d->intSubset);
    xmlFreeDtd(copy);
    xmlFreeDoc(d);
}
STITCH_ENDPOINT(dtd_validate_doc);
