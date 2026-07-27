#include "aural/library_window.h"

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "oslib/os.h"
#include "oslib/osfile.h"
#include "oslib/wimpspriteop.h"
#include "aural/audio_probe.h"
#include "aural/metadata_write.h"

enum {
    AURAL_LEFT_PANEL_WIDTH = 300,
    AURAL_RIGHT_PANEL_WIDTH = 560,
    AURAL_PLAYBACK_BAR_HEIGHT = 144,
    AURAL_PANEL_PADDING = 24,
    AURAL_ARTIST_ROW_HEIGHT = 40,
    AURAL_ALBUM_CELL_DEFAULT_WIDTH = 260,
    AURAL_ALBUM_CELL_MINIMUM_WIDTH = 160,
    AURAL_ALBUM_CELL_MAXIMUM_WIDTH = 380,
    AURAL_ALBUM_GAP = 24,
    AURAL_THUMBNAIL_SLIDER_WIDTH = 176,
    AURAL_THUMBNAIL_SLIDER_KNOB_WIDTH = 24,
    AURAL_INFO_DIALOG_WIDTH = 820,
    AURAL_INFO_DIALOG_HEIGHT = 720,
    AURAL_ALBUM_DIALOG_WIDTH = 760,
    AURAL_ALBUM_DIALOG_HEIGHT = 408,
    AURAL_PLAYLIST_DIALOG_WIDTH = 560,
    AURAL_PLAYLIST_DIALOG_HEIGHT = 180,
    AURAL_MAXIMUM_VISIBLE_ARTISTS = 256,
    AURAL_MAXIMUM_VISIBLE_ALBUMS = 512,
    AURAL_MAXIMUM_VISIBLE_TRACKS = 4096,
    AURAL_MAXIMUM_VISIBLE_GENRES = 256
};

enum {
    AURAL_VIEW_ARTIST,
    AURAL_VIEW_ALL_ALBUMS,
    AURAL_VIEW_RECENTLY_ADDED,
    AURAL_VIEW_GENRE,
    AURAL_VIEW_PLAYLIST,
    AURAL_VIEW_SEARCH,
    AURAL_VIEW_RATING,
    AURAL_VIEW_FAVOURITES,
    AURAL_VIEW_QUEUE
};

static bool aural_library_central_track_view(
    const aural_library_window *window
)
{
    return window->view_kind == AURAL_VIEW_PLAYLIST ||
        window->view_kind == AURAL_VIEW_SEARCH ||
        window->view_kind == AURAL_VIEW_RATING ||
        window->view_kind == AURAL_VIEW_FAVOURITES ||
        window->view_kind == AURAL_VIEW_QUEUE;
}

static int aural_library_compare_tracks(
    const void *left,
    const void *right
);
static size_t aural_library_selected_track_count(
    const aural_library_window *window
);

static char aural_empty_text[] = "";
static char aural_border_out[] = "R1";
static char aural_border_in[] = "R2";
static char aural_border_action[] = "R5";
static char aural_previous_label[] = "|<";
static char aural_play_label[] = "Play";
static char aural_pause_label[] = "Pause";
static char aural_resume_label[] = "Resume";
static char aural_next_label[] = ">|";
static char aural_stop_label[] = "Stop";
static char aural_information_label[] = "Information...";
static char aural_search_label[] = "Search...";
static char aural_shuffle_label[] = "Shuffle";
static char aural_repeat_label[] = "Repeat";
static char aural_thumbnail_size_label[] = "Thumbnail Size:";
static char aural_cancel_label[] = "Cancel";
static char aural_save_label[] = "Save";
static char aural_info_label_title[] = "Title:";
static char aural_info_label_artist[] = "Artist:";
static char aural_info_label_album[] = "Album:";
static char aural_info_label_album_artist[] = "Album artist:";
static char aural_info_label_track[] = "Track number:";
static char aural_info_label_disc[] = "Disc number:";
static char aural_info_label_year[] = "Year:";
static char aural_info_label_genre[] = "Genre:";
static char aural_info_label_rating[] = "Rating (0-5):";
static char aural_info_label_tags[] = "Tags:";
static char aural_info_label_comment[] = "Comments:";
static char aural_info_writeback_note[] =
    "Save writes these changes to the source MP3 file.";
static char aural_album_name_label[] = "Album title:";
static char aural_album_artist_label[] = "Album artist:";
static char aural_album_year_label[] = "Year:";
static char aural_album_genre_label[] = "Genre:";
static char aural_album_artwork_label[] = "Artwork:";
static char aural_remove_artwork_label[] = "Remove Artwork";
static char aural_new_playlist_label[] = "New Playlist...";
static char aural_playlist_name_label[] = "Playlist name:";
static char aural_ok_label[] = "OK";
static char aural_search_terms_label[16] = "Find:";
static char aural_add_playlist_label[] = "Add to Playlist...";
static char aural_play_next_label[] = "Play Next";
static char aural_add_queue_label[] = "Add to Queue";
static char aural_rating_label[] = "Rating...";
static char aural_favourite_label[] = "Favourite";
static char aural_rating_names[6][12] = {
    "Not rated", "1 star", "2 stars", "3 stars", "4 stars", "5 stars"
};
static char aural_remove_playlist_track_label[] = "Remove from Playlist";
static char aural_rename_playlist_label[] = "Rename";
static char aural_remove_playlist_label[] = "Remove Playlist";
static char aural_move_up_label[] = "Move Up";
static char aural_move_down_label[] = "Move Down";
static char aural_reveal_label[] = "Reveal in Filer";
static char aural_relink_label[] = "Relink...";
static char aural_remove_library_label[] = "Remove from Library";
static wimp_MENU(11) aural_track_menu;
static wimp_MENU(6) aural_rating_menu;
static wimp_MENU(2) aural_playlist_menu;
static wimp_menu *aural_playlist_submenu;
static bool aural_track_menu_initialised;
static bool aural_rating_menu_initialised;
static bool aural_playlist_menu_initialised;
static os_error aural_metadata_unsupported_error = {
    0,
    "Aural can currently write metadata back to MP3 files only"
};
static os_error aural_metadata_tag_error = {
    0,
    "This MP3 uses an ID3 layout that Aural cannot safely rewrite"
};
static os_error aural_metadata_write_error = {
    0,
    "Aural could not safely update the source audio file"
};
static os_error aural_metadata_memory_error = {
    0,
    "There is not enough memory to update this track's metadata"
};
static os_error aural_album_name_error = {
    0,
    "An album title is required"
};

enum {
    AURAL_INFO_TITLE_LABEL,
    AURAL_INFO_TITLE,
    AURAL_INFO_ARTIST_LABEL,
    AURAL_INFO_ARTIST,
    AURAL_INFO_ALBUM_LABEL,
    AURAL_INFO_ALBUM,
    AURAL_INFO_ALBUM_ARTIST_LABEL,
    AURAL_INFO_ALBUM_ARTIST,
    AURAL_INFO_TRACK_LABEL,
    AURAL_INFO_TRACK,
    AURAL_INFO_DISC_LABEL,
    AURAL_INFO_DISC,
    AURAL_INFO_YEAR_LABEL,
    AURAL_INFO_YEAR,
    AURAL_INFO_GENRE_LABEL,
    AURAL_INFO_GENRE,
    AURAL_INFO_RATING_LABEL,
    AURAL_INFO_RATING,
    AURAL_INFO_TAGS_LABEL,
    AURAL_INFO_TAGS,
    AURAL_INFO_COMMENT_LABEL,
    AURAL_INFO_COMMENT,
    AURAL_INFO_WRITEBACK_NOTE,
    AURAL_INFO_CANCEL,
    AURAL_INFO_SAVE,
    AURAL_INFO_ICON_COUNT
};

enum {
    AURAL_PLAYLIST_NAME_LABEL,
    AURAL_PLAYLIST_NAME,
    AURAL_PLAYLIST_CANCEL,
    AURAL_PLAYLIST_OK,
    AURAL_PLAYLIST_ICON_COUNT
};

enum {
    AURAL_ALBUM_NAME_LABEL,
    AURAL_ALBUM_NAME,
    AURAL_ALBUM_ARTIST_LABEL,
    AURAL_ALBUM_ARTIST,
    AURAL_ALBUM_YEAR_LABEL,
    AURAL_ALBUM_YEAR,
    AURAL_ALBUM_GENRE_LABEL,
    AURAL_ALBUM_GENRE,
    AURAL_ALBUM_ARTWORK_LABEL,
    AURAL_ALBUM_ARTWORK,
    AURAL_ALBUM_REMOVE_ARTWORK,
    AURAL_ALBUM_CANCEL,
    AURAL_ALBUM_SAVE,
    AURAL_ALBUM_ICON_COUNT
};

static const char *aural_track_artist(const aural_track_entry *track)
{
    if (track->album_artist[0] != '\0') {
        return track->album_artist;
    }
    return track->artist[0] != '\0' ? track->artist : "Unknown Artist";
}

static const char *aural_track_album(const aural_track_entry *track)
{
    return track->album[0] != '\0' ? track->album : "Unknown Album";
}

static const char *aural_track_format_name(aural_audio_format format)
{
    switch (format) {
    case AURAL_AUDIO_FORMAT_MP3:
        return "MP3";
    case AURAL_AUDIO_FORMAT_FLAC:
        return "FLAC";
    case AURAL_AUDIO_FORMAT_OGG_VORBIS:
        return "Ogg Vorbis";
    case AURAL_AUDIO_FORMAT_WAV:
        return "WAV";
    case AURAL_AUDIO_FORMAT_AIFF:
        return "AIFF";
    case AURAL_AUDIO_FORMAT_MIDI:
        return "MIDI";
    default:
        return "Unknown";
    }
}

static int aural_library_compare_names(
    const void *left,
    const void *right
)
{
    return strcmp((const char *) left, (const char *) right);
}

static void aural_library_initialise_menu(
    wimp_menu *menu,
    const char *title,
    int width
)
{
    memset(menu, 0, offsetof(wimp_menu, entries));
    snprintf(menu->title_data.text, sizeof(menu->title_data.text), "%s",
        title);
    menu->title_fg = wimp_COLOUR_BLACK;
    menu->title_bg = wimp_COLOUR_LIGHT_GREY;
    menu->work_fg = wimp_COLOUR_BLACK;
    menu->work_bg = wimp_COLOUR_WHITE;
    menu->width = width;
    menu->height = 44;
    menu->gap = 0;
}

static void aural_library_set_menu_entry(
    wimp_menu_entry *entry,
    char *label,
    wimp_menu_flags flags
)
{
    entry->menu_flags = flags;
    entry->icon_flags =
        wimp_ICON_TEXT | wimp_ICON_FILLED | wimp_ICON_INDIRECTED |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT);
    entry->data.indirected_text.text = label;
    entry->data.indirected_text.validation = (char *) -1;
    entry->data.indirected_text.size = strlen(label) + 1;
}

static wimp_menu *aural_library_build_playlist_submenu(
    const aural_library_window *window
)
{
    size_t count = window->playlists->count + 1;
    size_t index;

    free(aural_playlist_submenu);
    aural_playlist_submenu = calloc(1, wimp_SIZEOF_MENU(count));
    if (aural_playlist_submenu == NULL) {
        return NULL;
    }
    aural_library_initialise_menu(
        aural_playlist_submenu, "Playlists", 320);
    for (index = 0; index < window->playlists->count; ++index) {
        aural_library_set_menu_entry(
            &aural_playlist_submenu->entries[index],
            window->playlists->items[index].name, 0);
    }
    aural_library_set_menu_entry(
        &aural_playlist_submenu->entries[count - 1],
        aural_new_playlist_label,
        wimp_MENU_LAST |
            (window->playlists->count > 0 ? wimp_MENU_SEPARATE : 0));
    return aural_playlist_submenu;
}

static wimp_menu *aural_library_track_context_menu(
    const aural_library_window *window
)
{
    size_t rating;
    bool multiple = aural_library_selected_track_count(window) > 1;

    if (!aural_rating_menu_initialised) {
        memset(&aural_rating_menu, 0, sizeof(aural_rating_menu));
        aural_library_initialise_menu(
            (wimp_menu *) &aural_rating_menu, "Rating", 220);
        for (rating = 0; rating <= 5; ++rating) {
            aural_library_set_menu_entry(
                &aural_rating_menu.entries[rating],
                aural_rating_names[rating],
                rating == 5 ? wimp_MENU_LAST : 0);
        }
        aural_rating_menu_initialised = true;
    }
    if (!aural_track_menu_initialised) {
        memset(&aural_track_menu, 0, sizeof(aural_track_menu));
        aural_library_initialise_menu(
            (wimp_menu *) &aural_track_menu, "Track", 340);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[0], aural_add_playlist_label, 0);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[1], aural_play_next_label, 0);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[2], aural_add_queue_label, 0);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[3], aural_rating_label, 0);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[4], aural_favourite_label,
            wimp_MENU_SEPARATE);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[5], aural_remove_playlist_track_label,
            wimp_MENU_SEPARATE);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[6], aural_move_up_label, 0);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[7], aural_move_down_label,
            0);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[8], aural_relink_label,
            wimp_MENU_SEPARATE);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[9], aural_reveal_label, 0);
        aural_library_set_menu_entry(
            &aural_track_menu.entries[10], aural_remove_library_label,
            wimp_MENU_LAST);
        aural_track_menu_initialised = true;
    }
    aural_track_menu.entries[0].sub_menu =
        aural_library_build_playlist_submenu(window);
    aural_track_menu.entries[3].sub_menu =
        (wimp_menu *) &aural_rating_menu;
    snprintf(aural_track_menu.title_data.text,
        sizeof(aural_track_menu.title_data.text), "%s",
        multiple ? "Tracks" : "Track");
    if (window->selected_track_index < window->tracks->count &&
        window->tracks->items[
            window->selected_track_index].favourite) {
        aural_track_menu.entries[4].menu_flags |= wimp_MENU_TICKED;
    } else {
        aural_track_menu.entries[4].menu_flags &= ~wimp_MENU_TICKED;
    }
    if (window->view_kind == AURAL_VIEW_PLAYLIST ||
        window->view_kind == AURAL_VIEW_QUEUE) {
        aural_track_menu.entries[5].icon_flags &= ~wimp_ICON_SHADED;
        aural_track_menu.entries[6].icon_flags &= ~wimp_ICON_SHADED;
        aural_track_menu.entries[7].icon_flags &= ~wimp_ICON_SHADED;
    } else {
        aural_track_menu.entries[5].icon_flags |= wimp_ICON_SHADED;
        aural_track_menu.entries[6].icon_flags |= wimp_ICON_SHADED;
        aural_track_menu.entries[7].icon_flags |= wimp_ICON_SHADED;
    }
    if (multiple) {
        aural_track_menu.entries[6].icon_flags |= wimp_ICON_SHADED;
        aural_track_menu.entries[7].icon_flags |= wimp_ICON_SHADED;
        aural_track_menu.entries[8].icon_flags |= wimp_ICON_SHADED;
        aural_track_menu.entries[9].icon_flags |= wimp_ICON_SHADED;
    } else {
        aural_track_menu.entries[8].icon_flags &= ~wimp_ICON_SHADED;
        aural_track_menu.entries[9].icon_flags &= ~wimp_ICON_SHADED;
    }
    return (wimp_menu *) &aural_track_menu;
}

static wimp_menu *aural_library_playlist_context_menu(void)
{
    if (!aural_playlist_menu_initialised) {
        memset(&aural_playlist_menu, 0, sizeof(aural_playlist_menu));
        aural_library_initialise_menu(
            (wimp_menu *) &aural_playlist_menu, "Playlist", 280);
        aural_library_set_menu_entry(
            &aural_playlist_menu.entries[0],
            aural_rename_playlist_label, 0);
        aural_library_set_menu_entry(
            &aural_playlist_menu.entries[1],
            aural_remove_playlist_label,
            wimp_MENU_LAST | wimp_MENU_SEPARATE);
        aural_playlist_menu_initialised = true;
    }
    return (wimp_menu *) &aural_playlist_menu;
}

static size_t aural_library_collect_genres(
    const aural_library_window *window,
    char genres[][AURAL_GENRE_CAPACITY],
    size_t capacity
)
{
    size_t track_index;
    size_t count = 0;

    for (track_index = 0;
         track_index < window->tracks->count && count < capacity;
         ++track_index) {
        const char *genre = window->tracks->items[track_index].genre;
        size_t index;

        if (genre[0] == '\0') {
            genre = "Unknown Genre";
        }
        for (index = 0; index < count; ++index) {
            if (strcmp(genres[index], genre) == 0) {
                break;
            }
        }
        if (index == count) {
            snprintf(genres[count], AURAL_GENRE_CAPACITY, "%s", genre);
            ++count;
        }
    }
    qsort(genres, count, sizeof(genres[0]), aural_library_compare_names);
    return count;
}

static bool aural_library_contains_case_insensitive(
    const char *text,
    const char *term
)
{
    size_t term_length = strlen(term);

    if (term_length == 0) {
        return true;
    }
    while (*text != '\0') {
        size_t index;

        for (index = 0; index < term_length; ++index) {
            if (text[index] == '\0' ||
                tolower((unsigned char) text[index]) !=
                    tolower((unsigned char) term[index])) {
                break;
            }
        }
        if (index == term_length) {
            return true;
        }
        ++text;
    }
    return false;
}

static bool aural_library_track_matches_search(
    const aural_track_entry *track,
    const char *query
)
{
    const char *cursor = query;

    while (*cursor != '\0') {
        const char *end = strchr(cursor, ',');
        char term[128];
        size_t length;
        bool matches;

        if (end == NULL) {
            end = cursor + strlen(cursor);
        }
        while (cursor < end && isspace((unsigned char) *cursor)) {
            ++cursor;
        }
        while (end > cursor &&
               isspace((unsigned char) end[-1])) {
            --end;
        }
        length = (size_t) (end - cursor);
        if (length >= sizeof(term)) {
            length = sizeof(term) - 1;
        }
        memcpy(term, cursor, length);
        term[length] = '\0';
        matches = length == 0 ||
            aural_library_contains_case_insensitive(
                aural_track_entry_display_title(track), term) ||
            aural_library_contains_case_insensitive(track->artist, term) ||
            aural_library_contains_case_insensitive(
                track->album_artist, term) ||
            aural_library_contains_case_insensitive(track->album, term) ||
            aural_library_contains_case_insensitive(track->genre, term) ||
            aural_library_contains_case_insensitive(track->comment, term) ||
            aural_library_contains_case_insensitive(track->path, term);
        if (!matches) {
            return false;
        }
        cursor = *end == ',' ? end + 1 : end;
    }
    return true;
}

static size_t aural_library_collect_filtered_tracks(
    const aural_library_window *window,
    const aural_track_entry **tracks,
    size_t capacity
)
{
    size_t index;
    size_t count = 0;

    for (index = 0;
         index < window->tracks->count && count < capacity;
         ++index) {
        const aural_track_entry *track = &window->tracks->items[index];
        bool matches =
            (window->view_kind == AURAL_VIEW_SEARCH &&
             aural_library_track_matches_search(
                track, window->search_text)) ||
            (window->view_kind == AURAL_VIEW_RATING &&
             track->rating == window->selected_rating) ||
            (window->view_kind == AURAL_VIEW_FAVOURITES &&
             track->favourite);

        if (matches) {
            tracks[count++] = track;
        }
    }
    qsort(tracks, count, sizeof(tracks[0]),
        aural_library_compare_tracks);
    return count;
}

static int aural_library_compare_tracks(
    const void *left,
    const void *right
)
{
    const aural_track_entry *first =
        *(const aural_track_entry *const *) left;
    const aural_track_entry *second =
        *(const aural_track_entry *const *) right;
    unsigned int first_disc =
        first->disc_number != 0 ? first->disc_number : 1;
    unsigned int second_disc =
        second->disc_number != 0 ? second->disc_number : 1;

    if (first_disc != second_disc) {
        return first_disc < second_disc ? -1 : 1;
    }
    if (first->track_number != second->track_number) {
        if (first->track_number == 0) {
            return 1;
        }
        if (second->track_number == 0) {
            return -1;
        }
        return first->track_number < second->track_number ? -1 : 1;
    }
    return strcmp(
        aural_track_entry_display_title(first),
        aural_track_entry_display_title(second)
    );
}

static size_t aural_library_collect_artists(
    const aural_library_window *window,
    char artists[][AURAL_ARTIST_CAPACITY],
    size_t capacity
)
{
    size_t track_index;
    size_t count = 0;

    for (track_index = 0;
         track_index < window->tracks->count && count < capacity;
         ++track_index) {
        const char *artist =
            aural_track_artist(&window->tracks->items[track_index]);
        size_t index;

        for (index = 0; index < count; ++index) {
            if (strcmp(artists[index], artist) == 0) {
                break;
            }
        }
        if (index == count) {
            snprintf(artists[count], AURAL_ARTIST_CAPACITY, "%s", artist);
            ++count;
        }
    }
    qsort(artists, count, sizeof(artists[0]),
        aural_library_compare_names);
    return count;
}

static size_t aural_library_collect_albums(
    const aural_library_window *window,
    char albums[][AURAL_ALBUM_CAPACITY],
    size_t capacity
)
{
    size_t track_index;
    size_t count = 0;

    for (track_index = 0;
         track_index < window->tracks->count && count < capacity;
         ++track_index) {
        const aural_track_entry *track =
            &window->tracks->items[track_index];
        const char *album;
        size_t index;

        if (window->view_kind == AURAL_VIEW_ARTIST &&
            window->selected_artist[0] != '\0' &&
            strcmp(aural_track_artist(track), window->selected_artist) != 0) {
            continue;
        }
        if (window->view_kind == AURAL_VIEW_GENRE &&
            strcmp(track->genre[0] != '\0' ?
                    track->genre : "Unknown Genre",
                window->selected_genre) != 0) {
            continue;
        }
        album = aural_track_album(track);
        for (index = 0; index < count; ++index) {
            if (strcmp(albums[index], album) == 0) {
                break;
            }
        }
        if (index == count) {
            snprintf(albums[count], AURAL_ALBUM_CAPACITY, "%s", album);
            ++count;
        }
    }
    qsort(albums, count, sizeof(albums[0]),
        aural_library_compare_names);
    if (window->view_kind == AURAL_VIEW_RECENTLY_ADDED) {
        size_t left;

        for (left = 0; left < count; ++left) {
            size_t right;
            size_t newest = left;
            uint64_t newest_date = 0;

            for (right = left; right < count; ++right) {
                uint64_t date = 0;

                for (track_index = 0;
                     track_index < window->tracks->count;
                     ++track_index) {
                    const aural_track_entry *track =
                        &window->tracks->items[track_index];

                    if (strcmp(aural_track_album(track), albums[right]) == 0 &&
                        track->date_added_cs > date) {
                        date = track->date_added_cs;
                    }
                }
                if (date > newest_date) {
                    newest_date = date;
                    newest = right;
                }
            }
            if (newest != left) {
                char swap[AURAL_ALBUM_CAPACITY];

                memcpy(swap, albums[left], sizeof(swap));
                memcpy(albums[left], albums[newest], sizeof(swap));
                memcpy(albums[newest], swap, sizeof(swap));
            }
        }
    }
    return count;
}

static size_t aural_library_collect_album_tracks(
    const aural_library_window *window,
    const aural_track_entry **tracks,
    size_t capacity
)
{
    size_t index;
    size_t count = 0;

    for (index = 0;
         index < window->tracks->count && count < capacity;
         ++index) {
        const aural_track_entry *track = &window->tracks->items[index];

        bool artist_matches =
            window->view_kind != AURAL_VIEW_ARTIST ||
            strcmp(aural_track_artist(track),
                window->selected_artist) == 0;
        bool genre_matches =
            window->view_kind != AURAL_VIEW_GENRE ||
            strcmp(track->genre[0] != '\0' ?
                    track->genre : "Unknown Genre",
                window->selected_genre) == 0;

        if (artist_matches && genre_matches &&
            strcmp(aural_track_album(track),
                window->selected_album) == 0) {
            tracks[count++] = track;
        }
    }
    qsort(tracks, count, sizeof(tracks[0]),
        aural_library_compare_tracks);
    return count;
}

static size_t aural_library_collect_playlist_tracks(
    const aural_library_window *window,
    const aural_track_entry **tracks,
    size_t capacity
)
{
    const aural_playlist *playlist;
    size_t path_index;
    size_t count = 0;

    if (window->selected_playlist_index >= window->playlists->count) {
        return 0;
    }
    playlist = &window->playlists->items[window->selected_playlist_index];
    for (path_index = 0;
         path_index < playlist->count && count < capacity;
         ++path_index) {
        size_t track_index =
            aural_track_list_find_path(window->tracks,
                playlist->paths[path_index]);

        if (track_index != SIZE_MAX) {
            tracks[count++] = &window->tracks->items[track_index];
        }
    }
    return count;
}

static size_t aural_library_collect_queue_tracks(
    const aural_library_window *window,
    const aural_track_entry **tracks,
    size_t capacity
)
{
    size_t path_index;
    size_t count = 0;
    const aural_playlist *queue;

    if (window->play_queue->count == 0) {
        return 0;
    }
    queue = &window->play_queue->items[0];
    for (path_index = 0;
         path_index < queue->count && count < capacity;
         ++path_index) {
        size_t track_index = aural_track_list_find_path(
            window->tracks, queue->paths[path_index]);

        if (track_index != SIZE_MAX) {
            tracks[count++] = &window->tracks->items[track_index];
        }
    }
    return count;
}

static size_t aural_library_collect_central_tracks(
    const aural_library_window *window,
    const aural_track_entry **tracks,
    size_t capacity
)
{
    if (window->view_kind == AURAL_VIEW_PLAYLIST) {
        return aural_library_collect_playlist_tracks(
            window, tracks, capacity);
    }
    if (window->view_kind == AURAL_VIEW_QUEUE) {
        return aural_library_collect_queue_tracks(
            window, tracks, capacity);
    }
    return aural_library_collect_filtered_tracks(
        window, tracks, capacity);
}

static const char *aural_library_album_artwork_path(
    const aural_library_window *window,
    const char *album
)
{
    size_t index;

    for (index = 0; index < window->tracks->count; ++index) {
        const aural_track_entry *track = &window->tracks->items[index];

        if ((window->view_kind != AURAL_VIEW_ARTIST ||
             strcmp(aural_track_artist(track),
                window->selected_artist) == 0) &&
            (window->view_kind != AURAL_VIEW_GENRE ||
             strcmp(track->genre[0] != '\0' ?
                    track->genre : "Unknown Genre",
                window->selected_genre) == 0) &&
            strcmp(aural_track_album(track), album) == 0 &&
            track->artwork_path[0] != '\0') {
            return track->artwork_path;
        }
    }
    return NULL;
}

static aural_artwork *aural_library_cached_artwork(
    aural_library_window *window,
    const char *path
)
{
    size_t index;
    size_t empty = 64;

    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    for (index = 0; index < 64; ++index) {
        if (strcmp(window->artwork_cache[index].path, path) == 0) {
            return window->artwork_cache[index].artwork.area != NULL ?
                &window->artwork_cache[index].artwork : NULL;
        }
        if (empty == 64 &&
            window->artwork_cache[index].path[0] == '\0') {
            empty = index;
        }
    }
    if (empty == 64) {
        empty = 0;
        aural_artwork_destroy(&window->artwork_cache[empty].artwork);
    }
    snprintf(window->artwork_cache[empty].path,
        sizeof(window->artwork_cache[empty].path), "%s", path);
    window->artwork_cache[empty].attempted = true;
    if (!aural_artwork_load(
            &window->artwork_cache[empty].artwork,
            path,
            AURAL_ALBUM_CELL_MAXIMUM_WIDTH,
            AURAL_ALBUM_CELL_MAXIMUM_WIDTH)) {
        return NULL;
    }
    return &window->artwork_cache[empty].artwork;
}

static void aural_library_clear_artwork_cache(
    aural_library_window *window
)
{
    size_t index;

    for (index = 0; index < 64; ++index) {
        aural_artwork_destroy(&window->artwork_cache[index].artwork);
        window->artwork_cache[index].path[0] = '\0';
        window->artwork_cache[index].attempted = false;
    }
}

static size_t aural_library_track_index(
    const aural_library_window *window,
    const aural_track_entry *track
)
{
    return track == NULL ? SIZE_MAX :
        (size_t) (track - window->tracks->items);
}

static const aural_track_entry *aural_library_selected_track(
    const aural_library_window *window
)
{
    return window->selected_track_index < window->tracks->count ?
        &window->tracks->items[window->selected_track_index] : NULL;
}

static void aural_library_clear_track_selection(
    aural_library_window *window
)
{
    size_t index;

    for (index = 0; index < window->tracks->count; ++index) {
        window->tracks->items[index].selected = false;
    }
}

static size_t aural_library_selected_track_count(
    const aural_library_window *window
)
{
    size_t index;
    size_t count = 0;

    for (index = 0; index < window->tracks->count; ++index) {
        if (window->tracks->items[index].selected) {
            ++count;
        }
    }
    return count;
}

static bool aural_library_track_is_action_selected(
    const aural_library_window *window,
    size_t index
)
{
    size_t selected_count = aural_library_selected_track_count(window);

    return index < window->tracks->count &&
        (window->tracks->items[index].selected ||
         (selected_count == 0 && index == window->selected_track_index));
}

static void aural_library_select_track(
    aural_library_window *window,
    size_t index,
    bool adjust,
    bool preserve_group
)
{
    size_t replacement;

    if (index >= window->tracks->count) {
        return;
    }
    if (adjust) {
        window->tracks->items[index].selected =
            !window->tracks->items[index].selected;
        if (window->tracks->items[index].selected) {
            window->selected_track_index = index;
            return;
        }
        if (window->selected_track_index != index) {
            return;
        }
        window->selected_track_index = SIZE_MAX;
        for (replacement = 0;
             replacement < window->tracks->count;
             ++replacement) {
            if (window->tracks->items[replacement].selected) {
                window->selected_track_index = replacement;
                break;
            }
        }
        return;
    }
    if (preserve_group && window->tracks->items[index].selected) {
        window->selected_track_index = index;
        return;
    }
    aural_library_clear_track_selection(window);
    window->tracks->items[index].selected = true;
    window->selected_track_index = index;
}

static bool aural_library_track_missing(const aural_track_entry *track)
{
    fileswitch_object_type type;

    return track != NULL &&
        xosfile_read_stamped(
            track->path, &type, NULL, NULL, NULL, NULL, NULL) != NULL;
}

static void aural_library_remove_path_from_playlists(
    aural_playlist_list *playlists,
    const char *path
)
{
    size_t playlist_index;

    for (playlist_index = 0;
         playlist_index < playlists->count;
         ++playlist_index) {
        aural_playlist *playlist = &playlists->items[playlist_index];
        size_t index = playlist->count;

        while (index > 0) {
            --index;
            if (strcmp(playlist->paths[index], path) == 0) {
                (void) aural_playlist_remove_at(playlist, index);
            }
        }
    }
}

static void aural_library_replace_path_in_playlists(
    aural_playlist_list *playlists,
    const char *old_path,
    const char *new_path
)
{
    size_t playlist_index;

    for (playlist_index = 0;
         playlist_index < playlists->count;
         ++playlist_index) {
        aural_playlist *playlist = &playlists->items[playlist_index];
        size_t index;

        for (index = 0; index < playlist->count; ++index) {
            if (strcmp(playlist->paths[index], old_path) == 0) {
                snprintf(playlist->paths[index], AURAL_PATH_CAPACITY,
                    "%s", new_path);
            }
        }
    }
}

static int aural_library_album_cell_height(
    const aural_library_window *window
)
{
    return window->album_cell_width + 4;
}

static void aural_library_slider_track(
    const os_box *centre,
    os_box *track
)
{
    track->x1 = centre->x1 - 24;
    track->x0 = track->x1 - AURAL_THUMBNAIL_SLIDER_WIDTH;
    track->y0 = centre->y1 - 44;
    track->y1 = centre->y1 - 28;
}

static void aural_library_slider_knob(
    const aural_library_window *window,
    const os_box *centre,
    os_box *knob
)
{
    os_box track;
    int range = AURAL_ALBUM_CELL_MAXIMUM_WIDTH -
        AURAL_ALBUM_CELL_MINIMUM_WIDTH;
    int position;

    aural_library_slider_track(centre, &track);
    position = (window->album_cell_width -
        AURAL_ALBUM_CELL_MINIMUM_WIDTH) *
        (track.x1 - track.x0) / range;
    knob->x0 = track.x0 + position -
        AURAL_THUMBNAIL_SLIDER_KNOB_WIDTH / 2;
    knob->x1 = knob->x0 + AURAL_THUMBNAIL_SLIDER_KNOB_WIDTH;
    knob->y0 = centre->y1 - 54;
    knob->y1 = centre->y1 - 18;
}

static os_error *aural_library_set_thumbnail_width_from_pointer(
    aural_library_window *window,
    int pointer_x
)
{
    wimp_window_state state;
    os_box centre;
    os_box track;

    state.w = window->handle;
    if (xwimp_get_window_state(&state) != NULL) {
        return NULL;
    }
    centre = (os_box) {
        state.visible.x0 + AURAL_LEFT_PANEL_WIDTH,
        state.visible.y0 + AURAL_PLAYBACK_BAR_HEIGHT,
        state.visible.x1 - AURAL_RIGHT_PANEL_WIDTH,
        state.visible.y1
    };
    aural_library_slider_track(&centre, &track);
    if (pointer_x < track.x0) {
        pointer_x = track.x0;
    } else if (pointer_x > track.x1) {
        pointer_x = track.x1;
    }
    window->album_cell_width = AURAL_ALBUM_CELL_MINIMUM_WIDTH +
        (pointer_x - track.x0) *
        (AURAL_ALBUM_CELL_MAXIMUM_WIDTH -
         AURAL_ALBUM_CELL_MINIMUM_WIDTH) /
        (track.x1 - track.x0);
    return xwimp_force_redraw(window->handle, 0, -4096, 4096, 0);
}

static os_error *aural_library_redraw_transport_bars(
    aural_library_window *window
)
{
    wimp_window_state state;
    int height;
    int y0;
    int y1;

    if (window == NULL || !window->created) {
        return NULL;
    }
    state.w = window->handle;
    if (xwimp_get_window_state(&state) != NULL) {
        return NULL;
    }
    height = state.visible.y1 - state.visible.y0;
    y0 = state.yscroll - height + 20;
    y1 = state.yscroll - height + 48;
    return xwimp_force_redraw(
        window->handle,
        state.xscroll,
        y0,
        state.xscroll + state.visible.x1 - state.visible.x0,
        y1
    );
}

static os_error *aural_library_play_track(
    aural_library_window *window,
    const aural_track_entry *track
)
{
    os_error *error;

    if (track == NULL) {
        return NULL;
    }
    error = aural_player_play(window->player, track);
    if (error == NULL) {
        window->selected_track_index =
            aural_library_track_index(window, track);
        window->queue_active =
            window->view_kind == AURAL_VIEW_QUEUE;
    }
    return error;
}

static os_error *aural_library_step_track(
    aural_library_window *window,
    int direction
)
{
    const aural_track_entry *tracks[AURAL_MAXIMUM_VISIBLE_TRACKS];
    const aural_track_entry *selected =
        aural_library_selected_track(window);
    bool queue_context = window->queue_active;
    size_t count = queue_context ?
        aural_library_collect_queue_tracks(
            window, tracks, AURAL_MAXIMUM_VISIBLE_TRACKS) :
        (aural_library_central_track_view(window) ?
        aural_library_collect_central_tracks(
            window, tracks, AURAL_MAXIMUM_VISIBLE_TRACKS) :
        aural_library_collect_album_tracks(
            window, tracks, AURAL_MAXIMUM_VISIBLE_TRACKS));
    size_t index;

    if (count == 0) {
        return NULL;
    }
    if (direction > 0 && window->shuffle && count > 1) {
        size_t random_index = (size_t) rand() % count;

        if (tracks[random_index] == selected) {
            random_index = (random_index + 1) % count;
        }
        {
            os_error *error =
                aural_library_play_track(window, tracks[random_index]);
            window->queue_active = queue_context;
            return error;
        }
    }
    for (index = 0; index < count; ++index) {
        if (tracks[index] == selected ||
            aural_player_is_current(window->player, tracks[index])) {
            break;
        }
    }
    if (index == count) {
        index = direction > 0 ? 0 : count - 1;
    } else if (direction > 0) {
        if (index + 1 == count && !window->repeat) {
            return aural_player_stop(window->player);
        }
        index = (index + 1) % count;
    } else {
        index = index == 0 ? count - 1 : index - 1;
    }
    {
        os_error *error = aural_library_play_track(window, tracks[index]);
        window->queue_active = queue_context;
        return error;
    }
}

os_error *aural_library_window_poll(aural_library_window *window)
{
    os_t now;
    bool finished = false;
    os_error *error;

    if (window == NULL || window->player == NULL) {
        return NULL;
    }
    error = aural_player_refresh(window->player, &finished);
    if (error != NULL) {
        return error;
    }
    if (finished) {
        return aural_library_step_track(window, 1);
    }
    if (window->player->state != AURAL_PLAYER_PLAYING) {
        return NULL;
    }
    if (xos_read_monotonic_time(&now) == NULL &&
        (window->last_progress_redraw_cs == 0 ||
         (unsigned int) (now - window->last_progress_redraw_cs) >= 100u)) {
        window->last_progress_redraw_cs = now;
        error = aural_library_redraw_transport_bars(window);
        if (error != NULL) {
            return error;
        }
    }
    return NULL;
}

static os_error *aural_library_plot_panel(
    const wimp_draw *draw,
    const os_box *screen_box,
    wimp_colour background,
    char *validation,
    bool filled
)
{
    wimp_icon icon;
    int origin_x = draw->box.x0 - draw->xscroll;
    int origin_y = draw->box.y1 - draw->yscroll;

    memset(&icon, 0, sizeof(icon));
    icon.extent.x0 = screen_box->x0 - origin_x;
    icon.extent.x1 = screen_box->x1 - origin_x;
    icon.extent.y0 = screen_box->y0 - origin_y;
    icon.extent.y1 = screen_box->y1 - origin_y;
    icon.flags =
        wimp_ICON_TEXT | wimp_ICON_BORDER |
        (filled ? wimp_ICON_FILLED : 0) |
        wimp_ICON_INDIRECTED |
        (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT) |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (background << wimp_ICON_BG_COLOUR_SHIFT);
    icon.data.indirected_text.text = aural_empty_text;
    icon.data.indirected_text.validation = validation;
    icon.data.indirected_text.size = sizeof(aural_empty_text);
    return xwimp_plot_icon(&icon);
}

static os_error *aural_library_plot_button(
    const wimp_draw *draw,
    const os_box *screen_box,
    char *label
)
{
    wimp_icon icon;
    int origin_x = draw->box.x0 - draw->xscroll;
    int origin_y = draw->box.y1 - draw->yscroll;

    memset(&icon, 0, sizeof(icon));
    icon.extent.x0 = screen_box->x0 - origin_x;
    icon.extent.x1 = screen_box->x1 - origin_x;
    icon.extent.y0 = screen_box->y0 - origin_y;
    icon.extent.y1 = screen_box->y1 - origin_y;
    icon.flags =
        wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_FILLED |
        wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_INDIRECTED |
        (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT) |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT);
    icon.data.indirected_text.text = label;
    icon.data.indirected_text.validation = aural_border_action;
    icon.data.indirected_text.size = strlen(label) + 1;
    return xwimp_plot_icon(&icon);
}

static os_error *aural_library_text(
    const char *text,
    int x,
    int y,
    os_colour background
)
{
    os_error *error = xwimptextop_set_colour(os_COLOUR_BLACK, background);

    return error == NULL ? xwimptextop_paint(0, text, x, y) : error;
}

static void aural_library_ensure_selection(aural_library_window *window)
{
    char artists[AURAL_MAXIMUM_VISIBLE_ARTISTS][AURAL_ARTIST_CAPACITY];
    char albums[AURAL_MAXIMUM_VISIBLE_ALBUMS][AURAL_ALBUM_CAPACITY];
    size_t artist_count;
    size_t album_count;
    size_t index;
    bool found;

    if (window->view_kind == AURAL_VIEW_ARTIST) {
        artist_count = aural_library_collect_artists(
            window, artists, AURAL_MAXIMUM_VISIBLE_ARTISTS);
        found = false;
        for (index = 0; index < artist_count; ++index) {
            if (strcmp(artists[index], window->selected_artist) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            snprintf(window->selected_artist,
                sizeof(window->selected_artist),
                "%s", artist_count > 0 ? artists[0] : "");
        }
    } else {
        artist_count = 0;
    }

    if (aural_library_central_track_view(window)) {
        window->selected_album[0] = '\0';
        if (window->selected_playlist_index >= window->playlists->count) {
            window->selected_playlist_index = SIZE_MAX;
        }
        return;
    }
    album_count = aural_library_collect_albums(
        window, albums, AURAL_MAXIMUM_VISIBLE_ALBUMS);
    found = false;
    for (index = 0; index < album_count; ++index) {
        if (strcmp(albums[index], window->selected_album) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        snprintf(window->selected_album, sizeof(window->selected_album),
            "%s", album_count > 0 ? albums[0] : "");
    }
}

static os_error *aural_library_draw_content(
    aural_library_window *window,
    const wimp_draw *draw
)
{
    char artists[AURAL_MAXIMUM_VISIBLE_ARTISTS][AURAL_ARTIST_CAPACITY];
    char albums[AURAL_MAXIMUM_VISIBLE_ALBUMS][AURAL_ALBUM_CAPACITY];
    char genres[AURAL_MAXIMUM_VISIBLE_GENRES][AURAL_GENRE_CAPACITY];
    size_t artist_count;
    size_t album_count;
    size_t genre_count;
    os_box playback;
    os_box left;
    os_box centre;
    os_box right;
    os_box button;
    os_error *error;
    int y;
    size_t index;

    aural_library_ensure_selection(window);
    artist_count = aural_library_collect_artists(
        window, artists, AURAL_MAXIMUM_VISIBLE_ARTISTS);
    genre_count = aural_library_collect_genres(
        window, genres, AURAL_MAXIMUM_VISIBLE_GENRES);
    album_count = aural_library_collect_albums(
        window, albums, AURAL_MAXIMUM_VISIBLE_ALBUMS);

    playback = (os_box) {
        draw->box.x0,
        draw->box.y0,
        draw->box.x1,
        draw->box.y0 + AURAL_PLAYBACK_BAR_HEIGHT
    };
    left = (os_box) {
        draw->box.x0,
        playback.y1,
        draw->box.x0 + AURAL_LEFT_PANEL_WIDTH,
        draw->box.y1
    };
    right = (os_box) {
        draw->box.x1 - AURAL_RIGHT_PANEL_WIDTH,
        playback.y1,
        draw->box.x1,
        draw->box.y1
    };
    centre = (os_box) {
        left.x1,
        playback.y1,
        aural_library_central_track_view(window) ?
            draw->box.x1 : right.x0,
        draw->box.y1
    };

    error = aural_library_plot_panel(
        draw, &playback, wimp_COLOUR_LIGHT_GREY, aural_border_out, true);
    if (error == NULL) {
        error = aural_library_plot_panel(
            draw, &left, wimp_COLOUR_VERY_LIGHT_GREY, aural_border_in, true);
    }
    if (error == NULL) {
        error = aural_library_plot_panel(
            draw, &centre, wimp_COLOUR_VERY_LIGHT_GREY,
            aural_border_in, false);
    }
    if (error == NULL && !aural_library_central_track_view(window)) {
        error = aural_library_plot_panel(
            draw, &right, wimp_COLOUR_VERY_LIGHT_GREY, aural_border_in, true);
    }
    if (error != NULL) {
        return error;
    }

    button = (os_box) {
        playback.x0 + 24, playback.y0 + 72,
        playback.x0 + 88, playback.y1 - 20
    };
    error = aural_library_plot_button(draw, &button, aural_previous_label);
    button.x0 = button.x1 + 12;
    button.x1 = button.x0 + 112;
    if (error == NULL) {
        char *label = aural_play_label;

        if (window->player->state == AURAL_PLAYER_PLAYING) {
            label = aural_pause_label;
        } else if (window->player->state == AURAL_PLAYER_PAUSED) {
            label = aural_resume_label;
        }
        error = aural_library_plot_button(draw, &button, label);
    }
    button.x0 = button.x1 + 12;
    button.x1 = button.x0 + 64;
    if (error == NULL) {
        error = aural_library_plot_button(draw, &button, aural_next_label);
    }
    button.x0 = button.x1 + 12;
    button.x1 = button.x0 + 92;
    if (error == NULL) {
        error = aural_library_plot_button(draw, &button, aural_stop_label);
    }
    button.x0 = button.x1 + 12;
    button.x1 = button.x0 + 176;
    if (error == NULL) {
        error = aural_library_plot_button(
            draw, &button, aural_information_label);
    }
    button.x0 = button.x1 + 12;
    button.x1 = button.x0 + 132;
    if (error == NULL) {
        error = aural_library_plot_button(
            draw, &button, aural_search_label);
    }
    button.x0 = button.x1 + 12;
    button.x1 = button.x0 + 132;
    if (error == NULL) {
        error = aural_library_plot_button(
            draw, &button, aural_shuffle_label);
    }
    button.x0 = button.x1 + 12;
    button.x1 = button.x0 + 120;
    if (error == NULL) {
        error = aural_library_plot_button(
            draw, &button, aural_repeat_label);
    }
    if (error == NULL) {
        const aural_track_entry *playing = NULL;
        char status[AURAL_TITLE_CAPACITY + AURAL_ARTIST_CAPACITY + 32];

        for (index = 0; index < window->tracks->count; ++index) {
            if (aural_player_is_current(
                    window->player, &window->tracks->items[index])) {
                playing = &window->tracks->items[index];
                break;
            }
        }
        if (playing == NULL) {
            snprintf(status, sizeof(status), "Nothing playing%s%s",
                window->shuffle ? "  [Shuffle]" : "",
                window->repeat ? "  [Repeat]" : "");
        } else {
            snprintf(status, sizeof(status), "%s - %s%s%s%s",
                aural_track_artist(playing),
                aural_track_entry_display_title(playing),
                window->player->state == AURAL_PLAYER_PAUSED ?
                    " (Paused)" : "",
                window->shuffle ? "  [Shuffle]" : "",
                window->repeat ? "  [Repeat]" : "");
        }
        error = aural_library_text(
            status,
            button.x1 + 32,
            playback.y0 + 90,
            os_COLOUR_LIGHT_GREY
        );
    }
    if (error == NULL) {
        os_box seek = {
            playback.x0 + 24, playback.y0 + 24,
            playback.x1 - 280, playback.y0 + 44
        };
        os_box progress = seek;
        os_box volume = {
            playback.x1 - 240, playback.y0 + 24,
            playback.x1 - 24, playback.y0 + 44
        };
        os_box level = volume;
        uint64_t position = aural_player_position_ms(window->player);

        if (window->player->duration_ms > 0) {
            progress.x1 = progress.x0 +
                (int) ((uint64_t) (progress.x1 - progress.x0) *
                    position / window->player->duration_ms);
        } else {
            progress.x1 = progress.x0;
        }
        level.x1 = level.x0 +
            (volume.x1 - volume.x0) * (int) window->player->volume / 127;
        error = aural_library_plot_panel(
            draw, &seek, wimp_COLOUR_MID_LIGHT_GREY,
            aural_border_in, true);
        if (error == NULL && progress.x1 > progress.x0) {
            error = aural_library_plot_panel(
                draw, &progress, wimp_COLOUR_LIGHT_BLUE,
                aural_border_out, true);
        }
        if (error == NULL) {
            error = aural_library_plot_panel(
                draw, &volume, wimp_COLOUR_MID_LIGHT_GREY,
                aural_border_in, true);
        }
        if (error == NULL && level.x1 > level.x0) {
            error = aural_library_plot_panel(
                draw, &level, wimp_COLOUR_CREAM,
                aural_border_out, true);
        }
    }
    if (error != NULL) {
        return error;
    }

    y = left.y1 - 36 +
        (int) window->left_scroll_rows * AURAL_ARTIST_ROW_HEIGHT;
    error = aural_library_text(
        "LIBRARY", left.x0 + AURAL_PANEL_PADDING, y,
        os_COLOUR_VERY_LIGHT_GREY);
    y -= 44;
    {
        const char *library_rows[3] = {
            "All Albums", "Recently Added", "Play Queue"
        };
        int view_rows[3] = {
            AURAL_VIEW_ALL_ALBUMS, AURAL_VIEW_RECENTLY_ADDED,
            AURAL_VIEW_QUEUE
        };
        size_t row_index;

        for (row_index = 0; error == NULL && row_index < 3; ++row_index) {
            os_box row = {left.x0 + 8, y - 12, left.x1 - 8, y + 28};

            if (window->view_kind == view_rows[row_index]) {
                error = aural_library_plot_panel(
                    draw, &row, wimp_COLOUR_CREAM,
                    aural_border_in, true);
            }
            if (error == NULL) {
                error = aural_library_text(
                    library_rows[row_index],
                    left.x0 + AURAL_PANEL_PADDING, y,
                    os_COLOUR_VERY_LIGHT_GREY);
            }
            y -= AURAL_ARTIST_ROW_HEIGHT;
        }
    }
    y -= 12;
    if (error == NULL) {
        error = aural_library_text(
            "ARTISTS", left.x0 + AURAL_PANEL_PADDING, y,
            os_COLOUR_VERY_LIGHT_GREY);
    }
    y -= 44;
    for (index = 0;
         error == NULL && index < artist_count;
         ++index) {
        os_box row = {left.x0 + 8, y - 12, left.x1 - 8, y + 28};

        if (window->view_kind == AURAL_VIEW_ARTIST &&
            strcmp(artists[index], window->selected_artist) == 0) {
            error = aural_library_plot_panel(
                draw, &row, wimp_COLOUR_CREAM, aural_border_in, true);
        }
        if (error == NULL) {
            error = aural_library_text(
                artists[index], left.x0 + AURAL_PANEL_PADDING, y,
                os_COLOUR_VERY_LIGHT_GREY);
        }
        y -= AURAL_ARTIST_ROW_HEIGHT;
    }
    y -= 12;
    if (error == NULL) {
        error = aural_library_text(
            "GENRES", left.x0 + AURAL_PANEL_PADDING, y,
            os_COLOUR_VERY_LIGHT_GREY);
    }
    y -= 44;
    for (index = 0;
         error == NULL && index < genre_count;
         ++index) {
        os_box row = {left.x0 + 8, y - 12, left.x1 - 8, y + 28};

        if (window->view_kind == AURAL_VIEW_GENRE &&
            strcmp(genres[index], window->selected_genre) == 0) {
            error = aural_library_plot_panel(
                draw, &row, wimp_COLOUR_CREAM, aural_border_in, true);
        }
        if (error == NULL) {
            error = aural_library_text(
                genres[index], left.x0 + AURAL_PANEL_PADDING, y,
                os_COLOUR_VERY_LIGHT_GREY);
        }
        y -= AURAL_ARTIST_ROW_HEIGHT;
    }
    y -= 12;
    if (error == NULL) {
        error = aural_library_text(
            "ORGANISE", left.x0 + AURAL_PANEL_PADDING, y,
            os_COLOUR_VERY_LIGHT_GREY);
    }
    y -= 44;
    for (index = 1; error == NULL && index <= 5; ++index) {
        char label[16];
        os_box row = {left.x0 + 8, y - 12, left.x1 - 8, y + 28};

        if (window->view_kind == AURAL_VIEW_RATING &&
            window->selected_rating == index) {
            error = aural_library_plot_panel(
                draw, &row, wimp_COLOUR_CREAM, aural_border_in, true);
        }
        snprintf(label, sizeof(label), "%lu star%s",
            (unsigned long) index, index == 1 ? "" : "s");
        if (error == NULL) {
            error = aural_library_text(
                label, left.x0 + AURAL_PANEL_PADDING, y,
                os_COLOUR_VERY_LIGHT_GREY);
        }
        y -= AURAL_ARTIST_ROW_HEIGHT;
    }
    if (error == NULL) {
        os_box row = {left.x0 + 8, y - 12, left.x1 - 8, y + 28};

        if (window->view_kind == AURAL_VIEW_FAVOURITES) {
            error = aural_library_plot_panel(
                draw, &row, wimp_COLOUR_CREAM, aural_border_in, true);
        }
        if (error == NULL) {
            error = aural_library_text(
                "Favourites", left.x0 + AURAL_PANEL_PADDING, y,
                os_COLOUR_VERY_LIGHT_GREY);
        }
    }
    y -= AURAL_ARTIST_ROW_HEIGHT + 12;
    if (error == NULL) {
        error = aural_library_text(
            "PLAYLISTS", left.x0 + AURAL_PANEL_PADDING, y,
            os_COLOUR_VERY_LIGHT_GREY);
    }
    y -= 44;
    for (index = 0;
         error == NULL && index < window->playlists->count;
         ++index) {
        os_box row = {left.x0 + 8, y - 12, left.x1 - 8, y + 28};

        if (window->view_kind == AURAL_VIEW_PLAYLIST &&
            window->selected_playlist_index == index) {
            error = aural_library_plot_panel(
                draw, &row, wimp_COLOUR_CREAM, aural_border_in, true);
        }
        if (error == NULL) {
            error = aural_library_text(
                window->playlists->items[index].name,
                left.x0 + AURAL_PANEL_PADDING, y,
                os_COLOUR_VERY_LIGHT_GREY);
        }
        y -= AURAL_ARTIST_ROW_HEIGHT;
    }
    if (error == NULL) {
        error = aural_library_text(
            aural_new_playlist_label,
            left.x0 + AURAL_PANEL_PADDING, y,
            os_COLOUR_VERY_LIGHT_GREY);
    }

    if (error == NULL) {
        char heading[AURAL_ARTIST_CAPACITY + 16];
        os_box slider_track;
        os_box slider_knob;

        if (window->view_kind == AURAL_VIEW_PLAYLIST &&
            window->selected_playlist_index < window->playlists->count) {
            snprintf(heading, sizeof(heading), "PLAYLIST - %s",
                window->playlists->items[
                    window->selected_playlist_index].name);
        } else if (window->view_kind == AURAL_VIEW_SEARCH) {
            snprintf(heading, sizeof(heading), "SEARCH - %s",
                window->search_text);
        } else if (window->view_kind == AURAL_VIEW_RATING) {
            snprintf(heading, sizeof(heading), "%u STAR%s",
                window->selected_rating,
                window->selected_rating == 1 ? "" : "S");
        } else if (window->view_kind == AURAL_VIEW_FAVOURITES) {
            snprintf(heading, sizeof(heading), "FAVOURITES");
        } else if (window->view_kind == AURAL_VIEW_QUEUE) {
            snprintf(heading, sizeof(heading), "PLAY QUEUE");
        } else if (window->view_kind == AURAL_VIEW_ALL_ALBUMS) {
            snprintf(heading, sizeof(heading), "ALL ALBUMS");
        } else if (window->view_kind == AURAL_VIEW_RECENTLY_ADDED) {
            snprintf(heading, sizeof(heading), "RECENTLY ADDED");
        } else if (window->view_kind == AURAL_VIEW_GENRE) {
            snprintf(heading, sizeof(heading), "GENRE - %s",
                window->selected_genre);
        } else {
            snprintf(heading, sizeof(heading), "ALBUMS - %s",
                window->selected_artist[0] != '\0' ?
                    window->selected_artist : "Library");
        }
        error = aural_library_text(
            heading,
            centre.x0 + AURAL_PANEL_PADDING,
            centre.y1 - 40,
            os_COLOUR_WHITE
        );
        if (!aural_library_central_track_view(window)) {
            aural_library_slider_track(&centre, &slider_track);
            aural_library_slider_knob(window, &centre, &slider_knob);
        }
        if (error == NULL &&
            !aural_library_central_track_view(window)) {
            error = aural_library_text(
                aural_thumbnail_size_label,
                slider_track.x0 - 220,
                centre.y1 - 40,
                os_COLOUR_WHITE
            );
        }
        if (error == NULL &&
            !aural_library_central_track_view(window)) {
            error = aural_library_plot_panel(
                draw, &slider_track, wimp_COLOUR_MID_LIGHT_GREY,
                aural_border_in, true);
        }
        if (error == NULL &&
            !aural_library_central_track_view(window)) {
            error = aural_library_plot_panel(
                draw, &slider_knob, wimp_COLOUR_VERY_LIGHT_GREY,
                aural_border_out, true);
        }
    }
    if (error == NULL && album_count == 0 &&
        !aural_library_central_track_view(window)) {
        error = aural_library_text(
            window->sources->count == 0 ?
                "Drag a music folder here to add it to the library" :
                "This music source has not been scanned for tracks yet",
            centre.x0 + AURAL_PANEL_PADDING,
            centre.y1 - 104,
            os_COLOUR_WHITE
        );
    }
    for (index = 0;
         error == NULL && !aural_library_central_track_view(window) &&
             index < album_count;
         ++index) {
        int available = centre.x1 - centre.x0 - 2 * AURAL_PANEL_PADDING;
        int columns = available /
            (window->album_cell_width + AURAL_ALBUM_GAP);
        int column;
        int row;
        os_box cell;
        os_box artwork;
        const char *artwork_path;
        aural_artwork *cached_artwork;

        if (columns < 1) {
            columns = 1;
        }
        column = (int) index % columns;
        row = (int) index / columns;
        cell.x0 = centre.x0 + AURAL_PANEL_PADDING +
            column * (window->album_cell_width + AURAL_ALBUM_GAP);
        cell.x1 = cell.x0 + window->album_cell_width;
        cell.y1 = centre.y1 - 72 -
            (row - (int) window->central_scroll_rows) *
                (aural_library_album_cell_height(window) +
                AURAL_ALBUM_GAP);
        cell.y0 = cell.y1 - aural_library_album_cell_height(window);
        if (cell.y0 >= centre.y1 - 72 ||
            cell.y1 <= centre.y0) {
            continue;
        }
        error = aural_library_plot_panel(
            draw,
            &cell,
            strcmp(albums[index], window->selected_album) == 0 ?
                wimp_COLOUR_LIGHT_BLUE : wimp_COLOUR_VERY_LIGHT_GREY,
            aural_border_out,
            true
        );
        artwork = (os_box) {
            cell.x0 + 20, cell.y0 + 52, cell.x1 - 20, cell.y1 - 20
        };
        artwork_path = aural_library_album_artwork_path(
            window, albums[index]);
        cached_artwork = aural_library_cached_artwork(
            window, artwork_path);
        if (error == NULL) {
            error = aural_library_plot_panel(
                draw, &artwork, wimp_COLOUR_MID_LIGHT_GREY,
                aural_border_in, true);
        }
        if (error == NULL && cached_artwork != NULL) {
            error = aural_artwork_plot(cached_artwork, &artwork);
        } else if (error == NULL) {
            error = aural_library_text(
                artwork_path == NULL ?
                    "Drop artwork here" : "Artwork unavailable",
                artwork.x0 + 24,
                artwork.y0 + (artwork.y1 - artwork.y0) / 2,
                os_COLOUR_MID_LIGHT_GREY
            );
        }
        if (error == NULL) {
            error = aural_library_text(
                albums[index],
                cell.x0 + 16,
                cell.y0 + 20,
                os_COLOUR_VERY_LIGHT_GREY
            );
        }
    }

    if (error == NULL && aural_library_central_track_view(window)) {
        const aural_track_entry *central_tracks[
            AURAL_MAXIMUM_VISIBLE_TRACKS];
        size_t track_count = aural_library_collect_central_tracks(
            window, central_tracks, AURAL_MAXIMUM_VISIBLE_TRACKS);
        int track_y = centre.y1 - 104;

        error = aural_library_text(
            "#    TITLE                         ARTIST"
            "                 ALBUM                 TIME",
            centre.x0 + AURAL_PANEL_PADDING, track_y,
            os_COLOUR_WHITE);
        track_y -= 48;
        track_y += (int) window->central_scroll_rows * 44;
        if (error == NULL && track_count == 0) {
            error = aural_library_text(
                window->view_kind == AURAL_VIEW_PLAYLIST ?
                    "This playlist is empty. Drag tracks onto it or use "
                    "Menu > Add to Playlist." :
                    "No tracks match this view.",
                centre.x0 + AURAL_PANEL_PADDING, track_y,
                os_COLOUR_WHITE);
        }
        for (index = 0;
             error == NULL && index < track_count;
             ++index) {
            const aural_track_entry *track = central_tracks[index];
            char line[512];
            os_box row = {
                centre.x0 + 8, track_y - 12,
                centre.x1 - 8, track_y + 28
            };
            size_t track_index = aural_library_track_index(window, track);
            bool selected = track->selected ||
                track_index == window->selected_track_index;
            bool playing = aural_player_is_current(window->player, track);
            unsigned long seconds =
                (unsigned long) (track->duration_ms / 1000);

            if (track_y > centre.y1 - 152) {
                track_y -= 44;
                continue;
            }
            if (track_y < centre.y0 + 20) {
                break;
            }
            if (selected || playing) {
                error = aural_library_plot_panel(
                    draw, &row,
                    playing ? wimp_COLOUR_LIGHT_BLUE : wimp_COLOUR_CREAM,
                    aural_border_in, true);
            }
            snprintf(line, sizeof(line),
                "%c%2lu  %-28.28s  %-20.20s  %-20.20s  %lu:%02lu",
                aural_library_track_missing(track) ? '!' : ' ',
                (unsigned long) index + 1,
                aural_track_entry_display_title(track),
                track->artist[0] != '\0' ? track->artist : "Unknown Artist",
                aural_track_album(track),
                seconds / 60, seconds % 60);
            if (error == NULL) {
                error = aural_library_text(
                    line, centre.x0 + AURAL_PANEL_PADDING, track_y,
                    selected ? os_COLOUR_CREAM : os_COLOUR_WHITE);
            }
            track_y -= 44;
        }
    }

    if (error == NULL && !aural_library_central_track_view(window)) {
        error = aural_library_text(
            window->selected_album[0] != '\0' ?
                window->selected_album : "TRACKS",
            right.x0 + AURAL_PANEL_PADDING,
            right.y1 - 40,
            os_COLOUR_VERY_LIGHT_GREY
        );
    }
    y = right.y1 - 92 + (int) window->right_scroll_rows * 40;
    if (error == NULL &&
        !aural_library_central_track_view(window) &&
        window->selected_album[0] == '\0') {
        error = aural_library_text(
            "Select an album to see its tracks",
            right.x0 + AURAL_PANEL_PADDING,
            y,
            os_COLOUR_VERY_LIGHT_GREY
        );
    } else if (!aural_library_central_track_view(window)) {
        const aural_track_entry *album_tracks[AURAL_MAXIMUM_VISIBLE_TRACKS];
        size_t track_count;
        size_t track_number;

        track_count = aural_library_collect_album_tracks(
            window, album_tracks, AURAL_MAXIMUM_VISIBLE_TRACKS);
        for (track_number = 0;
             error == NULL && track_number < track_count;
             ++track_number) {
            const aural_track_entry *track = album_tracks[track_number];
            char line[AURAL_TITLE_CAPACITY + 32];
            os_box row_box = {
                right.x0 + 8, y - 12, right.x1 - 8, y + 28
            };
            size_t track_index = aural_library_track_index(window, track);
            bool selected = track->selected ||
                track_index == window->selected_track_index;
            bool playing = aural_player_is_current(window->player, track);
            unsigned long seconds;

            if (y > right.y1 - 92) {
                y -= 40;
                continue;
            }
            if (y < right.y0 + 300) {
                break;
            }
            if (selected || playing) {
                error = aural_library_plot_panel(
                    draw,
                    &row_box,
                    playing ? wimp_COLOUR_LIGHT_BLUE : wimp_COLOUR_CREAM,
                    aural_border_in,
                    true
                );
            }
            seconds = (unsigned long) (track->duration_ms / 1000);
            snprintf(line, sizeof(line), "%c %2u  %.32s  %lu:%02lu",
                aural_library_track_missing(track) ? '!' :
                    (playing ? '>' : ' '),
                track->track_number != 0 ?
                    track->track_number :
                    (unsigned int) track_number + 1,
                aural_track_entry_display_title(track),
                seconds / 60,
                seconds % 60);
            if (error == NULL) {
                error = aural_library_text(
                line,
                right.x0 + AURAL_PANEL_PADDING,
                y,
                playing ? os_COLOUR_LIGHT_BLUE :
                    (selected ? os_COLOUR_CREAM :
                        os_COLOUR_VERY_LIGHT_GREY)
                );
            }
            y -= 40;
        }
    }
    if (error == NULL && !aural_library_central_track_view(window)) {
        const aural_track_entry *track =
            aural_library_selected_track(window);
        os_box inspector = {
            right.x0 + 8, right.y0 + 8,
            right.x1 - 8, right.y0 + 276
        };
        int info_y = inspector.y1 - 40;

        error = aural_library_plot_panel(
            draw, &inspector, wimp_COLOUR_MID_LIGHT_GREY,
            aural_border_in, true);
        if (error == NULL) {
            error = aural_library_text(
                "AT A GLANCE",
                inspector.x0 + 16,
                info_y,
                os_COLOUR_MID_LIGHT_GREY
            );
        }
        info_y -= 44;
        if (error == NULL &&
            aural_library_selected_track_count(window) > 1) {
            char selection_text[48];

            snprintf(selection_text, sizeof(selection_text),
                "%lu tracks selected",
                (unsigned long)
                    aural_library_selected_track_count(window));
            error = aural_library_text(
                selection_text,
                inspector.x0 + 16,
                info_y,
                os_COLOUR_MID_LIGHT_GREY
            );
        } else if (error == NULL && track == NULL) {
            error = aural_library_text(
                "Select a track to inspect it",
                inspector.x0 + 16,
                info_y,
                os_COLOUR_MID_LIGHT_GREY
            );
        } else if (error == NULL) {
            char line[192];
            unsigned long seconds =
                (unsigned long) (track->duration_ms / 1000);

            snprintf(line, sizeof(line), "%s  %lu:%02lu  %u kbps",
                aural_track_format_name(track->format),
                seconds / 60, seconds % 60,
                track->bitrate_bps / 1000);
            error = aural_library_text(
                line, inspector.x0 + 16, info_y,
                os_COLOUR_MID_LIGHT_GREY);
            info_y -= 36;
            snprintf(line, sizeof(line), "%u Hz  %u channel%s",
                track->sample_rate_hz,
                track->channels,
                track->channels == 1 ? "" : "s");
            if (error == NULL) {
                error = aural_library_text(
                    line, inspector.x0 + 16, info_y,
                    os_COLOUR_MID_LIGHT_GREY);
            }
            info_y -= 36;
            snprintf(line, sizeof(line), "Year: %u   Genre: %.24s",
                track->year, track->genre[0] != '\0' ?
                    track->genre : "Not set");
            if (error == NULL) {
                error = aural_library_text(
                    line, inspector.x0 + 16, info_y,
                    os_COLOUR_MID_LIGHT_GREY);
            }
            info_y -= 36;
            snprintf(line, sizeof(line), "Rating: %u/5   Size: %lu KB",
                track->rating,
                (unsigned long) (track->size_bytes / 1024));
            if (error == NULL) {
                error = aural_library_text(
                    line, inspector.x0 + 16, info_y,
                    os_COLOUR_MID_LIGHT_GREY);
            }
            info_y -= 36;
            snprintf(line, sizeof(line), "Source: %.54s", track->path);
            if (error == NULL) {
                error = aural_library_text(
                    line, inspector.x0 + 16, info_y,
                    os_COLOUR_MID_LIGHT_GREY);
            }
        }
    }
    return error;
}

static void aural_library_define_info_label(
    wimp_icon *icon,
    int y,
    char *text
)
{
    memset(icon, 0, sizeof(*icon));
    icon->extent = (os_box) {24, y - 40, 220, y};
    icon->flags =
        wimp_ICON_TEXT | wimp_ICON_VCENTRED | wimp_ICON_INDIRECTED |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT);
    icon->data.indirected_text.text = text;
    icon->data.indirected_text.validation = (char *) -1;
    icon->data.indirected_text.size = strlen(text) + 1;
}

static void aural_library_define_info_field(
    wimp_icon *icon,
    int y,
    char *text,
    size_t capacity
)
{
    memset(icon, 0, sizeof(*icon));
    icon->extent = (os_box) {236, y - 40, 796, y};
    icon->flags =
        wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_FILLED |
        wimp_ICON_VCENTRED | wimp_ICON_INDIRECTED |
        (wimp_BUTTON_WRITABLE << wimp_ICON_BUTTON_TYPE_SHIFT) |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT);
    icon->data.indirected_text.text = text;
    icon->data.indirected_text.validation = (char *) -1;
    icon->data.indirected_text.size = capacity;
}

static void aural_library_define_info_button(
    wimp_icon *icon,
    const os_box *extent,
    char *text
)
{
    memset(icon, 0, sizeof(*icon));
    icon->extent = *extent;
    icon->flags =
        wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_FILLED |
        wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_INDIRECTED |
        (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT) |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT);
    icon->data.indirected_text.text = text;
    icon->data.indirected_text.validation = aural_border_action;
    icon->data.indirected_text.size = strlen(text) + 1;
}

static os_error *aural_library_create_info_dialog(
    aural_library_window *window
)
{
    static char *labels[] = {
        aural_info_label_title,
        aural_info_label_artist,
        aural_info_label_album,
        aural_info_label_album_artist,
        aural_info_label_track,
        aural_info_label_disc,
        aural_info_label_year,
        aural_info_label_genre,
        aural_info_label_rating,
        aural_info_label_tags,
        aural_info_label_comment
    };
    char *fields[] = {
        window->info_title,
        window->info_artist,
        window->info_album,
        window->info_album_artist,
        window->info_track,
        window->info_disc,
        window->info_year,
        window->info_genre,
        window->info_rating,
        window->info_tags,
        window->info_comment
    };
    size_t capacities[] = {
        sizeof(window->info_title),
        sizeof(window->info_artist),
        sizeof(window->info_album),
        sizeof(window->info_album_artist),
        sizeof(window->info_track),
        sizeof(window->info_disc),
        sizeof(window->info_year),
        sizeof(window->info_genre),
        sizeof(window->info_rating),
        sizeof(window->info_tags),
        sizeof(window->info_comment)
    };
    size_t index;
    int y = -24;
    os_box cancel = {516, -696, 640, -640};
    os_box save = {656, -696, 796, -640};
    wimp_window *dialog = calloc(
        1,
        sizeof(*dialog) +
            (AURAL_INFO_ICON_COUNT - 1) * sizeof(wimp_icon)
    );
    wimp_icon *icons;
    os_error *error;

    if (dialog == NULL) {
        static os_error no_memory = {
            0, "There is not enough memory to create the track information window"
        };

        return &no_memory;
    }
    icons = (wimp_icon *) ((char *) dialog +
        offsetof(wimp_window, icons));
    dialog->visible = (os_box) {
        200, 200,
        200 + AURAL_INFO_DIALOG_WIDTH,
        200 + AURAL_INFO_DIALOG_HEIGHT
    };
    dialog->next = wimp_TOP;
    dialog->flags =
        wimp_WINDOW_MOVEABLE | wimp_WINDOW_TITLE_ICON |
        wimp_WINDOW_CLOSE_ICON | wimp_WINDOW_AUTO_REDRAW |
        wimp_WINDOW_NEW_FORMAT;
    dialog->title_fg = wimp_COLOUR_BLACK;
    dialog->title_bg = wimp_COLOUR_LIGHT_GREY;
    dialog->work_fg = wimp_COLOUR_BLACK;
    dialog->work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
    dialog->extent = (os_box) {
        0, -AURAL_INFO_DIALOG_HEIGHT,
        AURAL_INFO_DIALOG_WIDTH, 0
    };
    dialog->title_flags =
        wimp_ICON_TEXT | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED;
    dialog->title_data.indirected_text.text = window->info_dialog_title;
    dialog->title_data.indirected_text.validation = (char *) -1;
    dialog->title_data.indirected_text.size =
        sizeof(window->info_dialog_title);
    dialog->work_flags =
        wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT;
    dialog->sprite_area = wimpspriteop_AREA;
    dialog->xmin = AURAL_INFO_DIALOG_WIDTH;
    dialog->ymin = AURAL_INFO_DIALOG_HEIGHT;
    dialog->icon_count = AURAL_INFO_ICON_COUNT;

    for (index = 0; index < 11; ++index) {
        aural_library_define_info_label(
            &icons[index * 2], y, labels[index]);
        aural_library_define_info_field(
            &icons[index * 2 + 1],
            y,
            fields[index],
            capacities[index]
        );
        y -= 52;
    }
    aural_library_define_info_label(
        &icons[AURAL_INFO_WRITEBACK_NOTE],
        -596,
        aural_info_writeback_note
    );
    icons[AURAL_INFO_WRITEBACK_NOTE].extent.x1 = 500;
    aural_library_define_info_button(
        &icons[AURAL_INFO_CANCEL], &cancel, aural_cancel_label);
    aural_library_define_info_button(
        &icons[AURAL_INFO_SAVE], &save, aural_save_label);
    error = xwimp_create_window(dialog, &window->info_dialog_handle);
    free(dialog);
    return error;
}

static os_error *aural_library_create_album_dialog(
    aural_library_window *window
)
{
    static char *labels[] = {
        aural_album_name_label,
        aural_album_artist_label,
        aural_album_year_label,
        aural_album_genre_label,
        aural_album_artwork_label
    };
    char *fields[] = {
        window->album_edit_name,
        window->album_edit_artist,
        window->album_edit_year,
        window->album_edit_genre,
        window->album_edit_artwork
    };
    size_t capacities[] = {
        sizeof(window->album_edit_name),
        sizeof(window->album_edit_artist),
        sizeof(window->album_edit_year),
        sizeof(window->album_edit_genre),
        sizeof(window->album_edit_artwork)
    };
    wimp_window *dialog = calloc(
        1,
        sizeof(*dialog) +
            (AURAL_ALBUM_ICON_COUNT - 1) * sizeof(wimp_icon)
    );
    wimp_icon *icons;
    os_box remove = {236, -332, 460, -276};
    os_box cancel = {476, -384, 600, -328};
    os_box save = {616, -384, 736, -328};
    size_t index;
    int y = -24;
    os_error *error;

    if (dialog == NULL) {
        return &aural_metadata_memory_error;
    }
    icons = (wimp_icon *) ((char *) dialog +
        offsetof(wimp_window, icons));
    dialog->visible = (os_box) {
        200, 200, 200 + AURAL_ALBUM_DIALOG_WIDTH,
        200 + AURAL_ALBUM_DIALOG_HEIGHT
    };
    dialog->next = wimp_TOP;
    dialog->flags =
        wimp_WINDOW_MOVEABLE | wimp_WINDOW_TITLE_ICON |
        wimp_WINDOW_CLOSE_ICON | wimp_WINDOW_AUTO_REDRAW |
        wimp_WINDOW_NEW_FORMAT;
    dialog->title_fg = wimp_COLOUR_BLACK;
    dialog->title_bg = wimp_COLOUR_LIGHT_GREY;
    dialog->work_fg = wimp_COLOUR_BLACK;
    dialog->work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
    dialog->extent = (os_box) {
        0, -AURAL_ALBUM_DIALOG_HEIGHT,
        AURAL_ALBUM_DIALOG_WIDTH, 0
    };
    dialog->title_flags =
        wimp_ICON_TEXT | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED;
    dialog->title_data.indirected_text.text =
        window->album_dialog_title;
    dialog->title_data.indirected_text.validation = (char *) -1;
    dialog->title_data.indirected_text.size =
        sizeof(window->album_dialog_title);
    dialog->work_flags =
        wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT;
    dialog->sprite_area = wimpspriteop_AREA;
    dialog->xmin = AURAL_ALBUM_DIALOG_WIDTH;
    dialog->ymin = AURAL_ALBUM_DIALOG_HEIGHT;
    dialog->icon_count = AURAL_ALBUM_ICON_COUNT;
    for (index = 0; index < 5; ++index) {
        aural_library_define_info_label(
            &icons[index * 2], y, labels[index]);
        aural_library_define_info_field(
            &icons[index * 2 + 1], y,
            fields[index], capacities[index]);
        icons[index * 2 + 1].extent.x1 = 736;
        y -= 52;
    }
    aural_library_define_info_button(
        &icons[AURAL_ALBUM_REMOVE_ARTWORK],
        &remove, aural_remove_artwork_label);
    aural_library_define_info_button(
        &icons[AURAL_ALBUM_CANCEL], &cancel, aural_cancel_label);
    aural_library_define_info_button(
        &icons[AURAL_ALBUM_SAVE], &save, aural_save_label);
    error = xwimp_create_window(dialog, &window->album_dialog_handle);
    free(dialog);
    return error;
}

static os_error *aural_library_create_playlist_dialog(
    aural_library_window *window
)
{
    wimp_window *dialog = calloc(
        1, sizeof(*dialog) +
            (AURAL_PLAYLIST_ICON_COUNT - 1) * sizeof(wimp_icon));
    wimp_icon *icons;
    os_box cancel = {280, -156, 404, -100};
    os_box ok = {420, -156, 536, -100};
    os_error *error;

    if (dialog == NULL) {
        return &aural_metadata_memory_error;
    }
    icons = (wimp_icon *) ((char *) dialog +
        offsetof(wimp_window, icons));
    dialog->visible = (os_box) {
        200, 200, 200 + AURAL_PLAYLIST_DIALOG_WIDTH,
        200 + AURAL_PLAYLIST_DIALOG_HEIGHT
    };
    dialog->next = wimp_TOP;
    dialog->flags =
        wimp_WINDOW_MOVEABLE | wimp_WINDOW_TITLE_ICON |
        wimp_WINDOW_CLOSE_ICON | wimp_WINDOW_AUTO_REDRAW |
        wimp_WINDOW_NEW_FORMAT;
    dialog->title_fg = wimp_COLOUR_BLACK;
    dialog->title_bg = wimp_COLOUR_LIGHT_GREY;
    dialog->work_fg = wimp_COLOUR_BLACK;
    dialog->work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
    dialog->extent = (os_box) {
        0, -AURAL_PLAYLIST_DIALOG_HEIGHT,
        AURAL_PLAYLIST_DIALOG_WIDTH, 0
    };
    dialog->title_flags =
        wimp_ICON_TEXT | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED;
    dialog->title_data.indirected_text.text =
        window->playlist_dialog_title;
    dialog->title_data.indirected_text.validation = (char *) -1;
    dialog->title_data.indirected_text.size =
        sizeof(window->playlist_dialog_title);
    dialog->work_flags =
        wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT;
    dialog->sprite_area = wimpspriteop_AREA;
    dialog->xmin = AURAL_PLAYLIST_DIALOG_WIDTH;
    dialog->ymin = AURAL_PLAYLIST_DIALOG_HEIGHT;
    dialog->icon_count = AURAL_PLAYLIST_ICON_COUNT;
    aural_library_define_info_label(
        &icons[AURAL_PLAYLIST_NAME_LABEL], -28,
        aural_playlist_name_label);
    icons[AURAL_PLAYLIST_NAME_LABEL].extent.x1 = 196;
    aural_library_define_info_field(
        &icons[AURAL_PLAYLIST_NAME], -28,
        window->playlist_edit_name,
        sizeof(window->playlist_edit_name));
    icons[AURAL_PLAYLIST_NAME].extent.x0 = 212;
    icons[AURAL_PLAYLIST_NAME].extent.x1 = 536;
    aural_library_define_info_button(
        &icons[AURAL_PLAYLIST_CANCEL], &cancel, aural_cancel_label);
    aural_library_define_info_button(
        &icons[AURAL_PLAYLIST_OK], &ok, aural_ok_label);
    error = xwimp_create_window(dialog, &window->playlist_dialog_handle);
    free(dialog);
    return error;
}

static os_error *aural_library_create_search_dialog(
    aural_library_window *window
)
{
    wimp_window *dialog = calloc(
        1, sizeof(*dialog) +
            (AURAL_PLAYLIST_ICON_COUNT - 1) * sizeof(wimp_icon));
    wimp_icon *icons;
    os_box cancel = {280, -156, 404, -100};
    os_box ok = {420, -156, 536, -100};
    os_error *error;

    if (dialog == NULL) {
        return &aural_metadata_memory_error;
    }
    icons = (wimp_icon *) ((char *) dialog +
        offsetof(wimp_window, icons));
    dialog->visible = (os_box) {
        200, 200, 200 + AURAL_PLAYLIST_DIALOG_WIDTH,
        200 + AURAL_PLAYLIST_DIALOG_HEIGHT
    };
    dialog->next = wimp_TOP;
    dialog->flags =
        wimp_WINDOW_MOVEABLE | wimp_WINDOW_TITLE_ICON |
        wimp_WINDOW_CLOSE_ICON | wimp_WINDOW_AUTO_REDRAW |
        wimp_WINDOW_NEW_FORMAT;
    dialog->title_fg = wimp_COLOUR_BLACK;
    dialog->title_bg = wimp_COLOUR_LIGHT_GREY;
    dialog->work_fg = wimp_COLOUR_BLACK;
    dialog->work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
    dialog->extent = (os_box) {
        0, -AURAL_PLAYLIST_DIALOG_HEIGHT,
        AURAL_PLAYLIST_DIALOG_WIDTH, 0
    };
    dialog->title_flags =
        wimp_ICON_TEXT | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED;
    dialog->title_data.indirected_text.text =
        window->search_dialog_title;
    dialog->title_data.indirected_text.validation = (char *) -1;
    dialog->title_data.indirected_text.size =
        sizeof(window->search_dialog_title);
    dialog->work_flags =
        wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT;
    dialog->sprite_area = wimpspriteop_AREA;
    dialog->xmin = AURAL_PLAYLIST_DIALOG_WIDTH;
    dialog->ymin = AURAL_PLAYLIST_DIALOG_HEIGHT;
    dialog->icon_count = AURAL_PLAYLIST_ICON_COUNT;
    aural_library_define_info_label(
        &icons[AURAL_PLAYLIST_NAME_LABEL], -28,
        aural_search_terms_label);
    icons[AURAL_PLAYLIST_NAME_LABEL].extent.x1 = 120;
    aural_library_define_info_field(
        &icons[AURAL_PLAYLIST_NAME], -28,
        window->search_text, sizeof(window->search_text));
    icons[AURAL_PLAYLIST_NAME].extent.x0 = 136;
    icons[AURAL_PLAYLIST_NAME].extent.x1 = 536;
    aural_library_define_info_button(
        &icons[AURAL_PLAYLIST_CANCEL], &cancel, aural_cancel_label);
    aural_library_define_info_button(
        &icons[AURAL_PLAYLIST_OK], &ok, aural_ok_label);
    error = xwimp_create_window(dialog, &window->search_dialog_handle);
    free(dialog);
    return error;
}

static os_error *aural_library_show_search_dialog(
    aural_library_window *window
)
{
    wimp_window_state parent;
    wimp_open open;
    os_error *error;

    if (!window->search_dialog_created) {
        return NULL;
    }
    window->search_dialog_relink = false;
    snprintf(aural_search_terms_label,
        sizeof(aural_search_terms_label), "Find:");
    snprintf(window->search_dialog_title,
        sizeof(window->search_dialog_title), "Search Music");
    parent.w = window->handle;
    error = xwimp_get_window_state(&parent);
    if (error != NULL) {
        return error;
    }
    open.w = window->search_dialog_handle;
    open.visible.x0 = parent.visible.x0 +
        (parent.visible.x1 - parent.visible.x0 -
         AURAL_PLAYLIST_DIALOG_WIDTH) / 2;
    open.visible.y0 = parent.visible.y0 +
        (parent.visible.y1 - parent.visible.y0 -
         AURAL_PLAYLIST_DIALOG_HEIGHT) / 2;
    open.visible.x1 = open.visible.x0 + AURAL_PLAYLIST_DIALOG_WIDTH;
    open.visible.y1 = open.visible.y0 + AURAL_PLAYLIST_DIALOG_HEIGHT;
    open.xscroll = 0;
    open.yscroll = 0;
    open.next = wimp_TOP;
    error = xwimp_open_window(&open);
    if (error != NULL) {
        return error;
    }
    window->search_dialog_open = true;
    return xwimp_set_caret_position(
        window->search_dialog_handle, AURAL_PLAYLIST_NAME,
        0, 0, -1, (int) strlen(window->search_text));
}

static os_error *aural_library_accept_search_dialog(
    aural_library_window *window
)
{
    if (window->search_dialog_relink) {
        aural_track_entry *track;
        aural_track_entry probed;
        fileswitch_object_type object_type;
        bits file_type;
        int size;
        const char *leafname;
        char old_path[AURAL_PATH_CAPACITY];
        os_error *error;

        if (window->relink_track_index >= window->tracks->count) {
            return NULL;
        }
        error = xosfile_read_stamped(
            window->search_text, &object_type, NULL, NULL,
            &size, NULL, &file_type);
        leafname = strrchr(window->search_text, '.');
        leafname = leafname != NULL ? leafname + 1 : window->search_text;
        if (error != NULL || object_type != fileswitch_IS_FILE ||
            !aural_audio_probe_file(
                window->search_text, leafname,
                size < 0 ? 0u : (uint64_t) size,
                file_type, &probed)) {
            static os_error invalid_relink = {
                0, "That is not a readable supported audio file"
            };
            return &invalid_relink;
        }
        track = &window->tracks->items[window->relink_track_index];
        snprintf(old_path, sizeof(old_path), "%s", track->path);
        snprintf(track->path, sizeof(track->path), "%s", probed.path);
        snprintf(track->leafname, sizeof(track->leafname), "%s",
            probed.leafname);
        track->size_bytes = probed.size_bytes;
        track->riscos_filetype = probed.riscos_filetype;
        track->format = probed.format;
        track->duration_ms = probed.duration_ms;
        track->sample_rate_hz = probed.sample_rate_hz;
        track->bitrate_bps = probed.bitrate_bps;
        track->channels = probed.channels;
        aural_library_replace_path_in_playlists(
            window->playlists, old_path, track->path);
        aural_library_replace_path_in_playlists(
            window->play_queue, old_path, track->path);
        window->catalog_dirty = true;
        window->search_dialog_relink = false;
        window->search_dialog_open = false;
        (void) xwimp_close_window(window->search_dialog_handle);
        return xwimp_force_redraw(
            window->handle, 0, -4096, 4096, 0);
    }
    window->search_dialog_open = false;
    window->view_kind = AURAL_VIEW_SEARCH;
    window->central_scroll_rows = 0;
    window->right_scroll_rows = 0;
    window->selected_track_index = SIZE_MAX;
    (void) xwimp_close_window(window->search_dialog_handle);
    return xwimp_force_redraw(window->handle, 0, -4096, 4096, 0);
}

static os_error *aural_library_show_relink_dialog(
    aural_library_window *window,
    size_t track_index
)
{
    os_error *error;

    if (track_index >= window->tracks->count) {
        return NULL;
    }
    snprintf(window->search_text, sizeof(window->search_text), "%s",
        window->tracks->items[track_index].path);
    error = aural_library_show_search_dialog(window);
    if (error != NULL) {
        return error;
    }
    window->search_dialog_relink = true;
    window->relink_track_index = track_index;
    snprintf(window->search_dialog_title,
        sizeof(window->search_dialog_title), "Relink Track");
    snprintf(aural_search_terms_label,
        sizeof(aural_search_terms_label), "File:");
    return xwimp_force_redraw(
        window->search_dialog_handle, 0,
        -AURAL_PLAYLIST_DIALOG_HEIGHT,
        AURAL_PLAYLIST_DIALOG_WIDTH, 0);
}

static os_error *aural_library_show_playlist_dialog(
    aural_library_window *window,
    bool rename,
    size_t playlist_index
)
{
    wimp_window_state parent;
    wimp_open open;
    os_error *error;

    if (!window->playlist_dialog_created ||
        (rename && playlist_index >= window->playlists->count)) {
        return NULL;
    }
    window->playlist_dialog_rename = rename;
    window->playlist_edit_index = playlist_index;
    snprintf(window->playlist_dialog_title,
        sizeof(window->playlist_dialog_title), "%s",
        rename ? "Rename Playlist" : "New Playlist");
    snprintf(window->playlist_edit_name,
        sizeof(window->playlist_edit_name), "%s",
        rename ? window->playlists->items[playlist_index].name :
            "New Playlist");
    parent.w = window->handle;
    error = xwimp_get_window_state(&parent);
    if (error != NULL) {
        return error;
    }
    open.w = window->playlist_dialog_handle;
    open.visible.x0 = parent.visible.x0 +
        (parent.visible.x1 - parent.visible.x0 -
         AURAL_PLAYLIST_DIALOG_WIDTH) / 2;
    open.visible.y0 = parent.visible.y0 +
        (parent.visible.y1 - parent.visible.y0 -
         AURAL_PLAYLIST_DIALOG_HEIGHT) / 2;
    open.visible.x1 = open.visible.x0 + AURAL_PLAYLIST_DIALOG_WIDTH;
    open.visible.y1 = open.visible.y0 + AURAL_PLAYLIST_DIALOG_HEIGHT;
    open.xscroll = 0;
    open.yscroll = 0;
    open.next = wimp_TOP;
    error = xwimp_open_window(&open);
    if (error != NULL) {
        return error;
    }
    window->playlist_dialog_open = true;
    (void) xwimp_set_caret_position(
        window->playlist_dialog_handle, AURAL_PLAYLIST_NAME,
        0, 0, -1, (int) strlen(window->playlist_edit_name));
    return NULL;
}

static os_error *aural_library_accept_playlist_dialog(
    aural_library_window *window
)
{
    size_t index = window->playlist_edit_index;
    bool success;

    if (window->playlist_edit_name[0] == '\0') {
        static os_error empty_name = {
            0, "A playlist name is required"
        };
        return &empty_name;
    }
    if (window->playlist_dialog_rename) {
        success = aural_playlist_list_rename(
            window->playlists, index, window->playlist_edit_name);
    } else {
        success = aural_playlist_list_add(
            window->playlists, window->playlist_edit_name, &index);
    }
    if (!success) {
        static os_error duplicate_name = {
            0, "That playlist name is already in use"
        };
        return &duplicate_name;
    }
    window->view_kind = AURAL_VIEW_PLAYLIST;
    window->central_scroll_rows = 0;
    window->right_scroll_rows = 0;
    window->selected_playlist_index = index;
    if (window->playlist_add_track_after_create &&
        window->playlist_pending_track_index < window->tracks->count) {
        size_t track_index;
        size_t selected_count =
            aural_library_selected_track_count(window);

        for (track_index = 0;
             track_index < window->tracks->count;
             ++track_index) {
            if ((selected_count > 0 &&
                 window->tracks->items[track_index].selected) ||
                (selected_count == 0 &&
                 track_index == window->playlist_pending_track_index)) {
                success = aural_playlist_add_path(
                    &window->playlists->items[index],
                    window->tracks->items[track_index].path);
                if (!success) {
                    return &aural_metadata_memory_error;
                }
            }
        }
        window->selected_track_index =
            window->playlist_pending_track_index;
    } else {
        window->selected_track_index = SIZE_MAX;
    }
    window->playlist_add_track_after_create = false;
    window->catalog_dirty = true;
    window->playlist_dialog_open = false;
    (void) xwimp_close_window(window->playlist_dialog_handle);
    return xwimp_force_redraw(window->handle, 0, -4096, 4096, 0);
}

static os_error *aural_library_show_track_info(
    aural_library_window *window
)
{
    const aural_track_entry *track =
        aural_library_selected_track(window);
    wimp_window_state parent;
    wimp_open open;
    os_error *error;

    if (track == NULL || !window->info_dialog_created) {
        return NULL;
    }
    window->info_track_index = window->selected_track_index;
    snprintf(window->info_dialog_title,
        sizeof(window->info_dialog_title), "Track Information");
    snprintf(window->info_title, sizeof(window->info_title), "%s",
        track->title);
    snprintf(window->info_artist, sizeof(window->info_artist), "%s",
        track->artist);
    snprintf(window->info_album, sizeof(window->info_album), "%s",
        track->album);
    snprintf(window->info_album_artist,
        sizeof(window->info_album_artist), "%s", track->album_artist);
    snprintf(window->info_track, sizeof(window->info_track), "%u",
        track->track_number);
    snprintf(window->info_disc, sizeof(window->info_disc), "%u",
        track->disc_number);
    snprintf(window->info_year, sizeof(window->info_year), "%u",
        track->year);
    snprintf(window->info_genre, sizeof(window->info_genre), "%s",
        track->genre);
    snprintf(window->info_rating, sizeof(window->info_rating), "%u",
        track->rating);
    snprintf(window->info_tags, sizeof(window->info_tags), "%s",
        track->tags);
    snprintf(window->info_comment, sizeof(window->info_comment), "%s",
        track->comment);

    parent.w = window->handle;
    error = xwimp_get_window_state(&parent);
    if (error != NULL) {
        return error;
    }
    open.w = window->info_dialog_handle;
    open.visible.x0 = parent.visible.x0 +
        (parent.visible.x1 - parent.visible.x0 -
         AURAL_INFO_DIALOG_WIDTH) / 2;
    open.visible.y0 = parent.visible.y0 +
        (parent.visible.y1 - parent.visible.y0 -
         AURAL_INFO_DIALOG_HEIGHT) / 2;
    open.visible.x1 = open.visible.x0 + AURAL_INFO_DIALOG_WIDTH;
    open.visible.y1 = open.visible.y0 + AURAL_INFO_DIALOG_HEIGHT;
    open.xscroll = 0;
    open.yscroll = 0;
    open.next = wimp_TOP;
    error = xwimp_open_window(&open);
    if (error != NULL) {
        return error;
    }
    window->info_dialog_open = true;
    (void) xwimp_set_icon_state(
        window->info_dialog_handle, AURAL_INFO_TITLE, 0, 0);
    return xwimp_set_caret_position(
        window->info_dialog_handle,
        AURAL_INFO_TITLE,
        0, 0, -1,
        (int) strlen(window->info_title)
    );
}

static os_error *aural_library_show_album_info(
    aural_library_window *window
)
{
    const aural_track_entry *tracks[AURAL_MAXIMUM_VISIBLE_TRACKS];
    size_t count;
    const aural_track_entry *track;
    wimp_window_state parent;
    wimp_open open;
    os_error *error;

    if (!window->album_dialog_created ||
        window->selected_album[0] == '\0') {
        return NULL;
    }
    count = aural_library_collect_album_tracks(
        window, tracks, AURAL_MAXIMUM_VISIBLE_TRACKS);
    if (count == 0) {
        return NULL;
    }
    track = tracks[0];
    snprintf(window->album_dialog_title,
        sizeof(window->album_dialog_title), "Album Information");
    snprintf(window->album_original_name,
        sizeof(window->album_original_name), "%s",
        window->selected_album);
    snprintf(window->album_original_artist,
        sizeof(window->album_original_artist), "%s",
        window->selected_artist);
    snprintf(window->album_edit_name,
        sizeof(window->album_edit_name), "%s",
        window->selected_album);
    snprintf(window->album_edit_artist,
        sizeof(window->album_edit_artist), "%s",
        track->album_artist[0] != '\0' ?
            track->album_artist : aural_track_artist(track));
    snprintf(window->album_edit_year,
        sizeof(window->album_edit_year), "%u", track->year);
    snprintf(window->album_edit_genre,
        sizeof(window->album_edit_genre), "%s", track->genre);
    snprintf(window->album_edit_artwork,
        sizeof(window->album_edit_artwork), "%s",
        track->artwork_path);
    window->album_remove_artwork = false;

    parent.w = window->handle;
    error = xwimp_get_window_state(&parent);
    if (error != NULL) {
        return error;
    }
    open.w = window->album_dialog_handle;
    open.visible.x0 = parent.visible.x0 +
        (parent.visible.x1 - parent.visible.x0 -
         AURAL_ALBUM_DIALOG_WIDTH) / 2;
    open.visible.y0 = parent.visible.y0 +
        (parent.visible.y1 - parent.visible.y0 -
         AURAL_ALBUM_DIALOG_HEIGHT) / 2;
    open.visible.x1 = open.visible.x0 + AURAL_ALBUM_DIALOG_WIDTH;
    open.visible.y1 = open.visible.y0 + AURAL_ALBUM_DIALOG_HEIGHT;
    open.xscroll = 0;
    open.yscroll = 0;
    open.next = wimp_TOP;
    error = xwimp_open_window(&open);
    if (error != NULL) {
        return error;
    }
    window->album_dialog_open = true;
    (void) xwimp_set_icon_state(
        window->album_dialog_handle, AURAL_ALBUM_NAME, 0, 0);
    return xwimp_set_caret_position(
        window->album_dialog_handle,
        AURAL_ALBUM_NAME,
        0, 0, -1,
        (int) strlen(window->album_edit_name)
    );
}

static os_error *aural_library_show_information(
    aural_library_window *window
)
{
    return aural_library_selected_track(window) != NULL ?
        aural_library_show_track_info(window) :
        aural_library_show_album_info(window);
}

static unsigned int aural_library_parse_unsigned(
    const char *text,
    unsigned int maximum
)
{
    unsigned long value = strtoul(text, NULL, 10);

    return value > maximum ? maximum : (unsigned int) value;
}

static os_error *aural_library_accept_track_info(
    aural_library_window *window
)
{
    aural_track_entry *track;
    aural_track_entry updated;
    aural_metadata_write_result write_result;

    if (window->info_track_index >= window->tracks->count) {
        return xwimp_close_window(window->info_dialog_handle);
    }
    track = &window->tracks->items[window->info_track_index];
    updated = *track;
    snprintf(updated.title, sizeof(updated.title), "%s", window->info_title);
    snprintf(updated.artist, sizeof(updated.artist), "%s",
        window->info_artist);
    snprintf(updated.album, sizeof(updated.album), "%s", window->info_album);
    snprintf(updated.album_artist, sizeof(updated.album_artist), "%s",
        window->info_album_artist);
    updated.track_number =
        aural_library_parse_unsigned(window->info_track, 999);
    updated.disc_number =
        aural_library_parse_unsigned(window->info_disc, 99);
    updated.year = aural_library_parse_unsigned(window->info_year, 9999);
    snprintf(updated.genre, sizeof(updated.genre), "%s", window->info_genre);
    updated.rating =
        aural_library_parse_unsigned(window->info_rating, 5);
    snprintf(updated.tags, sizeof(updated.tags), "%s", window->info_tags);
    snprintf(updated.comment, sizeof(updated.comment), "%s",
        window->info_comment);
    if (aural_player_is_current(window->player, track)) {
        os_error *error = aural_player_stop(window->player);

        if (error != NULL) {
            return error;
        }
    }
    write_result = aural_metadata_write_file(&updated);
    if (write_result == AURAL_METADATA_WRITE_UNSUPPORTED_FORMAT) {
        return &aural_metadata_unsupported_error;
    }
    if (write_result == AURAL_METADATA_WRITE_UNSUPPORTED_TAG) {
        return &aural_metadata_tag_error;
    }
    if (write_result == AURAL_METADATA_WRITE_NO_MEMORY) {
        return &aural_metadata_memory_error;
    }
    if (write_result != AURAL_METADATA_WRITE_OK) {
        return &aural_metadata_write_error;
    }
    (void) xosfile_set_type(updated.path, updated.riscos_filetype);
    {
        fileswitch_object_type object_type;
        bits load_addr;
        bits exec_addr;
        bits file_type;
        int size;

        if (xosfile_read_stamped(
                updated.path, &object_type, &load_addr, &exec_addr,
                &size, NULL, &file_type) == NULL &&
            size >= 0) {
            updated.size_bytes = (uint64_t) size;
        }
    }
    *track = updated;
    window->catalog_dirty = true;
    window->info_dialog_open = false;
    snprintf(window->selected_artist, sizeof(window->selected_artist), "%s",
        aural_track_artist(track));
    snprintf(window->selected_album, sizeof(window->selected_album), "%s",
        aural_track_album(track));
    (void) xwimp_close_window(window->info_dialog_handle);
    return xwimp_force_redraw(window->handle, 0, -4096, 4096, 0);
}

static os_error *aural_library_write_result_error(
    aural_metadata_write_result result
)
{
    if (result == AURAL_METADATA_WRITE_UNSUPPORTED_TAG) {
        return &aural_metadata_tag_error;
    }
    if (result == AURAL_METADATA_WRITE_NO_MEMORY) {
        return &aural_metadata_memory_error;
    }
    return result == AURAL_METADATA_WRITE_OK ?
        NULL : &aural_metadata_write_error;
}

static os_error *aural_library_accept_album_info(
    aural_library_window *window
)
{
    size_t index;
    unsigned int year;
    bool changed = false;

    if (window->album_edit_name[0] == '\0') {
        return &aural_album_name_error;
    }
    year = aural_library_parse_unsigned(window->album_edit_year, 9999);
    for (index = 0; index < window->tracks->count; ++index) {
        aural_track_entry *track = &window->tracks->items[index];
        aural_track_entry updated;

        if ((window->view_kind == AURAL_VIEW_ARTIST &&
             strcmp(aural_track_artist(track),
                window->album_original_artist) != 0) ||
            strcmp(aural_track_album(track),
                window->album_original_name) != 0) {
            continue;
        }
        updated = *track;
        snprintf(updated.album, sizeof(updated.album), "%s",
            window->album_edit_name);
        snprintf(updated.album_artist, sizeof(updated.album_artist), "%s",
            window->album_edit_artist);
        updated.year = year;
        snprintf(updated.genre, sizeof(updated.genre), "%s",
            window->album_edit_genre);
        snprintf(updated.artwork_path, sizeof(updated.artwork_path), "%s",
            window->album_remove_artwork ?
                "" : window->album_edit_artwork);
        if (updated.format == AURAL_AUDIO_FORMAT_MP3) {
            aural_metadata_write_result result;
            os_error *error;

            if (aural_player_is_current(window->player, track)) {
                error = aural_player_stop(window->player);
                if (error != NULL) {
                    return error;
                }
            }
            result = aural_metadata_write_file(&updated);
            error = aural_library_write_result_error(result);
            if (error != NULL) {
                if (changed) {
                    window->catalog_dirty = true;
                }
                return error;
            }
            (void) xosfile_set_type(
                updated.path, updated.riscos_filetype);
        }
        *track = updated;
        changed = true;
    }
    if (changed) {
        window->catalog_dirty = true;
        snprintf(window->selected_artist,
            sizeof(window->selected_artist), "%s",
            window->album_edit_artist[0] != '\0' ?
                window->album_edit_artist :
                window->album_original_artist);
        snprintf(window->selected_album,
            sizeof(window->selected_album), "%s",
            window->album_edit_name);
        window->selected_track_index = SIZE_MAX;
        aural_library_clear_artwork_cache(window);
    }
    window->album_dialog_open = false;
    (void) xwimp_close_window(window->album_dialog_handle);
    return xwimp_force_redraw(window->handle, 0, -4096, 4096, 0);
}

static void aural_library_read_desktop_size(int *width, int *height)
{
    int x_limit = 0;
    int y_limit = 0;
    int x_eigen = 1;
    int y_eigen = 1;

    (void) xos_read_mode_variable(
        os_CURRENT_MODE, os_MODEVAR_XEIG_FACTOR, &x_eigen, NULL);
    (void) xos_read_mode_variable(
        os_CURRENT_MODE, os_MODEVAR_YEIG_FACTOR, &y_eigen, NULL);
    (void) xos_read_mode_variable(
        os_CURRENT_MODE, os_MODEVAR_XWIND_LIMIT, &x_limit, NULL);
    (void) xos_read_mode_variable(
        os_CURRENT_MODE, os_MODEVAR_YWIND_LIMIT, &y_limit, NULL);
    *width = x_limit > 0 ? (x_limit + 1) << x_eigen : 1920;
    *height = y_limit > 0 ? (y_limit + 1) << y_eigen : 1200;
}

os_error *aural_library_window_create(
    aural_library_window *window,
    aural_source_list *sources,
    aural_track_list *tracks,
    aural_playlist_list *playlists,
    aural_playlist_list *play_queue,
    aural_playlist_list *ignored_tracks,
    aural_player *player
)
{
    wimp_window definition;
    int desktop_width;
    int desktop_height;
    int width;
    int height;
    os_error *error;

    if (window == NULL || sources == NULL || tracks == NULL ||
        playlists == NULL || play_queue == NULL ||
        ignored_tracks == NULL ||
        player == NULL) {
        return NULL;
    }
    memset(window, 0, sizeof(*window));
    window->sources = sources;
    window->tracks = tracks;
    window->playlists = playlists;
    window->play_queue = play_queue;
    window->ignored_tracks = ignored_tracks;
    window->player = player;
    window->selected_track_index = SIZE_MAX;
    window->selected_playlist_index = SIZE_MAX;
    window->view_kind = AURAL_VIEW_ALL_ALBUMS;
    window->album_cell_width = AURAL_ALBUM_CELL_DEFAULT_WIDTH;
    snprintf(window->title, sizeof(window->title), "Aural - Music Library");
    aural_library_read_desktop_size(&desktop_width, &desktop_height);
    width = desktop_width * 3 / 4;
    height = desktop_height * 3 / 4;

    memset(&definition, 0, sizeof(definition));
    definition.visible.x0 = (desktop_width - width) / 2;
    definition.visible.y0 = 96 +
        (desktop_height - 96 - height) / 2;
    definition.visible.x1 = definition.visible.x0 + width;
    definition.visible.y1 = definition.visible.y0 + height;
    definition.next = wimp_TOP;
    definition.flags =
        wimp_WINDOW_MOVEABLE | wimp_WINDOW_BACK_ICON |
        wimp_WINDOW_CLOSE_ICON | wimp_WINDOW_TITLE_ICON |
        wimp_WINDOW_NEW_FORMAT | wimp_WINDOW_IGNORE_XEXTENT |
        wimp_WINDOW_IGNORE_YEXTENT;
    definition.title_fg = wimp_COLOUR_BLACK;
    definition.title_bg = wimp_COLOUR_LIGHT_GREY;
    definition.work_fg = wimp_COLOUR_BLACK;
    definition.work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
    definition.extra_flags = wimp_WINDOW_ALWAYS3D;
    definition.extent = (os_box) {0, -4096, 4096, 0};
    definition.title_flags =
        wimp_ICON_TEXT | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED;
    definition.title_data.indirected_text.text = window->title;
    definition.title_data.indirected_text.validation = (char *) -1;
    definition.title_data.indirected_text.size = sizeof(window->title);
    definition.work_flags =
        wimp_BUTTON_DOUBLE_CLICK_DRAG << wimp_ICON_BUTTON_TYPE_SHIFT;
    definition.sprite_area = wimpspriteop_AREA;
    definition.xmin = 900;
    definition.ymin = 480;
    definition.icon_count = 0;

    error = xwimp_create_window(&definition, &window->handle);
    if (error != NULL) {
        return error;
    }
    window->created = true;
    error = aural_library_create_info_dialog(window);
    if (error != NULL) {
        (void) xwimp_delete_window(window->handle);
        window->handle = 0;
        window->created = false;
        return error;
    }
    window->info_dialog_created = true;
    error = aural_library_create_album_dialog(window);
    if (error != NULL) {
        (void) xwimp_delete_window(window->info_dialog_handle);
        (void) xwimp_delete_window(window->handle);
        window->info_dialog_handle = 0;
        window->handle = 0;
        window->info_dialog_created = false;
        window->created = false;
        return error;
    }
    window->album_dialog_created = true;
    error = aural_library_create_playlist_dialog(window);
    if (error != NULL) {
        (void) xwimp_delete_window(window->album_dialog_handle);
        (void) xwimp_delete_window(window->info_dialog_handle);
        (void) xwimp_delete_window(window->handle);
        window->album_dialog_handle = 0;
        window->info_dialog_handle = 0;
        window->handle = 0;
        window->album_dialog_created = false;
        window->info_dialog_created = false;
        window->created = false;
        return error;
    }
    window->playlist_dialog_created = true;
    error = aural_library_create_search_dialog(window);
    if (error != NULL) {
        (void) xwimp_delete_window(window->playlist_dialog_handle);
        (void) xwimp_delete_window(window->album_dialog_handle);
        (void) xwimp_delete_window(window->info_dialog_handle);
        (void) xwimp_delete_window(window->handle);
        window->playlist_dialog_created = false;
        window->album_dialog_created = false;
        window->info_dialog_created = false;
        window->created = false;
        return error;
    }
    window->search_dialog_created = true;
    return NULL;
}

os_error *aural_library_window_open(aural_library_window *window)
{
    wimp_window_state state;
    os_error *error;

    if (window == NULL || !window->created) {
        return NULL;
    }
    state.w = window->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }
    state.next = wimp_TOP;
    return xwimp_open_window((wimp_open *) &state);
}

bool aural_library_window_owns(
    const aural_library_window *window,
    wimp_w handle
)
{
    return window != NULL && window->created && window->handle == handle;
}

os_error *aural_library_window_redraw(
    aural_library_window *window,
    wimp_draw *redraw
)
{
    os_error *error;
    osbool more;

    if (!aural_library_window_owns(window, redraw->w)) {
        return NULL;
    }
    error = xwimp_redraw_window(redraw, &more);
    while (error == NULL && more) {
        error = aural_library_draw_content(window, redraw);
        if (error == NULL) {
            error = xwimp_get_rectangle(redraw, &more);
        }
    }
    return error;
}

static void aural_library_scroll_offset(
    size_t *offset,
    size_t maximum,
    int direction,
    size_t amount
)
{
    if (direction > 0) {
        *offset = *offset > amount ? *offset - amount : 0;
    } else if (direction < 0) {
        if (*offset > maximum || amount > maximum - *offset) {
            *offset = maximum;
        } else {
            *offset += amount;
        }
    }
}

static void aural_library_reset_content_scroll(
    aural_library_window *window
)
{
    window->central_scroll_rows = 0;
    window->right_scroll_rows = 0;
}

os_error *aural_library_window_handle_scroll(
    aural_library_window *window,
    const wimp_scroll *scroll
)
{
    wimp_pointer pointer;
    size_t amount;
    size_t maximum;
    os_error *error;

    if (window == NULL || scroll == NULL ||
        scroll->w != window->handle ||
        scroll->ymin == wimp_SCROLL_NONE) {
        return NULL;
    }
    error = xwimp_get_pointer_info(&pointer);
    if (error != NULL) {
        return error;
    }
    amount = (scroll->ymin == wimp_SCROLL_PAGE_UP ||
              scroll->ymin == wimp_SCROLL_PAGE_DOWN) ? 8u : 1u;
    if (pointer.pos.x <
        scroll->visible.x0 + AURAL_LEFT_PANEL_WIDTH) {
        char artists[AURAL_MAXIMUM_VISIBLE_ARTISTS][AURAL_ARTIST_CAPACITY];
        char genres[AURAL_MAXIMUM_VISIBLE_GENRES][AURAL_GENRE_CAPACITY];
        size_t artist_count = aural_library_collect_artists(
            window, artists, AURAL_MAXIMUM_VISIBLE_ARTISTS);
        size_t genre_count = aural_library_collect_genres(
            window, genres, AURAL_MAXIMUM_VISIBLE_GENRES);

        maximum = 18u + artist_count + genre_count +
            window->playlists->count;
        aural_library_scroll_offset(
            &window->left_scroll_rows, maximum,
            scroll->ymin, amount);
    } else if (!aural_library_central_track_view(window) &&
        pointer.pos.x >=
            scroll->visible.x1 - AURAL_RIGHT_PANEL_WIDTH) {
        const aural_track_entry *tracks[AURAL_MAXIMUM_VISIBLE_TRACKS];
        size_t count = aural_library_collect_album_tracks(
            window, tracks, AURAL_MAXIMUM_VISIBLE_TRACKS);

        maximum = count > 0 ? count - 1 : 0;
        aural_library_scroll_offset(
            &window->right_scroll_rows, maximum,
            scroll->ymin, amount);
    } else if (aural_library_central_track_view(window)) {
        const aural_track_entry *tracks[AURAL_MAXIMUM_VISIBLE_TRACKS];
        size_t count = aural_library_collect_central_tracks(
            window, tracks, AURAL_MAXIMUM_VISIBLE_TRACKS);

        maximum = count > 0 ? count - 1 : 0;
        aural_library_scroll_offset(
            &window->central_scroll_rows, maximum,
            scroll->ymin, amount);
    } else {
        char albums[AURAL_MAXIMUM_VISIBLE_ALBUMS][AURAL_ALBUM_CAPACITY];
        size_t count = aural_library_collect_albums(
            window, albums, AURAL_MAXIMUM_VISIBLE_ALBUMS);
        int available = scroll->visible.x1 -
            scroll->visible.x0 - AURAL_LEFT_PANEL_WIDTH -
            AURAL_RIGHT_PANEL_WIDTH - 2 * AURAL_PANEL_PADDING;
        int columns = available /
            (window->album_cell_width + AURAL_ALBUM_GAP);
        size_t rows;

        if (columns < 1) {
            columns = 1;
        }
        rows = (count + (size_t) columns - 1u) / (size_t) columns;
        maximum = rows > 0 ? rows - 1 : 0;
        aural_library_scroll_offset(
            &window->central_scroll_rows, maximum,
            scroll->ymin, amount);
    }
    return xwimp_force_redraw(window->handle, 0, -4096, 4096, 0);
}

os_error *aural_library_window_handle_pointer(
    aural_library_window *window,
    const wimp_pointer *pointer
)
{
    char artists[AURAL_MAXIMUM_VISIBLE_ARTISTS][AURAL_ARTIST_CAPACITY];
    char albums[AURAL_MAXIMUM_VISIBLE_ALBUMS][AURAL_ALBUM_CAPACITY];
    char genres[AURAL_MAXIMUM_VISIBLE_GENRES][AURAL_GENRE_CAPACITY];
    wimp_window_state state;
    size_t artist_count;
    size_t album_count;
    size_t genre_count;
    size_t index;
    int y;
    os_error *error;

    if (window != NULL && window->info_dialog_created &&
        pointer->w == window->info_dialog_handle &&
        (pointer->buttons & wimp_CLICK_SELECT) != 0) {
        if (pointer->i == AURAL_INFO_CANCEL) {
            window->info_dialog_open = false;
            return xwimp_close_window(window->info_dialog_handle);
        }
        if (pointer->i == AURAL_INFO_SAVE) {
            return aural_library_accept_track_info(window);
        }
        return NULL;
    }
    if (window != NULL && window->album_dialog_created &&
        pointer->w == window->album_dialog_handle &&
        (pointer->buttons & wimp_CLICK_SELECT) != 0) {
        if (pointer->i == AURAL_ALBUM_CANCEL) {
            window->album_dialog_open = false;
            return xwimp_close_window(window->album_dialog_handle);
        }
        if (pointer->i == AURAL_ALBUM_REMOVE_ARTWORK) {
            window->album_remove_artwork = true;
            window->album_edit_artwork[0] = '\0';
            return xwimp_set_icon_state(
                window->album_dialog_handle,
                AURAL_ALBUM_ARTWORK,
                0, 0);
        }
        if (pointer->i == AURAL_ALBUM_SAVE) {
            return aural_library_accept_album_info(window);
        }
        return NULL;
    }
    if (window != NULL && window->playlist_dialog_created &&
        pointer->w == window->playlist_dialog_handle &&
        (pointer->buttons & wimp_CLICK_SELECT) != 0) {
        if (pointer->i == AURAL_PLAYLIST_CANCEL) {
            window->playlist_dialog_open = false;
            return xwimp_close_window(window->playlist_dialog_handle);
        }
        if (pointer->i == AURAL_PLAYLIST_OK) {
            return aural_library_accept_playlist_dialog(window);
        }
        return NULL;
    }
    if (window != NULL && window->search_dialog_created &&
        pointer->w == window->search_dialog_handle &&
        (pointer->buttons & wimp_CLICK_SELECT) != 0) {
        if (pointer->i == AURAL_PLAYLIST_CANCEL) {
            window->search_dialog_open = false;
            return xwimp_close_window(window->search_dialog_handle);
        }
        if (pointer->i == AURAL_PLAYLIST_OK) {
            return aural_library_accept_search_dialog(window);
        }
        return NULL;
    }
    if (window != NULL &&
        (window->info_dialog_open || window->album_dialog_open ||
         window->playlist_dialog_open || window->search_dialog_open)) {
        return NULL;
    }
    if (!aural_library_window_owns(window, pointer->w) ||
        (pointer->buttons != wimp_SINGLE_SELECT &&
         pointer->buttons != wimp_DOUBLE_SELECT &&
         pointer->buttons != wimp_DRAG_SELECT &&
         pointer->buttons != wimp_CLICK_MENU)) {
        return NULL;
    }
    state.w = window->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }
    if (pointer->pos.y <
            state.visible.y0 + AURAL_PLAYBACK_BAR_HEIGHT) {
        int x = pointer->pos.x - state.visible.x0;
        int width = state.visible.x1 - state.visible.x0;

        if (pointer->pos.y < state.visible.y0 + 60) {
            if (x >= 24 && x < width - 280) {
                unsigned int percent = (unsigned int)
                    ((x - 24) * 100 / (width - 304));

                error = aural_player_seek(window->player, percent);
            } else if (x >= width - 240 && x < width - 24) {
                unsigned int volume = (unsigned int)
                    ((x - (width - 240)) * 127 / 216);

                error = aural_player_set_volume(
                    window->player, volume);
            }
            if (error == NULL) {
                error = aural_library_redraw_transport_bars(window);
            }
            return error;
        }

        if (x >= 24 && x < 88) {
            error = aural_library_step_track(window, -1);
        } else if (x >= 100 && x < 212) {
            const aural_track_entry *selected =
                aural_library_selected_track(window);

            if (selected == NULL) {
                const aural_track_entry *tracks[
                    AURAL_MAXIMUM_VISIBLE_TRACKS];

                size_t count =
                    aural_library_central_track_view(window) ?
                    aural_library_collect_central_tracks(
                        window, tracks,
                        AURAL_MAXIMUM_VISIBLE_TRACKS) :
                    aural_library_collect_album_tracks(
                        window, tracks,
                        AURAL_MAXIMUM_VISIBLE_TRACKS);

                if (count > 0) {
                    selected = tracks[0];
                }
            }
            if (selected != NULL &&
                aural_player_is_current(window->player, selected)) {
                error = aural_player_toggle_pause(window->player);
            } else {
                error = aural_library_play_track(window, selected);
            }
        } else if (x >= 224 && x < 288) {
            error = aural_library_step_track(window, 1);
        } else if (x >= 300 && x < 392) {
            error = aural_player_stop(window->player);
        } else if (x >= 404 && x < 580) {
            return aural_library_show_information(window);
        } else if (x >= 592 && x < 724) {
            return aural_library_show_search_dialog(window);
        } else if (x >= 736 && x < 868) {
            window->shuffle = !window->shuffle;
        } else if (x >= 880 && x < 1000) {
            window->repeat = !window->repeat;
        }
        if (error == NULL) {
            error = xwimp_force_redraw(
                window->handle, 0, -4096, 4096, 0);
        }
        return error;
    }
    artist_count = aural_library_collect_artists(
        window, artists, AURAL_MAXIMUM_VISIBLE_ARTISTS);
    genre_count = aural_library_collect_genres(
        window, genres, AURAL_MAXIMUM_VISIBLE_GENRES);
    if (pointer->pos.x <
            state.visible.x0 + AURAL_LEFT_PANEL_WIDTH &&
        pointer->pos.y >=
            state.visible.y0 + AURAL_PLAYBACK_BAR_HEIGHT) {
        y = state.visible.y1 - 80 +
            (int) window->left_scroll_rows * AURAL_ARTIST_ROW_HEIGHT;
        if (pointer->pos.y >= y - 12 &&
            pointer->pos.y < y + 28) {
            window->view_kind = AURAL_VIEW_ALL_ALBUMS;
            aural_library_reset_content_scroll(window);
            window->selected_album[0] = '\0';
            window->selected_track_index = SIZE_MAX;
            aural_library_ensure_selection(window);
            return xwimp_force_redraw(
                window->handle, 0, -4096, 4096, 0);
        }
        y -= AURAL_ARTIST_ROW_HEIGHT;
        if (pointer->pos.y >= y - 12 &&
            pointer->pos.y < y + 28) {
            window->view_kind = AURAL_VIEW_RECENTLY_ADDED;
            aural_library_reset_content_scroll(window);
            window->selected_album[0] = '\0';
            window->selected_track_index = SIZE_MAX;
            aural_library_ensure_selection(window);
            return xwimp_force_redraw(
                window->handle, 0, -4096, 4096, 0);
        }
        y -= AURAL_ARTIST_ROW_HEIGHT;
        if (pointer->pos.y >= y - 12 &&
            pointer->pos.y < y + 28) {
            window->view_kind = AURAL_VIEW_QUEUE;
            aural_library_reset_content_scroll(window);
            window->selected_track_index = SIZE_MAX;
            return xwimp_force_redraw(
                window->handle, 0, -4096, 4096, 0);
        }
        y -= AURAL_ARTIST_ROW_HEIGHT + 56;
        for (index = 0; index < artist_count; ++index) {
            if (pointer->pos.y >= y - 12 &&
                pointer->pos.y < y + 28) {
                window->view_kind = AURAL_VIEW_ARTIST;
                aural_library_reset_content_scroll(window);
                snprintf(window->selected_artist,
                    sizeof(window->selected_artist), "%s", artists[index]);
                window->selected_album[0] = '\0';
                window->selected_track_index = SIZE_MAX;
                aural_library_ensure_selection(window);
                return xwimp_force_redraw(
                    window->handle, 0, -4096, 4096, 0);
            }
            y -= AURAL_ARTIST_ROW_HEIGHT;
        }
        y -= 56;
        for (index = 0; index < genre_count; ++index) {
            if (pointer->pos.y >= y - 12 &&
                pointer->pos.y < y + 28) {
                window->view_kind = AURAL_VIEW_GENRE;
                aural_library_reset_content_scroll(window);
                snprintf(window->selected_genre,
                    sizeof(window->selected_genre), "%s", genres[index]);
                window->selected_album[0] = '\0';
                window->selected_track_index = SIZE_MAX;
                aural_library_ensure_selection(window);
                return xwimp_force_redraw(
                    window->handle, 0, -4096, 4096, 0);
            }
            y -= AURAL_ARTIST_ROW_HEIGHT;
        }
        y -= 56;
        for (index = 1; index <= 5; ++index) {
            if (pointer->pos.y >= y - 12 &&
                pointer->pos.y < y + 28) {
                window->view_kind = AURAL_VIEW_RATING;
                aural_library_reset_content_scroll(window);
                window->selected_rating = (unsigned int) index;
                window->selected_track_index = SIZE_MAX;
                return xwimp_force_redraw(
                    window->handle, 0, -4096, 4096, 0);
            }
            y -= AURAL_ARTIST_ROW_HEIGHT;
        }
        if (pointer->pos.y >= y - 12 &&
            pointer->pos.y < y + 28) {
            window->view_kind = AURAL_VIEW_FAVOURITES;
            aural_library_reset_content_scroll(window);
            window->selected_track_index = SIZE_MAX;
            return xwimp_force_redraw(
                window->handle, 0, -4096, 4096, 0);
        }
        y -= 96;
        for (index = 0;
             index < window->playlists->count;
             ++index) {
            if (pointer->pos.y >= y - 12 &&
                pointer->pos.y < y + 28) {
                if (pointer->buttons == wimp_CLICK_MENU) {
                    window->context_menu_open = true;
                    window->context_playlist_menu = true;
                    window->context_playlist_index = index;
                    return xwimp_create_menu(
                        aural_library_playlist_context_menu(),
                        pointer->pos.x, pointer->pos.y);
                }
                window->view_kind = AURAL_VIEW_PLAYLIST;
                aural_library_reset_content_scroll(window);
                window->selected_playlist_index = index;
                window->selected_album[0] = '\0';
                window->selected_track_index = SIZE_MAX;
                return xwimp_force_redraw(
                    window->handle, 0, -4096, 4096, 0);
            }
            y -= AURAL_ARTIST_ROW_HEIGHT;
        }
        if (pointer->pos.y >= y - 12 &&
            pointer->pos.y < y + 28 &&
            pointer->buttons == wimp_SINGLE_SELECT) {
            window->playlist_add_track_after_create = false;
            return aural_library_show_playlist_dialog(
                window, false, SIZE_MAX);
        }
        return NULL;
    }
    if (pointer->pos.x >=
            state.visible.x0 + AURAL_LEFT_PANEL_WIDTH &&
        pointer->pos.x <
            (aural_library_central_track_view(window) ?
                state.visible.x1 :
                state.visible.x1 - AURAL_RIGHT_PANEL_WIDTH) &&
        pointer->pos.y >=
            state.visible.y0 + AURAL_PLAYBACK_BAR_HEIGHT) {
        int centre_x0 = state.visible.x0 + AURAL_LEFT_PANEL_WIDTH;
        int centre_x1 = aural_library_central_track_view(window) ?
            state.visible.x1 :
            state.visible.x1 - AURAL_RIGHT_PANEL_WIDTH;
        int available = centre_x1 - centre_x0 - 2 * AURAL_PANEL_PADDING;
        int columns = available /
            (window->album_cell_width + AURAL_ALBUM_GAP);
        os_box centre = {
            centre_x0,
            state.visible.y0 + AURAL_PLAYBACK_BAR_HEIGHT,
            centre_x1,
            state.visible.y1
        };
        os_box slider;

        if (aural_library_central_track_view(window)) {
            const aural_track_entry *central_tracks[
                AURAL_MAXIMUM_VISIBLE_TRACKS];
            size_t count = aural_library_collect_central_tracks(
                window, central_tracks,
                AURAL_MAXIMUM_VISIBLE_TRACKS);

            y = state.visible.y1 - 152 +
                (int) window->central_scroll_rows * 44;
            for (index = 0; index < count; ++index) {
                if (y > state.visible.y1 - 152) {
                    y -= 44;
                    continue;
                }
                if (pointer->pos.y >= y - 12 &&
                    pointer->pos.y < y + 28) {
                    size_t track_index = aural_library_track_index(
                        window, central_tracks[index]);

                    if (pointer->buttons == wimp_SINGLE_ADJUST) {
                        aural_library_select_track(
                            window, track_index, true, false);
                        return xwimp_force_redraw(
                            window->handle, 0, -4096, 4096, 0);
                    }
                    aural_library_select_track(
                        window, track_index, false,
                        pointer->buttons == wimp_CLICK_MENU ||
                        pointer->buttons == wimp_DRAG_SELECT);
                    if (pointer->buttons == wimp_CLICK_MENU) {
                        window->context_menu_open = true;
                        window->context_playlist_menu = false;
                        window->context_track_index = index;
                        return xwimp_create_menu(
                            aural_library_track_context_menu(window),
                            pointer->pos.x, pointer->pos.y);
                    }
                    if (pointer->buttons == wimp_DRAG_SELECT) {
                        wimp_drag drag;

                        memset(&drag, 0, sizeof(drag));
                        drag.w = window->handle;
                        drag.type = wimp_DRAG_USER_FIXED;
                        drag.initial = (os_box) {
                            pointer->pos.x - 48, pointer->pos.y - 24,
                            pointer->pos.x + 48, pointer->pos.y + 24
                        };
                        drag.bbox = (os_box) {
                            -32768, -32768, 32767, 32767
                        };
                        error = xwimp_drag_box(&drag);
                        if (error == NULL) {
                            window->track_dragging = true;
                            window->dragged_track_index =
                                window->selected_track_index;
                        }
                        return error;
                    }
                    if (pointer->buttons == wimp_DOUBLE_SELECT) {
                        error = aural_library_play_track(
                            window, central_tracks[index]);
                        if (error != NULL) {
                            return error;
                        }
                    }
                    return xwimp_force_redraw(
                        window->handle, 0, -4096, 4096, 0);
                }
                y -= 44;
            }
            return NULL;
        }
        aural_library_slider_track(&centre, &slider);
        if ((pointer->buttons == wimp_SINGLE_SELECT ||
             pointer->buttons == wimp_DRAG_SELECT) &&
            pointer->pos.x >= slider.x0 -
                AURAL_THUMBNAIL_SLIDER_KNOB_WIDTH / 2 &&
            pointer->pos.x <= slider.x1 +
                AURAL_THUMBNAIL_SLIDER_KNOB_WIDTH / 2 &&
            pointer->pos.y >= centre.y1 - 60 &&
            pointer->pos.y < centre.y1 - 12) {
            error = aural_library_set_thumbnail_width_from_pointer(
                window, pointer->pos.x);
            if (error != NULL ||
                pointer->buttons != wimp_DRAG_SELECT) {
                return error;
            }
            {
                wimp_drag drag;

                memset(&drag, 0, sizeof(drag));
                drag.w = window->handle;
                drag.type = wimp_DRAG_USER_POINT;
                drag.initial = (os_box) {
                    pointer->pos.x, pointer->pos.y,
                    pointer->pos.x, pointer->pos.y
                };
                drag.bbox = (os_box) {
                    slider.x0, -32768, slider.x1, 32767
                };
                error = xwimp_drag_box(&drag);
                if (error == NULL) {
                    window->thumbnail_slider_dragging = true;
                }
                return error;
            }
        }
        album_count = aural_library_collect_albums(
            window, albums, AURAL_MAXIMUM_VISIBLE_ALBUMS);
        if (columns < 1) {
            columns = 1;
        }
        for (index = 0; index < album_count; ++index) {
            int column = (int) index % columns;
            int row = (int) index / columns;
            os_box cell;

            cell.x0 = centre_x0 + AURAL_PANEL_PADDING +
                column * (window->album_cell_width + AURAL_ALBUM_GAP);
            cell.x1 = cell.x0 + window->album_cell_width;
            cell.y1 = state.visible.y1 - 72 -
                (row - (int) window->central_scroll_rows) *
                    (aural_library_album_cell_height(window) +
                    AURAL_ALBUM_GAP);
            cell.y0 = cell.y1 -
                aural_library_album_cell_height(window);
            if (cell.y0 >= state.visible.y1 - 72 ||
                cell.y1 <= state.visible.y0 +
                    AURAL_PLAYBACK_BAR_HEIGHT) {
                continue;
            }
            if (pointer->pos.x >= cell.x0 &&
                pointer->pos.x < cell.x1 &&
                pointer->pos.y >= cell.y0 &&
                pointer->pos.y < cell.y1) {
                snprintf(window->selected_album,
                    sizeof(window->selected_album), "%s", albums[index]);
                window->right_scroll_rows = 0;
                window->selected_track_index = SIZE_MAX;
                return xwimp_force_redraw(
                    window->handle, 0, -4096, 4096, 0);
            }
        }
    }
    if (!aural_library_central_track_view(window) &&
        pointer->pos.x >=
            state.visible.x1 - AURAL_RIGHT_PANEL_WIDTH &&
        pointer->pos.y >=
            state.visible.y0 + AURAL_PLAYBACK_BAR_HEIGHT) {
        const aural_track_entry *tracks[AURAL_MAXIMUM_VISIBLE_TRACKS];
        size_t count = aural_library_collect_album_tracks(
            window, tracks, AURAL_MAXIMUM_VISIBLE_TRACKS);

        y = state.visible.y1 - 92 +
            (int) window->right_scroll_rows * 40;
        for (index = 0; index < count; ++index) {
            if (y > state.visible.y1 - 92) {
                y -= 40;
                continue;
            }
            if (y < state.visible.y0 +
                    AURAL_PLAYBACK_BAR_HEIGHT + 300) {
                break;
            }
            if (pointer->pos.y >= y - 12 &&
                pointer->pos.y < y + 28) {
                size_t track_index =
                    aural_library_track_index(window, tracks[index]);

                if (pointer->buttons == wimp_SINGLE_ADJUST) {
                    aural_library_select_track(
                        window, track_index, true, false);
                    return xwimp_force_redraw(
                        window->handle, 0, -4096, 4096, 0);
                }
                aural_library_select_track(
                    window, track_index, false,
                    pointer->buttons == wimp_CLICK_MENU ||
                    pointer->buttons == wimp_DRAG_SELECT);
                if (pointer->buttons == wimp_CLICK_MENU) {
                    window->context_menu_open = true;
                    window->context_playlist_menu = false;
                    window->context_track_index = index;
                    return xwimp_create_menu(
                        aural_library_track_context_menu(window),
                        pointer->pos.x, pointer->pos.y);
                }
                if (pointer->buttons == wimp_DRAG_SELECT) {
                    wimp_drag drag;

                    memset(&drag, 0, sizeof(drag));
                    drag.w = window->handle;
                    drag.type = wimp_DRAG_USER_FIXED;
                    drag.initial = (os_box) {
                        pointer->pos.x - 48, pointer->pos.y - 24,
                        pointer->pos.x + 48, pointer->pos.y + 24
                    };
                    drag.bbox = (os_box) {
                        -32768, -32768, 32767, 32767
                    };
                    error = xwimp_drag_box(&drag);
                    if (error == NULL) {
                        window->track_dragging = true;
                        window->dragged_track_index =
                            window->selected_track_index;
                    }
                    return error;
                }
                if (pointer->buttons == wimp_DOUBLE_SELECT) {
                    error = aural_library_play_track(window, tracks[index]);
                    if (error != NULL) {
                        return error;
                    }
                }
                return xwimp_force_redraw(
                    window->handle, 0, -4096, 4096, 0);
            }
            y -= 40;
        }
    }
    return NULL;
}

os_error *aural_library_window_handle_key(
    aural_library_window *window,
    const wimp_key *key
)
{
    if (window == NULL || key == NULL ||
        ((!window->info_dialog_created ||
          key->w != window->info_dialog_handle) &&
         (!window->album_dialog_created ||
          key->w != window->album_dialog_handle) &&
         (!window->playlist_dialog_created ||
          key->w != window->playlist_dialog_handle) &&
         (!window->search_dialog_created ||
          key->w != window->search_dialog_handle))) {
        return NULL;
    }
    if (window->search_dialog_created &&
        key->w == window->search_dialog_handle) {
        if (key->c == wimp_KEY_RETURN) {
            return aural_library_accept_search_dialog(window);
        }
        if (key->c == wimp_KEY_ESCAPE) {
            window->search_dialog_open = false;
            return xwimp_close_window(window->search_dialog_handle);
        }
        return xwimp_process_key(key->c);
    }
    if (window->playlist_dialog_created &&
        key->w == window->playlist_dialog_handle) {
        if (key->c == wimp_KEY_RETURN) {
            return aural_library_accept_playlist_dialog(window);
        }
        if (key->c == wimp_KEY_ESCAPE) {
            window->playlist_dialog_open = false;
            return xwimp_close_window(window->playlist_dialog_handle);
        }
        return xwimp_process_key(key->c);
    }
    if (window->album_dialog_created &&
        key->w == window->album_dialog_handle) {
        if (key->c == wimp_KEY_RETURN) {
            return aural_library_accept_album_info(window);
        }
        if (key->c == wimp_KEY_ESCAPE) {
            window->album_dialog_open = false;
            return xwimp_close_window(window->album_dialog_handle);
        }
        return xwimp_process_key(key->c);
    }
    if (key->c == wimp_KEY_RETURN) {
        return aural_library_accept_track_info(window);
    }
    if (key->c == wimp_KEY_ESCAPE) {
        window->info_dialog_open = false;
        return xwimp_close_window(window->info_dialog_handle);
    }
    return xwimp_process_key(key->c);
}

os_error *aural_library_window_handle_close(
    aural_library_window *window,
    wimp_w handle
)
{
    if (window != NULL && window->info_dialog_created &&
        handle == window->info_dialog_handle) {
        window->info_dialog_open = false;
        return xwimp_close_window(handle);
    }
    if (window != NULL && window->album_dialog_created &&
        handle == window->album_dialog_handle) {
        window->album_dialog_open = false;
        return xwimp_close_window(handle);
    }
    if (window != NULL && window->playlist_dialog_created &&
        handle == window->playlist_dialog_handle) {
        window->playlist_dialog_open = false;
        return xwimp_close_window(handle);
    }
    if (window != NULL && window->search_dialog_created &&
        handle == window->search_dialog_handle) {
        window->search_dialog_open = false;
        return xwimp_close_window(handle);
    }
    return xwimp_close_window(handle);
}

os_error *aural_library_window_handle_drag_end(
    aural_library_window *window,
    const wimp_dragged *dragged
)
{
    if (window == NULL || dragged == NULL) {
        return NULL;
    }
    if (window->track_dragging) {
        wimp_pointer pointer;
        wimp_window_state state;
        char artists[AURAL_MAXIMUM_VISIBLE_ARTISTS][AURAL_ARTIST_CAPACITY];
        char genres[AURAL_MAXIMUM_VISIBLE_GENRES][AURAL_GENRE_CAPACITY];
        size_t artist_count;
        size_t genre_count;
        int y;
        size_t index;
        os_error *error;

        window->track_dragging = false;
        error = xwimp_get_pointer_info(&pointer);
        if (error != NULL || pointer.w != window->handle ||
            window->dragged_track_index >= window->tracks->count) {
            return error;
        }
        state.w = window->handle;
        error = xwimp_get_window_state(&state);
        if (error != NULL ||
            pointer.pos.x >=
                state.visible.x0 + AURAL_LEFT_PANEL_WIDTH) {
            return error;
        }
        artist_count = aural_library_collect_artists(
            window, artists, AURAL_MAXIMUM_VISIBLE_ARTISTS);
        genre_count = aural_library_collect_genres(
            window, genres, AURAL_MAXIMUM_VISIBLE_GENRES);
        y = state.visible.y1 - 664 +
            (int) window->left_scroll_rows * AURAL_ARTIST_ROW_HEIGHT -
            (int) (artist_count + genre_count) *
                AURAL_ARTIST_ROW_HEIGHT;
        for (index = 0;
             index < window->playlists->count;
             ++index) {
            if (pointer.pos.y >= y - 12 &&
                pointer.pos.y < y + 28) {
                size_t track_index;
                size_t selected_count =
                    aural_library_selected_track_count(window);

                for (track_index = 0;
                     track_index < window->tracks->count;
                     ++track_index) {
                    if ((selected_count > 0 &&
                         window->tracks->items[track_index].selected) ||
                        (selected_count == 0 &&
                         track_index == window->dragged_track_index)) {
                        if (!aural_playlist_add_path(
                                &window->playlists->items[index],
                                window->tracks->items[track_index].path)) {
                            return &aural_metadata_memory_error;
                        }
                    }
                }
                window->catalog_dirty = true;
                window->view_kind = AURAL_VIEW_PLAYLIST;
                window->selected_playlist_index = index;
                window->selected_track_index =
                    window->dragged_track_index;
                return xwimp_force_redraw(
                    window->handle, 0, -4096, 4096, 0);
            }
            y -= AURAL_ARTIST_ROW_HEIGHT;
        }
        return NULL;
    }
    if (!window->thumbnail_slider_dragging) {
        return NULL;
    }
    window->thumbnail_slider_dragging = false;
    return aural_library_set_thumbnail_width_from_pointer(
        window, dragged->final.x0);
}

os_error *aural_library_window_handle_menu_selection(
    aural_library_window *window,
    const wimp_selection *selection,
    bool *handled
)
{
    if (handled != NULL) {
        *handled = false;
    }
    if (window == NULL || selection == NULL || handled == NULL ||
        !window->context_menu_open) {
        return NULL;
    }
    window->context_menu_open = false;
    *handled = true;
    if (window->context_playlist_menu) {
        size_t index = window->context_playlist_index;

        window->context_playlist_menu = false;
        if (index >= window->playlists->count) {
            return NULL;
        }
        if (selection->items[0] == 0) {
            window->playlist_add_track_after_create = false;
            return aural_library_show_playlist_dialog(
                window, true, index);
        }
        if (selection->items[0] == 1) {
            (void) aural_playlist_list_remove_at(
                window->playlists, index);
            if (window->view_kind == AURAL_VIEW_PLAYLIST) {
                window->view_kind = AURAL_VIEW_ALL_ALBUMS;
                window->selected_playlist_index = SIZE_MAX;
                window->selected_track_index = SIZE_MAX;
                window->selected_album[0] = '\0';
                aural_library_ensure_selection(window);
            }
            window->catalog_dirty = true;
            return xwimp_force_redraw(
                window->handle, 0, -4096, 4096, 0);
        }
        return NULL;
    }
    if (window->selected_track_index >= window->tracks->count) {
        return NULL;
    }
    if (selection->items[0] == 0) {
        size_t playlist_index;

        if (selection->items[1] < 0) {
            return NULL;
        }
        playlist_index = (size_t) selection->items[1];
        if (playlist_index < window->playlists->count) {
            size_t track_index;

            for (track_index = 0;
                 track_index < window->tracks->count;
                 ++track_index) {
                if (aural_library_track_is_action_selected(
                        window, track_index) &&
                    !aural_playlist_add_path(
                        &window->playlists->items[playlist_index],
                        window->tracks->items[track_index].path)) {
                    return &aural_metadata_memory_error;
                }
            }
            window->catalog_dirty = true;
            return xwimp_force_redraw(
                window->handle, 0, -4096, 4096, 0);
        }
        if (playlist_index == window->playlists->count) {
            window->playlist_add_track_after_create = true;
            window->playlist_pending_track_index =
                window->selected_track_index;
            return aural_library_show_playlist_dialog(
                window, false, SIZE_MAX);
        }
        return NULL;
    }
    if (selection->items[0] == 1 || selection->items[0] == 2) {
        aural_playlist *queue = &window->play_queue->items[0];
        size_t insertion = 0;
        size_t track_index;

        if (selection->items[0] == 1) {
            size_t current;

            for (current = 0; current < queue->count; ++current) {
                if (strcmp(queue->paths[current],
                        window->player->current_path) == 0) {
                    insertion = current + 1;
                    break;
                }
            }
        }
        for (track_index = 0;
             track_index < window->tracks->count;
             ++track_index) {
            size_t old_count;

            if (!aural_library_track_is_action_selected(
                    window, track_index)) {
                continue;
            }
            old_count = queue->count;
            if (!aural_playlist_add_path(
                    queue, window->tracks->items[track_index].path)) {
                return &aural_metadata_memory_error;
            }
            if (selection->items[0] == 1 &&
                queue->count > old_count) {
                (void) aural_playlist_move(
                    queue, queue->count - 1, insertion);
                ++insertion;
            }
        }
        window->catalog_dirty = true;
        return xwimp_force_redraw(
            window->handle, 0, -4096, 4096, 0);
    }
    if (selection->items[0] == 3 &&
        selection->items[1] >= 0 &&
        selection->items[1] <= 5) {
        size_t track_index;

        for (track_index = 0;
             track_index < window->tracks->count;
             ++track_index) {
            if (aural_library_track_is_action_selected(
                    window, track_index)) {
                window->tracks->items[track_index].rating =
                    (unsigned int) selection->items[1];
            }
        }
        window->catalog_dirty = true;
        return xwimp_force_redraw(
            window->handle, 0, -4096, 4096, 0);
    }
    if (selection->items[0] == 4) {
        size_t track_index;
        bool favourite = !window->tracks->items[
            window->selected_track_index].favourite;

        for (track_index = 0;
             track_index < window->tracks->count;
             ++track_index) {
            if (aural_library_track_is_action_selected(
                    window, track_index)) {
                window->tracks->items[track_index].favourite = favourite;
            }
        }
        window->catalog_dirty = true;
        return xwimp_force_redraw(
            window->handle, 0, -4096, 4096, 0);
    }
    if (selection->items[0] == 8) {
        return aural_library_show_relink_dialog(
            window, window->selected_track_index);
    }
    if (selection->items[0] == 9) {
        char directory[AURAL_PATH_CAPACITY];
        char command[AURAL_PATH_CAPACITY + 32];
        char *separator;

        snprintf(directory, sizeof(directory), "%s",
            window->tracks->items[
                window->selected_track_index].path);
        separator = strrchr(directory, '.');
        if (separator != NULL) {
            *separator = '\0';
        }
        if (strchr(directory, '"') != NULL ||
            snprintf(command, sizeof(command),
                "Filer_OpenDir \"%s\"", directory) >=
                (int) sizeof(command)) {
            return NULL;
        }
        return xos_cli(command);
    }
    if (selection->items[0] == 10) {
        size_t selected_count =
            aural_library_selected_track_count(window);
        size_t track_index = window->tracks->count;

        while (track_index > 0) {
            char path[AURAL_PATH_CAPACITY];
            bool selected;

            --track_index;
            selected = selected_count > 0 ?
                window->tracks->items[track_index].selected :
                track_index == window->selected_track_index;
            if (!selected) {
                continue;
            }
            snprintf(path, sizeof(path), "%s",
                window->tracks->items[track_index].path);
            if (aural_player_is_current(
                    window->player,
                    &window->tracks->items[track_index])) {
                (void) aural_player_stop(window->player);
            }
            aural_library_remove_path_from_playlists(
                window->playlists, path);
            aural_library_remove_path_from_playlists(
                window->play_queue, path);
            if (window->ignored_tracks->count > 0 &&
                !aural_playlist_add_path(
                    &window->ignored_tracks->items[0], path)) {
                return &aural_metadata_memory_error;
            }
            (void) aural_track_list_remove_at(
                window->tracks, track_index);
        }
        window->selected_track_index = SIZE_MAX;
        window->catalog_dirty = true;
        aural_library_ensure_selection(window);
        return xwimp_force_redraw(
            window->handle, 0, -4096, 4096, 0);
    }
    if (window->view_kind != AURAL_VIEW_PLAYLIST &&
        window->view_kind != AURAL_VIEW_QUEUE) {
        return NULL;
    }
    {
        aural_playlist *playlist;
        size_t index = window->context_track_index;

        if (window->view_kind == AURAL_VIEW_QUEUE) {
            playlist = &window->play_queue->items[0];
        } else {
            if (window->selected_playlist_index >=
                window->playlists->count) {
                return NULL;
            }
            playlist = &window->playlists->items[
                window->selected_playlist_index];
        }
        if (index >= playlist->count) {
            return NULL;
        }
        if (selection->items[0] == 5) {
            size_t selected_count =
                aural_library_selected_track_count(window);

            if (selected_count > 1) {
                size_t playlist_index = playlist->count;

                while (playlist_index > 0) {
                    size_t track_index;

                    --playlist_index;
                    track_index = aural_track_list_find_path(
                        window->tracks,
                        playlist->paths[playlist_index]);
                    if (track_index < window->tracks->count &&
                        window->tracks->items[track_index].selected) {
                        (void) aural_playlist_remove_at(
                            playlist, playlist_index);
                    }
                }
            } else {
                (void) aural_playlist_remove_at(playlist, index);
            }
            window->selected_track_index = SIZE_MAX;
            aural_library_clear_track_selection(window);
        } else if (selection->items[0] == 6 && index > 0) {
            (void) aural_playlist_move(playlist, index, index - 1);
        } else if (selection->items[0] == 7 &&
            index + 1 < playlist->count) {
            (void) aural_playlist_move(playlist, index, index + 1);
        } else {
            return NULL;
        }
    }
    window->catalog_dirty = true;
    return xwimp_force_redraw(window->handle, 0, -4096, 4096, 0);
}

bool aural_library_window_assign_artwork_at(
    aural_library_window *window,
    int screen_x,
    int screen_y,
    const char *path
)
{
    char albums[AURAL_MAXIMUM_VISIBLE_ALBUMS][AURAL_ALBUM_CAPACITY];
    wimp_window_state state;
    size_t album_count;
    size_t album_index;
    int centre_x0;
    int centre_x1;
    int columns;

    if (window == NULL || path == NULL || path[0] == '\0' ||
        aural_library_central_track_view(window)) {
        return false;
    }
    state.w = window->handle;
    if (xwimp_get_window_state(&state) != NULL) {
        return false;
    }
    centre_x0 = state.visible.x0 + AURAL_LEFT_PANEL_WIDTH;
    centre_x1 = state.visible.x1 - AURAL_RIGHT_PANEL_WIDTH;
    if (screen_x < centre_x0 || screen_x >= centre_x1 ||
        screen_y < state.visible.y0 + AURAL_PLAYBACK_BAR_HEIGHT) {
        return false;
    }
    columns = (centre_x1 - centre_x0 - 2 * AURAL_PANEL_PADDING) /
        (window->album_cell_width + AURAL_ALBUM_GAP);
    if (columns < 1) {
        columns = 1;
    }
    album_count = aural_library_collect_albums(
        window, albums, AURAL_MAXIMUM_VISIBLE_ALBUMS);
    for (album_index = 0; album_index < album_count; ++album_index) {
        int column = (int) album_index % columns;
        int row = (int) album_index / columns;
        os_box cell;
        size_t track_index;

        cell.x0 = centre_x0 + AURAL_PANEL_PADDING +
            column * (window->album_cell_width + AURAL_ALBUM_GAP);
        cell.x1 = cell.x0 + window->album_cell_width;
        cell.y1 = state.visible.y1 - 72 -
            (row - (int) window->central_scroll_rows) *
                (aural_library_album_cell_height(window) +
                AURAL_ALBUM_GAP);
        cell.y0 = cell.y1 - aural_library_album_cell_height(window);
        if (screen_x < cell.x0 || screen_x >= cell.x1 ||
            screen_y < cell.y0 || screen_y >= cell.y1) {
            continue;
        }
        for (track_index = 0;
             track_index < window->tracks->count;
             ++track_index) {
            aural_track_entry *track =
                &window->tracks->items[track_index];

            if ((window->view_kind != AURAL_VIEW_ARTIST ||
                 strcmp(aural_track_artist(track),
                    window->selected_artist) == 0) &&
                (window->view_kind != AURAL_VIEW_GENRE ||
                 strcmp(track->genre[0] != '\0' ?
                        track->genre : "Unknown Genre",
                    window->selected_genre) == 0) &&
                strcmp(aural_track_album(track),
                    albums[album_index]) == 0) {
                snprintf(track->artwork_path,
                    sizeof(track->artwork_path), "%s", path);
            }
        }
        snprintf(window->selected_album,
            sizeof(window->selected_album), "%s", albums[album_index]);
        window->catalog_dirty = true;
        aural_library_clear_artwork_cache(window);
        (void) xwimp_force_redraw(
            window->handle, 0, -4096, 4096, 0);
        return true;
    }
    return false;
}

bool aural_library_window_catalog_dirty(
    const aural_library_window *window
)
{
    return window != NULL && window->catalog_dirty;
}

void aural_library_window_catalog_saved(
    aural_library_window *window
)
{
    if (window != NULL) {
        window->catalog_dirty = false;
    }
}

void aural_library_window_destroy(aural_library_window *window)
{
    if (window == NULL) {
        return;
    }
    if (window->created) {
        (void) xwimp_delete_window(window->handle);
    }
    if (window->info_dialog_created) {
        (void) xwimp_delete_window(window->info_dialog_handle);
    }
    if (window->album_dialog_created) {
        (void) xwimp_delete_window(window->album_dialog_handle);
    }
    if (window->playlist_dialog_created) {
        (void) xwimp_delete_window(window->playlist_dialog_handle);
    }
    if (window->search_dialog_created) {
        (void) xwimp_delete_window(window->search_dialog_handle);
    }
    free(aural_playlist_submenu);
    aural_playlist_submenu = NULL;
    aural_library_clear_artwork_cache(window);
    window->handle = 0;
    window->info_dialog_handle = 0;
    window->album_dialog_handle = 0;
    window->created = false;
    window->info_dialog_created = false;
    window->info_dialog_open = false;
    window->album_dialog_created = false;
    window->album_dialog_open = false;
    window->playlist_dialog_handle = 0;
    window->playlist_dialog_created = false;
    window->playlist_dialog_open = false;
    window->search_dialog_handle = 0;
    window->search_dialog_created = false;
    window->search_dialog_open = false;
}
