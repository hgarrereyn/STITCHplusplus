#include "../../objects.hpp"
static void re(stitch::Source& s, std::string& o, int depth) {
    while (s.more()) {
        switch (s.choice(depth > 4 ? 4 : 6)) {
            case 0: o += s.str(3); break;
            case 1: o += s.pick({"\\d", "\\w", "\\s", "\\i", "\\c", "\\p{L}", "\\P{Nd}", "\\p{IsBasicLatin}", ".", "\\.", "\\-", "\\n"}); break;
            case 2: o += "[" + std::string(s.coin(0.2) ? "^" : "") + s.pick({"a-z", "0-9", "a-z-[aeiou]", "\\d\\s", "-a", "]", "\\p{Lu}"}) + "]"; break;
            case 3: o += s.pick({"*", "+", "?", "{2}", "{1,3}", "{0,}", "{100000}", "{3,2}", "|"}); break;
            default: o += "("; re(s, o, depth + 1); o += ")"; break;
        }
    }
}
static std::string regex_text(stitch::Source& s) {
    std::string o;
    re(s, o, 0);
    return o;
}
STITCH_DECODER(RegexText, regex_text);
