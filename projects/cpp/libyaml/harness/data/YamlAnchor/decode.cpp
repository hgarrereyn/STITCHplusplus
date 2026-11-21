#include "../../objects.hpp"
static std::string anchor(stitch::Source& s) { return s.coin(0.7) ? s.token() : s.str(12); }
STITCH_DECODER(YamlAnchor, anchor);
