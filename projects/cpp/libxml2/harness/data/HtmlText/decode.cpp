#include "../../objects.hpp"
static const char* kTags[] = {"html", "head", "body", "p", "div", "table", "tr", "td", "script", "style", "textarea", "title",
                              "a", "b", "i", "ul", "li", "br", "img", "form", "select", "option", "pre", "meta", "frameset"};
template <size_t N>
static const char* pk(stitch::Source& s, const char* (&a)[N]) { return a[s.choice(N)]; }
static void html(stitch::Source& s, std::string& o, int depth) {
    while (s.more()) {
        switch (s.choice(depth > 8 ? 4 : 6)) {
            case 0: o += s.str(10); break;
            case 1: o += s.pick({"&amp;", "&nbsp;", "&#160;", "&#x;", "&lt", "&copy;", "&unknown;"}); break;
            case 2: o += "<!--" + s.str(8) + (s.coin(0.9) ? "-->" : "--!>"); break;
            case 3: o += "</" + std::string(pk(s, kTags)) + ">"; break;
            default: {
                std::string t = pk(s, kTags);
                o += "<" + t;
                while (s.coin(0.3)) o += " " + std::string(s.pick({"href", "class", "id", "onclick", "charset", "http-equiv", "content"})) + (s.coin() ? "=\"" + s.str(8) + "\"" : "=" + s.token());
                o += ">";
                html(s, o, depth + 1);
                if (s.coin(0.7)) o += "</" + t + ">";
            }
        }
    }
}
static std::string html_text(stitch::Source& s) {
    std::string o;
    if (s.coin(0.3)) o += s.pick({"<!DOCTYPE html>", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\">", "<?xml version=\"1.0\"?>"});
    html(s, o, 0);
    return o;
}
STITCH_DECODER(HtmlText, html_text);
