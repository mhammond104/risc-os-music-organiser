#include "aural/playlist.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    AURAL_PLAYLIST_VERSION = 1,
    AURAL_MAXIMUM_PLAYLISTS = 4096,
    AURAL_MAXIMUM_PLAYLIST_TRACKS = 200000
};

static const unsigned char aural_playlist_magic[8] = {
    'A', 'U', 'R', 'A', 'L', 'P', 'L', 'S'
};

static void aural_playlist_destroy(aural_playlist *playlist)
{
    free(playlist->paths);
    memset(playlist, 0, sizeof(*playlist));
}

void aural_playlist_list_init(aural_playlist_list *playlists)
{
    if (playlists != NULL) {
        playlists->items = NULL;
        playlists->count = 0;
        playlists->capacity = 0;
    }
}

void aural_playlist_list_destroy(aural_playlist_list *playlists)
{
    size_t index;

    if (playlists == NULL) {
        return;
    }
    for (index = 0; index < playlists->count; ++index) {
        aural_playlist_destroy(&playlists->items[index]);
    }
    free(playlists->items);
    aural_playlist_list_init(playlists);
}

static bool aural_playlist_name_available(
    const aural_playlist_list *playlists,
    const char *name,
    size_t except
)
{
    size_t index;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (index = 0; index < playlists->count; ++index) {
        if (index != except &&
            strcmp(playlists->items[index].name, name) == 0) {
            return false;
        }
    }
    return true;
}

bool aural_playlist_list_add(
    aural_playlist_list *playlists,
    const char *name,
    size_t *index
)
{
    aural_playlist *items;
    size_t capacity;

    if (playlists == NULL || index == NULL ||
        !aural_playlist_name_available(playlists, name, SIZE_MAX)) {
        return false;
    }
    if (playlists->count == playlists->capacity) {
        capacity = playlists->capacity == 0 ? 8 : playlists->capacity * 2;
        if (capacity < playlists->capacity ||
            capacity > SIZE_MAX / sizeof(*items)) {
            return false;
        }
        items = realloc(playlists->items, capacity * sizeof(*items));
        if (items == NULL) {
            return false;
        }
        playlists->items = items;
        playlists->capacity = capacity;
    }
    *index = playlists->count++;
    memset(&playlists->items[*index], 0, sizeof(playlists->items[*index]));
    snprintf(playlists->items[*index].name,
        sizeof(playlists->items[*index].name), "%s", name);
    return true;
}

bool aural_playlist_list_rename(
    aural_playlist_list *playlists,
    size_t index,
    const char *name
)
{
    if (playlists == NULL || index >= playlists->count ||
        !aural_playlist_name_available(playlists, name, index)) {
        return false;
    }
    snprintf(playlists->items[index].name,
        sizeof(playlists->items[index].name), "%s", name);
    return true;
}

bool aural_playlist_list_remove_at(
    aural_playlist_list *playlists,
    size_t index
)
{
    if (playlists == NULL || index >= playlists->count) {
        return false;
    }
    aural_playlist_destroy(&playlists->items[index]);
    if (index + 1 < playlists->count) {
        memmove(&playlists->items[index], &playlists->items[index + 1],
            (playlists->count - index - 1) * sizeof(*playlists->items));
    }
    --playlists->count;
    return true;
}

bool aural_playlist_add_path(aural_playlist *playlist, const char *path)
{
    char (*paths)[AURAL_PATH_CAPACITY];
    size_t capacity;
    size_t index;

    if (playlist == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    for (index = 0; index < playlist->count; ++index) {
        if (strcmp(playlist->paths[index], path) == 0) {
            return true;
        }
    }
    if (playlist->count == playlist->capacity) {
        capacity = playlist->capacity == 0 ? 16 : playlist->capacity * 2;
        if (capacity < playlist->capacity ||
            capacity > SIZE_MAX / sizeof(*paths)) {
            return false;
        }
        paths = realloc(playlist->paths, capacity * sizeof(*paths));
        if (paths == NULL) {
            return false;
        }
        playlist->paths = paths;
        playlist->capacity = capacity;
    }
    snprintf(playlist->paths[playlist->count++],
        AURAL_PATH_CAPACITY, "%s", path);
    return true;
}

bool aural_playlist_remove_at(aural_playlist *playlist, size_t index)
{
    if (playlist == NULL || index >= playlist->count) {
        return false;
    }
    if (index + 1 < playlist->count) {
        memmove(&playlist->paths[index], &playlist->paths[index + 1],
            (playlist->count - index - 1) * sizeof(*playlist->paths));
    }
    --playlist->count;
    return true;
}

bool aural_playlist_move(aural_playlist *playlist, size_t from, size_t to)
{
    char path[AURAL_PATH_CAPACITY];

    if (playlist == NULL || from >= playlist->count ||
        to >= playlist->count) {
        return false;
    }
    if (from == to) {
        return true;
    }
    memcpy(path, playlist->paths[from], sizeof(path));
    if (from < to) {
        memmove(&playlist->paths[from], &playlist->paths[from + 1],
            (to - from) * sizeof(*playlist->paths));
    } else {
        memmove(&playlist->paths[to + 1], &playlist->paths[to],
            (from - to) * sizeof(*playlist->paths));
    }
    memcpy(playlist->paths[to], path, sizeof(path));
    return true;
}

static bool aural_write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char) value,
        (unsigned char) (value >> 8),
        (unsigned char) (value >> 16),
        (unsigned char) (value >> 24)
    };
    return fwrite(bytes, sizeof(bytes), 1, file) == 1;
}

static bool aural_read_u32(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];

    if (fread(bytes, sizeof(bytes), 1, file) != 1) {
        return false;
    }
    *value = (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8) |
        ((uint32_t) bytes[2] << 16) | ((uint32_t) bytes[3] << 24);
    return true;
}

static bool aural_write_string(FILE *file, const char *text)
{
    size_t length = strlen(text);

    return length <= UINT32_MAX &&
        aural_write_u32(file, (uint32_t) length) &&
        (length == 0 || fwrite(text, length, 1, file) == 1);
}

static bool aural_read_string(FILE *file, char *text, size_t capacity)
{
    uint32_t length;

    if (!aural_read_u32(file, &length) || length >= capacity ||
        (length > 0 && fread(text, length, 1, file) != 1)) {
        return false;
    }
    text[length] = '\0';
    return true;
}

bool aural_playlist_catalog_save(
    const char *file_name,
    const aural_playlist_list *playlists
)
{
    char temporary[AURAL_PATH_CAPACITY + 8];
    FILE *file;
    size_t playlist_index;
    bool success;

    if (file_name == NULL || playlists == NULL ||
        playlists->count > UINT32_MAX ||
        snprintf(temporary, sizeof(temporary), "%sTmp", file_name) >=
            (int) sizeof(temporary)) {
        return false;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        return false;
    }
    success =
        fwrite(aural_playlist_magic, sizeof(aural_playlist_magic), 1, file) ==
            1 &&
        aural_write_u32(file, AURAL_PLAYLIST_VERSION) &&
        aural_write_u32(file, (uint32_t) playlists->count);
    for (playlist_index = 0;
         success && playlist_index < playlists->count;
         ++playlist_index) {
        const aural_playlist *playlist = &playlists->items[playlist_index];
        size_t track_index;

        success = playlist->count <= UINT32_MAX &&
            aural_write_string(file, playlist->name) &&
            aural_write_u32(file, (uint32_t) playlist->count);
        for (track_index = 0;
             success && track_index < playlist->count;
             ++track_index) {
            success = aural_write_string(file, playlist->paths[track_index]);
        }
    }
    success = fclose(file) == 0 && success;
    if (!success) {
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

bool aural_playlist_catalog_load(
    const char *file_name,
    aural_playlist_list *playlists
)
{
    unsigned char magic[sizeof(aural_playlist_magic)];
    aural_playlist_list loaded;
    uint32_t version;
    uint32_t count;
    uint32_t playlist_index;
    FILE *file;
    bool success;

    if (file_name == NULL || playlists == NULL) {
        return false;
    }
    file = fopen(file_name, "rb");
    if (file == NULL) {
        return true;
    }
    aural_playlist_list_init(&loaded);
    success = fread(magic, sizeof(magic), 1, file) == 1 &&
        memcmp(magic, aural_playlist_magic, sizeof(magic)) == 0 &&
        aural_read_u32(file, &version) &&
        version == AURAL_PLAYLIST_VERSION &&
        aural_read_u32(file, &count) &&
        count <= AURAL_MAXIMUM_PLAYLISTS;
    for (playlist_index = 0;
         success && playlist_index < count;
         ++playlist_index) {
        char name[AURAL_PLAYLIST_NAME_CAPACITY];
        uint32_t track_count;
        uint32_t track_index;
        size_t added_index;

        success = aural_read_string(file, name, sizeof(name)) &&
            aural_read_u32(file, &track_count) &&
            track_count <= AURAL_MAXIMUM_PLAYLIST_TRACKS &&
            aural_playlist_list_add(&loaded, name, &added_index);
        for (track_index = 0;
             success && track_index < track_count;
             ++track_index) {
            char path[AURAL_PATH_CAPACITY];

            success = aural_read_string(file, path, sizeof(path)) &&
                aural_playlist_add_path(
                    &loaded.items[added_index], path);
        }
    }
    success = fclose(file) == 0 && success;
    if (!success) {
        aural_playlist_list_destroy(&loaded);
        return false;
    }
    aural_playlist_list_destroy(playlists);
    *playlists = loaded;
    return true;
}
