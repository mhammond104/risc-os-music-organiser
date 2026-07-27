#ifndef IMGORG_APPLICATION_H
#define IMGORG_APPLICATION_H

#include <stdbool.h>

#include "aural/library_window.h"
#include "aural/music_scanner.h"
#include "aural/playlist.h"
#include "aural/player.h"
#include "aural/track_catalog.h"
#include "oslib/wimp.h"

typedef struct imgorg_application {
    wimp_t task_handle;
    wimp_i iconbar_icon;
    bool quit;
    aural_library_window library;
    aural_source_list music_sources;
    aural_track_list music_tracks;
    aural_playlist_list playlists;
    aural_playlist_list play_queue;
    aural_playlist_list ignored_tracks;
    aural_music_scanner music_scanner;
    aural_player player;
    size_t music_source_scan_index;
} imgorg_application;

os_error *imgorg_application_initialise(imgorg_application *application);
os_error *imgorg_application_run(imgorg_application *application);
void imgorg_application_finalise(imgorg_application *application);

#endif
