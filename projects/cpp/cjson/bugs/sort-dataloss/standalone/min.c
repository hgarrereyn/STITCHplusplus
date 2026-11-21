#include <assert.h>
#include <cJSON.h>
#include <cJSON_Utils.h>

int main(void) {
    int nums[] = {1, 2};
    cJSON *array = cJSON_CreateIntArray(nums, 2);
    /* sorting an array (members have no names) relinks it but leaves the tail pointer
       (array->child->prev) pointing at the old last element */
    cJSONUtils_SortObjectCaseSensitive(array);
    cJSON *item = cJSON_DetachItemFromArray(array, 0);
    cJSON_AddItemToArray(array, item); /* appended after the stale tail: lost */
    assert(cJSON_GetArraySize(array) == 2 && "array grows by one");
    return 0;
}
