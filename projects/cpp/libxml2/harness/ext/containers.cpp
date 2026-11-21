#include "../objects.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <vector>
#include <libxml/xmlwriter.h>
#include <libxml/xmlschemas.h>
#include <libxml/xmlschemastypes.h>
#include <libxml/relaxng.h>
#include <libxml/schematron.h>
#include <libxml/pattern.h>
#include <libxml/debugXML.h>
#include <libxml/list.h>
#include <libxml/hash.h>
#include <libxml/HTMLparser.h>
#include <libxml/entities.h>
#include <strings.h>

void dict_model(Src<64> s) {
    xmlDictPtr d = xmlDictCreate();
    std::map<std::string, const xmlChar*> model;
    const char* names[] = {"a", "bb", "ccc", "x:y", "", "\xc3\xa9", "longer-name-here"};
    while (s->more()) {
        std::string k = names[s->choice(7)];
        int len = s->coin(0.2) ? (int)s->range(0, (long long)k.size()) : -1;
        std::string key = len < 0 ? k : k.substr(0, len);
        const xmlChar* p = xmlDictLookup(d, (const xmlChar*)k.c_str(), len);
        stitch::check(p && key == (const char*)p, "xmlDictLookup returns the string");
        auto it = model.find(key);
        if (it != model.end()) stitch::check(it->second == p, "the same string interns to the same pointer");
        model[key] = p;
        stitch::check(xmlDictOwns(d, p) == 1, "the dictionary owns its strings");
        stitch::check(xmlDictExists(d, (const xmlChar*)key.c_str(), -1) == p, "xmlDictExists");
    }
    stitch::check(xmlDictSize(d) == (int)model.size(), "xmlDictSize");
    const xmlChar* q = xmlDictQLookup(d, (const xmlChar*)"p", (const xmlChar*)"local");
    stitch::check(q && std::string((char*)q) == "p:local", "xmlDictQLookup");
    xmlDictPtr sub = xmlDictCreateSub(d);
    stitch::check(xmlDictLookup(sub, (const xmlChar*)"a", -1) == (model.count("a") ? model["a"] : xmlDictLookup(sub, (const xmlChar*)"a", -1)), "a sub-dictionary sees its parent");
    xmlDictFree(sub); xmlDictFree(d);
}
STITCH_ENDPOINT(dict_model);

void hash_model(Src<64> s) {
    xmlHashTablePtr h = xmlHashCreate((int)s->range(0, 4));
    std::map<std::string, intptr_t> model;
    const char* names[] = {"a", "b", "c", "d", "e", "f"};
    while (s->more()) {
        std::string k = names[s->choice(6)];
        intptr_t v = (intptr_t)s->range(1, 100);
        switch (s->choice(4)) {
            case 0: { int r = xmlHashAddEntry(h, (const xmlChar*)k.c_str(), (void*)v); stitch::check((r == 0) == !model.count(k), "xmlHashAddEntry fails only for an existing key"); if (r == 0) model[k] = v; break; }
            case 1: xmlHashUpdateEntry(h, (const xmlChar*)k.c_str(), (void*)v, nullptr); model[k] = v; break;
            case 2: { int r = xmlHashRemoveEntry(h, (const xmlChar*)k.c_str(), nullptr); stitch::check((r == 0) == (model.erase(k) == 1), "xmlHashRemoveEntry"); break; }
            default: xmlHashAddEntry2(h, (const xmlChar*)k.c_str(), (const xmlChar*)"two", (void*)v); xmlHashLookup2(h, (const xmlChar*)k.c_str(), (const xmlChar*)"two"); xmlHashRemoveEntry2(h, (const xmlChar*)k.c_str(), (const xmlChar*)"two", nullptr);
        }
        for (auto& e : model) stitch::check((intptr_t)xmlHashLookup(h, (const xmlChar*)e.first.c_str()) == e.second, "xmlHashLookup matches the model");
        stitch::check(xmlHashSize(h) == (int)model.size(), "xmlHashSize");
    }
    int n = 0;
    xmlHashScan(h, [](void* payload, void* data, const xmlChar*) { (*(int*)data)++; (void)payload; }, &n);
    stitch::check(n == (int)model.size(), "xmlHashScan visits every entry");
    xmlHashTablePtr c = xmlHashCopy(h, [](void* p, const xmlChar*) { return p; });
    stitch::check(xmlHashSize(c) == xmlHashSize(h), "xmlHashCopy");
    xmlHashFree(c, nullptr); xmlHashFree(h, nullptr);
}
STITCH_ENDPOINT(hash_model);

void list_model(Src<64> s) {
    xmlListPtr l = xmlListCreate(nullptr, [](const void* a, const void* b) { intptr_t x = (intptr_t)a, y = (intptr_t)b; return (int)((x > y) - (x < y)); });
    std::vector<intptr_t> model;
    while (s->more()) {
        intptr_t v = (intptr_t)s->range(1, 20);
        switch (s->choice(6)) {
            case 0: xmlListAppend(l, (void*)v); model.push_back(v); break;
            case 1: xmlListPushFront(l, (void*)v); model.insert(model.begin(), v); break;
            case 2: { int r = xmlListRemoveFirst(l, (void*)v); auto it = std::find(model.begin(), model.end(), v); stitch::check((r == 1) == (it != model.end()), "xmlListRemoveFirst"); if (it != model.end()) model.erase(it); break; }
            case 3: xmlListReverse(l); std::reverse(model.begin(), model.end()); break;
            case 4: xmlListSort(l); std::stable_sort(model.begin(), model.end()); break;
            default: if (!model.empty()) { xmlListPopFront(l); model.erase(model.begin()); } break;
        }
        stitch::check(xmlListSize(l) == (int)model.size(), "xmlListSize matches the model");
    }
    std::vector<intptr_t> got;
    xmlListWalk(l, [](const void* d, void* u) { ((std::vector<intptr_t>*)u)->push_back((intptr_t)d); return 1; }, &got);
    stitch::check(got.size() == model.size(), "xmlListWalk visits every element");
    xmlListDelete(l);
}
STITCH_ENDPOINT(list_model);

void buffer_model(Src<64> s) {
    xmlBufferPtr b = xmlBufferCreateSize((size_t)s->range(0, 64));
    std::string model;
    while (s->more()) {
        std::string t = s->str(8);
        switch (s->choice(5)) {
            case 0: xmlBufferAdd(b, (const xmlChar*)t.data(), (int)t.size()); model += t; break;
            case 1: xmlBufferAddHead(b, (const xmlChar*)t.data(), (int)t.size()); model = t + model; break;
            case 2: { int n = (int)s->range(0, 10); int r = xmlBufferShrink(b, n); if (r >= 0) model.erase(0, std::min<size_t>(n, model.size())); break; }
            case 3: xmlBufferCCat(b, t.c_str()); model += t.c_str(); break;
            default: xmlBufferEmpty(b); model.clear();
        }
        stitch::check(xmlBufferLength(b) == (int)model.size() && std::string((const char*)xmlBufferContent(b), xmlBufferLength(b)) == model, "the buffer matches the model");
    }
    xmlBufferWriteQuotedString(b, (const xmlChar*)"say \"hi\" 'x'");
    xmlBufferFree(b);
}
STITCH_ENDPOINT(buffer_model);
