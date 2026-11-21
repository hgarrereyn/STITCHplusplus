#include <quickjs.h>

int main(void) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    /* "0" becomes a tagged integer atom; JS_NewSymbolFromAtom asserts the atom is not one */
    JS_NewSymbol(ctx, "0", true);
    return 0;
}
