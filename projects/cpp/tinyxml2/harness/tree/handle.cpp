// XMLHandle / XMLConstHandle: null-safe navigation chains, checked against plain XMLNode calls.
#include "../objects.hpp"

New<Node> handle_walk(Borrow<Doc> d, Borrow<Node> n, Src<16> s) {
    stitch::require(live(d, n), "stale node");
    XMLNode* cur = n;
    XMLHandle h(cur);
    XMLConstHandle ch((const XMLNode*)cur);
    while (s->more()) {
        switch (s->choice(10)) {
            case 0: h = h.FirstChild(); ch = ch.FirstChild(); cur = cur ? cur->FirstChild() : nullptr; break;
            case 1: h = h.LastChild(); ch = ch.LastChild(); cur = cur ? cur->LastChild() : nullptr; break;
            case 2: h = h.NextSibling(); ch = ch.NextSibling(); cur = cur ? cur->NextSibling() : nullptr; break;
            case 3: h = h.PreviousSibling(); ch = ch.PreviousSibling(); cur = cur ? cur->PreviousSibling() : nullptr; break;
            case 4: h = h.FirstChildElement(); ch = ch.FirstChildElement(); cur = cur ? cur->FirstChildElement() : nullptr; break;
            case 5: h = h.LastChildElement(); ch = ch.LastChildElement(); cur = cur ? cur->LastChildElement() : nullptr; break;
            case 6: h = h.NextSiblingElement(); ch = ch.NextSiblingElement(); cur = cur ? cur->NextSiblingElement() : nullptr; break;
            case 7: h = h.PreviousSiblingElement(); ch = ch.PreviousSiblingElement(); cur = cur ? cur->PreviousSiblingElement() : nullptr; break;
            default: {
                // by the name of an existing child element (or a missing one)
                const char* nm = "nope";
                if (cur && cur->FirstChildElement() && s->coin(0.8)) nm = cur->FirstChildElement()->Name();
                std::string name = nm;
                if (s->coin()) { h = h.FirstChildElement(name.c_str()); ch = ch.FirstChildElement(name.c_str()); cur = cur ? cur->FirstChildElement(name.c_str()) : nullptr; }
                else { h = h.LastChildElement(name.c_str()); ch = ch.LastChildElement(name.c_str()); cur = cur ? cur->LastChildElement(name.c_str()) : nullptr; }
                break;
            }
        }
        stitch::check(h.ToNode() == cur && ch.ToNode() == cur, "handle follows the node");
    }
    stitch::check(h.ToElement() == (cur ? cur->ToElement() : nullptr) && ch.ToElement() == h.ToElement(), "handle ToElement");
    stitch::check(h.ToText() == (cur ? cur->ToText() : nullptr) && ch.ToText() == h.ToText(), "handle ToText");
    stitch::check(h.ToUnknown() == (cur ? cur->ToUnknown() : nullptr) && ch.ToUnknown() == h.ToUnknown(), "handle ToUnknown");
    stitch::check(h.ToDeclaration() == (cur ? cur->ToDeclaration() : nullptr) && ch.ToDeclaration() == h.ToDeclaration(),
                  "handle ToDeclaration");
    stitch::require(cur, "walked off the tree");
    return mk(d, cur);
}
STITCH_ENDPOINT(handle_walk);
