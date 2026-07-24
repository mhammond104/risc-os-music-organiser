#ifndef IMGORG_IMAGE_LIST_H
#define IMGORG_IMAGE_LIST_H

#include <stdbool.h>
#include <stddef.h>

#include "imgorg/image_entry.h"

typedef struct imgorg_image_list {
    imgorg_image_entry *items;
    size_t count;
    size_t capacity;
} imgorg_image_list;

void imgorg_image_list_init(imgorg_image_list *list);
void imgorg_image_list_destroy(imgorg_image_list *list);
bool imgorg_image_list_append(
    imgorg_image_list *list,
    const imgorg_image_entry *entry
);
bool imgorg_image_list_append_unique(
    imgorg_image_list *list,
    const imgorg_image_entry *entry,
    bool *added
);
size_t imgorg_image_list_find_path(
    const imgorg_image_list *list,
    const char *path
);
const imgorg_image_entry *imgorg_image_list_get(
    const imgorg_image_list *list,
    size_t index
);
bool imgorg_image_list_remove_at(
    imgorg_image_list *list,
    size_t index
);
void imgorg_image_list_clear(imgorg_image_list *list);

#endif
