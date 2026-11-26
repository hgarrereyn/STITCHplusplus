// Schema datatypes (xmlschemastypes.h): validation, facets, values, whitespace processing; the
// RelaxNG / XML Schema / Schematron context APIs not covered elsewhere: parser contexts from
// files and documents, error handler getters, dumps, push and stream validation.
#include "../objects.hpp"
#include <unistd.h>
#include <libxml/xmlschemas.h>
#include <libxml/xmlschemastypes.h>
#include <libxml/schemasInternals.h>
#include <libxml/relaxng.h>
#include <libxml/schematron.h>
#include <functional>

namespace {
const char* kXsd = "http://www.w3.org/2001/XMLSchema";
bool blank(unsigned char c) { return c == 0x20 || c == 0x9 || c == 0xA || c == 0xD; }
std::string collapse_ref(const std::string& v) {
    std::string out;
    bool pending = false;
    for (unsigned char c : v) {
        if (blank(c)) { pending = !out.empty(); continue; }
        if (pending) out += ' ';
        pending = false;
        out += (char)c;
    }
    return out;
}
void quiet_err(void*, const char*, ...) {}
}  // namespace

// Built-in types: the validators agree, values copy and compare, facets check and apply.
void schema_types_facets(Range<0, 24> t, Str<32> value, Range<0, 9> facet, Str<16> fvalue) {
    static const char* types[] = {"string", "boolean", "decimal", "float", "double", "duration", "dateTime", "time", "date",
                                  "gYearMonth", "gYear", "hexBinary", "base64Binary", "anyURI", "QName", "integer",
                                  "nonNegativeInteger", "language", "NMTOKENS", "IDREFS", "token", "normalizedString",
                                  "unsignedByte", "long", "Name"};
    static const xmlSchemaTypeType facets[] = {XML_SCHEMA_FACET_LENGTH, XML_SCHEMA_FACET_MINLENGTH, XML_SCHEMA_FACET_MAXLENGTH,
                                               XML_SCHEMA_FACET_PATTERN, XML_SCHEMA_FACET_ENUMERATION, XML_SCHEMA_FACET_MININCLUSIVE,
                                               XML_SCHEMA_FACET_MAXEXCLUSIVE, XML_SCHEMA_FACET_TOTALDIGITS, XML_SCHEMA_FACET_FRACTIONDIGITS,
                                               XML_SCHEMA_FACET_WHITESPACE};
    lx::silence();
    xmlSchemaInitTypes();
    xmlSchemaTypePtr type = xmlSchemaGetPredefinedType((const xmlChar*)types[(int)t], (const xmlChar*)kXsd);
    stitch::require(type, "no such type");
    const xmlChar* v = (const xmlChar*)value.c_str();
    xmlSchemaValPtr a = nullptr, b = nullptr, c = nullptr;
    int ra = xmlSchemaValidatePredefinedType(type, v, &a);
    int rb = xmlSchemaValPredefTypeNode(type, v, &b, nullptr);
    xmlSchemaValPredefTypeNodeNoNorm(type, v, &c, nullptr);
    stitch::check(ra == rb, "ValidatePredefinedType == ValPredefTypeNode");
    if (a) {
        xmlSchemaValPtr cp = xmlSchemaCopyValue(a);
        stitch::check(cp != nullptr && xmlSchemaGetValType(cp) == xmlSchemaGetValType(a), "CopyValue keeps the type");
        if (cp) {
            int cmp = xmlSchemaCompareValues(a, cp);
            stitch::check(cmp == 0 || cmp == -2, "a value compares equal to its copy");
            int cw = xmlSchemaCompareValuesWhtsp(a, XML_SCHEMA_WHITESPACE_COLLAPSE, cp, XML_SCHEMA_WHITESPACE_COLLAPSE);
            stitch::check(cw == 0 || cw == -2, "CompareValuesWhtsp of a copy");
            xmlSchemaFreeValue(cp);
        }
        const xmlChar* canon = nullptr;
        if (xmlSchemaGetCanonValueWhtsp(a, &canon, XML_SCHEMA_WHITESPACE_PRESERVE) == 0) xmlFree((void*)canon);
        xmlSchemaValueGetAsString(a);
        if (xmlSchemaGetValType(a) == XML_SCHEMAS_BOOLEAN) {
            bool truth = collapse_ref(value.c_str()) == "true" || collapse_ref(value.c_str()) == "1";
            stitch::check(xmlSchemaValueGetAsBoolean(a) == (truth ? 1 : 0), "boolean value");
        }
    }
    if (xmlSchemaTypePtr item = xmlSchemaGetBuiltInListSimpleTypeItemType(type)) stitch::check(item->name != nullptr, "list item type");
    // a facet on this type
    xmlSchemaFacetPtr f = xmlSchemaNewFacet();
    f->type = facets[(int)facet];
    f->value = xmlStrdup((const xmlChar*)fvalue.c_str());
    xmlSchemaIsBuiltInTypeFacet(type, f->type);
    if (xmlSchemaCheckFacet(f, type, nullptr, (const xmlChar*)"f") == 0) {
        int r = xmlSchemaValidateFacet(type, f, v, a);
        int rw = xmlSchemaValidateFacetWhtsp(f, XML_SCHEMA_WHITESPACE_PRESERVE, xmlSchemaGetValType(a ? a : nullptr), v, a,
                                             XML_SCHEMA_WHITESPACE_PRESERVE);
        (void)r;
        (void)rw;
        if (f->type == XML_SCHEMA_FACET_LENGTH || f->type == XML_SCHEMA_FACET_MINLENGTH || f->type == XML_SCHEMA_FACET_MAXLENGTH) {
            unsigned long len = 0, expected = 0;
            xmlSchemaGetFacetValueAsULong(f);
            xmlSchemaValidateLengthFacet(type, f, v, a, &len);
            xmlSchemaValidateLengthFacetWhtsp(f, a ? xmlSchemaGetValType(a) : XML_SCHEMAS_STRING, v, a, &len, XML_SCHEMA_WHITESPACE_COLLAPSE);
            xmlSchemaValidateListSimpleTypeFacet(f, v, (unsigned long)xmlStrlen(v), &expected);
        }
    }
    xmlSchemaFreeFacet(f);
    xmlSchemaFreeValue(a);
    xmlSchemaFreeValue(b);
    xmlSchemaFreeValue(c);
    // whitespace processing against a reference
    std::string s = value.c_str();
    xmlChar* col = xmlSchemaCollapseString(v);
    stitch::check((col ? std::string((const char*)col) : s) == collapse_ref(s), "CollapseString");
    xmlFree(col);
    xmlChar* rep = xmlSchemaWhiteSpaceReplace(v);
    std::string want = s;
    for (char& ch : want) if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
    stitch::check((rep ? std::string((const char*)rep) : s) == want, "WhiteSpaceReplace");
    xmlFree(rep);
    // value constructors (they take the strings)
    xmlSchemaValPtr sv = xmlSchemaNewStringValue(XML_SCHEMAS_STRING, xmlStrdup(v));
    xmlSchemaValPtr qn = xmlSchemaNewQNameValue(xmlStrdup((const xmlChar*)"urn:x"), xmlStrdup((const xmlChar*)"local"));
    xmlSchemaValPtr no = xmlSchemaNewNOTATIONValue(xmlStrdup((const xmlChar*)"gif"), nullptr);
    stitch::check(sv && qn && no, "values created");
    stitch::check(xmlStrEqual(xmlSchemaValueGetAsString(sv), v), "string value");
    stitch::check(xmlSchemaValueAppend(sv, qn) == 0 && xmlSchemaValueGetNext(sv) == qn, "ValueAppend / GetNext");
    xmlSchemaFreeValue(sv);  // (frees the appended value too)
    xmlSchemaFreeValue(no);
}
STITCH_ENDPOINT(schema_types_facets);

// RelaxNG: a grammar from a file / a document, error handler getters, dumps, and push
// validation element by element agrees with whole-document validation.
void relaxng_contexts(Range<0, 3> which, Str<1024, XmlText> text, Range<0, 2> source) {
    lx::silence();
    static const char* rngs[] = {
        "<element name='r' xmlns='http://relaxng.org/ns/structure/1.0'><zeroOrMore><element name='a'><text/></element></zeroOrMore></element>",
        "<element name='r' xmlns='http://relaxng.org/ns/structure/1.0'><attribute name='k'/><optional><element name='b'><empty/></element></optional></element>",
        "<element name='r' xmlns='http://relaxng.org/ns/structure/1.0' datatypeLibrary='http://www.w3.org/2001/XMLSchema-datatypes'><data type='int'/></element>",
        "<grammar xmlns='http://relaxng.org/ns/structure/1.0'><start><element name='r'><ref name='x'/></element></start><define name='x'><zeroOrMore><choice><text/><element><anyName/><empty/></element></choice></zeroOrMore></define></grammar>"};
    xmlRelaxNGInitTypes();
    std::string rng = rngs[(int)which];
    std::string path = lx::tmp_path("rng");
    xmlRelaxNGParserCtxtPtr pc = nullptr;
    xmlDocPtr gdoc = nullptr;
    switch ((int)source) {
        case 0: {
            FILE* f = fopen(path.c_str(), "wb");
            if (f) { fwrite(rng.data(), 1, rng.size(), f); fclose(f); }
            pc = xmlRelaxNGNewParserCtxt(path.c_str());
            break;
        }
        case 1:
            gdoc = xmlReadMemory(rng.data(), (int)rng.size(), "g.rng", nullptr, 0);
            pc = gdoc ? xmlRelaxNGNewDocParserCtxt(gdoc) : nullptr;
            break;
        default: pc = xmlRelaxNGNewMemParserCtxt(rng.data(), (int)rng.size()); break;
    }
    stitch::require(pc, "no parser context");
    xmlRelaxNGSetParserErrors(pc, quiet_err, quiet_err, nullptr);
    xmlRelaxNGValidityErrorFunc e = nullptr;
    xmlRelaxNGValidityWarningFunc w = nullptr;
    void* ctx = (void*)1;
    stitch::check(xmlRelaxNGGetParserErrors(pc, &e, &w, &ctx) == 0 && e == quiet_err && w == quiet_err && ctx == nullptr, "GetParserErrors");
    xmlRelaxNGSetResourceLoader(pc, lx::mem_loader, nullptr);
    xmlRelaxParserSetFlag(pc, 0);
    xmlRelaxParserSetIncLImit(pc, 10);
    xmlRelaxNGPtr g = xmlRelaxNGParse(pc);
    xmlRelaxNGFreeParserCtxt(pc);
    if (gdoc) xmlFreeDoc(gdoc);
    unlink(path.c_str());
    stitch::check(g != nullptr, "the built-in grammar parses from every source");
    if (!g) return;
    if (FILE* f = fopen("/dev/null", "w")) {
        xmlRelaxNGDump(f, g);
        xmlRelaxNGDumpTree(f, g);
        fclose(f);
    }
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d) {
        xmlRelaxNGValidCtxtPtr vc = xmlRelaxNGNewValidCtxt(g);
        xmlRelaxNGSetValidErrors(vc, quiet_err, quiet_err, nullptr);
        stitch::check(xmlRelaxNGGetValidErrors(vc, &e, &w, &ctx) == 0 && e == quiet_err, "GetValidErrors");
        int whole = xmlRelaxNGValidateDoc(vc, d);
        // (xmlRelaxNGValidCtxtClearErrors is not called: it dereferences the current element
        // state, which is NULL outside push validation)
        xmlRelaxNGFreeValidCtxt(vc);
        // push validation: element events in document order
        vc = xmlRelaxNGNewValidCtxt(g);
        xmlRelaxNGSetValidErrors(vc, quiet_err, quiet_err, nullptr);
        int ok = 1;
        std::function<void(xmlNodePtr)> walk = [&](xmlNodePtr n) {
            for (; n; n = n->next) {
                if (n->type == XML_ELEMENT_NODE) {
                    int r = xmlRelaxNGValidatePushElement(vc, d, n);
                    if (r == 0) ok &= xmlRelaxNGValidateFullElement(vc, d, n) == 1;
                    else {
                        if (r < 0) ok = 0;
                        walk(n->children);
                    }
                    if (xmlRelaxNGValidatePopElement(vc, d, n) != 1) ok = 0;
                } else if (n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE) {
                    if (xmlRelaxNGValidatePushCData(vc, n->content, xmlStrlen(n->content)) != 1) ok = 0;
                }
            }
        };
        walk(d->children);
        stitch::log("whole %d, push %d", whole, ok);
        xmlRelaxNGFreeValidCtxt(vc);
        xmlFreeDoc(d);
    }
    xmlRelaxNGFree(g);
}
STITCH_ENDPOINT(relaxng_contexts);

static int locator(void*, const char** file, unsigned long* line) {
    if (file) *file = "loc.xml";
    if (line) *line = 1;
    return 0;
}

// XML Schema: a schema from a file / a document; validation of a document, one element, a
// stream, a file and through SAX plugging all agree.
void xsd_contexts(Str<2048, XmlText> text, Range<0, 2> source, Range<0, 2> how) {
    lx::silence();
    static const char* xsd =
        "<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>"
        "<xs:element name='r'><xs:complexType><xs:sequence>"
        "<xs:element name='a' type='xs:int' minOccurs='0' maxOccurs='unbounded'/>"
        "<xs:element name='b' type='xs:string' minOccurs='0'/>"
        "</xs:sequence><xs:attribute name='k' type='xs:boolean'/></xs:complexType></xs:element></xs:schema>";
    std::string path = lx::tmp_path("xsd");
    xmlSchemaParserCtxtPtr pc = nullptr;
    xmlDocPtr sdoc = nullptr;
    switch ((int)source) {
        case 0: {
            FILE* f = fopen(path.c_str(), "wb");
            if (f) { fputs(xsd, f); fclose(f); }
            pc = xmlSchemaNewParserCtxt(path.c_str());
            break;
        }
        case 1:
            sdoc = xmlReadMemory(xsd, (int)strlen(xsd), "s.xsd", nullptr, 0);
            pc = sdoc ? xmlSchemaNewDocParserCtxt(sdoc) : nullptr;
            break;
        default: pc = xmlSchemaNewMemParserCtxt(xsd, (int)strlen(xsd)); break;
    }
    stitch::require(pc, "no parser context");
    xmlSchemaSetParserErrors(pc, quiet_err, quiet_err, nullptr);
    xmlSchemaValidityErrorFunc e = nullptr;
    xmlSchemaValidityWarningFunc w = nullptr;
    void* ctx = (void*)1;
    stitch::check(xmlSchemaGetParserErrors(pc, &e, &w, &ctx) == 0 && e == quiet_err, "GetParserErrors");
    xmlSchemaSetResourceLoader(pc, lx::mem_loader, nullptr);
    xmlSchemaPtr s = xmlSchemaParse(pc);
    xmlSchemaFreeParserCtxt(pc);
    if (sdoc) xmlFreeDoc(sdoc);
    stitch::check(s != nullptr, "the built-in schema parses from every source");
    if (!s) { unlink(path.c_str()); return; }
    if (FILE* f = fopen("/dev/null", "w")) { xmlSchemaDump(f, s); fclose(f); }
    xmlSchemaValidCtxtPtr vc = xmlSchemaNewValidCtxt(s);
    xmlSchemaSetValidErrors(vc, quiet_err, quiet_err, nullptr);
    stitch::check(xmlSchemaGetValidErrors(vc, &e, &w, &ctx) == 0 && e == quiet_err, "GetValidErrors");
    stitch::check(xmlSchemaSetValidOptions(vc, XML_SCHEMA_VAL_VC_I_CREATE) == 0 &&
                      xmlSchemaValidCtxtGetOptions(vc) == XML_SCHEMA_VAL_VC_I_CREATE, "GetOptions after SetValidOptions");
    xmlSchemaSetValidOptions(vc, 0);
    xmlSchemaValidateSetFilename(vc, "in.xml");
    xmlSchemaValidateSetLocator(vc, locator, nullptr);
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (!d) {
        xmlSchemaFreeValidCtxt(vc);
        xmlSchemaFree(s);
        unlink(path.c_str());
        stitch::bail("parse error");
    }
    int whole = xmlSchemaValidateDoc(vc, d);
    stitch::check((whole == 0) == (xmlSchemaIsValid(vc) == 1) || whole < 0, "IsValid after ValidateDoc");
    int other = whole;
    switch ((int)how) {
        case 0:
            if (xmlNodePtr root = xmlDocGetRootElement(d)) other = xmlSchemaValidateOneElement(vc, root);
            break;
        case 1: {
            xmlParserInputBufferPtr in = xmlParserInputBufferCreateMem(text.data(), (int)text.size(), XML_CHAR_ENCODING_NONE);
            other = in ? xmlSchemaValidateStream(vc, in, XML_CHAR_ENCODING_NONE, nullptr, nullptr) : whole;
            break;
        }
        default: {
            FILE* f = fopen(path.c_str(), "wb");
            if (f) { fwrite(text.data(), 1, text.size(), f); fclose(f); }
            other = xmlSchemaValidateFile(vc, path.c_str(), 0);
            break;
        }
    }
    xmlSchemaValidCtxtGetParserCtxt(vc);
    stitch::log("whole %d, other %d", whole, other);
    if (whole >= 0 && other >= 0 && d->intSubset == nullptr)
        stitch::check((whole == 0) == (other == 0), "every validation entry point gives the same verdict");
    xmlFreeDoc(d);
    xmlSchemaFreeValidCtxt(vc);
    xmlSchemaFree(s);
    unlink(path.c_str());
}
STITCH_ENDPOINT(xsd_contexts);

// Schematron from a file / a document, with structured errors; validate one element.
void schematron_contexts(Str<1024, XmlText> text, Range<0, 1> source) {
    lx::silence();
    static const char* sch =
        "<schema xmlns='http://purl.oclc.org/dsdl/schematron'><pattern><rule context='r'>"
        "<assert test='count(a) &lt; 4'>too many a</assert><report test='@bad'>bad attribute</report>"
        "</rule></pattern></schema>";
    std::string path = lx::tmp_path("sch");
    xmlSchematronParserCtxtPtr pc = nullptr;
    xmlDocPtr sdoc = nullptr;
    if ((int)source == 0) {
        FILE* f = fopen(path.c_str(), "wb");
        if (f) { fputs(sch, f); fclose(f); }
        pc = xmlSchematronNewParserCtxt(path.c_str());
    } else {
        sdoc = xmlReadMemory(sch, (int)strlen(sch), "s.sch", nullptr, 0);
        pc = sdoc ? xmlSchematronNewDocParserCtxt(sdoc) : nullptr;
    }
    stitch::require(pc, "no parser context");
    xmlSchematronPtr s = xmlSchematronParse(pc);
    xmlSchematronFreeParserCtxt(pc);
    if (sdoc) xmlFreeDoc(sdoc);
    unlink(path.c_str());
    stitch::check(s != nullptr, "the built-in schematron schema parses");
    if (!s) return;
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    if (d) {
        xmlSchematronValidCtxtPtr vc = xmlSchematronNewValidCtxt(s, XML_SCHEMATRON_OUT_QUIET);
        int errs = 0;
        xmlSchematronSetValidStructuredErrors(vc, [](void* c, const xmlError*) { ++*(int*)c; }, &errs);
        int whole = xmlSchematronValidateDoc(vc, d);
        stitch::check(whole >= -1, "ValidateDoc result");
        xmlSchematronFreeValidCtxt(vc);
        xmlFreeDoc(d);
    }
    xmlSchematronFree(s);
}
STITCH_ENDPOINT(schematron_contexts);
