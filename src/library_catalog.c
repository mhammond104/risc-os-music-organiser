#include "imgorg/library_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    IMGORG_LIBRARY_VERSION = 2,
    IMGORG_LIBRARY_MAXIMUM_FOLDERS = 4096,
    IMGORG_LIBRARY_MAXIMUM_IMAGES = 100000,
    IMGORG_LIBRARY_MAXIMUM_ALBUMS = 4096,
    IMGORG_LIBRARY_MAXIMUM_ALBUM_IMAGES = 100000
};

static const unsigned char imgorg_library_magic[8] = {
    'F', 'O', 'C', 'A', 'L', 'L', 'I', 'B'
};

void imgorg_folder_list_init(imgorg_folder_list *folders)
{
    if (folders == NULL) {
        return;
    }
    folders->items = NULL;
    folders->count = 0;
    folders->capacity = 0;
}

void imgorg_folder_list_destroy(imgorg_folder_list *folders)
{
    if (folders == NULL) {
        return;
    }
    free(folders->items);
    imgorg_folder_list_init(folders);
}

bool imgorg_folder_list_add(
    imgorg_folder_list *folders,
    const char *path,
    bool *added
)
{
    size_t index;
    size_t new_capacity;
    char (*new_items)[IMGORG_PATH_CAPACITY];
    int written;

    if (added != NULL) {
        *added = false;
    }
    if (folders == NULL || path == NULL || path[0] == '\0' ||
        added == NULL) {
        return false;
    }
    for (index = 0; index < folders->count; ++index) {
        if (strcmp(folders->items[index], path) == 0) {
            return true;
        }
    }
    if (folders->count == folders->capacity) {
        new_capacity = folders->capacity == 0 ? 8 : folders->capacity * 2;
        if (new_capacity < folders->capacity ||
            new_capacity > SIZE_MAX / sizeof(*new_items)) {
            return false;
        }
        new_items = realloc(
            folders->items,
            new_capacity * sizeof(*new_items)
        );
        if (new_items == NULL) {
            return false;
        }
        folders->items = new_items;
        folders->capacity = new_capacity;
    }
    written = snprintf(
        folders->items[folders->count],
        IMGORG_PATH_CAPACITY,
        "%s",
        path
    );
    if (written < 0 || written >= IMGORG_PATH_CAPACITY) {
        return false;
    }
    ++folders->count;
    *added = true;
    return true;
}

bool imgorg_folder_list_remove_at(
    imgorg_folder_list *folders,
    size_t index
)
{
    if (folders == NULL || index >= folders->count) {
        return false;
    }
    if (index + 1 < folders->count) {
        memmove(
            &folders->items[index],
            &folders->items[index + 1],
            (folders->count - index - 1) * sizeof(*folders->items)
        );
    }
    --folders->count;
    return true;
}

void imgorg_album_list_init(imgorg_album_list *albums)
{
    if (albums != NULL) {
        albums->items = NULL;
        albums->count = 0;
        albums->capacity = 0;
    }
}

void imgorg_album_list_destroy(imgorg_album_list *albums)
{
    size_t index;

    if (albums == NULL) {
        return;
    }
    for (index = 0; index < albums->count; ++index) {
        free(albums->items[index].image_paths);
    }
    free(albums->items);
    imgorg_album_list_init(albums);
}

static bool imgorg_album_name_available(
    const imgorg_album_list *albums,
    const char *name,
    size_t except
)
{
    size_t index;

    if (name == NULL || name[0] == '\0' ||
        strlen(name) >= IMGORG_ALBUM_NAME_CAPACITY) {
        return false;
    }
    for (index = 0; index < albums->count; ++index) {
        if (index != except && strcmp(albums->items[index].name, name) == 0) {
            return false;
        }
    }
    return true;
}

bool imgorg_album_list_add(
    imgorg_album_list *albums,
    const char *name,
    size_t *index_out
)
{
    imgorg_album *items;
    size_t capacity;

    if (albums == NULL || index_out == NULL ||
        !imgorg_album_name_available(albums, name, SIZE_MAX)) {
        return false;
    }
    if (albums->count == albums->capacity) {
        capacity = albums->capacity == 0 ? 8 : albums->capacity * 2;
        if (capacity < albums->capacity ||
            capacity > SIZE_MAX / sizeof(*items)) {
            return false;
        }
        items = realloc(albums->items, capacity * sizeof(*items));
        if (items == NULL) {
            return false;
        }
        albums->items = items;
        albums->capacity = capacity;
    }
    memset(&albums->items[albums->count], 0, sizeof(*albums->items));
    snprintf(
        albums->items[albums->count].name,
        sizeof(albums->items[albums->count].name),
        "%s",
        name
    );
    *index_out = albums->count++;
    return true;
}

bool imgorg_album_list_rename(
    imgorg_album_list *albums,
    size_t index,
    const char *name
)
{
    if (albums == NULL || index >= albums->count ||
        !imgorg_album_name_available(albums, name, index)) {
        return false;
    }
    snprintf(albums->items[index].name, sizeof(albums->items[index].name),
        "%s", name);
    return true;
}

bool imgorg_album_list_remove_at(imgorg_album_list *albums, size_t index)
{
    if (albums == NULL || index >= albums->count) {
        return false;
    }
    free(albums->items[index].image_paths);
    if (index + 1 < albums->count) {
        memmove(&albums->items[index], &albums->items[index + 1],
            (albums->count - index - 1) * sizeof(*albums->items));
    }
    --albums->count;
    return true;
}

bool imgorg_album_contains(const imgorg_album *album, const char *path)
{
    size_t index;

    if (album == NULL || path == NULL) {
        return false;
    }
    for (index = 0; index < album->image_count; ++index) {
        if (strcmp(album->image_paths[index], path) == 0) {
            return true;
        }
    }
    return false;
}

bool imgorg_album_add_image(imgorg_album *album, const char *path, bool *added)
{
    char (*paths)[IMGORG_PATH_CAPACITY];
    size_t capacity;

    if (added != NULL) {
        *added = false;
    }
    if (album == NULL || path == NULL || path[0] == '\0' || added == NULL) {
        return false;
    }
    if (imgorg_album_contains(album, path)) {
        return true;
    }
    if (album->image_count == album->image_capacity) {
        capacity = album->image_capacity == 0 ? 16 :
            album->image_capacity * 2;
        if (capacity < album->image_capacity ||
            capacity > SIZE_MAX / sizeof(*paths)) {
            return false;
        }
        paths = realloc(album->image_paths, capacity * sizeof(*paths));
        if (paths == NULL) {
            return false;
        }
        album->image_paths = paths;
        album->image_capacity = capacity;
    }
    snprintf(album->image_paths[album->image_count], IMGORG_PATH_CAPACITY,
        "%s", path);
    ++album->image_count;
    *added = true;
    return true;
}

void imgorg_album_list_remove_image(
    imgorg_album_list *albums,
    const char *path
)
{
    size_t album_index;

    if (albums == NULL || path == NULL) {
        return;
    }
    for (album_index = 0; album_index < albums->count; ++album_index) {
        imgorg_album *album = &albums->items[album_index];
        size_t index = 0;

        while (index < album->image_count) {
            if (strcmp(album->image_paths[index], path) != 0) {
                ++index;
                continue;
            }
            if (index + 1 < album->image_count) {
                memmove(&album->image_paths[index],
                    &album->image_paths[index + 1],
                    (album->image_count - index - 1) *
                    sizeof(*album->image_paths));
            }
            --album->image_count;
        }
    }
}

static bool imgorg_library_write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char) value;
    bytes[1] = (unsigned char) (value >> 8);
    bytes[2] = (unsigned char) (value >> 16);
    bytes[3] = (unsigned char) (value >> 24);
    return fwrite(bytes, sizeof(bytes), 1, file) == 1;
}

static bool imgorg_library_write_u64(FILE *file, uint64_t value)
{
    return imgorg_library_write_u32(file, (uint32_t) value) &&
        imgorg_library_write_u32(file, (uint32_t) (value >> 32));
}

static bool imgorg_library_read_u32(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];

    if (fread(bytes, sizeof(bytes), 1, file) != 1) {
        return false;
    }
    *value = (uint32_t) bytes[0] |
        ((uint32_t) bytes[1] << 8) |
        ((uint32_t) bytes[2] << 16) |
        ((uint32_t) bytes[3] << 24);
    return true;
}

static bool imgorg_library_read_u64(FILE *file, uint64_t *value)
{
    uint32_t low;
    uint32_t high;

    if (!imgorg_library_read_u32(file, &low) ||
        !imgorg_library_read_u32(file, &high)) {
        return false;
    }
    *value = (uint64_t) low | ((uint64_t) high << 32);
    return true;
}

static bool imgorg_library_write_string(FILE *file, const char *text)
{
    size_t length = strlen(text);

    return length <= UINT32_MAX &&
        imgorg_library_write_u32(file, (uint32_t) length) &&
        (length == 0 || fwrite(text, length, 1, file) == 1);
}

static bool imgorg_library_read_string(
    FILE *file,
    char *text,
    size_t capacity
)
{
    uint32_t length;

    if (!imgorg_library_read_u32(file, &length) ||
        length >= capacity ||
        (length > 0 && fread(text, length, 1, file) != 1)) {
        return false;
    }
    text[length] = '\0';
    return true;
}

bool imgorg_library_catalog_save(
    const char *file_name,
    const imgorg_folder_list *folders,
    const imgorg_image_list *images,
    const imgorg_album_list *albums
)
{
    char temporary[IMGORG_PATH_CAPACITY + 8];
    FILE *file;
    size_t index;
    bool ok;
    int temporary_length;

    if (file_name == NULL || folders == NULL || images == NULL ||
        albums == NULL || folders->count > UINT32_MAX ||
        images->count > UINT32_MAX || albums->count > UINT32_MAX) {
        return false;
    }
    temporary_length = snprintf(
        temporary,
        sizeof(temporary),
        "%sTmp",
        file_name
    );
    if (temporary_length < 0 ||
        (size_t) temporary_length >= sizeof(temporary)) {
        return false;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        return false;
    }
    ok = fwrite(
        imgorg_library_magic,
        sizeof(imgorg_library_magic),
        1,
        file
    ) == 1 &&
        imgorg_library_write_u32(file, IMGORG_LIBRARY_VERSION) &&
        imgorg_library_write_u32(file, (uint32_t) folders->count) &&
        imgorg_library_write_u32(file, (uint32_t) images->count) &&
        imgorg_library_write_u32(file, (uint32_t) albums->count);

    for (index = 0; ok && index < folders->count; ++index) {
        ok = imgorg_library_write_string(file, folders->items[index]);
    }
    for (index = 0; ok && index < images->count; ++index) {
        const imgorg_image_entry *entry = &images->items[index];

        ok = imgorg_library_write_u64(file, entry->size_bytes) &&
            imgorg_library_write_u32(file, entry->load_addr) &&
            imgorg_library_write_u32(file, entry->exec_addr) &&
            imgorg_library_write_u32(file, entry->riscos_filetype) &&
            imgorg_library_write_u32(file, entry->rating) &&
            imgorg_library_write_u32(file, entry->favourite ? 1u : 0u) &&
            imgorg_library_write_string(file, entry->path) &&
            imgorg_library_write_string(file, entry->leafname) &&
            imgorg_library_write_string(file, entry->tags);
    }
    for (index = 0; ok && index < albums->count; ++index) {
        const imgorg_album *album = &albums->items[index];
        size_t image_index;

        ok = album->image_count <= UINT32_MAX &&
            imgorg_library_write_string(file, album->name) &&
            imgorg_library_write_u32(file, (uint32_t) album->image_count);
        for (image_index = 0; ok && image_index < album->image_count;
             ++image_index) {
            ok = imgorg_library_write_string(
                file,
                album->image_paths[image_index]
            );
        }
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        (void) remove(temporary);
        return false;
    }
    (void) remove(file_name);
    if (rename(temporary, file_name) != 0) {
        (void) remove(temporary);
        return false;
    }
    return true;
}

bool imgorg_library_catalog_load(
    const char *file_name,
    imgorg_folder_list *folders,
    imgorg_image_list *images,
    imgorg_album_list *albums
)
{
    unsigned char magic[sizeof(imgorg_library_magic)];
    FILE *file;
    uint32_t version;
    uint32_t folder_count;
    uint32_t image_count;
    uint32_t album_count = 0;
    uint32_t index;
    bool ok = true;

    if (file_name == NULL || folders == NULL || images == NULL ||
        albums == NULL) {
        return false;
    }
    file = fopen(file_name, "rb");
    if (file == NULL) {
        return true;
    }
    if (fread(magic, sizeof(magic), 1, file) != 1 ||
        memcmp(magic, imgorg_library_magic, sizeof(magic)) != 0 ||
        !imgorg_library_read_u32(file, &version) ||
        (version != 1 && version != IMGORG_LIBRARY_VERSION) ||
        !imgorg_library_read_u32(file, &folder_count) ||
        folder_count > IMGORG_LIBRARY_MAXIMUM_FOLDERS ||
        !imgorg_library_read_u32(file, &image_count) ||
        image_count > IMGORG_LIBRARY_MAXIMUM_IMAGES ||
        (version >= 2 &&
         (!imgorg_library_read_u32(file, &album_count) ||
          album_count > IMGORG_LIBRARY_MAXIMUM_ALBUMS))) {
        fclose(file);
        return false;
    }

    for (index = 0; ok && index < folder_count; ++index) {
        char path[IMGORG_PATH_CAPACITY];
        bool added;

        ok = imgorg_library_read_string(file, path, sizeof(path)) &&
            imgorg_folder_list_add(folders, path, &added);
    }
    for (index = 0; ok && index < image_count; ++index) {
        imgorg_image_entry entry;
        uint64_t size_bytes;
        uint32_t load_addr;
        uint32_t exec_addr;
        uint32_t filetype;
        uint32_t rating;
        uint32_t favourite;
        char path[IMGORG_PATH_CAPACITY];
        char leafname[IMGORG_LEAFNAME_CAPACITY];
        char tags[IMGORG_TAGS_CAPACITY];
        bool added;

        ok = imgorg_library_read_u64(file, &size_bytes) &&
            imgorg_library_read_u32(file, &load_addr) &&
            imgorg_library_read_u32(file, &exec_addr) &&
            imgorg_library_read_u32(file, &filetype) &&
            imgorg_library_read_u32(file, &rating) &&
            rating <= 5 &&
            imgorg_library_read_u32(file, &favourite) &&
            favourite <= 1 &&
            imgorg_library_read_string(file, path, sizeof(path)) &&
            imgorg_library_read_string(file, leafname, sizeof(leafname)) &&
            imgorg_library_read_string(file, tags, sizeof(tags)) &&
            imgorg_image_entry_init(
                &entry,
                path,
                leafname,
                size_bytes,
                load_addr,
                exec_addr,
                filetype
            );
        if (ok) {
            entry.rating = rating;
            entry.favourite = favourite != 0;
            snprintf(entry.tags, sizeof(entry.tags), "%s", tags);
            ok = imgorg_image_list_append_unique(images, &entry, &added);
        }
    }
    for (index = 0; ok && index < album_count; ++index) {
        char name[IMGORG_ALBUM_NAME_CAPACITY];
        uint32_t album_image_count;
        uint32_t image_index;
        size_t album_index;

        ok = imgorg_library_read_string(file, name, sizeof(name)) &&
            imgorg_library_read_u32(file, &album_image_count) &&
            album_image_count <= IMGORG_LIBRARY_MAXIMUM_ALBUM_IMAGES &&
            imgorg_album_list_add(albums, name, &album_index);
        for (image_index = 0; ok && image_index < album_image_count;
             ++image_index) {
            char path[IMGORG_PATH_CAPACITY];
            bool added;

            ok = imgorg_library_read_string(file, path, sizeof(path)) &&
                imgorg_album_add_image(
                    &albums->items[album_index],
                    path,
                    &added
                );
        }
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        folders->count = 0;
        imgorg_image_list_clear(images);
        imgorg_album_list_destroy(albums);
        imgorg_album_list_init(albums);
    }
    return ok;
}
