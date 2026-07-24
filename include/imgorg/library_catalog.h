#ifndef IMGORG_LIBRARY_CATALOG_H
#define IMGORG_LIBRARY_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

#include "imgorg/image_list.h"

typedef struct imgorg_folder_list {
    char (*items)[IMGORG_PATH_CAPACITY];
    size_t count;
    size_t capacity;
} imgorg_folder_list;

#define IMGORG_ALBUM_NAME_CAPACITY 64

typedef struct imgorg_album {
    char name[IMGORG_ALBUM_NAME_CAPACITY];
    char (*image_paths)[IMGORG_PATH_CAPACITY];
    size_t image_count;
    size_t image_capacity;
} imgorg_album;

typedef struct imgorg_album_list {
    imgorg_album *items;
    size_t count;
    size_t capacity;
} imgorg_album_list;

void imgorg_folder_list_init(imgorg_folder_list *folders);
void imgorg_folder_list_destroy(imgorg_folder_list *folders);
bool imgorg_folder_list_add(
    imgorg_folder_list *folders,
    const char *path,
    bool *added
);
bool imgorg_folder_list_remove_at(
    imgorg_folder_list *folders,
    size_t index
);
void imgorg_album_list_init(imgorg_album_list *albums);
void imgorg_album_list_destroy(imgorg_album_list *albums);
bool imgorg_album_list_add(
    imgorg_album_list *albums,
    const char *name,
    size_t *index_out
);
bool imgorg_album_list_rename(
    imgorg_album_list *albums,
    size_t index,
    const char *name
);
bool imgorg_album_list_remove_at(imgorg_album_list *albums, size_t index);
bool imgorg_album_add_image(imgorg_album *album, const char *path, bool *added);
bool imgorg_album_contains(const imgorg_album *album, const char *path);
void imgorg_album_list_remove_image(
    imgorg_album_list *albums,
    const char *path
);

bool imgorg_library_catalog_load(
    const char *file_name,
    imgorg_folder_list *folders,
    imgorg_image_list *images,
    imgorg_album_list *albums
);
bool imgorg_library_catalog_save(
    const char *file_name,
    const imgorg_folder_list *folders,
    const imgorg_image_list *images,
    const imgorg_album_list *albums
);

#endif
