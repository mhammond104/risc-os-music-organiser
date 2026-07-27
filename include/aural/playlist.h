#ifndef AURAL_PLAYLIST_H
#define AURAL_PLAYLIST_H

#include <stdbool.h>
#include <stddef.h>

#include "aural/track_entry.h"

#define AURAL_PLAYLIST_NAME_CAPACITY 128

typedef struct aural_playlist {
    char name[AURAL_PLAYLIST_NAME_CAPACITY];
    char (*paths)[AURAL_PATH_CAPACITY];
    size_t count;
    size_t capacity;
} aural_playlist;

typedef struct aural_playlist_list {
    aural_playlist *items;
    size_t count;
    size_t capacity;
} aural_playlist_list;

void aural_playlist_list_init(aural_playlist_list *playlists);
void aural_playlist_list_destroy(aural_playlist_list *playlists);
bool aural_playlist_list_add(
    aural_playlist_list *playlists,
    const char *name,
    size_t *index
);
bool aural_playlist_list_rename(
    aural_playlist_list *playlists,
    size_t index,
    const char *name
);
bool aural_playlist_list_remove_at(
    aural_playlist_list *playlists,
    size_t index
);
bool aural_playlist_add_path(aural_playlist *playlist, const char *path);
bool aural_playlist_remove_at(aural_playlist *playlist, size_t index);
bool aural_playlist_move(
    aural_playlist *playlist,
    size_t from,
    size_t to
);
bool aural_playlist_catalog_load(
    const char *file_name,
    aural_playlist_list *playlists
);
bool aural_playlist_catalog_save(
    const char *file_name,
    const aural_playlist_list *playlists
);

#endif
