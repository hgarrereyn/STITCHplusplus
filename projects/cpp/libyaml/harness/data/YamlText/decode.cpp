// YAML documents: block and flow collections, every scalar style, anchors/aliases, tags,
// directives, comments, multiple documents.
#include "../../objects.hpp"

static std::string ind(int n) { return std::string((size_t)n * 2, ' '); }

static std::string scalar(stitch::Source& s) {
    switch (s.choice(7)) {
        case 0: return s.token();
        case 1: return std::to_string(s.range(-100000, 100000));
        case 2: return "'" + s.str(16) + "'";
        case 3: return "\"" + s.str(16) + s.pick({"", "\\n", "\\t", "\\x41", "\\u263A", "\\U0001F600", "\\"}) + "\"";
        case 4: return std::string(s.pick({"null", "~", "true", "false", ".inf", "-.Inf", ".nan", "0x1F", "0o17", "1e3", "2001-12-14"}));
        case 5: return s.str(12);
        default: return std::string(s.pick({"|", ">", "|-", ">+", "|2"})) + "\n";
    }
}

static std::string prefix(stitch::Source& s) {
    std::string o;
    if (s.coin(0.15)) o += "&" + s.token() + " ";
    if (s.coin(0.15)) o += std::string(s.pick({"!!str ", "!!map ", "!!seq ", "!local ", "!<tag:x.org,2000:a> ", "! "}));
    return o;
}

static void node(stitch::Source& s, std::string& o, int depth, bool flow);

static void flow(stitch::Source& s, std::string& o, int depth) {
    bool map = s.coin();
    o += map ? "{" : "[";
    bool first = true;
    while (s.more()) {
        if (!first) o += ", ";
        first = false;
        if (map) {
            o += scalar(s) + ": ";
        }
        node(s, o, depth + 1, true);
    }
    o += map ? "}" : "]";
}

static void node(stitch::Source& s, std::string& o, int depth, bool in_flow) {
    o += prefix(s);
    size_t k = depth > 6 ? 2 : (in_flow ? 3 : 5);
    switch (s.choice(k)) {
        case 0: o += scalar(s); break;
        case 1: o += "*" + s.token(); break;  // alias
        case 2: flow(s, o, depth); break;
        case 3: {  // block sequence
            while (s.more()) {
                o += "\n" + ind(depth) + "- ";
                node(s, o, depth + 1, false);
            }
            break;
        }
        default: {  // block mapping
            while (s.more()) {
                o += "\n" + ind(depth);
                if (s.coin(0.1)) o += "? " + scalar(s) + "\n" + ind(depth);
                o += scalar(s) + ":";
                if (s.coin(0.5)) o += " ";
                node(s, o, depth + 1, false);
                if (s.coin(0.1)) o += " # " + s.str(8);
            }
        }
    }
}

static std::string yaml_text(stitch::Source& s) {
    std::string o;
    if (s.coin(0.1)) o += std::string(s.pick({"%YAML 1.1\n", "%YAML 1.2\n", "%YAML 2.0\n"}));
    if (s.coin(0.1)) o += "%TAG !e! tag:example.com,2000:\n";
    do {
        if (s.coin(0.3)) o += "---";
        node(s, o, 0, false);
        o += "\n";
        if (s.coin(0.2)) o += "...\n";
    } while (s.more());
    return o;
}
STITCH_DECODER(YamlText, yaml_text);
