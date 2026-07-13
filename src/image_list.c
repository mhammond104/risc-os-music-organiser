#include "imgorg/image_list.h"

#include <stdlib.h>

static bool imgorg_image_list_grow(imgorg_image_list *list)
{
    size_t new_capacity;
    imgorg_image_entry *new_items;

    if (list->capacity == 0) {
        new_capacity = 32;
    } else {
        if (list->capacity > SIZE_MAX / 2) {
            return false;
        }
        new_capacity = list->capacity * 2;
    }

    if (new_capacity > SIZE_MAX / sizeof(*new_items)) {
        return false;
    }

    new_items = realloc(list->items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) {
        return false;
    }

    list->items = new_items;
    list->capacity = new_capacity;
    return true;
}

void imgorg_image_list_init(imgorg_image_list *list)
{
    if (list == NULL) {
        return;
    }

    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void imgorg_image_list_destroy(imgorg_image_list *list)
{
    if (list == NULL) {
        return;
    }

    free(list->items);
    imgorg_image_list_init(list);
}

bool imgorg_image_list_append(
    imgorg_image_list *list,
    const imgorg_image_entry *entry
)
{
    if (list == NULL || entry == NULL) {
        return false;
    }

    if (list->count == list->capacity && !imgorg_image_list_grow(list)) {
        return false;
    }

    list->items[list->count++] = *entry;
    return true;
}

const imgorg_image_entry *imgorg_image_list_get(
    const imgorg_image_list *list,
    size_t index
)
{
    if (list == NULL || index >= list->count) {
        return NULL;
    }

    return &list->items[index];
}

void imgorg_image_list_clear(imgorg_image_list *list)
{
    if (list != NULL) {
        list->count = 0;
    }
}
