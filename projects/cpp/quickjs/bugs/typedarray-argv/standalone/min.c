#include <quickjs.h>

int main(void) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    JSValue argv[1] = {JS_NewArrayBufferCopy(ctx, NULL, 0)};
    /* argc is 1, but with an ArrayBuffer js_typed_array_constructor reads argv[1] and argv[2] */
    JS_NewTypedArray(ctx, 1, argv, JS_TYPED_ARRAY_UINT8C);
    return 0;
}
