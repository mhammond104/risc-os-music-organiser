#include "imgorg/image_list.h"

#include <stdlib.h>
#include <string.h>

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

size_t imgorg_image_list_find_path(
    const imgorg_image_list *list,
    const char *path
)
{
    size_t index;

    if (list == NULL || path == NULL) {
        return SIZE_MAX;
    }
    for (index = 0; index < list->count; ++index) {
        if (strcmp(list->items[index].path, path) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

bool imgorg_image_list_append_unique(
    imgorg_image_list *list,
    const imgorg_image_entry *entry,
    bool *added
)
{
    if (added != NULL) {
        *added = false;
    }
    if (list == NULL || entry == NULL || added == NULL) {
        return false;
    }
    if (imgorg_image_list_find_path(list, entry->path) != SIZE_MAX) {
        return true;
    }
    if (!imgorg_image_list_append(list, entry)) {
        return false;
    }
    *added = true;
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

bool imgorg_image_list_remove_at(
    imgorg_image_list *list,
    size_t index
)
{
    if (list == NULL || index >= list->count) {
        return false;
    }
    if (index + 1 < list->count) {
        memmove(
            &list->items[index],
            &list->items[index + 1],
            (list->count - index - 1) * sizeof(*list->items)
        );
    }
    --list->count;
    return true;
}

void imgorg_image_list_clear(imgorg_image_list *list)
{
    if (list != NULL) {
        list->count = 0;
    }
}
