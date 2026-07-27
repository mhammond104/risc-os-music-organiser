#include "imgorg/application.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "oslib/os.h"
#include "oslib/osfile.h"
#include "oslib/wimpspriteop.h"
#include "aural/audio_probe.h"

#define IMGORG_MENU_ICON_FLAGS \
    (wimp_ICON_TEXT | (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT))

static const char AURAL_CHOICES_DIRECTORY[] = "<Choices$Write>.Aural";
static const char AURAL_TRACK_CATALOG[] = "<Choices$Write>.Aural.Tracks";
static const char AURAL_PLAYLIST_CATALOG[] =
    "<Choices$Write>.Aural.Playlists";
static const char AURAL_QUEUE_CATALOG[] =
    "<Choices$Write>.Aural.Queue";
static const char AURAL_IGNORED_CATALOG[] =
    "<Choices$Write>.Aural.Ignored";
static const char AURAL_ARTWORK_DIRECTORY[] =
    "<Choices$Write>.Aural.Artwork";
static os_error aural_catalog_error = {
    0,
    "The Aural music catalogue could not be read"
};
static os_error aural_catalog_save_error = {
    0,
    "The Aural music catalogue could not be saved"
};
static os_error aural_audio_import_error = {
    0,
    "Aural does not recognise this as a supported audio file"
};
static os_error aural_artwork_drop_error = {
    0,
    "Drop the PNG or JPEG directly onto the album that should use it"
};
static os_error aural_artwork_store_error = {
    0,
    "Aural could not copy this artwork into its managed library"
};

static bool aural_text_ends_with_case_insensitive(
    const char *text,
    const char *suffix
)
{
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    size_t index;

    if (suffix_length > text_length) {
        return false;
    }
    text += text_length - suffix_length;
    for (index = 0; index < suffix_length; ++index) {
        if (tolower((unsigned char) text[index]) !=
            tolower((unsigned char) suffix[index])) {
            return false;
        }
    }
    return true;
}

static bool aural_is_artwork_file(bits file_type, const char *path)
{
    return (file_type & 0xFFFu) == 0xC85u ||
        (file_type & 0xFFFu) == 0xB60u ||
        aural_text_ends_with_case_insensitive(path, ".jpg") ||
        aural_text_ends_with_case_insensitive(path, ".jpeg") ||
        aural_text_ends_with_case_insensitive(path, ".png") ||
        aural_text_ends_with_case_insensitive(path, "/jpg") ||
        aural_text_ends_with_case_insensitive(path, "/png");
}

static uint32_t aural_artwork_hash(const char *text)
{
    uint32_t hash = 2166136261u;

    while (*text != '\0') {
        hash ^= (unsigned char) *text++;
        hash *= 16777619u;
    }
    return hash;
}

static os_error *aural_application_store_artwork(
    const char *source,
    bits file_type,
    char *destination,
    size_t capacity
)
{
    char temporary[AURAL_PATH_CAPACITY + 8];
    unsigned char buffer[32768];
    FILE *input;
    FILE *output;
    bool success = true;
    os_error *error;

    error = xosfile_create_dir(AURAL_CHOICES_DIRECTORY, 0);
    if (error == NULL) {
        error = xosfile_create_dir(AURAL_ARTWORK_DIRECTORY, 0);
    }
    if (error != NULL ||
        snprintf(destination, capacity, "%s.Art%08lX",
            AURAL_ARTWORK_DIRECTORY,
            (unsigned long) aural_artwork_hash(source)) >=
            (int) capacity ||
        snprintf(temporary, sizeof(temporary), "%sTmp",
            destination) >= (int) sizeof(temporary)) {
        return error != NULL ? error : &aural_artwork_store_error;
    }
    input = fopen(source, "rb");
    if (input == NULL) {
        return &aural_artwork_store_error;
    }
    (void) remove(temporary);
    output = fopen(temporary, "wb");
    if (output == NULL) {
        fclose(input);
        return &aural_artwork_store_error;
    }
    while (success) {
        size_t amount = fread(buffer, 1, sizeof(buffer), input);

        if (amount > 0 &&
            fwrite(buffer, 1, amount, output) != amount) {
            success = false;
        }
        if (amount < sizeof(buffer)) {
            if (ferror(input)) {
                success = false;
            }
            break;
        }
    }
    success = fclose(input) == 0 && success;
    success = fclose(output) == 0 && success;
    if (!success) {
        (void) remove(temporary);
        return &aural_artwork_store_error;
    }
    (void) remove(destination);
    if (rename(temporary, destination) != 0) {
        (void) remove(temporary);
        return &aural_artwork_store_error;
    }
    error = xosfile_set_type(destination, file_type & 0xFFFu);
    return error;
}

static os_error *aural_application_save_music_catalog(
    const imgorg_application *application
)
{
    os_error *error = xosfile_create_dir(AURAL_CHOICES_DIRECTORY, 0);

    if (error != NULL) {
        return error;
    }
    if (!aural_track_catalog_save(
            AURAL_TRACK_CATALOG,
            &application->music_sources,
            &application->music_tracks)) {
        return &aural_catalog_save_error;
    }
    if (!aural_playlist_catalog_save(
            AURAL_PLAYLIST_CATALOG, &application->playlists)) {
        return &aural_catalog_save_error;
    }
    if (!aural_playlist_catalog_save(
            AURAL_QUEUE_CATALOG, &application->play_queue)) {
        return &aural_catalog_save_error;
    }
    if (!aural_playlist_catalog_save(
            AURAL_IGNORED_CATALOG, &application->ignored_tracks)) {
        return &aural_catalog_save_error;
    }
    error = xosfile_set_type(AURAL_TRACK_CATALOG, 0xFFD);
    if (error == NULL) {
        error = xosfile_set_type(AURAL_PLAYLIST_CATALOG, 0xFFD);
    }
    if (error == NULL) {
        error = xosfile_set_type(AURAL_QUEUE_CATALOG, 0xFFD);
    }
    return error != NULL ? error :
        xosfile_set_type(AURAL_IGNORED_CATALOG, 0xFFD);
}

static bool aural_application_start_next_source_scan(
    imgorg_application *application
)
{
    while (!application->music_scanner.active &&
           application->music_source_scan_index <
               application->music_sources.count) {
        const char *path = application->music_sources.items[
            application->music_source_scan_index++];

        if (aural_music_scanner_start(
                &application->music_scanner, path)) {
            return true;
        }
    }
    return application->music_scanner.active;
}

static bool aural_application_path_ignored(
    const imgorg_application *application,
    const char *path
)
{
    size_t index;
    const aural_playlist *ignored;

    if (application->ignored_tracks.count == 0) {
        return false;
    }
    ignored = &application->ignored_tracks.items[0];
    for (index = 0; index < ignored->count; ++index) {
        if (strcmp(ignored->paths[index], path) == 0) {
            return true;
        }
    }
    return false;
}

static void aural_application_prune_ignored(
    imgorg_application *application
)
{
    size_t index = application->music_tracks.count;

    while (index > 0) {
        --index;
        if (aural_application_path_ignored(
                application,
                application->music_tracks.items[index].path)) {
            (void) aural_track_list_remove_at(
                &application->music_tracks, index);
        }
    }
}

static void aural_application_unignore_path(
    imgorg_application *application,
    const char *path
)
{
    aural_playlist *ignored;
    size_t index;

    if (application->ignored_tracks.count == 0) {
        return;
    }
    ignored = &application->ignored_tracks.items[0];
    for (index = 0; index < ignored->count; ++index) {
        if (strcmp(ignored->paths[index], path) == 0) {
            (void) aural_playlist_remove_at(ignored, index);
            return;
        }
    }
}

static wimp_MENU(1) imgorg_iconbar_menu = {
    {"Aural"},
    wimp_COLOUR_BLACK,
    wimp_COLOUR_LIGHT_GREY,
    wimp_COLOUR_BLACK,
    wimp_COLOUR_WHITE,
    200,
    44,
    0,
    {
        {
            wimp_MENU_LAST,
            0,
            IMGORG_MENU_ICON_FLAGS,
            {"Quit"}
        }
    }
};

static const wimp_MESSAGE_LIST(3) imgorg_messages = {
    {message_DATA_LOAD, message_QUIT, 0}
};

static os_error *imgorg_application_create_iconbar_icon(
    imgorg_application *application
)
{
    static char icon_sprite[] = "!aural";
    wimp_icon_create icon;

    memset(&icon, 0, sizeof(icon));

    icon.w = wimp_ICON_BAR_RIGHT;
    icon.icon.extent.x0 = 0;
    icon.icon.extent.y0 = 0;
    icon.icon.extent.x1 = 68;
    icon.icon.extent.y1 = 68;

    icon.icon.flags =
        wimp_ICON_SPRITE |
        wimp_ICON_HCENTRED |
        wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED |
        (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);

    icon.icon.data.indirected_sprite.id = (osspriteop_id) icon_sprite;
    icon.icon.data.indirected_sprite.area = wimpspriteop_AREA;
    icon.icon.data.indirected_sprite.size = sizeof(icon_sprite);

    return xwimp_create_icon(&icon, &application->iconbar_icon);
}

static os_error *imgorg_application_handle_mouse_click(
    imgorg_application *application,
    const wimp_pointer *pointer
)
{
    if (pointer->w == wimp_ICON_BAR &&
        pointer->i == application->iconbar_icon) {
        if ((pointer->buttons & wimp_CLICK_MENU) != 0) {
            return xwimp_create_menu(
                (wimp_menu *) &imgorg_iconbar_menu,
                pointer->pos.x,
                pointer->pos.y
            );
        }

        if ((pointer->buttons &
             (wimp_CLICK_SELECT | wimp_CLICK_ADJUST)) != 0) {
            return aural_library_window_open(&application->library);
        }
    }

    {
        os_error *error = aural_library_window_handle_pointer(
            &application->library, pointer);

        if (aural_library_window_catalog_dirty(&application->library)) {
            os_error *save_error =
                aural_application_save_music_catalog(application);

            if (save_error == NULL) {
                aural_library_window_catalog_saved(&application->library);
            }
            if (error == NULL) {
                error = save_error;
            }
        }
        return error;
    }
}

static void imgorg_application_handle_message(
    imgorg_application *application,
    const wimp_message *message
)
{
    if (message->action == message_QUIT) {
        application->quit = true;
    }
}

static os_error *imgorg_application_handle_data_load(
    imgorg_application *application,
    wimp_message *message
)
{
    wimp_message_data_xfer *transfer = &message->data.data_xfer;
    fileswitch_object_type object_type;
    bits file_type;
    bits load_addr;
    bits exec_addr;
    int size;
    os_error *error;
    bool added_to_library = false;

    if (!((transfer->w == wimp_ICON_BAR &&
           transfer->i == application->iconbar_icon) ||
          aural_library_window_owns(
              &application->library, transfer->w))) {
        return NULL;
    }

    error = xosfile_read_stamped(
        transfer->file_name,
        &object_type,
        &load_addr,
        &exec_addr,
        &size,
        NULL,
        &file_type
    );
    if (error == NULL && object_type == fileswitch_IS_DIR) {
        bool source_added;
        size_t source_index;

        if (!aural_source_list_add(
                &application->music_sources,
                transfer->file_name,
                &source_added)) {
            error = &aural_catalog_save_error;
        } else if (source_added) {
            error = aural_application_save_music_catalog(application);
        }
        for (source_index = 0;
             error == NULL &&
             source_index < application->music_sources.count;
             ++source_index) {
            if (strcmp(
                    application->music_sources.items[source_index],
                    transfer->file_name) == 0) {
                if (source_index <
                    application->music_source_scan_index) {
                    application->music_source_scan_index = source_index;
                }
                break;
            }
        }
        if (error == NULL) {
            (void) aural_application_start_next_source_scan(application);
        }
        added_to_library = error == NULL;
    } else if (error == NULL &&
        aural_is_artwork_file(file_type, transfer->file_name)) {
        char managed_artwork[AURAL_PATH_CAPACITY];

        if (!aural_library_window_owns(
                &application->library, transfer->w)) {
            error = &aural_artwork_drop_error;
        } else {
            error = aural_application_store_artwork(
                transfer->file_name,
                file_type,
                managed_artwork,
                sizeof(managed_artwork)
            );
        }
        if (error == NULL &&
            !aural_library_window_assign_artwork_at(
                &application->library,
                transfer->pos.x,
                transfer->pos.y,
                managed_artwork)) {
            error = &aural_artwork_drop_error;
        } else if (error == NULL) {
            error = aural_application_save_music_catalog(application);
            if (error == NULL) {
                aural_library_window_catalog_saved(
                    &application->library);
                added_to_library = true;
            }
        }
    } else if (error == NULL) {
        aural_track_entry track;
        bool added;
        const char *leafname = strrchr(transfer->file_name, '.');

        leafname = leafname != NULL ? leafname + 1 : transfer->file_name;

        if (!aural_audio_probe_file(
                transfer->file_name,
                leafname,
                size < 0 ? 0u : (uint64_t) size,
                file_type,
                &track)) {
            error = &aural_audio_import_error;
        } else {
            track.date_added_cs = (uint64_t) time(NULL) * 100u;
            aural_application_unignore_path(
                application, transfer->file_name);
        }
        if (error == NULL && !aural_track_list_append_unique(
                &application->music_tracks, &track, &added)) {
            error = &aural_catalog_save_error;
        } else if (error == NULL && added) {
            error = aural_application_save_music_catalog(application);
            added_to_library = error == NULL;
        }
    }
    if (error != NULL) {
        (void) wimp_report_error(
            error,
            wimp_ERROR_BOX_OK_ICON,
            "Aural"
        );
        return NULL;
    }

    message->action = message_DATA_LOAD_ACK;
    message->your_ref = message->my_ref;
    error = xwimp_send_message(
        wimp_USER_MESSAGE,
        message,
        message->sender
    );
    if (error != NULL) {
        return error;
    }

    return added_to_library ?
        aural_library_window_open(&application->library) : NULL;
}

os_error *imgorg_application_initialise(imgorg_application *application)
{
    os_error *error;

    if (application == NULL) {
        return NULL;
    }

    memset(application, 0, sizeof(*application));
    application->iconbar_icon = wimp_ICON_WINDOW;
    aural_source_list_init(&application->music_sources);
    aural_track_list_init(&application->music_tracks);
    aural_playlist_list_init(&application->playlists);
    aural_playlist_list_init(&application->play_queue);
    aural_playlist_list_init(&application->ignored_tracks);
    aural_music_scanner_init(&application->music_scanner);
    aural_player_init(&application->player);
    if (!aural_track_catalog_load(
            AURAL_TRACK_CATALOG,
            &application->music_sources,
            &application->music_tracks)) {
        return &aural_catalog_error;
    }
    if (!aural_playlist_catalog_load(
            AURAL_PLAYLIST_CATALOG, &application->playlists)) {
        return &aural_catalog_error;
    }
    if (!aural_playlist_catalog_load(
            AURAL_QUEUE_CATALOG, &application->play_queue)) {
        return &aural_catalog_error;
    }
    if (application->play_queue.count == 0) {
        size_t queue_index;

        if (!aural_playlist_list_add(
                &application->play_queue, "Play Queue",
                &queue_index)) {
            return &aural_catalog_error;
        }
    }
    if (!aural_playlist_catalog_load(
            AURAL_IGNORED_CATALOG, &application->ignored_tracks)) {
        return &aural_catalog_error;
    }
    if (application->ignored_tracks.count == 0) {
        size_t ignored_index;

        if (!aural_playlist_list_add(
                &application->ignored_tracks, "Ignored",
                &ignored_index)) {
            return &aural_catalog_error;
        }
    }

    error = xwimp_initialise(
        wimp_VERSION_RO3,
        "Aural",
        (wimp_message_list *) &imgorg_messages,
        NULL,
        &application->task_handle
    );
    if (error != NULL) {
        return error;
    }

    error = aural_library_window_create(
        &application->library,
        &application->music_sources,
        &application->music_tracks,
        &application->playlists,
        &application->play_queue,
        &application->ignored_tracks,
        &application->player
    );
    if (error != NULL) {
        return error;
    }
    application->music_source_scan_index = 0;
    (void) aural_application_start_next_source_scan(application);

    return imgorg_application_create_iconbar_icon(application);
}

os_error *imgorg_application_run(imgorg_application *application)
{
    wimp_block block;
    wimp_event_no event;
    os_error *error = NULL;

    if (application == NULL) {
        return NULL;
    }

    while (!application->quit && error == NULL) {
        if (application->music_scanner.active ||
            application->music_source_scan_index <
                application->music_sources.count ||
            application->player.state == AURAL_PLAYER_PLAYING) {
            os_t now;

            error = xos_read_monotonic_time(&now);
            if (error == NULL) {
                error = xwimp_poll_idle(
                    0, &block, now + 2, NULL, &event);
            }
            if (error != NULL) {
                break;
            }
        } else {
            event = wimp_poll(wimp_MASK_NULL, &block, NULL);
        }

        switch (event) {
        case wimp_NULL_REASON_CODE:
        {
            bool changed = false;

            error = aural_music_scanner_step(
                &application->music_scanner,
                &application->music_tracks,
                &changed
            );
            if (error == NULL && changed) {
                aural_application_prune_ignored(application);
                error = aural_application_save_music_catalog(application);
            }
            if (error == NULL && changed) {
                error = xwimp_force_redraw(
                    application->library.handle, 0, -4096, 4096, 0);
            }
            if (error == NULL && !application->music_scanner.active) {
                (void) aural_application_start_next_source_scan(application);
            }
            if (error == NULL) {
                error = aural_library_window_poll(
                    &application->library);
            }
            break;
        }

        case wimp_REDRAW_WINDOW_REQUEST:
            error = aural_library_window_redraw(
                &application->library, &block.redraw);
            break;

        case wimp_OPEN_WINDOW_REQUEST:
            error = xwimp_open_window(&block.open);
            break;

        case wimp_CLOSE_WINDOW_REQUEST:
            error = aural_library_window_handle_close(
                &application->library, block.close.w);
            break;

        case wimp_MOUSE_CLICK:
            error = imgorg_application_handle_mouse_click(
                application,
                &block.pointer
            );
            if (error != NULL) {
                (void) wimp_report_error(
                    error,
                    wimp_ERROR_BOX_OK_ICON,
                    "Aural"
                );
                error = NULL;
            }
            break;

        case wimp_USER_DRAG_BOX:
            error = aural_library_window_handle_drag_end(
                &application->library, &block.dragged);
            break;

        case wimp_SCROLL_REQUEST:
            error = aural_library_window_handle_scroll(
                &application->library, &block.scroll);
            break;

        case wimp_KEY_PRESSED:
            if ((application->library.info_dialog_created &&
                 block.key.w ==
                    application->library.info_dialog_handle) ||
                (application->library.album_dialog_created &&
                 block.key.w ==
                    application->library.album_dialog_handle) ||
                (application->library.playlist_dialog_created &&
                 block.key.w ==
                    application->library.playlist_dialog_handle) ||
                (application->library.search_dialog_created &&
                 block.key.w ==
                    application->library.search_dialog_handle)) {
                error = aural_library_window_handle_key(
                    &application->library, &block.key);
                if (aural_library_window_catalog_dirty(
                        &application->library)) {
                    os_error *save_error =
                        aural_application_save_music_catalog(
                            application);

                    if (save_error == NULL) {
                        aural_library_window_catalog_saved(
                            &application->library);
                    }
                    if (error == NULL) {
                        error = save_error;
                    }
                }
            } else {
                error = xwimp_process_key(block.key.c);
            }
            if (error != NULL) {
                (void) wimp_report_error(
                    error,
                    wimp_ERROR_BOX_OK_ICON,
                    "Aural"
                );
                error = NULL;
            }
            break;

        case wimp_MENU_SELECTION:
        {
            bool handled = false;

            error = aural_library_window_handle_menu_selection(
                &application->library, &block.selection, &handled);
            if (error == NULL &&
                aural_library_window_catalog_dirty(
                    &application->library)) {
                error = aural_application_save_music_catalog(application);
                if (error == NULL) {
                    aural_library_window_catalog_saved(
                        &application->library);
                }
            }
            if (!handled && block.selection.items[0] == 0) {
                application->quit = true;
            }
            break;
        }

        case wimp_USER_MESSAGE:
        case wimp_USER_MESSAGE_RECORDED:
            if (block.message.action == message_DATA_LOAD) {
                error = imgorg_application_handle_data_load(
                    application,
                    &block.message
                );
            } else {
                imgorg_application_handle_message(application, &block.message);
            }
            break;

        default:
            break;
        }
    }

    return error;
}

void imgorg_application_finalise(imgorg_application *application)
{
    if (application == NULL) {
        return;
    }

    (void) aural_application_save_music_catalog(application);
    (void) aural_player_stop(&application->player);
    aural_source_list_destroy(&application->music_sources);
    aural_track_list_destroy(&application->music_tracks);
    aural_playlist_list_destroy(&application->playlists);
    aural_playlist_list_destroy(&application->play_queue);
    aural_playlist_list_destroy(&application->ignored_tracks);
    aural_music_scanner_destroy(&application->music_scanner);
    aural_library_window_destroy(&application->library);

    if (application->task_handle != 0) {
        (void) xwimp_close_down(application->task_handle);
        application->task_handle = 0;
    }
}
