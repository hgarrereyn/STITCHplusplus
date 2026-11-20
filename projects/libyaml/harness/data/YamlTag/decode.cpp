#include "../../objects.hpp"
static std::string tag(stitch::Source& s) {
    switch (s.choice(4)) {
        case 0: return std::string(s.pick({"!", "!!", "!!str", "tag:yaml.org,2002:str", "tag:yaml.org,2002:map", "!local"}));
        case 1: return "tag:" + s.str(10) + ",2000:" + s.str(6);
        case 2: return "!" + s.str(8);
        default: return s.str(16);
    }
}
STITCH_DECODER(YamlTag, tag);
