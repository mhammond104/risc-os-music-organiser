#ifndef AURAL_LIBRARY_WINDOW_H
#define AURAL_LIBRARY_WINDOW_H

#include <stdbool.h>
#include <stddef.h>

#include "aural/player.h"
#include "aural/playlist.h"
#include "aural/artwork.h"
#include "aural/track_catalog.h"
#include "oslib/wimp.h"

typedef struct aural_library_window {
    wimp_w handle;
    wimp_w info_dialog_handle;
    wimp_w album_dialog_handle;
    wimp_w playlist_dialog_handle;
    wimp_w search_dialog_handle;
    bool created;
    bool info_dialog_created;
    bool info_dialog_open;
    bool album_dialog_created;
    bool album_dialog_open;
    bool playlist_dialog_created;
    bool playlist_dialog_open;
    bool search_dialog_created;
    bool search_dialog_open;
    bool catalog_dirty;
    char title[128];
    char info_dialog_title[64];
    char selected_artist[AURAL_ARTIST_CAPACITY];
    char selected_album[AURAL_ALBUM_CAPACITY];
    char selected_genre[AURAL_GENRE_CAPACITY];
    char search_text[AURAL_PATH_CAPACITY];
    size_t selected_playlist_index;
    unsigned int selected_rating;
    int view_kind;
    size_t selected_track_index;
    size_t info_track_index;
    int album_cell_width;
    bool thumbnail_slider_dragging;
    char info_title[AURAL_TITLE_CAPACITY];
    char info_artist[AURAL_ARTIST_CAPACITY];
    char info_album[AURAL_ALBUM_CAPACITY];
    char info_album_artist[AURAL_ARTIST_CAPACITY];
    char info_track[16];
    char info_disc[16];
    char info_year[16];
    char info_genre[AURAL_GENRE_CAPACITY];
    char info_rating[16];
    char info_tags[AURAL_TAGS_CAPACITY];
    char info_comment[AURAL_COMMENT_CAPACITY];
    char album_dialog_title[64];
    char album_edit_name[AURAL_ALBUM_CAPACITY];
    char album_edit_artist[AURAL_ARTIST_CAPACITY];
    char album_edit_year[16];
    char album_edit_genre[AURAL_GENRE_CAPACITY];
    char album_edit_artwork[AURAL_PATH_CAPACITY];
    char album_original_name[AURAL_ALBUM_CAPACITY];
    char album_original_artist[AURAL_ARTIST_CAPACITY];
    bool album_remove_artwork;
    char playlist_dialog_title[64];
    char playlist_edit_name[AURAL_PLAYLIST_NAME_CAPACITY];
    size_t playlist_edit_index;
    bool playlist_dialog_rename;
    bool playlist_add_track_after_create;
    size_t playlist_pending_track_index;
    bool context_menu_open;
    bool context_playlist_menu;
    size_t context_playlist_index;
    size_t context_track_index;
    bool track_dragging;
    size_t dragged_track_index;
    char search_dialog_title[64];
    bool shuffle;
    bool repeat;
    bool queue_active;
    os_t last_progress_redraw_cs;
    size_t left_scroll_rows;
    size_t central_scroll_rows;
    size_t right_scroll_rows;
    bool search_dialog_relink;
    size_t relink_track_index;
    aural_source_list *sources;
    aural_track_list *tracks;
    aural_playlist_list *playlists;
    aural_playlist_list *play_queue;
    aural_playlist_list *ignored_tracks;
    aural_player *player;
    struct {
        char path[AURAL_PATH_CAPACITY];
        aural_artwork artwork;
        bool attempted;
    } artwork_cache[64];
} aural_library_window;

os_error *aural_library_window_create(
    aural_library_window *window,
    aural_source_list *sources,
    aural_track_list *tracks,
    aural_playlist_list *playlists,
    aural_playlist_list *play_queue,
    aural_playlist_list *ignored_tracks,
    aural_player *player
);
os_error *aural_library_window_open(aural_library_window *window);
bool aural_library_window_owns(
    const aural_library_window *window,
    wimp_w handle
);
os_error *aural_library_window_redraw(
    aural_library_window *window,
    wimp_draw *redraw
);
os_error *aural_library_window_handle_pointer(
    aural_library_window *window,
    const wimp_pointer *pointer
);
os_error *aural_library_window_handle_key(
    aural_library_window *window,
    const wimp_key *key
);
os_error *aural_library_window_handle_close(
    aural_library_window *window,
    wimp_w handle
);
os_error *aural_library_window_handle_drag_end(
    aural_library_window *window,
    const wimp_dragged *dragged
);
os_error *aural_library_window_handle_menu_selection(
    aural_library_window *window,
    const wimp_selection *selection,
    bool *handled
);
os_error *aural_library_window_poll(aural_library_window *window);
os_error *aural_library_window_handle_scroll(
    aural_library_window *window,
    const wimp_scroll *scroll
);
bool aural_library_window_catalog_dirty(
    const aural_library_window *window
);
void aural_library_window_catalog_saved(
    aural_library_window *window
);
bool aural_library_window_assign_artwork_at(
    aural_library_window *window,
    int screen_x,
    int screen_y,
    const char *path
);
void aural_library_window_destroy(aural_library_window *window);

#endif
