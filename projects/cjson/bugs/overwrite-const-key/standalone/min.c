#include <stdlib.h>
#include <string.h>
#include <cJSON.h>
#include <cJSON_Utils.h>

int main(void) {
    cJSON *object = cJSON_CreateObject();
    char *key = strdup("k");
    cJSON_AddItemToObjectCS(object, key, cJSON_CreateFalse()); /* key is borrowed (cJSON_StringIsConst) */

    /* the duplicate shares the borrowed key pointer */
    cJSON *copy = cJSON_Duplicate(cJSON_GetObjectItem(object, "k"), 1);

    /* replacing the root: overwrite_item frees copy->string without checking cJSON_StringIsConst */
    cJSON *patches = cJSON_Parse("[{\"op\":\"replace\",\"path\":\"\",\"value\":1}]");
    cJSONUtils_ApplyPatchesCaseSensitive(copy, patches);

    /* `object` still names its member with the freed key */
    cJSON_ReplaceItemInObjectCaseSensitive(object, "k", cJSON_CreateNull());
    return 0;
}
