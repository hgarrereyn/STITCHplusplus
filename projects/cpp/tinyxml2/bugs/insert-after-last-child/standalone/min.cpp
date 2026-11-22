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
