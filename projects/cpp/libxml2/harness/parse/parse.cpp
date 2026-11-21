#include "../objects.hpp"

// Attribute "wellformed" (1): parsed without RECOVER from XML that parsed cleanly, so its
// serialization must parse again.
New<Doc> parse(Str<8192, XmlText> text, lx::ParseOpts opts) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(d, "parse error");
    New<Doc> out(d);
    stitch::set(out, "wellformed", ((int)opts & XML_PARSE_RECOVER) ? 0 : 1);
    return out;
}
STITCH_ENDPOINT(parse);

New<Doc> parse_html(Str<8192, HtmlText> text, lx::HtmlOpts opts) {
    lx::silence();
    htmlDocPtr d = htmlReadMemory(text.data(), (int)text.size(), "in.html", nullptr, (int)opts | lx::kQuiet);
    stitch::require(d, "html parse error");
    New<Doc> out(d);
    stitch::set(out, "html", 1);
    return out;
}
STITCH_ENDPOINT(parse_html);

// Push parsing in fuzzed chunks gives the same document as parsing in one go.
void push_parse(Str<4096, XmlText> text, Range<1, 64> chunk) {
    lx::silence();
    stitch::require(text.s.find('\0') == std::string::npos, "NUL in input");
    xmlDocPtr whole = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    xmlParserCtxtPtr ctx = xmlCreatePushParserCtxt(nullptr, nullptr, nullptr, 0, "in.xml");
    stitch::require(ctx, "no push parser");
    xmlCtxtUseOptions(ctx, lx::kQuiet);
    size_t n = text.size(), step = (size_t)(long long)chunk;
    for (size_t off = 0; off < n; off += step) xmlParseChunk(ctx, text.data() + off, (int)std::min(step, n - off), 0);
    xmlParseChunk(ctx, nullptr, 0, 1);
    bool ok = ctx->wellFormed;
    xmlDocPtr pushed = ctx->myDoc;
    xmlFreeParserCtxt(ctx);
    std::string a = whole ? lx::dump(whole, 0) : "", b = (ok && pushed) ? lx::dump(pushed, 0) : "";
    if (pushed) xmlFreeDoc(pushed);
    if (whole) xmlFreeDoc(whole);
    stitch::log("whole=%d push=%d", whole != nullptr, ok);
    stitch::check((whole != nullptr) == ok, "push parser accepts what the one-shot parser accepts");
    stitch::check(a == b, "push parser builds the same document");
}
STITCH_ENDPOINT(push_parse);

// The text reader walks the whole document (and expands subtrees).
void reader(Str<4096, XmlText> text, lx::ParseOpts opts, Bool expand) {
    lx::silence();
    xmlTextReaderPtr r = xmlReaderForMemory(text.data(), (int)text.size(), "in.xml", nullptr, (int)opts | lx::kQuiet);
    stitch::require(r, "no reader");
    for (int i = 0; i < 20000 && xmlTextReaderRead(r) == 1; i++) {
        xmlTextReaderConstName(r);
        xmlTextReaderConstValue(r);
        xmlTextReaderDepth(r);
        if (xmlTextReaderHasAttributes(r))
            while (xmlTextReaderMoveToNextAttribute(r) == 1) xmlTextReaderConstValue(r);
        if (expand && i % 7 == 0) xmlTextReaderExpand(r);
    }
    xmlFreeTextReader(r);
}
STITCH_ENDPOINT(reader);
