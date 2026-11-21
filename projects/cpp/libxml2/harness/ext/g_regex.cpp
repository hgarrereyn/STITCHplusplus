#include "../objects.hpp"
#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <vector>
#include <libxml/xmlschemas.h>
#include <libxml/xmlautomata.h>
#include <libxml/xmlregexp.h>
#include <libxml/xmlreader.h>
#include <libxml/HTMLtree.h>


static bool simple_regex(const std::string& p) {
    for (char c : p) if (!(isalnum((unsigned char)c) || strchr("()|*+?.[]-{},", c))) return false;
    return !p.empty();
}

void regexp_vs_std(Str<24, RegexText> pattern, Str<16> input) {
    std::string p = pattern.c_str(), in = input.c_str();
    stitch::require(simple_regex(p), "outside the common subset");
    for (char c : in) stitch::require(isprint((unsigned char)c), "non-printable input");
    xmlRegexpPtr re = xmlRegexpCompile((const xmlChar*)p.c_str());
    std::regex sr;
    bool std_ok = true;
    try { sr = std::regex(p, std::regex::ECMAScript); } catch (const std::regex_error&) { std_ok = false; }
    if (!re || !std_ok) { xmlRegFreeRegexp(re); stitch::bail("one side rejects the pattern"); }
    // (XML Schema '.' excludes \n\r; inputs here are printable)
    int a = xmlRegexpExec(re, (const xmlChar*)in.c_str());
    bool b = std::regex_match(in, sr);
    stitch::log("pattern %s input %s: libxml2 %d std %d", p.c_str(), in.c_str(), a, (int)b);
    stitch::check((a == 1) == b, "xmlRegexpExec agrees with std::regex_match");
    xmlRegFreeRegexp(re);
}
STITCH_ENDPOINT(regexp_vs_std);

void automata_build(Src<64> s) {
    xmlAutomataPtr am = xmlNewAutomata();
    xmlAutomataStatePtr st = xmlAutomataGetInitState(am);
    std::vector<xmlAutomataStatePtr> states = {st};
    while (s->more() && states.size() < 20) {
        xmlAutomataStatePtr from = states[s->choice(states.size())];
        const char* tok = s->pick({"a", "b", "c"});
        xmlAutomataStatePtr to = nullptr;
        switch (s->choice(4)) {
            case 0: to = xmlAutomataNewTransition(am, from, nullptr, (const xmlChar*)tok, nullptr); break;
            case 1: to = xmlAutomataNewEpsilon(am, from, nullptr); break;
            case 2: to = xmlAutomataNewCountTrans(am, from, nullptr, (const xmlChar*)tok, (int)s->range(0, 2), (int)s->range(2, 4), nullptr); break;
            default: to = xmlAutomataNewAllTrans(am, from, nullptr, 0); break;
        }
        if (to) states.push_back(to);
    }
    xmlAutomataSetFinalState(am, states.back());
    xmlRegexpPtr re = xmlAutomataCompile(am);
    if (re) { xmlRegexpIsDeterminist(re); xmlRegFreeRegexp(re); }
    xmlFreeAutomata(am);
}
STITCH_ENDPOINT(automata_build);

void regexp_push_exec(Str<24, RegexText> pattern, Src<16> s) {
    xmlRegexpPtr re = xmlRegexpCompile((const xmlChar*)pattern.c_str());
    stitch::require(re, "invalid regexp");
    xmlRegExecCtxtPtr ex = xmlRegNewExecCtxt(re, nullptr, nullptr);
    while (s->more()) xmlRegExecPushString(ex, (const xmlChar*)s->pick({"a", "b", "c", "d"}), nullptr);
    xmlRegExecPushString(ex, nullptr, nullptr);
    xmlRegFreeExecCtxt(ex);
    xmlRegFreeRegexp(re);
}
STITCH_ENDPOINT(regexp_push_exec);
