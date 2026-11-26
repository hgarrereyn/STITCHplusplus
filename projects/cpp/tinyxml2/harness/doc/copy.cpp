// Whole-document operations: DeepCopy, FILE* load/save, Print, BOM and error state.
#include "../objects.hpp"
#include <cstdio>
#include <memory>

// DeepCopy copies the nodes, not the document settings (BOM flag, entity processing used when
// printing): give the target the same settings before comparing the output.
// DeepCopy into a fresh document: both print the same.
New<Doc> deep_copy(Borrow<Doc> d) {
    XMLDocument* t = new XMLDocument(d->ProcessEntities(), d->WhitespaceMode());
    d->DeepCopy(t);
    t->SetBOM(d->HasBOM());
    stitch::check(print_node(d, true) == print_node(t, true), "deep copy prints the same");
    New<Doc> r(t);
    stitch::set(r, "tok", fresh_tok());
    return r;
}
STITCH_ENDPOINT(deep_copy);

// DeepCopy into an existing document clears it first (its nodes are freed).
void deep_copy_into(Borrow<Doc> d, Borrow<Doc> target) {
    XMLDocument* t = target;
    if (t != (XMLDocument*)d) retok(target);
    d->DeepCopy(t);
    if (t->ProcessEntities() != d->ProcessEntities()) return;
    t->SetBOM(d->HasBOM());
    stitch::check(print_node(d, true) == print_node(t, true), "deep copy prints the same");
}
STITCH_ENDPOINT(deep_copy_into);

New<Doc> load_file_fp(File<4096, XmlText> f, Bool processEntities, WsMode ws) {
    FILE* fp = fopen(f.path(), "rb");
    stitch::require(fp, "open failed");
    XMLDocument* d = new XMLDocument(processEntities, (Whitespace)(int)ws);
    XMLError e = d->LoadFile(fp);
    fclose(fp);
    if (e != XML_SUCCESS) {
        stitch::check(d->Error() && d->ErrorID() == e, "LoadFile result agrees with ErrorID");
        delete d;
        stitch::bail("parse error");
    }
    New<Doc> r(d);
    stitch::set(r, "tok", fresh_tok());
    return r;
}
STITCH_ENDPOINT(load_file_fp);

// SaveFile(FILE*) writes exactly what an XMLPrinter makes.
void save_file_fp(Borrow<Doc> d, Bool compact) {
    FILE* fp = tmpfile();
    stitch::require(fp, "tmpfile failed");
    XMLError e = d->SaveFile(fp, compact);
    std::string out;
    rewind(fp);
    char buf[4096];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, fp)) > 0) out.append(buf, k);
    fclose(fp);
    stitch::require(e == XML_SUCCESS, "save failed");
    XMLPrinter p(0, compact);
    d->Print(&p);
    stitch::check(out == std::string(p.CStr()), "SaveFile(FILE*) == Print to memory");
}
STITCH_ENDPOINT(save_file_fp);

// XMLDocument::Print(printer) is the same as Accept(printer).
void print_doc(Borrow<Doc> d, Bool compact, Range<0, 3> depth, Bool escape_apos) {
    XMLPrinter p(0, compact, (int)depth,
                 escape_apos ? XMLPrinter::ESCAPE_APOS_CHARS_IN_ATTRIBUTES : XMLPrinter::DONT_ESCAPE_APOS_CHARS_IN_ATTRIBUTES);
    d->Print(&p);
    XMLPrinter q(0, compact, (int)depth,
                 escape_apos ? XMLPrinter::ESCAPE_APOS_CHARS_IN_ATTRIBUTES : XMLPrinter::DONT_ESCAPE_APOS_CHARS_IN_ATTRIBUTES);
    d->Accept(&q);
    stitch::check(std::string(p.CStr()) == std::string(q.CStr()), "Print == Accept");
    stitch::check(p.CStrSize() == strlen(p.CStr()) + 1, "CStrSize matches the buffer");
    // ClearBuffer empties the memory buffer
    p.ClearBuffer();
    stitch::check(p.CStrSize() == 1 && p.CStr()[0] == 0, "ClearBuffer empties the buffer");
}
STITCH_ENDPOINT(print_doc);

// The BOM flag is written by the printer and read back by the parser.
void set_bom(Borrow<Doc> d, Bool bom) {
    d->SetBOM(bom);
    stitch::check(d->HasBOM() == (bool)bom, "HasBOM after SetBOM");
    std::string s = print_node(d, true);
    XMLDocument d2;
    if (d2.Parse(s.c_str()) == XML_SUCCESS) stitch::check(d2.HasBOM() == (bool)bom, "BOM survives print -> parse");
}
STITCH_ENDPOINT(set_bom);

void error_info(Borrow<Doc> d, Bool clear) {
    XMLError e = d->ErrorID();
    stitch::check(!strcmp(XMLDocument::ErrorIDToName(e), d->ErrorName()), "ErrorIDToName(ErrorID()) == ErrorName()");
    stitch::check(d->ErrorLineNum() >= 0, "error line is not negative");
    if (e == XML_SUCCESS) stitch::check(d->ErrorLineNum() == 0, "no error line without an error");
    if (clear) {
        d->ClearError();
        stitch::check(!d->Error() && d->ErrorID() == XML_SUCCESS, "ClearError clears the error");
    }
}
STITCH_ENDPOINT(error_info);

void error_names(Range<0, XML_ERROR_COUNT - 1> id) {
    const char* n = XMLDocument::ErrorIDToName((XMLError)(int)id);
    stitch::check(n && *n, "every error has a name");
}
STITCH_ENDPOINT(error_names);
