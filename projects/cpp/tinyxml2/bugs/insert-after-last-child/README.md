# tinyxml2: SEGV in tinyxml2::XMLNode::InsertAfterChild

| | |
|---|---|
| Sanitizer | `SEGV` |
| Location | `tinyxml2::XMLNode::InsertAfterChild tinyxml2.cpp:931:29`, `insert_after insert.cpp:34:26`, `insert_after (stub)` |
| Library source | [https://github.com/leethomason/tinyxml2](https://github.com/leethomason/tinyxml2) @ `397f22581b6f` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `tinyxml2-c1` |

## Standalone reproducer

`standalone/min.cpp` (13 lines of code) needs only the library: no harness, no STITCH++ runtime.

```cpp
#include <tinyxml2.h>
using namespace tinyxml2;

int main() {
    XMLDocument doc;
    XMLElement* root = doc.NewElement("r");
    doc.InsertEndChild(root);
    XMLElement* a = doc.NewElement("a");
    XMLElement* b = doc.NewElement("b");
    root->InsertEndChild(a);
    root->InsertEndChild(b);
    // b is a's next sibling and the last child: moving it "after a" unlinks it first,
    // which leaves a->_next null, then a->_next->_prev is written.
    root->InsertAfterChild(a, b);
    return 0;
}
```

Run it: `./run.sh --standalone` (builds the library at the pinned commit, compiles this file with AddressSanitizer, runs it). Build line: `clang++ -std=c++17 $(stitch-flags --no-cov) -I. -I/src min.cpp /src/build/libtinyxml2.a -lstdc++ -lm -lpthread -o /tmp/repro`

`standalone/generated.cpp` (87 lines of code) is what `stitch write` produced before minimization; `stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).

## Call sequence (4 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 load_file(f=File[28,decoded]"<?xml version=\"1.0\"?><a></a>")
#1 doc_node(d=o0.0)
#2 child_by_existing_name(d=o0.0, n=o1.0, s=Src[0])
#3 insert_after(d=o0.0, parent=o1.0, child=o2.0, s=Src[1]b6)
```

## Stubs used

What each call does (from `project/harness/`):

`load_file` · harness/doc/doc.cpp

```cpp
New<Doc> load_file(File<4096, XmlText> f) {
    XMLDocument* d = new XMLDocument();
    return finish(d, d->LoadFile(f.path()));
}
```

`doc_node` · harness/doc/doc.cpp

```cpp
// The document as a Node, to insert top-level children or walk from the top.
New<Node> doc_node(Borrow<Doc> d) {
    XMLDocument* doc = d;
    return mk(d, static_cast<XMLNode*>(doc));
}
```

`child_by_existing_name` · harness/tree/nav.cpp

```cpp
// Pick the name of an existing child so the lookup finds something.
New<Node> child_by_existing_name(Borrow<Doc> d, Borrow<Node> n, Src<8> s) {
    stitch::require(live(d, n), "stale node");
    int cnt = 0;
    for (XMLElement* e = n->FirstChildElement(); e; e = e->NextSiblingElement()) cnt++;
    stitch::require(cnt > 0, "no child elements");
    XMLElement* e = n->FirstChildElement();
    for (size_t i = s->choice(cnt); i > 0; i--) e = e->NextSiblingElement();
    XMLElement* f = n->FirstChildElement(e->Name());
    stitch::check(f != nullptr, "FirstChildElement(name) finds an existing child");
    XMLElement* l = n->LastChildElement(e->Name());
    stitch::check(l != nullptr, "LastChildElement(name) finds an existing child");
    return mk(d, f);
}
```

`insert_after` · harness/tree/insert.cpp

```cpp
void insert_after(Borrow<Doc> d, Borrow<Node> parent, Borrow<Node> child, Src<8> s) {
    need_insertable(d, parent, child);
    int n = 0;
    for (XMLNode* c = parent->FirstChild(); c; c = c->NextSibling()) n++;
    stitch::require(n > 0, "no children");
    XMLNode* after = parent->FirstChild();
    for (size_t i = s->choice(n); i > 0; i--) after = after->NextSibling();
    XMLNode* r = parent->InsertAfterChild(after, child);
    stitch::check(r == (XMLNode*)child, "InsertAfterChild returns the node");
    stitch::check(child->PreviousSibling() == after || (XMLNode*)child == after, "inserted after");
}
```

## Sanitizer report

```
==9==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000048 (pc 0x55ac674122c3 bp 0x7ffddff61ce0 sp 0x7ffddff61c60 T0)
==9==The signal is caused by a WRITE memory access.
==9==Hint: address points to the zero page.
    #0 0x55ac674122c3 in tinyxml2::XMLNode::InsertAfterChild(tinyxml2::XMLNode*, tinyxml2::XMLNode*) /src/tinyxml2.cpp:931:29
    #1 0x55ac6739566a in insert_after(stitch::Borrow<Doc>, stitch::Borrow<Node>, stitch::Borrow<Node>, stitch::Src<8ul>) harness/tree/insert.cpp:34:26
    #2 0x55ac6739d578 in void stitch::detail::Endpoint<&insert_after(stitch::Borrow<Doc>, stitch::Borrow<Node>, stitch::Borrow<Node>, stitch::Src<8ul>)>::call<0ul, 1ul, 2ul, 3ul>(stitch::detail::Ctx&, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul>) /stitch/runtime/include/stitch.h:898:13

AddressSanitizer can not provide additional info.
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH++ cpp toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

