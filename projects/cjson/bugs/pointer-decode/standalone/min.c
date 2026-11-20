#include <assert.h>
#include <cJSON.h>
#include <cJSON_Utils.h>

int main(void) {
    cJSON *from = cJSON_Parse("{\"a/b\":null}");
    cJSON *to = cJSON_CreateObject();
    /* [{"op":"remove","path":"/a~1b"}] */
    cJSON *patches = cJSONUtils_GeneratePatchesCaseSensitive(from, to);
    /* decode_pointer_inplace mis-decodes the escaped "a~1b", so the key is not found */
    int rc = cJSONUtils_ApplyPatchesCaseSensitive(from, patches);
    assert(rc == 0 && "generated patches apply");
    return 0;
}
