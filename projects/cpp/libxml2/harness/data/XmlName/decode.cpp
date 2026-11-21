// element and attribute names: mostly valid NCNames / QNames, sometimes reserved or odd ones
#include "../../objects.hpp"
static std::string xml_name(stitch::Source& s) {
    switch (s.choice(6)) {
        case 0: return s.pick({"a", "b", "item", "id", "x:a", "a:b", "xml:lang", "xml:space", "xmlns", "xmlns:p", "p:q", "_", "\xc3\xa9"});
        case 1: return s.token();
        case 2: return std::string(s.pick({"a", "x", "p"})) + ":" + s.pick({"b", "id", "lang"});
        case 3: return std::string(s.pick({"n", "e", "_"})) + std::to_string(s.range(0, 99));
        case 4: return s.str(8);
        default: return s.pick({"href", "base", "type", "name", "class", "xi:include", "include"});
    }
}
STITCH_DECODER(XmlName, xml_name);
