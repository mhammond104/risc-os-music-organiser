#ifndef AURAL_MUSIC_SCANNER_H
#define AURAL_MUSIC_SCANNER_H

#include <stdbool.h>
#include <stddef.h>

#include "aural/track_catalog.h"
#include "oslib/os.h"

typedef struct aural_music_scanner {
    char (*directories)[AURAL_PATH_CAPACITY];
    size_t directory_count;
    size_t directory_capacity;
    size_t directory_index;
    int context;
    bool active;
} aural_music_scanner;

void aural_music_scanner_init(aural_music_scanner *scanner);
void aural_music_scanner_destroy(aural_music_scanner *scanner);
bool aural_music_scanner_start(
    aural_music_scanner *scanner,
    const char *root_path
);
os_error *aural_music_scanner_step(
    aural_music_scanner *scanner,
    aural_track_list *tracks,
    bool *changed
);

#endif
