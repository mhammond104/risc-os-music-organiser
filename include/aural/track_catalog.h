#ifndef AURAL_TRACK_CATALOG_H
#define AURAL_TRACK_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

#include "aural/track_entry.h"

typedef struct aural_track_list {
    aural_track_entry *items;
    size_t count;
    size_t capacity;
} aural_track_list;

typedef struct aural_source_list {
    char (*items)[AURAL_PATH_CAPACITY];
    size_t count;
    size_t capacity;
} aural_source_list;

void aural_track_list_init(aural_track_list *tracks);
void aural_track_list_destroy(aural_track_list *tracks);
size_t aural_track_list_find_path(
    const aural_track_list *tracks,
    const char *path
);
bool aural_track_list_append_unique(
    aural_track_list *tracks,
    const aural_track_entry *entry,
    bool *added
);
bool aural_track_list_remove_at(aural_track_list *tracks, size_t index);

void aural_source_list_init(aural_source_list *sources);
void aural_source_list_destroy(aural_source_list *sources);
bool aural_source_list_add(
    aural_source_list *sources,
    const char *path,
    bool *added
);
bool aural_source_list_remove_at(aural_source_list *sources, size_t index);

bool aural_track_catalog_load(
    const char *file_name,
    aural_source_list *sources,
    aural_track_list *tracks
);
bool aural_track_catalog_save(
    const char *file_name,
    const aural_source_list *sources,
    const aural_track_list *tracks
);

#endif
