#include "imgorg/browser_window.h"

#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>
#include <png.h>

#include "oslib/colourtrans.h"
#include "oslib/os.h"
#include "oslib/osfile.h"
#include "oslib/wimpspriteop.h"
#include "imgorg/thumbnail_cache.h"

enum {
    BROWSER_VISIBLE_WIDTH = 1760,
    BROWSER_VISIBLE_HEIGHT = 1080,
    BROWSER_WORKSPACE_WIDTH = 4096
};

enum {
    MAXIMUM_PNG_SIZE = 64 * 1024 * 1024,
    MAXIMUM_JPEG_SIZE = 64 * 1024 * 1024,
    MAXIMUM_SPRITE_FILE_SIZE = 64 * 1024 * 1024,
    /* Keep this in step with the WimpSlot maximum in !Run. */
    WIMP_SLOT_SIZE_BYTES = 128 * 1024 * 1024,
    WIMP_SLOT_RESERVE_BYTES = 12 * 1024 * 1024,
    MAXIMUM_IMAGE_DIMENSION = 8192,
    IMAGE_BORDER = 32,
    SPRITE_DPI = 90,
    MINIMUM_ZOOM_PERCENT = 10,
    MAXIMUM_ZOOM_PERCENT = 800
};

enum {
    THUMBNAIL_DEFAULT_CELL_WIDTH = 280,
    THUMBNAIL_MINIMUM_CELL_WIDTH = 184,
    THUMBNAIL_MAXIMUM_CELL_WIDTH = 440,
    THUMBNAIL_GAP = 24,
    THUMBNAIL_MARGIN = 32,
    THUMBNAIL_IMAGE_INSET = 16,
    THUMBNAIL_MAXIMUM_WIDTH = 160,
    THUMBNAIL_MAXIMUM_HEIGHT = 112,
    THUMBNAIL_SLIDER_WIDTH = 176,
    THUMBNAIL_SLIDER_KNOB_WIDTH = 24,
    LOADING_WINDOW_WIDTH = 360,
    LOADING_WINDOW_HEIGHT = 120,
    WORKSPACE_LEFT_PANEL_WIDTH = 280,
    WORKSPACE_RIGHT_PANEL_WIDTH = 320,
    WORKSPACE_HEADER_HEIGHT = 72,
    WORKSPACE_PANEL_PADDING = 24,
    INSPECTOR_RATING_BUTTON_WIDTH = 32,
    INSPECTOR_RATING_BUTTON_GAP = 6,
    INSPECTOR_BUTTON_HEIGHT = 44,
    VIEWER_TOOLBAR_HEIGHT = 72,
    VIEWER_TOOLBAR_ICON_COUNT = 6,
    ALBUM_DIALOG_WIDTH = 520,
    ALBUM_DIALOG_HEIGHT = 180
};

static const char IMGORG_LIBRARY_DIRECTORY[] = "<Choices$Write>.Aural";
static const char IMGORG_LIBRARY_FILE[] = "<Choices$Write>.Aural.Library";
static char IMGORG_EMPTY_ICON_TEXT[] = "";
static char IMGORG_BORDER_SLAB_OUT[] = "R1";
static char IMGORG_BORDER_SLAB_IN[] = "R2";
static char IMGORG_BORDER_RIDGE[] = "R3";
static char IMGORG_BORDER_ACTION[] = "R5";
static char IMGORG_VIEWER_BACK_LABEL[] = "<";
static char IMGORG_VIEWER_FORWARD_LABEL[] = ">";
static char IMGORG_VIEWER_ACTUAL_LABEL[] = "100%";
static char IMGORG_VIEWER_FIT_LABEL[] = "Fit";
static char IMGORG_VIEWER_FULLSCREEN_LABEL[] = "Full Screen";
static char IMGORG_VIEWER_LEAVE_FULLSCREEN_LABEL[] = "Leave Full Screen";
static char IMGORG_RATING_LABELS[5][2] = {"1", "2", "3", "4", "5"};
static char IMGORG_FAVOURITE_ADD_LABEL[] = "Add";
static char IMGORG_FAVOURITE_REMOVE_LABEL[] = "Remove";
static char IMGORG_INSPECTOR_ADD_TAG_LABEL[] = "Add Tag";
static char IMGORG_MENU_OPEN_LABEL[] = "Open";
static char IMGORG_MENU_ADD_ALBUM_LABEL[] = "Add to Album...";
static char IMGORG_MENU_ADD_TAG_LABEL[] = "Add Tag...";
static char IMGORG_MENU_REMOVE_TAG_LABEL[] = "Remove Tag...";
static char IMGORG_MENU_CREATE_ALBUM_LABEL[] = "Create New...";
static char IMGORG_MENU_REMOVE_LABEL[] = "Remove from library";
static char IMGORG_MENU_RENAME_ALBUM_LABEL[] = "Rename";
static char IMGORG_MENU_REMOVE_ALBUM_LABEL[] = "Remove Album";
static char IMGORG_ALBUM_DIALOG_CANCEL_LABEL[] = "Cancel";
static char IMGORG_ALBUM_DIALOG_OK_LABEL[] = "OK";
static wimp_MENU(5) imgorg_thumbnail_menu;
static wimp_MENU(2) imgorg_album_menu;
static wimp_menu *imgorg_album_submenu;
static wimp_menu *imgorg_add_tag_submenu;
static wimp_menu *imgorg_remove_tag_submenu;
static bool imgorg_thumbnail_menu_initialised;
static bool imgorg_album_menu_initialised;

enum {
    VIEWER_TOOLBAR_BACKGROUND = 0,
    VIEWER_TOOLBAR_BACK,
    VIEWER_TOOLBAR_FORWARD,
    VIEWER_TOOLBAR_ACTUAL_SIZE,
    VIEWER_TOOLBAR_FIT,
    VIEWER_TOOLBAR_FULLSCREEN
};

enum {
    ALBUM_DIALOG_LABEL = 0,
    ALBUM_DIALOG_NAME,
    ALBUM_DIALOG_CANCEL,
    ALBUM_DIALOG_OK,
    ALBUM_DIALOG_ICON_COUNT
};

static os_error browser_error;

static void imgorg_browser_window_set_thumbnail_priority(
    imgorg_browser_window *browser,
    int yscroll,
    int visible_height
);
static os_error *imgorg_browser_window_plot_workspace_chrome(
    const imgorg_browser_window *browser,
    const wimp_draw *draw
);
static os_error *imgorg_browser_window_accept_album_dialog(
    imgorg_browser_window *browser
);
static os_error *imgorg_browser_window_set_filter(
    imgorg_browser_window *browser,
    imgorg_library_filter_kind kind,
    size_t value
);
static os_error *imgorg_browser_window_show_album_dialog(
    imgorg_browser_window *browser,
    imgorg_album_dialog_mode mode,
    size_t album_index
);

static void imgorg_browser_window_collect_tags(
    imgorg_browser_window *browser,
    bool selected_only
)
{
    char (*names)[IMGORG_TAG_NAME_CAPACITY] = selected_only ?
        browser->selection_tag_names : browser->tag_names;
    size_t *count = selected_only ?
        &browser->selection_tag_count : &browser->tag_count;
    size_t image_index;

    *count = 0;
    for (image_index = 0;
         image_index < browser->images.count && *count < 64;
         ++image_index) {
        const imgorg_image_entry *entry = &browser->images.items[image_index];
        const char *cursor = entry->tags;

        if (selected_only && !entry->selected) {
            continue;
        }
        while (*cursor != '\0' && *count < 64) {
            const char *end = strchr(cursor, ',');
            char tag[IMGORG_TAG_NAME_CAPACITY];
            size_t length;
            size_t existing;
            bool duplicate = false;

            if (end == NULL) {
                end = cursor + strlen(cursor);
            }
            length = (size_t) (end - cursor);
            if (length < sizeof(tag)) {
                memcpy(tag, cursor, length);
                tag[length] = '\0';
                if (imgorg_tag_name_normalise(
                        tag, sizeof(tag), tag)) {
                    for (existing = 0; existing < *count; ++existing) {
                        imgorg_image_entry probe;

                        memset(&probe, 0, sizeof(probe));
                        snprintf(probe.tags, sizeof(probe.tags), "%s",
                            names[existing]);
                        if (imgorg_image_entry_has_tag(&probe, tag)) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        snprintf(names[*count], IMGORG_TAG_NAME_CAPACITY,
                            "%s", tag);
                        ++*count;
                    }
                }
            }
            cursor = *end == ',' ? end + 1 : end;
        }
    }
}

static void imgorg_browser_window_initialise_menu(
    wimp_menu *menu,
    const char *title,
    int width
)
{
    memset(menu, 0, offsetof(wimp_menu, entries));
    snprintf(menu->title_data.text, sizeof(menu->title_data.text), "%s", title);
    menu->title_fg = wimp_COLOUR_BLACK;
    menu->title_bg = wimp_COLOUR_LIGHT_GREY;
    menu->work_fg = wimp_COLOUR_BLACK;
    menu->work_bg = wimp_COLOUR_WHITE;
    menu->width = width;
    menu->height = 44;
    menu->gap = 0;
}

static void imgorg_browser_window_set_menu_entry(
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

static wimp_menu *imgorg_browser_window_build_album_submenu(
    const imgorg_browser_window *browser
)
{
    size_t count = browser->albums.count + 1;
    size_t index;

    free(imgorg_album_submenu);
    imgorg_album_submenu = calloc(1, wimp_SIZEOF_MENU(count));
    if (imgorg_album_submenu == NULL) {
        return NULL;
    }
    imgorg_browser_window_initialise_menu(
        imgorg_album_submenu,
        "Albums",
        320
    );
    for (index = 0; index < browser->albums.count; ++index) {
        imgorg_browser_window_set_menu_entry(
            &imgorg_album_submenu->entries[index],
            browser->albums.items[index].name,
            0
        );
    }
    imgorg_browser_window_set_menu_entry(
        &imgorg_album_submenu->entries[count - 1],
        IMGORG_MENU_CREATE_ALBUM_LABEL,
        wimp_MENU_LAST |
            (browser->albums.count > 0 ? wimp_MENU_SEPARATE : 0)
    );
    return imgorg_album_submenu;
}

static wimp_menu *imgorg_browser_window_build_tag_submenu(
    wimp_menu **storage,
    char names[][IMGORG_TAG_NAME_CAPACITY],
    size_t count,
    bool include_create
)
{
    size_t menu_count = count + (include_create ? 1 : 0);
    size_t index;

    free(*storage);
    *storage = NULL;
    if (menu_count == 0) {
        return NULL;
    }
    *storage = calloc(1, wimp_SIZEOF_MENU(menu_count));
    if (*storage == NULL) {
        return NULL;
    }
    imgorg_browser_window_initialise_menu(*storage, "Tags", 300);
    for (index = 0; index < count; ++index) {
        imgorg_browser_window_set_menu_entry(
            &(*storage)->entries[index],
            names[index],
            (!include_create && index + 1 == count) ? wimp_MENU_LAST : 0
        );
    }
    if (include_create) {
        imgorg_browser_window_set_menu_entry(
            &(*storage)->entries[count],
            IMGORG_MENU_CREATE_ALBUM_LABEL,
            wimp_MENU_LAST | (count > 0 ? wimp_MENU_SEPARATE : 0)
        );
    }
    return *storage;
}

static wimp_menu *imgorg_browser_window_thumbnail_menu(
    imgorg_browser_window *browser
)
{
    wimp_icon_flags item_flags =
        wimp_ICON_TEXT |
        wimp_ICON_FILLED |
        wimp_ICON_INDIRECTED |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT);

    if (!imgorg_thumbnail_menu_initialised) {
        (void) item_flags;
        memset(&imgorg_thumbnail_menu, 0, sizeof(imgorg_thumbnail_menu));
        imgorg_browser_window_initialise_menu(
            (wimp_menu *) &imgorg_thumbnail_menu,
            "Photograph",
            320
        );
        imgorg_browser_window_set_menu_entry(
            &imgorg_thumbnail_menu.entries[0], IMGORG_MENU_OPEN_LABEL, 0);
        imgorg_browser_window_set_menu_entry(
            &imgorg_thumbnail_menu.entries[1],
            IMGORG_MENU_ADD_ALBUM_LABEL, 0);
        imgorg_browser_window_set_menu_entry(
            &imgorg_thumbnail_menu.entries[2],
            IMGORG_MENU_ADD_TAG_LABEL, 0);
        imgorg_browser_window_set_menu_entry(
            &imgorg_thumbnail_menu.entries[3],
            IMGORG_MENU_REMOVE_TAG_LABEL, 0);
        imgorg_browser_window_set_menu_entry(
            &imgorg_thumbnail_menu.entries[4], IMGORG_MENU_REMOVE_LABEL,
            wimp_MENU_LAST | wimp_MENU_SEPARATE);
        imgorg_thumbnail_menu_initialised = true;
    }
    imgorg_browser_window_collect_tags(browser, false);
    imgorg_browser_window_collect_tags(browser, true);
    imgorg_thumbnail_menu.entries[1].sub_menu =
        imgorg_browser_window_build_album_submenu(browser);
    imgorg_thumbnail_menu.entries[2].sub_menu =
        imgorg_browser_window_build_tag_submenu(
            &imgorg_add_tag_submenu,
            browser->tag_names,
            browser->tag_count,
            true
        );
    imgorg_thumbnail_menu.entries[3].sub_menu =
        imgorg_browser_window_build_tag_submenu(
            &imgorg_remove_tag_submenu,
            browser->selection_tag_names,
            browser->selection_tag_count,
            false
        );
    if (browser->selection_tag_count == 0) {
        imgorg_thumbnail_menu.entries[3].icon_flags |= wimp_ICON_SHADED;
    } else {
        imgorg_thumbnail_menu.entries[3].icon_flags &= ~wimp_ICON_SHADED;
    }
    return (wimp_menu *) &imgorg_thumbnail_menu;
}

static wimp_menu *imgorg_browser_window_album_menu(void)
{
    if (!imgorg_album_menu_initialised) {
        memset(&imgorg_album_menu, 0, sizeof(imgorg_album_menu));
        imgorg_browser_window_initialise_menu(
            (wimp_menu *) &imgorg_album_menu, "Album", 260);
        imgorg_browser_window_set_menu_entry(
            &imgorg_album_menu.entries[0], IMGORG_MENU_RENAME_ALBUM_LABEL, 0);
        imgorg_browser_window_set_menu_entry(
            &imgorg_album_menu.entries[1], IMGORG_MENU_REMOVE_ALBUM_LABEL,
            wimp_MENU_LAST | wimp_MENU_SEPARATE);
        imgorg_album_menu_initialised = true;
    }
    return (wimp_menu *) &imgorg_album_menu;
}

typedef struct imgorg_jpeg_error_state {
    struct jpeg_error_mgr manager;
    jmp_buf escape;
} imgorg_jpeg_error_state;

typedef struct imgorg_image_memory_estimate {
    uint64_t retained_bytes;
    uint64_t decode_peak_bytes;
} imgorg_image_memory_estimate;

static void imgorg_browser_window_jpeg_error_exit(j_common_ptr common)
{
    imgorg_jpeg_error_state *error =
        (imgorg_jpeg_error_state *) common->err;
    longjmp(error->escape, 1);
}

static uint32_t imgorg_browser_window_read_be32(const byte *data)
{
    return ((uint32_t) data[0] << 24) |
        ((uint32_t) data[1] << 16) |
        ((uint32_t) data[2] << 8) |
        (uint32_t) data[3];
}

static uint64_t imgorg_browser_window_tracked_thumbnail_bytes(
    const imgorg_browser_window *browser
)
{
    uint64_t bytes = 0;
    size_t index;

    for (index = 0; index < browser->thumbnail_count; ++index) {
        if (browser->thumbnails[index].sprite_area != NULL &&
            browser->thumbnails[index].sprite_area->size > 0) {
            bytes += (uint32_t) browser->thumbnails[index].sprite_area->size;
        }
    }
    return bytes;
}

static bool imgorg_browser_window_estimate_image_memory(
    const char *file_name,
    imgorg_image_format format,
    imgorg_image_memory_estimate *estimate
)
{
    static const byte png_signature[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };
    FILE *file;
    uint64_t width;
    uint64_t height;
    uint64_t pixels;

    memset(estimate, 0, sizeof(*estimate));
    file = fopen(file_name, "rb");
    if (file == NULL) {
        return false;
    }
    if (format == IMGORG_IMAGE_FORMAT_PNG) {
        byte header[24];
        long file_size;

        if (fread(header, sizeof(header), 1, file) != 1 ||
            memcmp(header, png_signature, sizeof(png_signature)) != 0 ||
            memcmp(header + 12, "IHDR", 4) != 0 ||
            fseek(file, 0, SEEK_END) != 0) {
            fclose(file);
            return false;
        }
        file_size = ftell(file);
        width = imgorg_browser_window_read_be32(header + 16);
        height = imgorg_browser_window_read_be32(header + 20);
        fclose(file);
        if (file_size <= 0 || width == 0 || height == 0 ||
            width > MAXIMUM_IMAGE_DIMENSION ||
            height > MAXIMUM_IMAGE_DIMENSION) {
            return false;
        }
        pixels = width * height;
        estimate->retained_bytes = sizeof(osspriteop_area) +
            sizeof(osspriteop_header) + pixels * 4;
        estimate->decode_peak_bytes = estimate->retained_bytes +
            pixels * 4 + (uint64_t) file_size;
    } else if (format == IMGORG_IMAGE_FORMAT_JPEG) {
        struct jpeg_decompress_struct decoder;
        imgorg_jpeg_error_state jpeg_error;
        volatile bool decoder_created = false;

        decoder.err = jpeg_std_error(&jpeg_error.manager);
        jpeg_error.manager.error_exit = imgorg_browser_window_jpeg_error_exit;
        if (setjmp(jpeg_error.escape) != 0) {
            if (decoder_created) {
                jpeg_destroy_decompress(&decoder);
            }
            fclose(file);
            return false;
        }
        jpeg_create_decompress(&decoder);
        decoder_created = true;
        jpeg_stdio_src(&decoder, file);
        if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK) {
            jpeg_destroy_decompress(&decoder);
            fclose(file);
            return false;
        }
        width = decoder.image_width;
        height = decoder.image_height;
        jpeg_destroy_decompress(&decoder);
        fclose(file);
        if (width == 0 || height == 0 ||
            width > MAXIMUM_IMAGE_DIMENSION ||
            height > MAXIMUM_IMAGE_DIMENSION) {
            return false;
        }
        pixels = width * height;
        estimate->retained_bytes = sizeof(osspriteop_area) +
            sizeof(osspriteop_header) + pixels * 4;
        estimate->decode_peak_bytes = estimate->retained_bytes + pixels * 3;
    } else {
        fclose(file);
        return false;
    }
    return true;
}

static bool imgorg_browser_window_image_fits_slot(
    const imgorg_browser_window *browser,
    const imgorg_image_memory_estimate *estimate
)
{
    uint64_t current = WIMP_SLOT_RESERVE_BYTES +
        browser->viewer_image_bytes +
        imgorg_browser_window_tracked_thumbnail_bytes(browser);

    return current <= WIMP_SLOT_SIZE_BYTES &&
        estimate->decode_peak_bytes <= WIMP_SLOT_SIZE_BYTES - current;
}

static os_error *imgorg_browser_window_update_drag(
    const imgorg_viewer_window *viewer
);
static os_error *imgorg_browser_window_set_toolbar_visible(
    imgorg_viewer_window *viewer,
    bool visible
);
static void imgorg_browser_window_inspector_rating_box(
    const os_box *right,
    int rating_baseline,
    unsigned int rating,
    os_box *box
);
static void imgorg_browser_window_inspector_favourite_box(
    const os_box *right,
    int favourite_baseline,
    os_box *box
);
static bool imgorg_browser_window_point_in_box(
    const os_coord *point,
    const os_box *box
);

static bool imgorg_browser_window_entry_matches_filter(
    const imgorg_browser_window *browser,
    const imgorg_image_entry *entry
)
{
    switch (browser->filter_kind) {
    case IMGORG_LIBRARY_FILTER_FOLDER:
        if (browser->filter_folder_index < browser->folders.count) {
            const char *folder =
                browser->folders.items[browser->filter_folder_index];
            size_t length = strlen(folder);

            return strncmp(entry->path, folder, length) == 0 &&
                entry->path[length] == '.';
        }
        return false;

    case IMGORG_LIBRARY_FILTER_RATING:
        return entry->rating == browser->filter_rating;

    case IMGORG_LIBRARY_FILTER_FAVOURITES:
        return entry->favourite;

    case IMGORG_LIBRARY_FILTER_ALBUM:
        return browser->filter_album_index < browser->albums.count &&
            imgorg_album_contains(
                &browser->albums.items[browser->filter_album_index],
                entry->path
            );

    case IMGORG_LIBRARY_FILTER_TAG:
        return imgorg_image_entry_has_tag(entry, browser->filter_tag);

    case IMGORG_LIBRARY_FILTER_ALL:
    default:
        return true;
    }
}

static size_t imgorg_browser_window_visible_image_count(
    const imgorg_browser_window *browser
)
{
    size_t count = 0;
    size_t index;

    for (index = 0; index < browser->images.count; ++index) {
        if (imgorg_browser_window_entry_matches_filter(
                browser,
                &browser->images.items[index]
            )) {
            ++count;
        }
    }
    return count;
}

static size_t imgorg_browser_window_actual_image_index(
    const imgorg_browser_window *browser,
    size_t visible_index
)
{
    size_t matched = 0;
    size_t index;

    for (index = 0; index < browser->images.count; ++index) {
        if (!imgorg_browser_window_entry_matches_filter(
                browser,
                &browser->images.items[index]
            )) {
            continue;
        }
        if (matched == visible_index) {
            return index;
        }
        ++matched;
    }
    return SIZE_MAX;
}

static void imgorg_browser_window_prune_empty_folders(
    imgorg_browser_window *browser
)
{
    size_t folder_index = 0;

    while (folder_index < browser->folders.count) {
        const char *folder = browser->folders.items[folder_index];
        size_t folder_length = strlen(folder);
        size_t image_index;
        bool has_image = false;

        for (image_index = 0;
             image_index < browser->images.count;
             ++image_index) {
            const char *path = browser->images.items[image_index].path;

            if (strncmp(path, folder, folder_length) == 0 &&
                path[folder_length] == '.') {
                has_image = true;
                break;
            }
        }
        if (has_image) {
            ++folder_index;
            continue;
        }
        if (browser->filter_kind == IMGORG_LIBRARY_FILTER_FOLDER) {
            if (browser->filter_folder_index == folder_index) {
                browser->filter_kind = IMGORG_LIBRARY_FILTER_ALL;
                browser->filter_folder_index = 0;
            } else if (browser->filter_folder_index > folder_index) {
                --browser->filter_folder_index;
            }
        }
        (void) imgorg_folder_list_remove_at(
            &browser->folders,
            folder_index
        );
        if (browser->folder_scan_index > folder_index) {
            --browser->folder_scan_index;
        }
    }
}

static int imgorg_browser_window_thumbnail_cell_width(
    const imgorg_browser_window *browser
)
{
    return browser != NULL &&
        browser->thumbnail_cell_width >= THUMBNAIL_MINIMUM_CELL_WIDTH ?
            browser->thumbnail_cell_width : THUMBNAIL_DEFAULT_CELL_WIDTH;
}

static int imgorg_browser_window_thumbnail_cell_height(
    const imgorg_browser_window *browser
)
{
    return imgorg_browser_window_thumbnail_cell_width(browser) * 6 / 7;
}

static void imgorg_browser_window_thumbnail_slider_track(
    const os_box *visible,
    os_box *track
)
{
    track->x1 = visible->x1 - WORKSPACE_RIGHT_PANEL_WIDTH - 24;
    track->x0 = track->x1 - THUMBNAIL_SLIDER_WIDTH;
    track->y0 = visible->y1 - 44;
    track->y1 = visible->y1 - 28;
}

static void imgorg_browser_window_thumbnail_slider_knob(
    const imgorg_browser_window *browser,
    const os_box *visible,
    os_box *knob
)
{
    os_box track;
    int range = THUMBNAIL_MAXIMUM_CELL_WIDTH -
        THUMBNAIL_MINIMUM_CELL_WIDTH;
    int position;

    imgorg_browser_window_thumbnail_slider_track(visible, &track);
    position = (imgorg_browser_window_thumbnail_cell_width(browser) -
        THUMBNAIL_MINIMUM_CELL_WIDTH) * (track.x1 - track.x0) / range;
    knob->x0 = track.x0 + position - THUMBNAIL_SLIDER_KNOB_WIDTH / 2;
    knob->x1 = knob->x0 + THUMBNAIL_SLIDER_KNOB_WIDTH;
    knob->y0 = visible->y1 - 54;
    knob->y1 = visible->y1 - 18;
}

static size_t imgorg_browser_window_thumbnail_columns_for_width(
    const imgorg_browser_window *browser,
    int width
)
{
    int cell_width = imgorg_browser_window_thumbnail_cell_width(browser);
    int available = width - WORKSPACE_LEFT_PANEL_WIDTH -
        WORKSPACE_RIGHT_PANEL_WIDTH - (2 * THUMBNAIL_MARGIN);
    int columns = (available + THUMBNAIL_GAP) /
        (cell_width + THUMBNAIL_GAP);

    if (columns < 1) {
        return 1;
    }
    return (size_t) columns;
}

static int imgorg_browser_window_thumbnail_grid_x0(
    const imgorg_browser_window *browser,
    int width,
    size_t columns
)
{
    int cell_width = imgorg_browser_window_thumbnail_cell_width(browser);
    int content_width = width - WORKSPACE_LEFT_PANEL_WIDTH -
        WORKSPACE_RIGHT_PANEL_WIDTH;
    int grid_width = (int) columns * cell_width +
        ((int) columns - 1) * THUMBNAIL_GAP;
    int remaining = content_width - grid_width;

    if (remaining < 2 * THUMBNAIL_MARGIN) {
        remaining = 2 * THUMBNAIL_MARGIN;
    }
    return WORKSPACE_LEFT_PANEL_WIDTH + remaining / 2;
}

static size_t imgorg_browser_window_thumbnail_columns(
    const imgorg_browser_window *browser
)
{
    wimp_window_state state;

    if (browser != NULL && browser->created) {
        state.w = browser->handle;
        if (xwimp_get_window_state(&state) == NULL) {
            return imgorg_browser_window_thumbnail_columns_for_width(
                browser,
                state.visible.x1 - state.visible.x0
            );
        }
    }
    return imgorg_browser_window_thumbnail_columns_for_width(
        browser,
        BROWSER_VISIBLE_WIDTH
    );
}

static int imgorg_browser_window_directory_extent_y0_for_width(
    const imgorg_browser_window *browser,
    int width,
    int minimum_height
)
{
    size_t columns = imgorg_browser_window_thumbnail_columns_for_width(
        browser,
        width
    );
    size_t visible_count =
        imgorg_browser_window_visible_image_count(browser);
    size_t rows = (visible_count + columns - 1) / columns;
    int height = WORKSPACE_HEADER_HEIGHT + THUMBNAIL_MARGIN + (int) rows *
        (imgorg_browser_window_thumbnail_cell_height(browser) +
         THUMBNAIL_GAP);

    if (height < minimum_height) {
        height = minimum_height;
    }
    return -height;
}

static int imgorg_browser_window_directory_extent_y0(
    const imgorg_browser_window *browser
)
{
    wimp_window_state state;

    state.w = browser->handle;
    if (browser->created && xwimp_get_window_state(&state) == NULL) {
        return imgorg_browser_window_directory_extent_y0_for_width(
            browser,
            state.visible.x1 - state.visible.x0,
            state.visible.y1 - state.visible.y0
        );
    }
    return imgorg_browser_window_directory_extent_y0_for_width(
        browser,
        BROWSER_VISIBLE_WIDTH,
        BROWSER_VISIBLE_HEIGHT
    );
}

static os_error *imgorg_browser_window_update_directory_extent_for_width(
    const imgorg_browser_window *browser,
    int width,
    int minimum_height
)
{
    os_box extent;

    extent.x0 = 0;
    extent.y0 = imgorg_browser_window_directory_extent_y0_for_width(
        browser,
        width,
        minimum_height
    );
    extent.x1 = width > BROWSER_WORKSPACE_WIDTH ?
        width : BROWSER_WORKSPACE_WIDTH;
    extent.y1 = 0;
    return xwimp_set_extent(browser->handle, &extent);
}

static os_error *imgorg_browser_window_update_directory_extent(
    const imgorg_browser_window *browser
)
{
    wimp_window_state state;

    state.w = browser->handle;
    if (browser->created && xwimp_get_window_state(&state) == NULL) {
        return imgorg_browser_window_update_directory_extent_for_width(
            browser,
            state.visible.x1 - state.visible.x0,
            state.visible.y1 - state.visible.y0
        );
    }
    return imgorg_browser_window_update_directory_extent_for_width(
        browser,
        BROWSER_VISIBLE_WIDTH,
        BROWSER_VISIBLE_HEIGHT
    );
}

static os_error *imgorg_browser_window_error(const char *message)
{
    browser_error.errnum = 0x80F001;
    snprintf(
        browser_error.errmess,
        sizeof(browser_error.errmess),
        "%s",
        message
    );
    return &browser_error;
}

static osspriteop_mode_word imgorg_browser_window_sprite_mode(void)
{
    return osspriteop_NEW_STYLE |
        (SPRITE_DPI << osspriteop_XRES_SHIFT) |
        (SPRITE_DPI << osspriteop_YRES_SHIFT) |
        (osspriteop_TYPE32BPP << osspriteop_TYPE_SHIFT);
}

static void imgorg_browser_window_read_eigen_factors(
    int *x_eigen,
    int *y_eigen
)
{
    *x_eigen = 1;
    *y_eigen = 1;
    (void) xos_read_mode_variable(
        os_CURRENT_MODE,
        os_MODEVAR_XEIG_FACTOR,
        x_eigen,
        NULL
    );
    (void) xos_read_mode_variable(
        os_CURRENT_MODE,
        os_MODEVAR_YEIG_FACTOR,
        y_eigen,
        NULL
    );
}

static void imgorg_browser_window_read_desktop_size(
    int *width,
    int *height
)
{
    int x_limit = 0;
    int y_limit = 0;
    int x_eigen;
    int y_eigen;

    imgorg_browser_window_read_eigen_factors(&x_eigen, &y_eigen);
    (void) xos_read_mode_variable(
        os_CURRENT_MODE,
        os_MODEVAR_XWIND_LIMIT,
        &x_limit,
        NULL
    );
    (void) xos_read_mode_variable(
        os_CURRENT_MODE,
        os_MODEVAR_YWIND_LIMIT,
        &y_limit,
        NULL
    );
    *width = x_limit > 0 ?
        (x_limit + 1) << x_eigen : BROWSER_VISIBLE_WIDTH + 256;
    *height = y_limit > 0 ?
        (y_limit + 1) << y_eigen : BROWSER_VISIBLE_HEIGHT + 256;
}

static int imgorg_browser_window_fit_zoom(
    const imgorg_viewer_window *viewer,
    const os_box *visible
)
{
    int x_eigen;
    int y_eigen;
    int available_width;
    int available_height;
    int x_percent;
    int y_percent;

    if (viewer->image_width <= 0 || viewer->image_height <= 0) {
        return 100;
    }

    imgorg_browser_window_read_eigen_factors(&x_eigen, &y_eigen);
    available_width =
        (visible->x1 - visible->x0 - (2 * IMAGE_BORDER)) >> x_eigen;
    available_height =
        (visible->y1 - visible->y0 - (2 * IMAGE_BORDER) -
         (viewer->toolbar_visible ? VIEWER_TOOLBAR_HEIGHT : 0)) >> y_eigen;

    if (available_width <= 0 || available_height <= 0) {
        return MINIMUM_ZOOM_PERCENT;
    }

    x_percent = (int) ((long long) available_width * 100 /
                       viewer->image_width);
    y_percent = (int) ((long long) available_height * 100 /
                       viewer->image_height);
    return x_percent < y_percent ? x_percent : y_percent;
}

static os_error *imgorg_browser_window_redraw_browser(
    const imgorg_browser_window *browser
)
{
    return xwimp_force_redraw(
        browser->handle,
        0,
        -32768,
        BROWSER_WORKSPACE_WIDTH,
        0
    );
}

static os_error *imgorg_browser_window_update_chrome_region(
    const imgorg_browser_window *browser,
    const os_box *box
)
{
    wimp_draw update;
    osbool more;
    os_error *error;

    memset(&update, 0, sizeof(update));
    update.w = browser->handle;
    update.box = *box;
    error = xwimp_update_window(&update, &more);
    while (error == NULL && more) {
        error = imgorg_browser_window_plot_workspace_chrome(
            browser,
            &update
        );
        if (error == NULL) {
            error = xwimp_get_rectangle(&update, &more);
        }
    }
    return error;
}

static os_error *imgorg_browser_window_update_fixed_chrome(
    const imgorg_browser_window *browser
)
{
    wimp_window_state state;
    os_box region;
    os_error *error;
    int width;
    int height;

    state.w = browser->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }
    width = state.visible.x1 - state.visible.x0;
    height = state.visible.y1 - state.visible.y0;

    region.x0 = state.xscroll;
    region.y0 = state.yscroll - height;
    region.x1 = state.xscroll + WORKSPACE_LEFT_PANEL_WIDTH;
    region.y1 = state.yscroll;
    error = imgorg_browser_window_update_chrome_region(browser, &region);
    if (error != NULL) {
        return error;
    }

    region.x0 = state.xscroll + width - WORKSPACE_RIGHT_PANEL_WIDTH;
    region.x1 = state.xscroll + width;
    error = imgorg_browser_window_update_chrome_region(browser, &region);
    if (error != NULL) {
        return error;
    }

    region.x0 = state.xscroll + WORKSPACE_LEFT_PANEL_WIDTH;
    region.y0 = state.yscroll - WORKSPACE_HEADER_HEIGHT;
    region.x1 = state.xscroll + width - WORKSPACE_RIGHT_PANEL_WIDTH;
    region.y1 = state.yscroll;
    return imgorg_browser_window_update_chrome_region(browser, &region);
}

static os_error *imgorg_browser_window_redraw_thumbnail_canvas(
    const imgorg_browser_window *browser
)
{
    wimp_window_state state;
    os_error *error;
    int width;
    int height;

    state.w = browser->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }
    width = state.visible.x1 - state.visible.x0;
    height = state.visible.y1 - state.visible.y0;
    return xwimp_force_redraw(
        browser->handle,
        state.xscroll + WORKSPACE_LEFT_PANEL_WIDTH,
        state.yscroll - height,
        state.xscroll + width - WORKSPACE_RIGHT_PANEL_WIDTH,
        state.yscroll - WORKSPACE_HEADER_HEIGHT
    );
}

static os_error *imgorg_browser_window_set_thumbnail_cell_width(
    imgorg_browser_window *browser,
    int width
)
{
    wimp_window_state state;
    os_error *error;
    int minimum_scroll;
    int visible_height;

    if (width < THUMBNAIL_MINIMUM_CELL_WIDTH) {
        width = THUMBNAIL_MINIMUM_CELL_WIDTH;
    } else if (width > THUMBNAIL_MAXIMUM_CELL_WIDTH) {
        width = THUMBNAIL_MAXIMUM_CELL_WIDTH;
    }
    width = (width + 4) & ~7;
    if (width == browser->thumbnail_cell_width) {
        return NULL;
    }
    browser->thumbnail_cell_width = width;

    state.w = browser->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }
    visible_height = state.visible.y1 - state.visible.y0;
    error = imgorg_browser_window_update_directory_extent_for_width(
        browser,
        state.visible.x1 - state.visible.x0,
        visible_height
    );
    if (error != NULL) {
        return error;
    }
    minimum_scroll =
        imgorg_browser_window_directory_extent_y0_for_width(
            browser,
            state.visible.x1 - state.visible.x0,
            visible_height
        ) + visible_height;
    if (minimum_scroll > 0) {
        minimum_scroll = 0;
    }
    if (state.yscroll < minimum_scroll) {
        state.yscroll = minimum_scroll;
    }
    state.next = wimp_TOP;
    error = xwimp_open_window((wimp_open *) &state);
    if (error != NULL) {
        return error;
    }
    imgorg_browser_window_set_thumbnail_priority(
        browser,
        state.yscroll,
        visible_height
    );
    error = imgorg_browser_window_redraw_thumbnail_canvas(browser);
    return error == NULL ?
        imgorg_browser_window_update_fixed_chrome(browser) : error;
}

static os_error *imgorg_browser_window_set_thumbnail_width_from_pointer(
    imgorg_browser_window *browser,
    int pointer_x
)
{
    wimp_window_state state;
    os_box track;
    int width;

    state.w = browser->handle;
    if (xwimp_get_window_state(&state) != NULL) {
        return NULL;
    }
    imgorg_browser_window_thumbnail_slider_track(&state.visible, &track);
    if (pointer_x < track.x0) {
        pointer_x = track.x0;
    } else if (pointer_x > track.x1) {
        pointer_x = track.x1;
    }
    width = THUMBNAIL_MINIMUM_CELL_WIDTH +
        (pointer_x - track.x0) *
        (THUMBNAIL_MAXIMUM_CELL_WIDTH -
         THUMBNAIL_MINIMUM_CELL_WIDTH) /
        (track.x1 - track.x0);
    return imgorg_browser_window_set_thumbnail_cell_width(browser, width);
}

static os_error *imgorg_browser_window_redraw_viewer(
    const imgorg_viewer_window *viewer
)
{
    return xwimp_force_redraw(
        viewer->handle,
        0,
        -32768,
        BROWSER_WORKSPACE_WIDTH,
        0
    );
}

static os_error *imgorg_browser_window_update_title(
    imgorg_browser_window *browser
)
{
    if (browser->scanner.active) {
        snprintf(
            browser->title,
            sizeof(browser->title),
            "Aural - %lu image%s - Adding %.48s",
            (unsigned long) browser->images.count,
            browser->images.count == 1 ? "" : "s",
            browser->directory_name
        );
    } else {
        snprintf(
            browser->title,
            sizeof(browser->title),
            "Aural - %lu image%s",
            (unsigned long) browser->images.count,
            browser->images.count == 1 ? "" : "s"
        );
    }

    if (browser->created) {
        return xwimp_force_redraw_title(browser->handle);
    }

    return NULL;
}

static os_error *imgorg_browser_window_update_viewer_title(
    imgorg_viewer_window *viewer
)
{
    if (viewer->image_name[0] != '\0' && viewer->fit_to_window) {
        snprintf(
            viewer->title,
            sizeof(viewer->title),
            "%s - Fit",
            viewer->image_name
        );
    } else if (viewer->image_name[0] != '\0') {
        snprintf(
            viewer->title,
            sizeof(viewer->title),
            "%s - %d%%",
            viewer->image_name,
            viewer->zoom_percent
        );
    } else {
        snprintf(
            viewer->title,
            sizeof(viewer->title),
            "Image Viewer"
        );
    }
    if (viewer->created) {
        return xwimp_force_redraw_title(viewer->handle);
    }
    return NULL;
}

static void imgorg_browser_window_copy_leafname(
    char *destination,
    size_t destination_size,
    const char *path
)
{
    const char *cursor;
    const char *leaf = path;

    for (cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '.' || *cursor == ':') {
            leaf = cursor + 1;
        }
    }

    snprintf(destination, destination_size, "%s", leaf);
}

static os_error *imgorg_browser_window_save_library(
    imgorg_browser_window *browser
)
{
    os_error *error;

    error = xosfile_create_dir(IMGORG_LIBRARY_DIRECTORY, 0);
    if (error != NULL) {
        return error;
    }
    if (!imgorg_library_catalog_save(
            IMGORG_LIBRARY_FILE,
            &browser->folders,
            &browser->images,
            &browser->albums
        )) {
        return imgorg_browser_window_error(
            "The Aural library catalogue could not be saved"
        );
    }
    browser->library_dirty = false;
    (void) xosfile_set_type(IMGORG_LIBRARY_FILE, 0xFFD);
    return NULL;
}

static os_error *imgorg_browser_window_start_next_folder_scan(
    imgorg_browser_window *browser
)
{
    while (!browser->scanner.active &&
           browser->folder_scan_index < browser->folders.count) {
        const char *path =
            browser->folders.items[browser->folder_scan_index++];
        int path_length = snprintf(
            browser->directory_path,
            sizeof(browser->directory_path),
            "%s",
            path
        );

        if (path_length < 0 ||
            (size_t) path_length >= sizeof(browser->directory_path)) {
            continue;
        }
        if (!imgorg_directory_scanner_start(&browser->scanner, path)) {
            continue;
        }
        imgorg_browser_window_copy_leafname(
            browser->directory_name,
            sizeof(browser->directory_name),
            path
        );
    }
    return imgorg_browser_window_update_title(browser);
}

static os_error *imgorg_browser_window_apply_zoom(
    imgorg_viewer_window *viewer,
    const os_box *visible,
    bool zoom_in
)
{
    os_error *error;
    int zoom = viewer->fit_to_window ?
        imgorg_browser_window_fit_zoom(viewer, visible) :
        viewer->zoom_percent;

    if (zoom_in) {
        zoom = (zoom * 5 + 3) / 4;
    } else {
        zoom = (zoom * 4) / 5;
    }

    if (zoom < MINIMUM_ZOOM_PERCENT) {
        zoom = MINIMUM_ZOOM_PERCENT;
    } else if (zoom > MAXIMUM_ZOOM_PERCENT) {
        zoom = MAXIMUM_ZOOM_PERCENT;
    }

    viewer->fit_to_window = false;
    viewer->zoom_percent = zoom;
    error = imgorg_browser_window_update_viewer_title(viewer);
    if (error != NULL) {
        return error;
    }
    return imgorg_browser_window_redraw_viewer(viewer);
}

static osspriteop_area *imgorg_browser_window_decode_png(
    const byte *data,
    size_t data_size,
    int maximum_width,
    int maximum_height,
    int *width_out,
    int *height_out
)
{
    png_image image;
    byte *pixels;
    osspriteop_area *area;
    osspriteop_header *sprite;
    size_t source_pixel_count;
    size_t source_pixel_bytes;
    size_t output_pixel_bytes;
    size_t area_size;
    size_t index;
    int output_width;
    int output_height;

    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&image, data, data_size)) {
        return NULL;
    }

    if (image.width == 0 || image.height == 0 ||
        image.width > MAXIMUM_IMAGE_DIMENSION ||
        image.height > MAXIMUM_IMAGE_DIMENSION) {
        png_image_free(&image);
        return NULL;
    }

    image.format = PNG_FORMAT_RGBA;
    source_pixel_bytes = PNG_IMAGE_SIZE(image);
    pixels = malloc(source_pixel_bytes);
    if (pixels == NULL) {
        png_image_free(&image);
        return NULL;
    }

    if (!png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        free(pixels);
        png_image_free(&image);
        return NULL;
    }

    source_pixel_count = (size_t) image.width * image.height;
    for (index = 0; index < source_pixel_count; ++index) {
        byte *pixel = pixels + (index * 4);
        unsigned int alpha = pixel[3];

        pixel[0] = (byte) ((pixel[0] * alpha +
                            255u * (255u - alpha) + 127u) / 255u);
        pixel[1] = (byte) ((pixel[1] * alpha +
                            255u * (255u - alpha) + 127u) / 255u);
        pixel[2] = (byte) ((pixel[2] * alpha +
                            255u * (255u - alpha) + 127u) / 255u);
        pixel[3] = 0;
    }

    output_width = (int) image.width;
    output_height = (int) image.height;
    if (maximum_width > 0 && maximum_height > 0 &&
        (output_width > maximum_width || output_height > maximum_height)) {
        if ((long long) output_width * maximum_height >
            (long long) output_height * maximum_width) {
            output_height = (int) ((long long) output_height *
                maximum_width / output_width);
            output_width = maximum_width;
        } else {
            output_width = (int) ((long long) output_width *
                maximum_height / output_height);
            output_height = maximum_height;
        }
        if (output_width < 1) {
            output_width = 1;
        }
        if (output_height < 1) {
            output_height = 1;
        }
    }

    output_pixel_bytes = (size_t) output_width * output_height * 4;
    area_size = sizeof(*area) + sizeof(*sprite) + output_pixel_bytes;
    if (area_size > INT_MAX) {
        free(pixels);
        png_image_free(&image);
        return NULL;
    }

    area = malloc(area_size);
    if (area == NULL) {
        free(pixels);
        png_image_free(&image);
        return NULL;
    }

    memset(area, 0, sizeof(*area) + sizeof(*sprite));
    area->size = (int) area_size;
    area->sprite_count = 1;
    area->first = sizeof(*area);
    area->used = (int) area_size;

    sprite = (osspriteop_header *) ((byte *) area + area->first);
    sprite->size = sizeof(*sprite) + (int) output_pixel_bytes;
    memcpy(sprite->name, "aural", 6);
    sprite->width = output_width - 1;
    sprite->height = output_height - 1;
    sprite->left_bit = 0;
    sprite->right_bit = 31;
    sprite->image = sizeof(*sprite);
    sprite->mask = sizeof(*sprite);
    sprite->mode = (os_mode) imgorg_browser_window_sprite_mode();

    if (output_width == (int) image.width &&
        output_height == (int) image.height) {
        memcpy(
            (byte *) sprite + sprite->image,
            pixels,
            output_pixel_bytes
        );
    } else {
        byte *output = (byte *) sprite + sprite->image;
        int y;

        for (y = 0; y < output_height; ++y) {
            int source_y = (int) ((long long) y * image.height /
                output_height);
            int x;

            for (x = 0; x < output_width; ++x) {
                int source_x = (int) ((long long) x * image.width /
                    output_width);
                memcpy(
                    output + (((size_t) y * output_width + x) * 4),
                    pixels + (((size_t) source_y * image.width +
                        source_x) * 4),
                    4
                );
            }
        }
    }
    free(pixels);

    *width_out = output_width;
    *height_out = output_height;
    png_image_free(&image);
    return area;
}

static osspriteop_area *imgorg_browser_window_decode_png_file(
    const char *file_name,
    int maximum_width,
    int maximum_height,
    int *width_out,
    int *height_out
)
{
    static const byte png_signature[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };
    FILE *file;
    byte *data;
    long file_size;
    size_t bytes_read;
    osspriteop_area *area;

    file = fopen(file_name, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    file_size = ftell(file);
    if (file_size < (long) sizeof(png_signature) ||
        file_size > MAXIMUM_PNG_SIZE || file_size > INT_MAX ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    data = malloc((size_t) file_size);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    bytes_read = fread(data, 1, (size_t) file_size, file);
    fclose(file);
    if (bytes_read != (size_t) file_size ||
        memcmp(data, png_signature, sizeof(png_signature)) != 0) {
        free(data);
        return NULL;
    }

    area = imgorg_browser_window_decode_png(
        data,
        (size_t) file_size,
        maximum_width,
        maximum_height,
        width_out,
        height_out
    );
    free(data);
    return area;
}

static osspriteop_area *imgorg_browser_window_create_sprite(
    int width,
    int height
)
{
    osspriteop_area *area;
    osspriteop_header *sprite;
    byte *pixels;
    size_t pixel_bytes;
    size_t area_size;
    size_t index;

    if (width <= 0 || height <= 0 ||
        (size_t) width > SIZE_MAX / (size_t) height / 4) {
        return NULL;
    }
    pixel_bytes = (size_t) width * height * 4;
    area_size = sizeof(*area) + sizeof(*sprite) + pixel_bytes;
    if (area_size > INT_MAX) {
        return NULL;
    }

    area = malloc(area_size);
    if (area == NULL) {
        return NULL;
    }
    memset(area, 0, sizeof(*area) + sizeof(*sprite));
    area->size = (int) area_size;
    area->sprite_count = 1;
    area->first = sizeof(*area);
    area->used = (int) area_size;

    sprite = (osspriteop_header *) ((byte *) area + area->first);
    sprite->size = sizeof(*sprite) + (int) pixel_bytes;
    memcpy(sprite->name, "aural", 6);
    sprite->width = width - 1;
    sprite->height = height - 1;
    sprite->left_bit = 0;
    sprite->right_bit = 31;
    sprite->image = sizeof(*sprite);
    sprite->mask = sizeof(*sprite);
    sprite->mode = (os_mode) imgorg_browser_window_sprite_mode();
    pixels = (byte *) sprite + sprite->image;
    for (index = 0; index < (size_t) width * height; ++index) {
        pixels[index * 4] = 255;
        pixels[index * 4 + 1] = 255;
        pixels[index * 4 + 2] = 255;
        pixels[index * 4 + 3] = 0;
    }
    return area;
}

static bool imgorg_browser_window_exif_read_u16(
    const byte *data,
    size_t size,
    size_t offset,
    bool little_endian,
    uint16_t *value
)
{
    if (offset > size || size - offset < 2) {
        return false;
    }
    if (little_endian) {
        *value = (uint16_t) data[offset] |
            ((uint16_t) data[offset + 1] << 8);
    } else {
        *value = ((uint16_t) data[offset] << 8) |
            (uint16_t) data[offset + 1];
    }
    return true;
}

static bool imgorg_browser_window_exif_read_u32(
    const byte *data,
    size_t size,
    size_t offset,
    bool little_endian,
    uint32_t *value
)
{
    if (offset > size || size - offset < 4) {
        return false;
    }
    if (little_endian) {
        *value = (uint32_t) data[offset] |
            ((uint32_t) data[offset + 1] << 8) |
            ((uint32_t) data[offset + 2] << 16) |
            ((uint32_t) data[offset + 3] << 24);
    } else {
        *value = ((uint32_t) data[offset] << 24) |
            ((uint32_t) data[offset + 1] << 16) |
            ((uint32_t) data[offset + 2] << 8) |
            (uint32_t) data[offset + 3];
    }
    return true;
}

static bool imgorg_browser_window_extract_exif_thumbnail(
    const byte *app1,
    size_t app1_size,
    byte **thumbnail_out,
    size_t *thumbnail_size_out
)
{
    const byte *tiff;
    size_t tiff_size;
    bool little_endian;
    uint16_t magic;
    uint32_t ifd0_offset;
    uint16_t entry_count;
    size_t next_ifd_position;
    uint32_t ifd1_offset;
    uint32_t jpeg_offset = 0;
    uint32_t jpeg_size = 0;
    size_t index;

    if (app1_size < 14 || memcmp(app1, "Exif\0\0", 6) != 0) {
        return false;
    }
    tiff = app1 + 6;
    tiff_size = app1_size - 6;
    if (tiff[0] == 'I' && tiff[1] == 'I') {
        little_endian = true;
    } else if (tiff[0] == 'M' && tiff[1] == 'M') {
        little_endian = false;
    } else {
        return false;
    }
    if (!imgorg_browser_window_exif_read_u16(
            tiff, tiff_size, 2, little_endian, &magic
        ) || magic != 42 ||
        !imgorg_browser_window_exif_read_u32(
            tiff, tiff_size, 4, little_endian, &ifd0_offset
        ) ||
        !imgorg_browser_window_exif_read_u16(
            tiff, tiff_size, ifd0_offset, little_endian, &entry_count
        ) || ifd0_offset > tiff_size || tiff_size - ifd0_offset < 2 ||
        entry_count > (tiff_size - ifd0_offset - 2) / 12) {
        return false;
    }
    next_ifd_position = (size_t) ifd0_offset + 2 +
        ((size_t) entry_count * 12);
    if (!imgorg_browser_window_exif_read_u32(
            tiff,
            tiff_size,
            next_ifd_position,
            little_endian,
            &ifd1_offset
        ) || ifd1_offset == 0 ||
        !imgorg_browser_window_exif_read_u16(
            tiff, tiff_size, ifd1_offset, little_endian, &entry_count
        ) || ifd1_offset > tiff_size || tiff_size - ifd1_offset < 2 ||
        entry_count > (tiff_size - ifd1_offset - 2) / 12) {
        return false;
    }

    for (index = 0; index < entry_count; ++index) {
        size_t entry_offset = (size_t) ifd1_offset + 2 + index * 12;
        uint16_t tag;
        uint16_t type;
        uint32_t count;
        uint32_t value;

        if (!imgorg_browser_window_exif_read_u16(
                tiff, tiff_size, entry_offset, little_endian, &tag
            ) ||
            !imgorg_browser_window_exif_read_u16(
                tiff, tiff_size, entry_offset + 2, little_endian, &type
            ) ||
            !imgorg_browser_window_exif_read_u32(
                tiff, tiff_size, entry_offset + 4, little_endian, &count
            ) ||
            !imgorg_browser_window_exif_read_u32(
                tiff, tiff_size, entry_offset + 8, little_endian, &value
            )) {
            return false;
        }
        if (type == 4 && count == 1) {
            if (tag == 0x0201) {
                jpeg_offset = value;
            } else if (tag == 0x0202) {
                jpeg_size = value;
            }
        }
    }

    if (jpeg_offset == 0 || jpeg_size < 4 ||
        jpeg_offset > tiff_size || jpeg_size > tiff_size - jpeg_offset ||
        jpeg_size > MAXIMUM_JPEG_SIZE ||
        tiff[jpeg_offset] != 0xFF || tiff[jpeg_offset + 1] != 0xD8) {
        return false;
    }
    *thumbnail_out = malloc(jpeg_size);
    if (*thumbnail_out == NULL) {
        return false;
    }
    memcpy(*thumbnail_out, tiff + jpeg_offset, jpeg_size);
    *thumbnail_size_out = jpeg_size;
    return true;
}

static bool imgorg_browser_window_read_exif_thumbnail(
    FILE *file,
    byte **thumbnail_out,
    size_t *thumbnail_size_out
)
{
    int first;
    int second;

    *thumbnail_out = NULL;
    *thumbnail_size_out = 0;
    rewind(file);
    first = fgetc(file);
    second = fgetc(file);
    if (first != 0xFF || second != 0xD8) {
        return false;
    }

    for (;;) {
        int marker_prefix;
        int marker;
        int length_high;
        int length_low;
        size_t payload_size;

        do {
            marker_prefix = fgetc(file);
        } while (marker_prefix != EOF && marker_prefix != 0xFF);
        if (marker_prefix == EOF) {
            return false;
        }
        do {
            marker = fgetc(file);
        } while (marker == 0xFF);
        if (marker == EOF || marker == 0xDA || marker == 0xD9) {
            return false;
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        length_high = fgetc(file);
        length_low = fgetc(file);
        if (length_high == EOF || length_low == EOF) {
            return false;
        }
        payload_size = ((size_t) length_high << 8) |
            (size_t) length_low;
        if (payload_size < 2) {
            return false;
        }
        payload_size -= 2;
        if (marker == 0xE1 && payload_size <= 1024 * 1024) {
            byte *app1 = malloc(payload_size);
            bool found;

            if (app1 == NULL) {
                return false;
            }
            if (fread(app1, payload_size, 1, file) != 1) {
                free(app1);
                return false;
            }
            found = imgorg_browser_window_extract_exif_thumbnail(
                app1,
                payload_size,
                thumbnail_out,
                thumbnail_size_out
            );
            free(app1);
            if (found) {
                return true;
            }
        } else if (fseek(file, (long) payload_size, SEEK_CUR) != 0) {
            return false;
        }
    }
}

static osspriteop_area *imgorg_browser_window_decode_jpeg_file(
    const char *file_name,
    int maximum_width,
    int maximum_height,
    bool use_exif_thumbnail,
    int *width_out,
    int *height_out
)
{
    struct jpeg_decompress_struct decoder;
    imgorg_jpeg_error_state jpeg_error;
    FILE *file;
    volatile byte *decoded_pixels = NULL;
    volatile byte *exif_thumbnail = NULL;
    byte *exif_thumbnail_found = NULL;
    size_t exif_thumbnail_size = 0;
    volatile bool decoder_created = false;
    long file_size;
    int scale_denominator;
    int decoded_width;
    int decoded_height;
    int decoded_components;
    size_t decoded_row_bytes;
    int output_width;
    int output_height;
    osspriteop_area *area;
    osspriteop_header *sprite;
    byte *output_pixels;
    int y;

    file = fopen(file_name, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    file_size = ftell(file);
    if (file_size <= 0 || file_size > MAXIMUM_JPEG_SIZE ||
        file_size > INT_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    if (use_exif_thumbnail) {
        (void) imgorg_browser_window_read_exif_thumbnail(
            file,
            &exif_thumbnail_found,
            &exif_thumbnail_size
        );
    }
    exif_thumbnail = exif_thumbnail_found;
    rewind(file);

    decoder.err = jpeg_std_error(&jpeg_error.manager);
    jpeg_error.manager.error_exit = imgorg_browser_window_jpeg_error_exit;
    if (setjmp(jpeg_error.escape) != 0) {
        if (decoder_created) {
            jpeg_destroy_decompress(&decoder);
        }
        free((void *) decoded_pixels);
        free((void *) exif_thumbnail);
        fclose(file);
        return NULL;
    }

    jpeg_create_decompress(&decoder);
    decoder_created = true;
    if (exif_thumbnail != NULL) {
        jpeg_mem_src(
            &decoder,
            (const unsigned char *) exif_thumbnail,
            (unsigned long) exif_thumbnail_size
        );
    } else {
        jpeg_stdio_src(&decoder, file);
    }
    if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK ||
        decoder.image_width == 0 || decoder.image_height == 0 ||
        decoder.image_width > MAXIMUM_IMAGE_DIMENSION ||
        decoder.image_height > MAXIMUM_IMAGE_DIMENSION) {
        jpeg_destroy_decompress(&decoder);
        free((void *) exif_thumbnail);
        fclose(file);
        return NULL;
    }

    scale_denominator = 8;
    while (scale_denominator > 1 &&
        decoder.image_width / scale_denominator <
            (unsigned int) maximum_width &&
        decoder.image_height / scale_denominator <
            (unsigned int) maximum_height) {
        scale_denominator /= 2;
    }
    decoder.scale_num = 1;
    decoder.scale_denom = scale_denominator;
    decoder.out_color_space = JCS_RGB;
    decoder.dct_method = JDCT_IFAST;
    decoder.do_fancy_upsampling = FALSE;
    if (!jpeg_start_decompress(&decoder)) {
        jpeg_destroy_decompress(&decoder);
        free((void *) exif_thumbnail);
        fclose(file);
        return NULL;
    }

    decoded_width = (int) decoder.output_width;
    decoded_height = (int) decoder.output_height;
    decoded_components = decoder.output_components;
    if (decoded_width <= 0 || decoded_height <= 0 ||
        decoded_components != 3 ||
        (size_t) decoded_width > SIZE_MAX / (size_t) decoded_height /
            (size_t) decoded_components) {
        jpeg_destroy_decompress(&decoder);
        free((void *) exif_thumbnail);
        fclose(file);
        return NULL;
    }
    decoded_row_bytes = (size_t) decoded_width * decoded_components;
    decoded_pixels = malloc(decoded_row_bytes * decoded_height);
    if (decoded_pixels == NULL) {
        jpeg_destroy_decompress(&decoder);
        free((void *) exif_thumbnail);
        fclose(file);
        return NULL;
    }
    while (decoder.output_scanline < decoder.output_height) {
        JSAMPROW row = (byte *) decoded_pixels +
            ((size_t) decoder.output_scanline * decoded_row_bytes);
        if (jpeg_read_scanlines(&decoder, &row, 1) != 1) {
            free((void *) decoded_pixels);
            jpeg_destroy_decompress(&decoder);
            free((void *) exif_thumbnail);
            fclose(file);
            return NULL;
        }
    }
    (void) jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    decoder_created = false;
    free((void *) exif_thumbnail);
    exif_thumbnail = NULL;
    fclose(file);

    output_width = decoded_width;
    output_height = decoded_height;
    if (output_width > maximum_width || output_height > maximum_height) {
        if ((long long) output_width * maximum_height >
            (long long) output_height * maximum_width) {
            output_height = (int) ((long long) output_height *
                maximum_width / output_width);
            output_width = maximum_width;
        } else {
            output_width = (int) ((long long) output_width *
                maximum_height / output_height);
            output_height = maximum_height;
        }
        if (output_width < 1) {
            output_width = 1;
        }
        if (output_height < 1) {
            output_height = 1;
        }
    }

    area = imgorg_browser_window_create_sprite(output_width, output_height);
    if (area == NULL) {
        free((void *) decoded_pixels);
        return NULL;
    }
    sprite = (osspriteop_header *) ((byte *) area + area->first);
    output_pixels = (byte *) sprite + sprite->image;
    for (y = 0; y < output_height; ++y) {
        int source_y = (int) ((long long) y * decoded_height /
            output_height);
        int x;

        for (x = 0; x < output_width; ++x) {
            int source_x = (int) ((long long) x * decoded_width /
                output_width);
            const byte *source = (const byte *) decoded_pixels +
                ((size_t) source_y * decoded_row_bytes) +
                ((size_t) source_x * decoded_components);
            byte *destination = output_pixels +
                (((size_t) y * output_width + x) * 4);

            destination[0] = source[0];
            destination[1] = source[1];
            destination[2] = source[2];
            destination[3] = 0;
        }
    }
    free((void *) decoded_pixels);

    *width_out = output_width;
    *height_out = output_height;
    return area;
}

static osspriteop_area *imgorg_browser_window_decode_sprite_file(
    const imgorg_image_entry *entry,
    int maximum_width,
    int maximum_height,
    int *width_out,
    int *height_out
)
{
    osspriteop_area *source_area = NULL;
    osspriteop_header *source_sprite;
    osspriteop_area *output_area = NULL;
    osspriteop_header *output_sprite;
    osspriteop_save_area *save_area = NULL;
    osspriteop_trans_tab *translation = NULL;
    os_error *error;
    osbool has_mask;
    os_factors factors;
    size_t area_size;
    int source_width;
    int source_height;
    int output_width;
    int output_height;
    int save_area_size;
    int translation_size = 0;
    int context0;
    int context1;
    int context2;
    int context3;
    bool output_switched = false;

    if (entry == NULL || entry->size_bytes < 12 ||
        entry->size_bytes > MAXIMUM_SPRITE_FILE_SIZE ||
        entry->size_bytes > (uint64_t) INT_MAX - 4) {
        return NULL;
    }
    area_size = (size_t) entry->size_bytes + 4;
    source_area = malloc(area_size);
    if (source_area == NULL) {
        return NULL;
    }
    source_area->size = (int) area_size;
    error = xosspriteop_load_sprite_file(
        osspriteop_USER_AREA,
        source_area,
        entry->path
    );
    if (error != NULL || source_area->sprite_count < 1 ||
        source_area->first < (int) sizeof(*source_area) ||
        source_area->used > source_area->size ||
        source_area->used < source_area->first + (int) sizeof(*source_sprite) ||
        (size_t) source_area->first + sizeof(*source_sprite) > area_size) {
        free(source_area);
        return NULL;
    }
    source_sprite = (osspriteop_header *)
        ((byte *) source_area + source_area->first);
    error = xosspriteop_read_sprite_info(
        osspriteop_PTR,
        source_area,
        (osspriteop_id) source_sprite,
        &source_width,
        &source_height,
        &has_mask,
        NULL
    );
    if (error != NULL || source_width <= 0 || source_height <= 0 ||
        source_width > MAXIMUM_IMAGE_DIMENSION ||
        source_height > MAXIMUM_IMAGE_DIMENSION) {
        free(source_area);
        return NULL;
    }

    output_width = source_width;
    output_height = source_height;
    if (output_width > maximum_width || output_height > maximum_height) {
        if ((long long) output_width * maximum_height >
            (long long) output_height * maximum_width) {
            output_height = (int) ((long long) output_height *
                maximum_width / output_width);
            output_width = maximum_width;
        } else {
            output_width = (int) ((long long) output_width *
                maximum_height / output_height);
            output_height = maximum_height;
        }
        if (output_width < 1) {
            output_width = 1;
        }
        if (output_height < 1) {
            output_height = 1;
        }
    }

    output_area = imgorg_browser_window_create_sprite(
        output_width,
        output_height
    );
    if (output_area == NULL) {
        free(source_area);
        return NULL;
    }
    output_sprite = (osspriteop_header *)
        ((byte *) output_area + output_area->first);
    error = xcolourtrans_generate_table_for_sprite(
        source_area,
        (osspriteop_id) source_sprite,
        output_sprite->mode,
        NULL,
        NULL,
        0,
        NULL,
        NULL,
        &translation_size
    );
    if (error != NULL) {
        goto cleanup;
    }
    if (translation_size < 0 || translation_size > 1024 * 1024) {
        goto cleanup;
    }
    if (translation_size > 0) {
        translation = malloc((size_t) translation_size);
        if (translation == NULL) {
            goto cleanup;
        }
        error = xcolourtrans_generate_table_for_sprite(
            source_area,
            (osspriteop_id) source_sprite,
            output_sprite->mode,
            NULL,
            translation,
            0,
            NULL,
            NULL,
            &translation_size
        );
        if (error != NULL) {
            goto cleanup;
        }
    }

    error = xosspriteop_read_save_area_size(
        osspriteop_PTR,
        output_area,
        (osspriteop_id) output_sprite,
        &save_area_size
    );
    if (error != NULL || save_area_size <= 0) {
        goto cleanup;
    }
    save_area = malloc((size_t) save_area_size);
    if (save_area == NULL) {
        goto cleanup;
    }
    error = xosspriteop_switch_output_to_sprite(
        osspriteop_PTR,
        output_area,
        (osspriteop_id) output_sprite,
        save_area,
        &context0,
        &context1,
        &context2,
        &context3
    );
    if (error != NULL) {
        goto cleanup;
    }
    output_switched = true;
    factors.xmul = output_width;
    factors.ymul = output_height;
    factors.xdiv = source_width;
    factors.ydiv = source_height;
    error = xosspriteop_put_sprite_scaled(
        osspriteop_PTR,
        source_area,
        (osspriteop_id) source_sprite,
        0,
        0,
        os_ACTION_OVERWRITE | (has_mask ? osspriteop_USE_MASK : 0),
        &factors,
        translation
    );
    {
        os_error *unswitch_error = xosspriteop_unswitch_output(
            context0,
            context1,
            context2,
            context3
        );
        output_switched = false;
        if (error == NULL) {
            error = unswitch_error;
        }
    }
    if (error != NULL) {
        goto cleanup;
    }

    free(save_area);
    free(translation);
    free(source_area);
    *width_out = output_width;
    *height_out = output_height;
    return output_area;

cleanup:
    if (output_switched) {
        (void) xosspriteop_unswitch_output(
            context0,
            context1,
            context2,
            context3
        );
    }
    free(save_area);
    free(translation);
    free(source_area);
    free(output_area);
    return NULL;
}

static void imgorg_browser_window_clear_thumbnails(
    imgorg_browser_window *browser
)
{
    size_t index;

    for (index = 0; index < browser->thumbnail_count; ++index) {
        free(browser->thumbnails[index].sprite_area);
    }
    free(browser->thumbnails);
    browser->thumbnails = NULL;
    browser->thumbnail_count = 0;
    browser->thumbnail_capacity = 0;
    browser->thumbnail_cursor = 0;
    browser->thumbnail_priority_start = 0;
    browser->thumbnail_priority_end =
        imgorg_browser_window_thumbnail_columns(browser) * 3;
}

static imgorg_viewer_window *imgorg_browser_window_find_viewer(
    imgorg_browser_window *browser,
    wimp_w handle
)
{
    imgorg_viewer_window *viewer;

    for (viewer = browser->viewers; viewer != NULL; viewer = viewer->next) {
        if (viewer->created && viewer->handle == handle) {
            return viewer;
        }
    }
    return NULL;
}

static imgorg_viewer_window *imgorg_browser_window_find_image(
    imgorg_browser_window *browser,
    const char *file_name
)
{
    imgorg_viewer_window *viewer;

    for (viewer = browser->viewers; viewer != NULL; viewer = viewer->next) {
        if (viewer->sprite_area != NULL &&
            strcmp(viewer->image_path, file_name) == 0) {
            return viewer;
        }
    }
    return NULL;
}

static imgorg_viewer_window *imgorg_browser_window_find_available_viewer(
    imgorg_browser_window *browser
)
{
    imgorg_viewer_window *viewer;

    for (viewer = browser->viewers; viewer != NULL; viewer = viewer->next) {
        if (viewer->created && viewer->sprite_area == NULL) {
            return viewer;
        }
    }
    return NULL;
}

static imgorg_viewer_window *imgorg_browser_window_create_viewer(
    imgorg_browser_window *browser,
    os_error **error_out
)
{
    imgorg_viewer_window *viewer;
    imgorg_viewer_window *cursor;
    wimp_WINDOW(VIEWER_TOOLBAR_ICON_COUNT) definition;
    size_t viewer_count = 0;
    int desktop_width;
    int desktop_height;
    int viewer_width;
    int viewer_height;

    *error_out = NULL;
    viewer = calloc(1, sizeof(*viewer));
    if (viewer == NULL) {
        *error_out = imgorg_browser_window_error(
            "There is not enough memory for another image viewer"
        );
        return NULL;
    }
    for (cursor = browser->viewers; cursor != NULL; cursor = cursor->next) {
        ++viewer_count;
    }
    viewer->toolbar_visible = true;
    snprintf(
        viewer->fullscreen_label,
        sizeof(viewer->fullscreen_label),
        "%s",
        IMGORG_VIEWER_FULLSCREEN_LABEL
    );
    (void) imgorg_browser_window_update_viewer_title(viewer);
    memset(&definition, 0, sizeof(definition));
    imgorg_browser_window_read_desktop_size(
        &desktop_width,
        &desktop_height
    );
    viewer_width = desktop_width * 3 / 4;
    viewer_height = desktop_height * 3 / 4;
    if (viewer_width < 640) {
        viewer_width = 640;
    }
    if (viewer_height < 480) {
        viewer_height = 480;
    }
    definition.visible.x0 = (desktop_width - viewer_width) / 2 +
        (int) (viewer_count % 5) * 8;
    definition.visible.y0 = (desktop_height - viewer_height) / 2 +
        (int) (viewer_count % 5) * 8;
    definition.visible.x1 = definition.visible.x0 + viewer_width;
    definition.visible.y1 = definition.visible.y0 + viewer_height;
    definition.next = wimp_TOP;
    definition.flags =
        wimp_WINDOW_MOVEABLE |
        wimp_WINDOW_BACK_ICON |
        wimp_WINDOW_CLOSE_ICON |
        wimp_WINDOW_TITLE_ICON |
        wimp_WINDOW_TOGGLE_ICON |
        wimp_WINDOW_SIZE_ICON |
        wimp_WINDOW_SCROLL |
        wimp_WINDOW_IGNORE_XEXTENT |
        wimp_WINDOW_IGNORE_YEXTENT |
        wimp_WINDOW_VSCROLL |
        wimp_WINDOW_NEW_FORMAT;
    definition.title_fg = wimp_COLOUR_BLACK;
    definition.title_bg = wimp_COLOUR_LIGHT_GREY;
    definition.work_fg = wimp_COLOUR_BLACK;
    definition.work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
    definition.scroll_outer = wimp_COLOUR_MID_LIGHT_GREY;
    definition.scroll_inner = wimp_COLOUR_VERY_LIGHT_GREY;
    definition.highlight_bg = wimp_COLOUR_CREAM;
    definition.extra_flags =
        wimp_WINDOW_USE_EXTENDED_SCROLL_REQUEST |
        wimp_WINDOW_ALWAYS3D;
    definition.extent.x0 = 0;
    definition.extent.y0 = -desktop_height;
    definition.extent.x1 = desktop_width;
    definition.extent.y1 = 0;
    definition.title_flags =
        wimp_ICON_TEXT |
        wimp_ICON_HCENTRED |
        wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED;
    definition.work_flags =
        wimp_BUTTON_CLICK_DRAG << wimp_ICON_BUTTON_TYPE_SHIFT;
    definition.sprite_area = wimpspriteop_AREA;
    definition.xmin = 1;
    definition.ymin = 1;
    definition.title_data.indirected_text.text = viewer->title;
    definition.title_data.indirected_text.validation = (char *) -1;
    definition.title_data.indirected_text.size = sizeof(viewer->title);
    definition.icon_count = VIEWER_TOOLBAR_ICON_COUNT;

    definition.icons[VIEWER_TOOLBAR_BACKGROUND].extent.x0 = 0;
    definition.icons[VIEWER_TOOLBAR_BACKGROUND].extent.y0 =
        -VIEWER_TOOLBAR_HEIGHT;
    definition.icons[VIEWER_TOOLBAR_BACKGROUND].extent.x1 =
        BROWSER_WORKSPACE_WIDTH;
    definition.icons[VIEWER_TOOLBAR_BACKGROUND].extent.y1 = 0;
    definition.icons[VIEWER_TOOLBAR_BACKGROUND].flags =
        wimp_ICON_TEXT |
        wimp_ICON_BORDER |
        wimp_ICON_FILLED |
        wimp_ICON_INDIRECTED |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT);
    definition.icons[VIEWER_TOOLBAR_BACKGROUND].data.indirected_text.text =
        IMGORG_EMPTY_ICON_TEXT;
    definition.icons[VIEWER_TOOLBAR_BACKGROUND].data.indirected_text.validation =
        IMGORG_BORDER_RIDGE;
    definition.icons[VIEWER_TOOLBAR_BACKGROUND].data.indirected_text.size =
        sizeof(IMGORG_EMPTY_ICON_TEXT);

    definition.icons[VIEWER_TOOLBAR_BACK].extent.x0 = 8;
    definition.icons[VIEWER_TOOLBAR_BACK].extent.y0 = -64;
    definition.icons[VIEWER_TOOLBAR_BACK].extent.x1 = 56;
    definition.icons[VIEWER_TOOLBAR_BACK].extent.y1 = -8;
    definition.icons[VIEWER_TOOLBAR_FORWARD].extent.x0 = 64;
    definition.icons[VIEWER_TOOLBAR_FORWARD].extent.y0 = -64;
    definition.icons[VIEWER_TOOLBAR_FORWARD].extent.x1 = 112;
    definition.icons[VIEWER_TOOLBAR_FORWARD].extent.y1 = -8;
    definition.icons[VIEWER_TOOLBAR_ACTUAL_SIZE].extent.x0 = 120;
    definition.icons[VIEWER_TOOLBAR_ACTUAL_SIZE].extent.y0 = -64;
    definition.icons[VIEWER_TOOLBAR_ACTUAL_SIZE].extent.x1 = 200;
    definition.icons[VIEWER_TOOLBAR_ACTUAL_SIZE].extent.y1 = -8;
    definition.icons[VIEWER_TOOLBAR_FIT].extent.x0 = 208;
    definition.icons[VIEWER_TOOLBAR_FIT].extent.y0 = -64;
    definition.icons[VIEWER_TOOLBAR_FIT].extent.x1 = 288;
    definition.icons[VIEWER_TOOLBAR_FIT].extent.y1 = -8;
    definition.icons[VIEWER_TOOLBAR_FULLSCREEN].extent.x0 = 296;
    definition.icons[VIEWER_TOOLBAR_FULLSCREEN].extent.y0 = -64;
    definition.icons[VIEWER_TOOLBAR_FULLSCREEN].extent.x1 = 544;
    definition.icons[VIEWER_TOOLBAR_FULLSCREEN].extent.y1 = -8;

    {
        int icon;
        char *const labels[] = {
            IMGORG_VIEWER_BACK_LABEL,
            IMGORG_VIEWER_FORWARD_LABEL,
            IMGORG_VIEWER_ACTUAL_LABEL,
            IMGORG_VIEWER_FIT_LABEL,
            viewer->fullscreen_label
        };

        for (icon = VIEWER_TOOLBAR_BACK;
             icon <= VIEWER_TOOLBAR_FULLSCREEN;
             ++icon) {
            definition.icons[icon].flags =
                wimp_ICON_TEXT |
                wimp_ICON_BORDER |
                wimp_ICON_HCENTRED |
                wimp_ICON_VCENTRED |
                wimp_ICON_FILLED |
                wimp_ICON_INDIRECTED |
                (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT) |
                (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
                (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT);
            definition.icons[icon].data.indirected_text.text =
                labels[icon - VIEWER_TOOLBAR_BACK];
            definition.icons[icon].data.indirected_text.validation =
                IMGORG_BORDER_ACTION;
            definition.icons[icon].data.indirected_text.size =
                icon == VIEWER_TOOLBAR_FULLSCREEN ?
                    sizeof(viewer->fullscreen_label) :
                    strlen(labels[icon - VIEWER_TOOLBAR_BACK]) + 1;
        }
    }
    *error_out = xwimp_create_window(
        (wimp_window *) &definition,
        &viewer->handle
    );
    if (*error_out != NULL) {
        free(viewer);
        return NULL;
    }
    viewer->created = true;
    viewer->next = browser->viewers;
    browser->viewers = viewer;
    return viewer;
}

static os_error *imgorg_browser_window_open_viewer(
    imgorg_viewer_window *viewer
)
{
    wimp_window_state state;
    os_error *error;

    state.w = viewer->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }
    state.next = wimp_TOP;
    error = xwimp_open_window((wimp_open *) &state);
    if (error != NULL) {
        return error;
    }
    return xwimp_set_caret_position(
        viewer->handle,
        wimp_ICON_WINDOW,
        0,
        0,
        1 << 25,
        -1
    );
}

static os_error *imgorg_browser_window_show_image(
    imgorg_browser_window *browser,
    imgorg_viewer_window *viewer,
    const char *file_name,
    osspriteop_area *new_area,
    int width,
    int height
)
{
    os_error *error;
    size_t old_size = viewer->sprite_area == NULL ? 0u :
        (size_t) viewer->sprite_area->size;
    size_t new_size = (size_t) new_area->size;
    uint64_t projected = WIMP_SLOT_RESERVE_BYTES +
        imgorg_browser_window_tracked_thumbnail_bytes(browser) +
        browser->viewer_image_bytes - old_size + new_size;

    if (projected > WIMP_SLOT_SIZE_BYTES) {
        free(new_area);
        return imgorg_browser_window_error(
            "Opening this image would exceed the 128 MB Wimp slot"
        );
    }

    imgorg_browser_window_copy_leafname(
        viewer->image_name,
        sizeof(viewer->image_name),
        file_name
    );
    snprintf(viewer->image_path, sizeof(viewer->image_path), "%s", file_name);
    free(viewer->sprite_area);
    browser->viewer_image_bytes =
        browser->viewer_image_bytes - old_size + new_size;
    viewer->sprite_area = new_area;
    viewer->sprite = (osspriteop_header *)
        ((byte *) new_area + new_area->first);
    viewer->image_width = width;
    viewer->image_height = height;
    viewer->fit_to_window = true;
    viewer->zoom_percent = 100;
    viewer->pan_x = 0;
    viewer->pan_y = 0;
    viewer->dragging = false;

    if (!viewer->created) {
        return NULL;
    }
    error = imgorg_browser_window_update_viewer_title(viewer);
    if (error != NULL) {
        return error;
    }
    error = imgorg_browser_window_redraw_viewer(viewer);
    if (error != NULL) {
        return error;
    }
    return imgorg_browser_window_open_viewer(viewer);
}

static void imgorg_browser_window_set_thumbnail_priority(
    imgorg_browser_window *browser,
    int yscroll,
    int visible_height
)
{
    const int row_height =
        imgorg_browser_window_thumbnail_cell_height(browser) +
        THUMBNAIL_GAP;
    size_t columns = imgorg_browser_window_thumbnail_columns(browser);
    size_t first_row;
    size_t visible_rows;

    if (yscroll > 0) {
        yscroll = 0;
    }
    if (-yscroll > WORKSPACE_HEADER_HEIGHT) {
        first_row = (size_t) (
            (-yscroll - WORKSPACE_HEADER_HEIGHT) / row_height
        );
    } else {
        first_row = 0;
    }
    visible_rows = (size_t) ((visible_height + row_height - 1) / row_height);
    browser->thumbnail_priority_start = first_row * columns;
    browser->thumbnail_priority_end =
        (first_row + visible_rows + 1) * columns;
}

static bool imgorg_browser_window_ensure_thumbnail_slots(
    imgorg_browser_window *browser
)
{
    imgorg_thumbnail *thumbnails;
    size_t new_capacity;
    size_t old_count = browser->thumbnail_count;

    if (browser->images.count > browser->thumbnail_capacity) {
        new_capacity = browser->thumbnail_capacity == 0 ?
            32 : browser->thumbnail_capacity;
        while (new_capacity < browser->images.count) {
            if (new_capacity > SIZE_MAX / 2) {
                return false;
            }
            new_capacity *= 2;
        }
        if (new_capacity > SIZE_MAX / sizeof(*thumbnails)) {
            return false;
        }
        thumbnails = realloc(
            browser->thumbnails,
            new_capacity * sizeof(*thumbnails)
        );
        if (thumbnails == NULL) {
            return false;
        }
        browser->thumbnails = thumbnails;
        browser->thumbnail_capacity = new_capacity;
    }

    if (browser->images.count > old_count) {
        memset(
            browser->thumbnails + old_count,
            0,
            (browser->images.count - old_count) *
                sizeof(*browser->thumbnails)
        );
    }
    browser->thumbnail_count = browser->images.count;
    return true;
}

static size_t imgorg_browser_window_next_thumbnail_index(
    imgorg_browser_window *browser
)
{
    size_t index;
    size_t visible_index;
    size_t visible_count =
        imgorg_browser_window_visible_image_count(browser);
    size_t priority_end = browser->thumbnail_priority_end;

    if (priority_end > visible_count) {
        priority_end = visible_count;
    }
    for (visible_index = browser->thumbnail_priority_start;
         visible_index < priority_end;
         ++visible_index) {
        const imgorg_image_entry *entry;

        index = imgorg_browser_window_actual_image_index(
            browser,
            visible_index
        );
        if (index == SIZE_MAX) {
            continue;
        }
        if (browser->thumbnails[index].attempted) {
            continue;
        }
        entry = imgorg_image_list_get(&browser->images, index);
        if (entry->format == IMGORG_IMAGE_FORMAT_PNG ||
            entry->format == IMGORG_IMAGE_FORMAT_JPEG ||
            entry->format == IMGORG_IMAGE_FORMAT_SPRITE) {
            return index;
        }
        browser->thumbnails[index].attempted = true;
    }

    while (browser->thumbnail_cursor < browser->images.count) {
        const imgorg_image_entry *entry;

        index = browser->thumbnail_cursor++;
        if (browser->thumbnails[index].attempted) {
            continue;
        }
        entry = imgorg_image_list_get(&browser->images, index);
        if (entry->format == IMGORG_IMAGE_FORMAT_PNG ||
            entry->format == IMGORG_IMAGE_FORMAT_JPEG ||
            entry->format == IMGORG_IMAGE_FORMAT_SPRITE) {
            return index;
        }
        browser->thumbnails[index].attempted = true;
    }
    return SIZE_MAX;
}

os_error *imgorg_browser_window_create(imgorg_browser_window *browser)
{
    wimp_window definition;
    wimp_WINDOW(1) loading_definition;
    wimp_WINDOW(ALBUM_DIALOG_ICON_COUNT) album_definition;
    os_error *error;
    int desktop_width;
    int desktop_height;
    int initial_width;
    int initial_height;
    int usable_y0;
    size_t loaded_folder_count;

    if (browser == NULL) {
        return NULL;
    }

    memset(browser, 0, sizeof(*browser));
    browser->thumbnail_cell_width = THUMBNAIL_DEFAULT_CELL_WIDTH;
    snprintf(browser->album_dialog_title,
        sizeof(browser->album_dialog_title), "Album");
    snprintf(browser->album_dialog_label,
        sizeof(browser->album_dialog_label), "Album name:");
    imgorg_image_list_init(&browser->images);
    imgorg_folder_list_init(&browser->folders);
    imgorg_album_list_init(&browser->albums);
    imgorg_directory_scanner_init(&browser->scanner);
    if (!imgorg_library_catalog_load(
            IMGORG_LIBRARY_FILE,
            &browser->folders,
            &browser->images,
            &browser->albums
        )) {
        imgorg_folder_list_destroy(&browser->folders);
        imgorg_album_list_destroy(&browser->albums);
        imgorg_image_list_destroy(&browser->images);
        return imgorg_browser_window_error(
            "The Aural library catalogue could not be read"
        );
    }
    /*
     * Imported folders are catalogue sources, not live views. Existing
     * sources are not silently re-imported at startup, so an explicit
     * "Remove from library" remains removed.
     */
    loaded_folder_count = browser->folders.count;
    imgorg_browser_window_prune_empty_folders(browser);
    browser->library_dirty =
        browser->folders.count != loaded_folder_count;
    browser->folder_scan_index = browser->folders.count;
    memset(&definition, 0, sizeof(definition));
    (void) imgorg_browser_window_update_title(browser);

    imgorg_browser_window_read_desktop_size(
        &desktop_width,
        &desktop_height
    );
    initial_width = desktop_width * 3 / 5;
    initial_height = desktop_height * 3 / 4;
    usable_y0 = 96;
    definition.visible.x0 = (desktop_width - initial_width) / 2;
    definition.visible.y0 =
        usable_y0 + (desktop_height - usable_y0 - initial_height) / 2;
    definition.visible.x1 = definition.visible.x0 + initial_width;
    definition.visible.y1 = definition.visible.y0 + initial_height;
    definition.xscroll = 0;
    definition.yscroll = 0;
    definition.next = wimp_TOP;

    definition.flags =
        wimp_WINDOW_MOVEABLE |
        wimp_WINDOW_BACK_ICON |
        wimp_WINDOW_CLOSE_ICON |
        wimp_WINDOW_TITLE_ICON |
        wimp_WINDOW_TOGGLE_ICON |
        wimp_WINDOW_SIZE_ICON |
        wimp_WINDOW_SCROLL |
        wimp_WINDOW_IGNORE_XEXTENT |
        wimp_WINDOW_IGNORE_YEXTENT |
        wimp_WINDOW_VSCROLL |
        wimp_WINDOW_NEW_FORMAT;

    definition.title_fg = wimp_COLOUR_BLACK;
    definition.title_bg = wimp_COLOUR_LIGHT_GREY;
    definition.work_fg = wimp_COLOUR_BLACK;
    definition.work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
    definition.scroll_outer = wimp_COLOUR_MID_LIGHT_GREY;
    definition.scroll_inner = wimp_COLOUR_VERY_LIGHT_GREY;
    definition.highlight_bg = wimp_COLOUR_CREAM;
    definition.extra_flags =
        wimp_WINDOW_USE_EXTENDED_SCROLL_REQUEST |
        wimp_WINDOW_ALWAYS3D;

    definition.extent.x0 = 0;
    definition.extent.y0 = -2048;
    definition.extent.x1 = BROWSER_WORKSPACE_WIDTH;
    definition.extent.y1 = 0;

    definition.title_flags =
        wimp_ICON_TEXT |
        wimp_ICON_HCENTRED |
        wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED;

    definition.work_flags =
        wimp_BUTTON_DOUBLE_CLICK_DRAG << wimp_ICON_BUTTON_TYPE_SHIFT;

    definition.sprite_area = wimpspriteop_AREA;
    definition.xmin = 960;
    definition.ymin = 480;

    definition.title_data.indirected_text.text = browser->title;
    definition.title_data.indirected_text.validation = (char *) -1;
    definition.title_data.indirected_text.size = sizeof(browser->title);

    definition.icon_count = 0;

    error = xwimp_create_window(&definition, &browser->handle);
    if (error != NULL) {
        imgorg_folder_list_destroy(&browser->folders);
        imgorg_album_list_destroy(&browser->albums);
        imgorg_image_list_destroy(&browser->images);
        return error;
    }
    browser->created = true;

    memset(&loading_definition, 0, sizeof(loading_definition));
    snprintf(
        browser->loading_text,
        sizeof(browser->loading_text),
        "Loading image..."
    );
    loading_definition.visible.x0 = 320;
    loading_definition.visible.y0 = 320;
    loading_definition.visible.x1 = 320 + LOADING_WINDOW_WIDTH;
    loading_definition.visible.y1 = 320 + LOADING_WINDOW_HEIGHT;
    loading_definition.next = wimp_TOP;
    loading_definition.flags =
        wimp_WINDOW_MOVEABLE |
        wimp_WINDOW_TITLE_ICON |
        wimp_WINDOW_NEW_FORMAT;
    loading_definition.title_fg = wimp_COLOUR_BLACK;
    loading_definition.title_bg = wimp_COLOUR_LIGHT_GREY;
    loading_definition.work_fg = wimp_COLOUR_BLACK;
    loading_definition.work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
    loading_definition.extent.x0 = 0;
    loading_definition.extent.y0 = -LOADING_WINDOW_HEIGHT;
    loading_definition.extent.x1 = LOADING_WINDOW_WIDTH;
    loading_definition.extent.y1 = 0;
    loading_definition.title_flags =
        wimp_ICON_TEXT | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED;
    snprintf(
        loading_definition.title_data.text,
        sizeof(loading_definition.title_data.text),
        "Aural"
    );
    loading_definition.work_flags =
        wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT;
    loading_definition.sprite_area = wimpspriteop_AREA;
    loading_definition.xmin = LOADING_WINDOW_WIDTH;
    loading_definition.ymin = LOADING_WINDOW_HEIGHT;
    loading_definition.icon_count = 1;
    loading_definition.icons[0].extent = loading_definition.extent;
    loading_definition.icons[0].flags =
        wimp_ICON_TEXT |
        wimp_ICON_HCENTRED |
        wimp_ICON_VCENTRED |
        wimp_ICON_FILLED |
        wimp_ICON_INDIRECTED |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT);
    loading_definition.icons[0].data.indirected_text.text =
        browser->loading_text;
    loading_definition.icons[0].data.indirected_text.validation = (char *) -1;
    loading_definition.icons[0].data.indirected_text.size =
        sizeof(browser->loading_text);

    error = xwimp_create_window(
        (wimp_window *) &loading_definition,
        &browser->loading_handle
    );
    if (error != NULL) {
        (void) xwimp_delete_window(browser->handle);
        browser->handle = 0;
        browser->created = false;
        imgorg_folder_list_destroy(&browser->folders);
        imgorg_album_list_destroy(&browser->albums);
        imgorg_image_list_destroy(&browser->images);
        return error;
    }
    browser->loading_created = true;

    memset(&album_definition, 0, sizeof(album_definition));
    album_definition.visible.x0 = 320;
    album_definition.visible.y0 = 320;
    album_definition.visible.x1 = 320 + ALBUM_DIALOG_WIDTH;
    album_definition.visible.y1 = 320 + ALBUM_DIALOG_HEIGHT;
    album_definition.next = wimp_TOP;
    album_definition.flags =
        wimp_WINDOW_MOVEABLE |
        wimp_WINDOW_TITLE_ICON |
        wimp_WINDOW_CLOSE_ICON |
        wimp_WINDOW_AUTO_REDRAW |
        wimp_WINDOW_NEW_FORMAT;
    album_definition.title_fg = wimp_COLOUR_BLACK;
    album_definition.title_bg = wimp_COLOUR_LIGHT_GREY;
    album_definition.work_fg = wimp_COLOUR_BLACK;
    album_definition.work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
    album_definition.extent.x0 = 0;
    album_definition.extent.y0 = -ALBUM_DIALOG_HEIGHT;
    album_definition.extent.x1 = ALBUM_DIALOG_WIDTH;
    album_definition.extent.y1 = 0;
    album_definition.title_flags =
        wimp_ICON_TEXT | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED;
    album_definition.title_data.indirected_text.text =
        browser->album_dialog_title;
    album_definition.title_data.indirected_text.validation = (char *) -1;
    album_definition.title_data.indirected_text.size =
        sizeof(browser->album_dialog_title);
    album_definition.work_flags =
        wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT;
    album_definition.sprite_area = wimpspriteop_AREA;
    album_definition.xmin = ALBUM_DIALOG_WIDTH;
    album_definition.ymin = ALBUM_DIALOG_HEIGHT;
    album_definition.icon_count = ALBUM_DIALOG_ICON_COUNT;

    album_definition.icons[ALBUM_DIALOG_LABEL].extent =
        (os_box) {20, -72, 180, -24};
    album_definition.icons[ALBUM_DIALOG_LABEL].flags =
        wimp_ICON_TEXT | wimp_ICON_VCENTRED | wimp_ICON_INDIRECTED |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT);
    album_definition.icons[ALBUM_DIALOG_LABEL].data.indirected_text.text =
        browser->album_dialog_label;
    album_definition.icons[ALBUM_DIALOG_LABEL].
        data.indirected_text.validation = (char *) -1;
    album_definition.icons[ALBUM_DIALOG_LABEL].data.indirected_text.size =
        sizeof(browser->album_dialog_label);
    album_definition.icons[ALBUM_DIALOG_NAME].extent =
        (os_box) {200, -72, 500, -24};
    album_definition.icons[ALBUM_DIALOG_NAME].flags =
        wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_FILLED |
        wimp_ICON_INDIRECTED |
        (wimp_BUTTON_WRITABLE << wimp_ICON_BUTTON_TYPE_SHIFT) |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT);
    album_definition.icons[ALBUM_DIALOG_NAME].data.indirected_text.text =
        browser->album_dialog_name;
    album_definition.icons[ALBUM_DIALOG_NAME].data.indirected_text.validation =
        (char *) -1;
    album_definition.icons[ALBUM_DIALOG_NAME].data.indirected_text.size =
        sizeof(browser->album_dialog_name);
    album_definition.icons[ALBUM_DIALOG_CANCEL].extent =
        (os_box) {244, -152, 364, -96};
    album_definition.icons[ALBUM_DIALOG_CANCEL].flags =
        wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
        wimp_ICON_VCENTRED | wimp_ICON_FILLED | wimp_ICON_INDIRECTED |
        (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT) |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT);
    album_definition.icons[ALBUM_DIALOG_CANCEL].data.indirected_text.text =
        IMGORG_ALBUM_DIALOG_CANCEL_LABEL;
    album_definition.icons[ALBUM_DIALOG_CANCEL].
        data.indirected_text.validation = IMGORG_BORDER_ACTION;
    album_definition.icons[ALBUM_DIALOG_CANCEL].data.indirected_text.size =
        sizeof(IMGORG_ALBUM_DIALOG_CANCEL_LABEL);
    album_definition.icons[ALBUM_DIALOG_OK].extent =
        (os_box) {380, -152, 500, -96};
    album_definition.icons[ALBUM_DIALOG_OK].flags =
        wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
        wimp_ICON_VCENTRED | wimp_ICON_FILLED | wimp_ICON_INDIRECTED |
        (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT) |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT);
    album_definition.icons[ALBUM_DIALOG_OK].data.indirected_text.text =
        IMGORG_ALBUM_DIALOG_OK_LABEL;
    album_definition.icons[ALBUM_DIALOG_OK].
        data.indirected_text.validation = IMGORG_BORDER_ACTION;
    album_definition.icons[ALBUM_DIALOG_OK].data.indirected_text.size =
        sizeof(IMGORG_ALBUM_DIALOG_OK_LABEL);
    error = xwimp_create_window(
        (wimp_window *) &album_definition,
        &browser->album_dialog_handle
    );
    if (error != NULL) {
        (void) xwimp_delete_window(browser->loading_handle);
        (void) xwimp_delete_window(browser->handle);
        browser->loading_created = false;
        browser->created = false;
        imgorg_folder_list_destroy(&browser->folders);
        imgorg_album_list_destroy(&browser->albums);
        imgorg_image_list_destroy(&browser->images);
        return error;
    }
    browser->album_dialog_created = true;
    if (!imgorg_browser_window_ensure_thumbnail_slots(browser)) {
        return imgorg_browser_window_error(
            "There is not enough memory for the thumbnail list"
        );
    }
    return imgorg_browser_window_start_next_folder_scan(browser);
}

os_error *imgorg_browser_window_open(imgorg_browser_window *browser)
{
    wimp_window_state state;
    os_error *error;

    if (browser == NULL || !browser->created) {
        return NULL;
    }

    state.w = browser->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }

    state.next = wimp_TOP;
    return xwimp_open_window((wimp_open *) &state);
}

bool imgorg_browser_window_owns_window(
    const imgorg_browser_window *browser,
    wimp_w window
)
{
    const imgorg_viewer_window *viewer;

    if (browser == NULL || window == browser->handle ||
        (browser != NULL && browser->album_dialog_created &&
         window == browser->album_dialog_handle)) {
        return browser != NULL;
    }
    for (viewer = browser->viewers; viewer != NULL; viewer = viewer->next) {
        if (viewer->created && viewer->handle == window) {
            return true;
        }
    }
    return false;
}

static os_error *imgorg_browser_window_show_loading(
    imgorg_browser_window *browser,
    wimp_w parent_handle
)
{
    wimp_window_state parent;
    wimp_open open;
    wimp_draw redraw;
    os_error *error;
    int more;

    if (!browser->loading_created) {
        return NULL;
    }
    parent.w = parent_handle;
    error = xwimp_get_window_state(&parent);
    if (error != NULL) {
        return error;
    }
    open.w = browser->loading_handle;
    open.visible.x0 = parent.visible.x0 +
        ((parent.visible.x1 - parent.visible.x0 - LOADING_WINDOW_WIDTH) / 2);
    open.visible.y0 = parent.visible.y0 +
        ((parent.visible.y1 - parent.visible.y0 - LOADING_WINDOW_HEIGHT) / 2);
    open.visible.x1 = open.visible.x0 + LOADING_WINDOW_WIDTH;
    open.visible.y1 = open.visible.y0 + LOADING_WINDOW_HEIGHT;
    open.xscroll = 0;
    open.yscroll = 0;
    open.next = wimp_TOP;
    error = xwimp_open_window(&open);
    if (error != NULL) {
        return error;
    }

    memset(&redraw, 0, sizeof(redraw));
    redraw.w = browser->loading_handle;
    error = xwimp_redraw_window(&redraw, &more);
    while (error == NULL && more) {
        error = xwimp_get_rectangle(&redraw, &more);
    }
    return error;
}

static void imgorg_browser_window_hide_loading(
    const imgorg_browser_window *browser
)
{
    if (browser->loading_created) {
        (void) xwimp_close_window(browser->loading_handle);
    }
}

os_error *imgorg_browser_window_handle_open_request(
    imgorg_browser_window *browser,
    const wimp_open *open,
    bool *handled
)
{
    wimp_window_state state;
    os_error *error;
    imgorg_viewer_window *viewer;

    if (handled != NULL) {
        *handled = false;
    }

    if (browser == NULL || open == NULL || handled == NULL) {
        return NULL;
    }

    if (open->w == browser->handle) {
        int width = open->visible.x1 - open->visible.x0;
        int height = open->visible.y1 - open->visible.y0;
        bool width_changed = width != browser->layout_width;

        if (browser->images.count > 0) {
            imgorg_browser_window_set_thumbnail_priority(
                browser,
                open->yscroll,
                open->visible.y1 - open->visible.y0
            );
        }
        *handled = true;
        error = imgorg_browser_window_update_directory_extent_for_width(
            browser,
            width,
            height
        );
        if (error != NULL) {
            return error;
        }
        error = xwimp_open_window((wimp_open *) open);
        browser->layout_width = width;
        browser->layout_height = height;
        if (error == NULL && width_changed) {
            error = imgorg_browser_window_redraw_thumbnail_canvas(browser);
        }
        return error == NULL ?
            imgorg_browser_window_update_fixed_chrome(browser) : error;
    }
    viewer = imgorg_browser_window_find_viewer(browser, open->w);
    if (viewer == NULL || viewer->sprite_area == NULL) {
        return NULL;
    }

    state.w = viewer->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }

    if (memcmp(&open->visible, &state.visible, sizeof(open->visible)) == 0 &&
        open->xscroll == state.xscroll &&
        open->yscroll != state.yscroll) {
        *handled = true;
        return imgorg_browser_window_apply_zoom(
            viewer,
            &state.visible,
            open->yscroll > state.yscroll
        );
    }

    return NULL;
}

os_error *imgorg_browser_window_handle_close_request(
    imgorg_browser_window *browser,
    wimp_w window,
    bool *handled
)
{
    imgorg_viewer_window *viewer;
    size_t image_size;

    if (handled != NULL) {
        *handled = false;
    }
    if (browser == NULL || handled == NULL) {
        return NULL;
    }
    if (browser->album_dialog_created &&
        window == browser->album_dialog_handle) {
        *handled = true;
        browser->album_dialog_mode = IMGORG_ALBUM_DIALOG_NONE;
        return xwimp_close_window(window);
    }
    viewer = imgorg_browser_window_find_viewer(browser, window);
    if (viewer == NULL || viewer->sprite_area == NULL) {
        return NULL;
    }
    image_size = (size_t) viewer->sprite_area->size;
    free(viewer->sprite_area);
    browser->viewer_image_bytes -= image_size;
    viewer->sprite_area = NULL;
    viewer->sprite = NULL;
    viewer->image_width = 0;
    viewer->image_height = 0;
    viewer->image_name[0] = '\0';
    viewer->image_path[0] = '\0';
    viewer->dragging = false;
    return NULL;
}

static os_error *imgorg_browser_window_load_png_mode(
    imgorg_browser_window *browser,
    imgorg_viewer_window *viewer,
    const char *file_name
)
{
    static const byte png_signature[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };
    FILE *file;
    byte *data;
    osspriteop_area *new_area;
    long file_size;
    size_t bytes_read;
    int width;
    int height;

    if (browser == NULL || file_name == NULL) {
        return imgorg_browser_window_error("No PNG file was supplied");
    }

    file = fopen(file_name, "rb");
    if (file == NULL) {
        return imgorg_browser_window_error("The PNG file could not be opened");
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return imgorg_browser_window_error("The PNG file size could not be read");
    }

    file_size = ftell(file);
    if (file_size < (long) sizeof(png_signature) ||
        file_size > MAXIMUM_PNG_SIZE || file_size > INT_MAX) {
        fclose(file);
        return imgorg_browser_window_error("The PNG file has an invalid size");
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return imgorg_browser_window_error("The PNG file could not be rewound");
    }

    data = malloc((size_t) file_size);
    if (data == NULL) {
        fclose(file);
        return imgorg_browser_window_error("There is not enough memory for the PNG");
    }

    bytes_read = fread(data, 1, (size_t) file_size, file);
    fclose(file);

    if (bytes_read != (size_t) file_size) {
        free(data);
        return imgorg_browser_window_error("The PNG file could not be read completely");
    }

    if (memcmp(data, png_signature, sizeof(png_signature)) != 0) {
        free(data);
        return imgorg_browser_window_error("The selected file is not a PNG image");
    }

    new_area = imgorg_browser_window_decode_png(
        data,
        (size_t) file_size,
        MAXIMUM_IMAGE_DIMENSION,
        MAXIMUM_IMAGE_DIMENSION,
        &width,
        &height
    );
    free(data);
    if (new_area == NULL) {
        return imgorg_browser_window_error("The PNG image could not be decoded");
    }

    return imgorg_browser_window_show_image(
        browser,
        viewer,
        file_name,
        new_area,
        width,
        height
    );
}

os_error *imgorg_browser_window_load_png(
    imgorg_browser_window *browser,
    const char *file_name
)
{
    return imgorg_browser_window_load_image_into(
        browser,
        file_name,
        IMGORG_IMAGE_FORMAT_PNG,
        browser == NULL ? wimp_ICON_BAR : browser->handle
    );
}

static os_error *imgorg_browser_window_load_jpeg_mode(
    imgorg_browser_window *browser,
    imgorg_viewer_window *viewer,
    const char *file_name
)
{
    osspriteop_area *new_area;
    int width;
    int height;

    if (browser == NULL || file_name == NULL) {
        return imgorg_browser_window_error("No JPEG file was supplied");
    }
    new_area = imgorg_browser_window_decode_jpeg_file(
        file_name,
        MAXIMUM_IMAGE_DIMENSION,
        MAXIMUM_IMAGE_DIMENSION,
        false,
        &width,
        &height
    );
    if (new_area == NULL) {
        return imgorg_browser_window_error("The JPEG image could not be decoded");
    }
    return imgorg_browser_window_show_image(
        browser,
        viewer,
        file_name,
        new_area,
        width,
        height
    );
}

os_error *imgorg_browser_window_load_jpeg(
    imgorg_browser_window *browser,
    const char *file_name
)
{
    return imgorg_browser_window_load_image_into(
        browser,
        file_name,
        IMGORG_IMAGE_FORMAT_JPEG,
        browser == NULL ? wimp_ICON_BAR : browser->handle
    );
}

static os_error *imgorg_browser_window_load_image_mode(
    imgorg_browser_window *browser,
    const char *file_name,
    imgorg_image_format format,
    wimp_w target
)
{
    os_error *error;
    imgorg_viewer_window *viewer;
    bool replace_existing;
    imgorg_image_memory_estimate estimate;

    if (browser == NULL || file_name == NULL) {
        return imgorg_browser_window_error("No browser was supplied");
    }
    if (format != IMGORG_IMAGE_FORMAT_PNG &&
        format != IMGORG_IMAGE_FORMAT_JPEG) {
        return imgorg_browser_window_error(
            "That image format is not yet supported"
        );
    }
    viewer = imgorg_browser_window_find_viewer(browser, target);
    replace_existing = viewer != NULL;
    if (!replace_existing) {
        viewer = imgorg_browser_window_find_image(browser, file_name);
        if (viewer != NULL) {
            return imgorg_browser_window_open_viewer(viewer);
        }
    }
    if (!imgorg_browser_window_estimate_image_memory(
            file_name,
            format,
            &estimate
        )) {
        return imgorg_browser_window_error(
            "The image dimensions could not be read"
        );
    }
    if (!imgorg_browser_window_image_fits_slot(browser, &estimate)) {
        return imgorg_browser_window_error(
            "Opening this image would exceed the 128 MB Wimp slot; "
            "close one or more image viewers and try again"
        );
    }
    if (!replace_existing) {
        viewer = imgorg_browser_window_find_available_viewer(browser);
        if (viewer == NULL) {
            viewer = imgorg_browser_window_create_viewer(browser, &error);
            if (viewer == NULL) {
                return error;
            }
        } else if (!viewer->toolbar_visible) {
            error = imgorg_browser_window_set_toolbar_visible(viewer, true);
            if (error != NULL) {
                return error;
            }
        }
    }
    (void) imgorg_browser_window_show_loading(
        browser,
        replace_existing ? viewer->handle : browser->handle
    );
    if (format == IMGORG_IMAGE_FORMAT_PNG) {
        error = imgorg_browser_window_load_png_mode(
            browser,
            viewer,
            file_name
        );
    } else if (format == IMGORG_IMAGE_FORMAT_JPEG) {
        error = imgorg_browser_window_load_jpeg_mode(
            browser,
            viewer,
            file_name
        );
    } else {
        error = imgorg_browser_window_error(
            "That image format is not yet supported"
        );
    }
    imgorg_browser_window_hide_loading(browser);
    return error;
}

os_error *imgorg_browser_window_load_image(
    imgorg_browser_window *browser,
    const char *file_name,
    imgorg_image_format format
)
{
    return imgorg_browser_window_load_image_mode(
        browser,
        file_name,
        format,
        browser == NULL ? wimp_ICON_BAR : browser->handle
    );
}

os_error *imgorg_browser_window_load_image_into(
    imgorg_browser_window *browser,
    const char *file_name,
    imgorg_image_format format,
    wimp_w target
)
{
    return imgorg_browser_window_load_image_mode(
        browser,
        file_name,
        format,
        target
    );
}

os_error *imgorg_browser_window_load_directory(
    imgorg_browser_window *browser,
    const char *directory_path
)
{
    bool added;
    os_error *error;

    if (browser == NULL || directory_path == NULL || directory_path[0] == '\0') {
        return imgorg_browser_window_error("No directory was supplied");
    }
    if (!imgorg_folder_list_add(&browser->folders, directory_path, &added)) {
        return imgorg_browser_window_error(
            "There is not enough memory to add that folder"
        );
    }
    if (added) {
        browser->library_dirty = true;
    }
    error = imgorg_browser_window_save_library(browser);
    if (error != NULL) {
        return error;
    }
    error = imgorg_browser_window_start_next_folder_scan(browser);
    if (error != NULL) {
        return error;
    }
    return imgorg_browser_window_redraw_browser(browser);
}

os_error *imgorg_browser_window_add_image(
    imgorg_browser_window *browser,
    const char *file_name,
    uint64_t size_bytes,
    uint32_t load_addr,
    uint32_t exec_addr,
    uint32_t file_type
)
{
    imgorg_image_entry entry;
    char leafname[IMGORG_LEAFNAME_CAPACITY];
    bool added;
    os_error *error;

    if (browser == NULL || file_name == NULL || file_name[0] == '\0') {
        return imgorg_browser_window_error("No image was supplied");
    }
    imgorg_browser_window_copy_leafname(
        leafname,
        sizeof(leafname),
        file_name
    );
    if (!imgorg_image_entry_init(
            &entry,
            file_name,
            leafname,
            size_bytes,
            load_addr,
            exec_addr,
            file_type
        ) ||
        entry.format == IMGORG_IMAGE_FORMAT_UNKNOWN) {
        return imgorg_browser_window_error(
            "Aural can only add Sprite, JPEG and PNG images"
        );
    }
    if (!imgorg_image_list_append_unique(&browser->images, &entry, &added)) {
        return imgorg_browser_window_error(
            "There is not enough memory to add that image"
        );
    }
    if (added) {
        browser->library_dirty = true;
        if (!imgorg_browser_window_ensure_thumbnail_slots(browser)) {
            return imgorg_browser_window_error(
                "There is not enough memory for the thumbnail list"
            );
        }
    }
    error = imgorg_browser_window_save_library(browser);
    if (error != NULL) {
        return error;
    }
    error = imgorg_browser_window_update_title(browser);
    if (error == NULL) {
        error = imgorg_browser_window_update_directory_extent(browser);
    }
    if (error == NULL) {
        error = imgorg_browser_window_redraw_browser(browser);
    }
    return error;
}

bool imgorg_browser_window_has_background_work(
    const imgorg_browser_window *browser
)
{
    const imgorg_viewer_window *viewer;

    if (browser == NULL) {
        return false;
    }
    if (browser->thumbnail_slider_dragging ||
        browser->scanner.active ||
        browser->thumbnail_cursor < browser->images.count) {
        return true;
    }
    for (viewer = browser->viewers; viewer != NULL; viewer = viewer->next) {
        if (viewer->dragging) {
            return true;
        }
    }
    return false;
}

os_error *imgorg_browser_window_scan_step(imgorg_browser_window *browser)
{
    os_error *error;
    bool changed = false;
    bool was_active;
    bool thumbnail_completed = false;

    if (browser == NULL) {
        return NULL;
    }

    was_active = browser->scanner.active;
    if (browser->scanner.active) {
        error = imgorg_directory_scanner_step(
            &browser->scanner,
            &browser->images,
            &changed
        );
        if (error != NULL) {
            return error;
        }
    }

    if (changed) {
        browser->library_dirty = true;
        if (!imgorg_browser_window_ensure_thumbnail_slots(browser)) {
            return imgorg_browser_window_error(
                "There is not enough memory for the thumbnail list"
            );
        }
        error = imgorg_browser_window_update_directory_extent(browser);
        if (error != NULL) {
            return error;
        }
        error = imgorg_browser_window_redraw_browser(browser);
        if (error != NULL) {
            return error;
        }
    }

    if (browser->thumbnail_count < browser->images.count &&
        !imgorg_browser_window_ensure_thumbnail_slots(browser)) {
        return imgorg_browser_window_error(
            "There is not enough memory for the thumbnail list"
        );
    }
    {
        size_t index = imgorg_browser_window_next_thumbnail_index(browser);
        if (index != SIZE_MAX) {
        const imgorg_image_entry *entry =
            imgorg_image_list_get(&browser->images, index);
        imgorg_thumbnail *thumbnail;
        bool thumbnail_from_cache;

        thumbnail = &browser->thumbnails[index];
        thumbnail->attempted = true;
        thumbnail->sprite_area = imgorg_thumbnail_cache_load(
            entry,
            &thumbnail->width,
            &thumbnail->height
        );
        thumbnail_from_cache = thumbnail->sprite_area != NULL;
        if (thumbnail->sprite_area == NULL &&
            entry->format == IMGORG_IMAGE_FORMAT_PNG) {
            thumbnail->sprite_area =
                imgorg_browser_window_decode_png_file(
                    entry->path,
                    THUMBNAIL_MAXIMUM_WIDTH,
                    THUMBNAIL_MAXIMUM_HEIGHT,
                    &thumbnail->width,
                    &thumbnail->height
                );
        } else if (thumbnail->sprite_area == NULL &&
            entry->format == IMGORG_IMAGE_FORMAT_JPEG) {
            thumbnail->sprite_area =
                imgorg_browser_window_decode_jpeg_file(
                    entry->path,
                    THUMBNAIL_MAXIMUM_WIDTH,
                    THUMBNAIL_MAXIMUM_HEIGHT,
                    true,
                    &thumbnail->width,
                    &thumbnail->height
                );
        } else if (thumbnail->sprite_area == NULL &&
            entry->format == IMGORG_IMAGE_FORMAT_SPRITE) {
            thumbnail->sprite_area =
                imgorg_browser_window_decode_sprite_file(
                    entry,
                    THUMBNAIL_MAXIMUM_WIDTH,
                    THUMBNAIL_MAXIMUM_HEIGHT,
                    &thumbnail->width,
                    &thumbnail->height
                );
        }
        if (thumbnail->sprite_area != NULL) {
            thumbnail->sprite = (osspriteop_header *)
                ((byte *) thumbnail->sprite_area +
                thumbnail->sprite_area->first);
            thumbnail_completed = true;
            if (!thumbnail_from_cache) {
                (void) imgorg_thumbnail_cache_save(
                    entry,
                    thumbnail->sprite_area,
                    thumbnail->width,
                    thumbnail->height
                );
            }
        }
        }
    }

    if (thumbnail_completed) {
        error = imgorg_browser_window_redraw_browser(browser);
        if (error != NULL) {
            return error;
        }
    }

    if (was_active && !browser->scanner.active) {
        if (browser->library_dirty) {
            error = imgorg_browser_window_save_library(browser);
            if (error != NULL) {
                return error;
            }
        }
        error = imgorg_browser_window_start_next_folder_scan(browser);
        if (error != NULL) {
            return error;
        }
    }
    if (changed || was_active != browser->scanner.active) {
        return imgorg_browser_window_update_title(browser);
    }
    return NULL;
}

static os_error *imgorg_browser_window_set_viewer_zoom_mode(
    imgorg_viewer_window *viewer,
    bool fit_to_window
)
{
    os_error *error;

    viewer->fit_to_window = fit_to_window;
    viewer->zoom_percent = 100;
    viewer->pan_x = 0;
    viewer->pan_y = 0;
    error = imgorg_browser_window_update_viewer_title(viewer);
    if (error != NULL) {
        return error;
    }
    return imgorg_browser_window_redraw_viewer(viewer);
}

static os_error *imgorg_browser_window_toggle_fullscreen(
    imgorg_viewer_window *viewer
)
{
    wimp_window_state state;
    wimp_open open;
    os_error *error;
    bool entering_fullscreen;
    int desktop_width;
    int desktop_height;

    state.w = viewer->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }
    open.w = viewer->handle;
    open.xscroll = state.xscroll;
    open.yscroll = state.yscroll;
    open.next = wimp_TOP;
    entering_fullscreen = !viewer->fullscreen;
    if (entering_fullscreen) {
        viewer->restore_visible = state.visible;
        viewer->restore_xscroll = state.xscroll;
        viewer->restore_yscroll = state.yscroll;
        imgorg_browser_window_read_desktop_size(
            &desktop_width,
            &desktop_height
        );
        open.visible.x0 = 0;
        open.visible.y0 = 0;
        open.visible.x1 = desktop_width;
        open.visible.y1 = desktop_height;
    } else {
        open.visible = viewer->restore_visible;
        open.xscroll = viewer->restore_xscroll;
        open.yscroll = viewer->restore_yscroll;
    }
    error = xwimp_open_window(&open);
    if (error != NULL) {
        return error;
    }
    viewer->fullscreen = entering_fullscreen;
    snprintf(
        viewer->fullscreen_label,
        sizeof(viewer->fullscreen_label),
        "%s",
        viewer->fullscreen ?
            IMGORG_VIEWER_LEAVE_FULLSCREEN_LABEL :
            IMGORG_VIEWER_FULLSCREEN_LABEL
    );
    error = xwimp_set_icon_state(
        viewer->handle,
        VIEWER_TOOLBAR_FULLSCREEN,
        0,
        0
    );
    return error == NULL ?
        imgorg_browser_window_redraw_viewer(viewer) : error;
}

static os_error *imgorg_browser_window_navigate_viewer(
    imgorg_browser_window *browser,
    imgorg_viewer_window *viewer,
    int direction
)
{
    size_t current = SIZE_MAX;
    size_t step;

    if (browser->images.count < 2) {
        return NULL;
    }
    for (step = 0; step < browser->images.count; ++step) {
        const imgorg_image_entry *entry =
            imgorg_image_list_get(&browser->images, step);

        if (strcmp(entry->path, viewer->image_path) == 0) {
            current = step;
            break;
        }
    }
    if (current == SIZE_MAX) {
        return NULL;
    }
    for (step = 1; step < browser->images.count; ++step) {
        size_t index;
        const imgorg_image_entry *entry;

        if (direction < 0) {
            index = (current + browser->images.count - step) %
                browser->images.count;
        } else {
            index = (current + step) % browser->images.count;
        }
        entry = imgorg_image_list_get(&browser->images, index);
        if (entry->format == IMGORG_IMAGE_FORMAT_PNG ||
            entry->format == IMGORG_IMAGE_FORMAT_JPEG) {
            return imgorg_browser_window_load_image_mode(
                browser,
                entry->path,
                entry->format,
                viewer->handle
            );
        }
    }
    return NULL;
}

static os_error *imgorg_browser_window_set_toolbar_visible(
    imgorg_viewer_window *viewer,
    bool visible
)
{
    int icon;
    os_error *error = NULL;

    if (viewer->toolbar_visible == visible) {
        return NULL;
    }
    for (icon = 0;
         icon < VIEWER_TOOLBAR_ICON_COUNT && error == NULL;
         ++icon) {
        error = xwimp_set_icon_state(
            viewer->handle,
            icon,
            visible ? 0 : wimp_ICON_DELETED,
            wimp_ICON_DELETED
        );
    }
    if (error != NULL) {
        return error;
    }
    viewer->toolbar_visible = visible;
    return imgorg_browser_window_redraw_viewer(viewer);
}

os_error *imgorg_browser_window_handle_key(
    imgorg_browser_window *browser,
    const wimp_key *key,
    bool *handled
)
{
    imgorg_viewer_window *viewer;

    if (handled != NULL) {
        *handled = false;
    }
    if (browser == NULL || key == NULL || handled == NULL) {
        return NULL;
    }
    if (browser->album_dialog_created &&
        key->w == browser->album_dialog_handle) {
        if (key->c == wimp_KEY_RETURN) {
            *handled = true;
            return imgorg_browser_window_accept_album_dialog(browser);
        }
        if (key->c == wimp_KEY_ESCAPE) {
            *handled = true;
            browser->album_dialog_mode = IMGORG_ALBUM_DIALOG_NONE;
            return xwimp_close_window(browser->album_dialog_handle);
        }
        return NULL;
    }
    viewer = imgorg_browser_window_find_viewer(browser, key->w);
    if (viewer == NULL || viewer->sprite_area == NULL) {
        return NULL;
    }
    switch (key->c) {
    case wimp_KEY_LEFT:
        *handled = true;
        return imgorg_browser_window_navigate_viewer(browser, viewer, -1);

    case wimp_KEY_RIGHT:
        *handled = true;
        return imgorg_browser_window_navigate_viewer(browser, viewer, 1);

    case 'T':
    case 't':
        *handled = true;
        return imgorg_browser_window_set_toolbar_visible(
            viewer,
            !viewer->toolbar_visible
        );

    default:
        return NULL;
    }
}

static bool imgorg_browser_window_thumbnail_at_pointer(
    const imgorg_browser_window *browser,
    const wimp_pointer *pointer,
    size_t *index_out
)
{
    wimp_window_state state;
    int work_x;
    int work_y;
    int cell_x;
    int cell_y;
    int cell_width;
    int cell_height;
    size_t column;
    size_t row;
    size_t index;
    size_t columns;

    state.w = browser->handle;
    if (xwimp_get_window_state(&state) != NULL) {
        return false;
    }
    if (pointer->pos.x < state.visible.x0 + WORKSPACE_LEFT_PANEL_WIDTH ||
        pointer->pos.x >= state.visible.x1 - WORKSPACE_RIGHT_PANEL_WIDTH) {
        return false;
    }
    work_x = pointer->pos.x - state.visible.x0 + state.xscroll;
    work_y = pointer->pos.y - state.visible.y1 + state.yscroll;
    columns = imgorg_browser_window_thumbnail_columns_for_width(
        browser,
        state.visible.x1 - state.visible.x0
    );
    cell_width = imgorg_browser_window_thumbnail_cell_width(browser);
    cell_height = imgorg_browser_window_thumbnail_cell_height(browser);
    cell_x = work_x - imgorg_browser_window_thumbnail_grid_x0(
        browser,
        state.visible.x1 - state.visible.x0,
        columns
    );
    cell_y = -work_y - WORKSPACE_HEADER_HEIGHT - THUMBNAIL_MARGIN;
    if (cell_x < 0 || cell_y < 0) {
        return false;
    }

    column = (size_t) cell_x /
        (cell_width + THUMBNAIL_GAP);
    row = (size_t) cell_y /
        (cell_height + THUMBNAIL_GAP);
    if (column >= columns ||
        cell_x % (cell_width + THUMBNAIL_GAP) >= cell_width ||
        cell_y % (cell_height + THUMBNAIL_GAP) >= cell_height) {
        return false;
    }
    index = imgorg_browser_window_actual_image_index(
        browser,
        row * columns + column
    );
    if (index == SIZE_MAX) {
        return false;
    }
    *index_out = index;
    return true;
}

static bool imgorg_browser_window_select_thumbnail(
    imgorg_browser_window *browser,
    size_t selected_index
)
{
    size_t index;
    bool changed = false;

    for (index = 0; index < browser->images.count; ++index) {
        imgorg_image_entry *entry = &browser->images.items[index];
        bool selected = index == selected_index;

        if (entry->selected != selected) {
            entry->selected = selected;
            changed = true;
        }
    }
    return changed;
}

static bool imgorg_browser_window_toggle_thumbnail(
    imgorg_browser_window *browser,
    size_t index
)
{
    if (index >= browser->images.count) {
        return false;
    }
    browser->images.items[index].selected =
        !browser->images.items[index].selected;
    return true;
}

static int imgorg_browser_window_first_tag_baseline(
    imgorg_browser_window *browser,
    int visible_y1
)
{
    size_t folder_rows;
    int y = visible_y1 - 180;

    imgorg_browser_window_collect_tags(browser, false);
    if (browser->folders.count == 0) {
        folder_rows = 1;
    } else {
        folder_rows = browser->folders.count < 6 ?
            browser->folders.count : 6;
        if (browser->folders.count > 6) {
            ++folder_rows;
        }
    }
    y -= (int) folder_rows * 36;
    if (browser->images.count > 0) {
        y -= 36;
    }
    y -= 28 + 44 + 5 * 36 + 52 + 44;
    return y;
}

static int imgorg_browser_window_first_album_baseline(
    imgorg_browser_window *browser,
    int visible_y1
)
{
    size_t tag_rows;
    int y = imgorg_browser_window_first_tag_baseline(browser, visible_y1);

    tag_rows = browser->tag_count == 0 ? 1 :
        (browser->tag_count < 5 ? browser->tag_count : 5);
    if (browser->tag_count > 5) {
        ++tag_rows;
    }
    y -= (int) tag_rows * 36;
    y -= 52 + 44;
    return y;
}

static os_error *imgorg_browser_window_set_tag_filter(
    imgorg_browser_window *browser,
    const char *tag
)
{
    if (!imgorg_tag_name_normalise(
            browser->filter_tag,
            sizeof(browser->filter_tag),
            tag)) {
        return NULL;
    }
    return imgorg_browser_window_set_filter(
        browser,
        IMGORG_LIBRARY_FILTER_TAG,
        0
    );
}

static os_error *imgorg_browser_window_set_filter(
    imgorg_browser_window *browser,
    imgorg_library_filter_kind kind,
    size_t value
)
{
    wimp_window_state state;
    os_error *error;

    if (kind == IMGORG_LIBRARY_FILTER_FOLDER &&
        value >= browser->folders.count) {
        return NULL;
    }
    if (kind == IMGORG_LIBRARY_FILTER_RATING &&
        (value < 1 || value > 5)) {
        return NULL;
    }
    if (kind == IMGORG_LIBRARY_FILTER_ALBUM &&
        value >= browser->albums.count) {
        return NULL;
    }
    browser->filter_kind = kind;
    browser->filter_folder_index = value;
    browser->filter_rating = (unsigned int) value;
    browser->filter_album_index = value;
    (void) imgorg_browser_window_select_thumbnail(browser, SIZE_MAX);
    browser->thumbnail_priority_start = 0;
    browser->thumbnail_priority_end =
        imgorg_browser_window_thumbnail_columns(browser) * 3;
    error = imgorg_browser_window_update_directory_extent(browser);
    if (error != NULL) {
        return error;
    }
    state.w = browser->handle;
    error = xwimp_get_window_state(&state);
    if (error == NULL) {
        state.xscroll = 0;
        state.yscroll = 0;
        state.next = wimp_TOP;
        error = xwimp_open_window((wimp_open *) &state);
    }
    if (error == NULL) {
        error = imgorg_browser_window_redraw_browser(browser);
    }
    return error;
}

os_error *imgorg_browser_window_handle_pointer(
    imgorg_browser_window *browser,
    const wimp_pointer *pointer
)
{
    wimp_window_state state;
    wimp_drag drag;
    os_error *error;
    imgorg_viewer_window *viewer;

    if (browser == NULL || pointer == NULL) {
        return NULL;
    }
    if (browser->album_dialog_created &&
        pointer->w == browser->album_dialog_handle &&
        (pointer->buttons & wimp_CLICK_SELECT) != 0) {
        if (pointer->i == ALBUM_DIALOG_CANCEL) {
            browser->album_dialog_mode = IMGORG_ALBUM_DIALOG_NONE;
            return xwimp_close_window(browser->album_dialog_handle);
        }
        if (pointer->i == ALBUM_DIALOG_OK) {
            return imgorg_browser_window_accept_album_dialog(browser);
        }
        return NULL;
    }
    if (browser->album_dialog_mode != IMGORG_ALBUM_DIALOG_NONE) {
        return NULL;
    }

    if (pointer->w == browser->handle) {
        size_t index;
        const imgorg_image_entry *entry;
        bool selection_changed;
        os_box slider_track;

        if (pointer->buttons != wimp_SINGLE_SELECT &&
            pointer->buttons != wimp_SINGLE_ADJUST &&
            pointer->buttons != wimp_DOUBLE_SELECT &&
            pointer->buttons != wimp_CLICK_MENU &&
            pointer->buttons != wimp_DRAG_SELECT) {
            return NULL;
        }
        state.w = browser->handle;
        error = xwimp_get_window_state(&state);
        if (error != NULL) {
            return error;
        }
        imgorg_browser_window_thumbnail_slider_track(
            &state.visible,
            &slider_track
        );
        if ((pointer->buttons == wimp_SINGLE_SELECT ||
             pointer->buttons == wimp_DRAG_SELECT) &&
            pointer->pos.x >=
                slider_track.x0 - THUMBNAIL_SLIDER_KNOB_WIDTH / 2 &&
            pointer->pos.x <=
                slider_track.x1 + THUMBNAIL_SLIDER_KNOB_WIDTH / 2 &&
            pointer->pos.y >= state.visible.y1 - 60 &&
            pointer->pos.y < state.visible.y1 - 12) {
            error = imgorg_browser_window_set_thumbnail_width_from_pointer(
                browser,
                pointer->pos.x
            );
            if (error != NULL || pointer->buttons != wimp_DRAG_SELECT) {
                return error;
            }
            memset(&drag, 0, sizeof(drag));
            drag.w = browser->handle;
            drag.type = wimp_DRAG_USER_POINT;
            drag.initial.x0 = pointer->pos.x;
            drag.initial.y0 = pointer->pos.y;
            drag.initial.x1 = pointer->pos.x;
            drag.initial.y1 = pointer->pos.y;
            drag.bbox.x0 = slider_track.x0;
            drag.bbox.y0 = -32768;
            drag.bbox.x1 = slider_track.x1;
            drag.bbox.y1 = 32767;
            error = xwimp_drag_box(&drag);
            if (error == NULL) {
                browser->thumbnail_slider_dragging = true;
            }
            return error;
        }
        if (browser->images.count == 0) {
            return NULL;
        }
        if (pointer->pos.x <
                state.visible.x0 + WORKSPACE_LEFT_PANEL_WIDTH) {
            int y;
            int album_y;
            int tag_y;
            size_t folder_count =
                browser->folders.count < 6 ? browser->folders.count : 6;
            size_t folder_index;
            size_t album_index;
            size_t tag_index;

            album_y = imgorg_browser_window_first_album_baseline(
                browser,
                state.visible.y1
            );
            for (album_index = 0;
                 album_index < browser->albums.count && album_index < 6;
                 ++album_index) {
                if (pointer->pos.y >= album_y - 12 &&
                    pointer->pos.y < album_y + 24) {
                    if (pointer->buttons == wimp_CLICK_MENU) {
                        browser->context_menu_open = true;
                        browser->context_album_menu = true;
                        browser->context_album_index = album_index;
                        return xwimp_create_menu(
                            imgorg_browser_window_album_menu(),
                            pointer->pos.x,
                            pointer->pos.y
                        );
                    }
                    if (pointer->buttons == wimp_SINGLE_SELECT) {
                        return imgorg_browser_window_set_filter(
                            browser,
                            IMGORG_LIBRARY_FILTER_ALBUM,
                            album_index
                        );
                    }
                    return NULL;
                }
                album_y -= 36;
            }
            if (pointer->buttons != wimp_SINGLE_SELECT) {
                return NULL;
            }
            tag_y = imgorg_browser_window_first_tag_baseline(
                browser,
                state.visible.y1
            );
            for (tag_index = 0;
                 tag_index < browser->tag_count && tag_index < 5;
                 ++tag_index) {
                if (pointer->pos.y >= tag_y - 12 &&
                    pointer->pos.y < tag_y + 24) {
                    return imgorg_browser_window_set_tag_filter(
                        browser,
                        browser->tag_names[tag_index]
                    );
                }
                tag_y -= 36;
            }
            if (pointer->pos.y >= state.visible.y1 - 108 &&
                pointer->pos.y < state.visible.y1 - 68) {
                return imgorg_browser_window_set_filter(
                    browser,
                    IMGORG_LIBRARY_FILTER_ALL,
                    0
                );
            }
            y = state.visible.y1 - 180;
            for (folder_index = 0;
                 folder_index < folder_count;
                 ++folder_index) {
                if (pointer->pos.y >= y - 12 &&
                    pointer->pos.y < y + 24) {
                    return imgorg_browser_window_set_filter(
                        browser,
                        IMGORG_LIBRARY_FILTER_FOLDER,
                        folder_index
                    );
                }
                y -= 36;
            }
            if (browser->folders.count == 0) {
                y -= 36;
            } else if (browser->folders.count > 6) {
                y -= 36;
            }
            if (browser->images.count > 0) {
                y -= 36;
            }
            y -= 28;
            y -= 44;
            for (index = 1; index <= 5; ++index) {
                if (pointer->pos.y >= y - 12 &&
                    pointer->pos.y < y + 24) {
                    return imgorg_browser_window_set_filter(
                        browser,
                        IMGORG_LIBRARY_FILTER_RATING,
                        index
                    );
                }
                y -= 36;
            }
            if (pointer->pos.y >= y - 12 &&
                pointer->pos.y < y + 24) {
                return imgorg_browser_window_set_filter(
                    browser,
                    IMGORG_LIBRARY_FILTER_FAVOURITES,
                    0
                );
            }
            return NULL;
        }
        if (pointer->pos.x >=
                state.visible.x1 - WORKSPACE_RIGHT_PANEL_WIDTH) {
            imgorg_image_entry *selected = NULL;
            os_box right;
            os_box button;
            size_t selected_index;
            unsigned int rating;

            if (pointer->buttons != wimp_SINGLE_SELECT) {
                return NULL;
            }
            for (selected_index = 0;
                 selected_index < browser->images.count;
                 ++selected_index) {
                if (browser->images.items[selected_index].selected) {
                    selected = &browser->images.items[selected_index];
                    break;
                }
            }
            if (selected == NULL) {
                return NULL;
            }
            right.x0 =
                state.visible.x1 - WORKSPACE_RIGHT_PANEL_WIDTH;
            right.x1 = state.visible.x1;
            right.y0 = state.visible.y0;
            right.y1 = state.visible.y1;
            for (rating = 1; rating <= 5; ++rating) {
                imgorg_browser_window_inspector_rating_box(
                    &right,
                    state.visible.y1 - 312,
                    rating,
                    &button
                );
                if (imgorg_browser_window_point_in_box(
                        &pointer->pos,
                        &button
                    )) {
                    selected->rating =
                        selected->rating == rating ? 0 : rating;
                    if (!imgorg_browser_window_entry_matches_filter(
                            browser,
                            selected
                        )) {
                        selected->selected = false;
                    }
                    browser->library_dirty = true;
                    error = imgorg_browser_window_save_library(browser);
                    if (error == NULL) {
                        error =
                            imgorg_browser_window_update_directory_extent(
                                browser
                            );
                    }
                    return error == NULL ?
                        imgorg_browser_window_redraw_browser(browser) : error;
                }
            }
            imgorg_browser_window_inspector_favourite_box(
                &right,
                state.visible.y1 - 372,
                &button
            );
            if (imgorg_browser_window_point_in_box(
                    &pointer->pos,
                    &button
                )) {
                selected->favourite = !selected->favourite;
                if (!imgorg_browser_window_entry_matches_filter(
                        browser,
                        selected
                    )) {
                    selected->selected = false;
                }
                browser->library_dirty = true;
                error = imgorg_browser_window_save_library(browser);
                if (error == NULL) {
                    error = imgorg_browser_window_update_directory_extent(
                        browser
                    );
                }
                return error == NULL ?
                    imgorg_browser_window_redraw_browser(browser) : error;
            }
            imgorg_browser_window_inspector_favourite_box(
                &right,
                state.visible.y1 - 520,
                &button
            );
            if (imgorg_browser_window_point_in_box(
                    &pointer->pos,
                    &button
                )) {
                return imgorg_browser_window_show_album_dialog(
                    browser,
                    IMGORG_ALBUM_DIALOG_CREATE_TAG,
                    0
                );
            }
            return NULL;
        }
        if (pointer->pos.x <
                state.visible.x0 + WORKSPACE_LEFT_PANEL_WIDTH ||
            pointer->pos.x >=
                state.visible.x1 - WORKSPACE_RIGHT_PANEL_WIDTH) {
            return NULL;
        }
        if (!imgorg_browser_window_thumbnail_at_pointer(
                browser,
                pointer,
                &index
            )) {
            if (pointer->buttons == wimp_SINGLE_SELECT &&
                imgorg_browser_window_select_thumbnail(browser, SIZE_MAX)) {
                return imgorg_browser_window_redraw_browser(browser);
            }
            return NULL;
        }
        if (pointer->buttons == wimp_SINGLE_ADJUST) {
            selection_changed = imgorg_browser_window_toggle_thumbnail(
                browser,
                index
            );
        } else if ((pointer->buttons == wimp_CLICK_MENU ||
                    pointer->buttons == wimp_DRAG_SELECT) &&
                   browser->images.items[index].selected) {
            selection_changed = false;
        } else {
            selection_changed = imgorg_browser_window_select_thumbnail(
                browser,
                index
            );
        }
        if (pointer->buttons == wimp_CLICK_MENU) {
            wimp_menu *menu = imgorg_browser_window_thumbnail_menu(browser);

            browser->context_menu_open = true;
            browser->context_album_menu = false;
            browser->context_image_index = index;
            if (browser->images.items[index].format ==
                    IMGORG_IMAGE_FORMAT_PNG ||
                browser->images.items[index].format ==
                    IMGORG_IMAGE_FORMAT_JPEG) {
                menu->entries[0].icon_flags &= ~wimp_ICON_SHADED;
            } else {
                menu->entries[0].icon_flags |= wimp_ICON_SHADED;
            }
            if (selection_changed) {
                error = imgorg_browser_window_redraw_browser(browser);
                if (error != NULL) {
                    return error;
                }
            }
            return xwimp_create_menu(
                menu,
                pointer->pos.x,
                pointer->pos.y
            );
        }
        if (pointer->buttons == wimp_SINGLE_SELECT) {
            return selection_changed ?
                imgorg_browser_window_redraw_browser(browser) : NULL;
        }
        if (pointer->buttons == wimp_SINGLE_ADJUST) {
            return imgorg_browser_window_redraw_browser(browser);
        }
        if (pointer->buttons == wimp_DRAG_SELECT) {
            memset(&drag, 0, sizeof(drag));
            drag.w = browser->handle;
            drag.type = wimp_DRAG_USER_FIXED;
            drag.initial.x0 = pointer->pos.x - 48;
            drag.initial.y0 = pointer->pos.y - 32;
            drag.initial.x1 = pointer->pos.x + 48;
            drag.initial.y1 = pointer->pos.y + 32;
            drag.bbox.x0 = -32768;
            drag.bbox.y0 = -32768;
            drag.bbox.x1 = 32767;
            drag.bbox.y1 = 32767;
            error = xwimp_drag_box(&drag);
            if (error == NULL) {
                browser->thumbnail_image_dragging = true;
                browser->thumbnail_drag_image_index = index;
            }
            return error;
        }
        entry = imgorg_image_list_get(&browser->images, index);
        if (entry->format != IMGORG_IMAGE_FORMAT_PNG &&
            entry->format != IMGORG_IMAGE_FORMAT_JPEG) {
            return NULL;
        }
        return imgorg_browser_window_load_image_mode(
            browser,
            entry->path,
            entry->format,
            browser->handle
        );
    }
    viewer = imgorg_browser_window_find_viewer(browser, pointer->w);
    if (viewer == NULL || viewer->sprite_area == NULL) {
        return NULL;
    }
    error = xwimp_set_caret_position(
        viewer->handle,
        wimp_ICON_WINDOW,
        0,
        0,
        1 << 25,
        -1
    );
    if (error != NULL) {
        return error;
    }

    if (viewer->toolbar_visible &&
        pointer->i >= VIEWER_TOOLBAR_BACKGROUND &&
        pointer->i <= VIEWER_TOOLBAR_FULLSCREEN) {
        if ((pointer->buttons & wimp_CLICK_SELECT) == 0) {
            return NULL;
        }
        switch (pointer->i) {
        case VIEWER_TOOLBAR_BACK:
            return imgorg_browser_window_navigate_viewer(
                browser,
                viewer,
                -1
            );

        case VIEWER_TOOLBAR_FORWARD:
            return imgorg_browser_window_navigate_viewer(
                browser,
                viewer,
                1
            );

        case VIEWER_TOOLBAR_ACTUAL_SIZE:
            return imgorg_browser_window_set_viewer_zoom_mode(
                viewer,
                false
            );

        case VIEWER_TOOLBAR_FIT:
            return imgorg_browser_window_set_viewer_zoom_mode(
                viewer,
                true
            );

        case VIEWER_TOOLBAR_FULLSCREEN:
            return imgorg_browser_window_toggle_fullscreen(viewer);

        default:
            return NULL;
        }
    }

    if ((pointer->buttons & wimp_CLICK_ADJUST) != 0) {
        return imgorg_browser_window_set_viewer_zoom_mode(viewer, true);
    }

    if ((pointer->buttons &
         (wimp_CLICK_SELECT | wimp_DRAG_SELECT)) == 0) {
        return NULL;
    }

    if (viewer->fit_to_window) {
        state.w = viewer->handle;
        error = xwimp_get_window_state(&state);
        if (error != NULL) {
            return error;
        }
        viewer->zoom_percent = imgorg_browser_window_fit_zoom(
            viewer,
            &state.visible
        );
        viewer->fit_to_window = false;
        error = imgorg_browser_window_update_viewer_title(viewer);
        if (error != NULL) {
            return error;
        }
    }

    memset(&drag, 0, sizeof(drag));
    drag.w = viewer->handle;
    drag.type = wimp_DRAG_USER_POINT;
    drag.initial.x0 = pointer->pos.x;
    drag.initial.y0 = pointer->pos.y;
    drag.initial.x1 = pointer->pos.x;
    drag.initial.y1 = pointer->pos.y;
    drag.bbox.x0 = -32768;
    drag.bbox.y0 = -32768;
    drag.bbox.x1 = 32767;
    drag.bbox.y1 = 32767;

    error = xwimp_drag_box(&drag);
    if (error == NULL) {
        viewer->dragging = true;
        viewer->drag_start = pointer->pos;
        viewer->drag_pan_x = viewer->pan_x;
        viewer->drag_pan_y = viewer->pan_y;
    }
    return error;
}

static os_error *imgorg_browser_window_remove_library_image(
    imgorg_browser_window *browser,
    size_t index
)
{
    os_error *error;
    char removed_path[IMGORG_PATH_CAPACITY];

    if (index >= browser->images.count) {
        return NULL;
    }
    snprintf(removed_path, sizeof(removed_path), "%s",
        browser->images.items[index].path);
    if (index < browser->thumbnail_count) {
        free(browser->thumbnails[index].sprite_area);
        if (index + 1 < browser->thumbnail_count) {
            memmove(
                &browser->thumbnails[index],
                &browser->thumbnails[index + 1],
                (browser->thumbnail_count - index - 1) *
                    sizeof(*browser->thumbnails)
            );
        }
        --browser->thumbnail_count;
    }
    if (!imgorg_image_list_remove_at(&browser->images, index)) {
        return NULL;
    }
    imgorg_album_list_remove_image(&browser->albums, removed_path);
    imgorg_browser_window_prune_empty_folders(browser);
    if (browser->filter_kind == IMGORG_LIBRARY_FILTER_TAG) {
        size_t image_index;
        bool tag_still_used = false;

        for (image_index = 0;
             image_index < browser->images.count;
             ++image_index) {
            if (imgorg_image_entry_has_tag(
                    &browser->images.items[image_index],
                    browser->filter_tag)) {
                tag_still_used = true;
                break;
            }
        }
        if (!tag_still_used) {
            browser->filter_kind = IMGORG_LIBRARY_FILTER_ALL;
            browser->filter_tag[0] = '\0';
        }
    }
    if (browser->thumbnail_cursor > index) {
        --browser->thumbnail_cursor;
    }
    browser->thumbnail_priority_start = 0;
    browser->thumbnail_priority_end =
        imgorg_browser_window_thumbnail_columns(browser) * 3;
    browser->library_dirty = true;
    error = imgorg_browser_window_save_library(browser);
    if (error == NULL) {
        error = imgorg_browser_window_update_title(browser);
    }
    if (error == NULL) {
        error = imgorg_browser_window_update_directory_extent(browser);
    }
    if (error == NULL) {
        error = imgorg_browser_window_redraw_browser(browser);
    }
    return error;
}

static os_error *imgorg_browser_window_add_selection_to_album(
    imgorg_browser_window *browser,
    size_t album_index
)
{
    size_t index;
    bool changed = false;

    if (album_index >= browser->albums.count) {
        return NULL;
    }
    for (index = 0; index < browser->images.count; ++index) {
        bool added;

        if (!browser->images.items[index].selected) {
            continue;
        }
        if (!imgorg_album_add_image(
                &browser->albums.items[album_index],
                browser->images.items[index].path,
                &added
            )) {
            return imgorg_browser_window_error(
                "There is not enough memory to update the album"
            );
        }
        changed = changed || added;
    }
    if (!changed) {
        return NULL;
    }
    browser->library_dirty = true;
    return imgorg_browser_window_save_library(browser);
}

static os_error *imgorg_browser_window_apply_tag_to_selection(
    imgorg_browser_window *browser,
    const char *tag,
    bool remove
)
{
    size_t index;
    bool any_changed = false;

    for (index = 0; index < browser->images.count; ++index) {
        bool changed;
        bool success;

        if (!browser->images.items[index].selected) {
            continue;
        }
        success = remove ?
            imgorg_image_entry_remove_tag(
                &browser->images.items[index], tag, &changed) :
            imgorg_image_entry_add_tag(
                &browser->images.items[index], tag, &changed);
        if (!success) {
            return imgorg_browser_window_error(
                remove ?
                    "The tag could not be removed" :
                    "There is not enough room to add that tag"
            );
        }
        any_changed = any_changed || changed;
    }
    if (!any_changed) {
        return NULL;
    }
    browser->library_dirty = true;
    if (imgorg_browser_window_save_library(browser) != NULL) {
        return imgorg_browser_window_error(
            "The tag changes could not be saved"
        );
    }
    if (browser->filter_kind == IMGORG_LIBRARY_FILTER_TAG) {
        bool filter_exists = false;
        size_t index;

        imgorg_browser_window_collect_tags(browser, false);
        for (index = 0; index < browser->tag_count; ++index) {
            imgorg_image_entry probe;

            memset(&probe, 0, sizeof(probe));
            snprintf(probe.tags, sizeof(probe.tags), "%s",
                browser->tag_names[index]);
            if (imgorg_image_entry_has_tag(
                    &probe, browser->filter_tag)) {
                filter_exists = true;
                break;
            }
        }
        if (!filter_exists) {
            browser->filter_kind = IMGORG_LIBRARY_FILTER_ALL;
            browser->filter_tag[0] = '\0';
        }
        (void) imgorg_browser_window_update_directory_extent(browser);
    }
    return imgorg_browser_window_redraw_browser(browser);
}

static os_error *imgorg_browser_window_show_album_dialog(
    imgorg_browser_window *browser,
    imgorg_album_dialog_mode mode,
    size_t album_index
)
{
    wimp_window_state parent;
    wimp_open open;
    os_error *error;

    if (!browser->album_dialog_created) {
        return NULL;
    }
    browser->album_dialog_mode = mode;
    browser->album_dialog_index = album_index;
    if (mode == IMGORG_ALBUM_DIALOG_CREATE_TAG) {
        snprintf(browser->album_dialog_title,
            sizeof(browser->album_dialog_title), "New Tag");
        snprintf(browser->album_dialog_label,
            sizeof(browser->album_dialog_label), "Tag name:");
    } else {
        snprintf(browser->album_dialog_title,
            sizeof(browser->album_dialog_title), "Album");
        snprintf(browser->album_dialog_label,
            sizeof(browser->album_dialog_label), "Album name:");
    }
    if (mode == IMGORG_ALBUM_DIALOG_RENAME &&
        album_index < browser->albums.count) {
        snprintf(browser->album_dialog_name,
            sizeof(browser->album_dialog_name), "%s",
            browser->albums.items[album_index].name);
    } else {
        browser->album_dialog_name[0] = '\0';
    }
    parent.w = browser->handle;
    error = xwimp_get_window_state(&parent);
    if (error != NULL) {
        return error;
    }
    open.w = browser->album_dialog_handle;
    open.visible.x0 = parent.visible.x0 +
        (parent.visible.x1 - parent.visible.x0 - ALBUM_DIALOG_WIDTH) / 2;
    open.visible.y0 = parent.visible.y0 +
        (parent.visible.y1 - parent.visible.y0 - ALBUM_DIALOG_HEIGHT) / 2;
    open.visible.x1 = open.visible.x0 + ALBUM_DIALOG_WIDTH;
    open.visible.y1 = open.visible.y0 + ALBUM_DIALOG_HEIGHT;
    open.xscroll = 0;
    open.yscroll = 0;
    open.next = wimp_TOP;
    error = xwimp_open_window(&open);
    if (error != NULL) {
        return error;
    }
    (void) xwimp_set_icon_state(
        browser->album_dialog_handle,
        ALBUM_DIALOG_NAME,
        0,
        0
    );
    return xwimp_set_caret_position(
        browser->album_dialog_handle,
        ALBUM_DIALOG_NAME,
        0,
        0,
        -1,
        (int) strlen(browser->album_dialog_name)
    );
}

static os_error *imgorg_browser_window_accept_album_dialog(
    imgorg_browser_window *browser
)
{
    size_t album_index;
    os_error *error;

    if (browser->album_dialog_mode == IMGORG_ALBUM_DIALOG_CREATE_TAG) {
        char tag[IMGORG_TAG_NAME_CAPACITY];

        if (!imgorg_tag_name_normalise(
                tag, sizeof(tag), browser->album_dialog_name)) {
            return imgorg_browser_window_error(
                "Please enter a tag name without commas"
            );
        }
        error = imgorg_browser_window_apply_tag_to_selection(
            browser, tag, false);
        if (error != NULL) {
            return error;
        }
        browser->album_dialog_mode = IMGORG_ALBUM_DIALOG_NONE;
        (void) xwimp_close_window(browser->album_dialog_handle);
        return imgorg_browser_window_redraw_browser(browser);
    }
    if (browser->album_dialog_name[0] == '\0') {
        return imgorg_browser_window_error("Please enter an album name");
    }
    if (browser->album_dialog_mode == IMGORG_ALBUM_DIALOG_CREATE) {
        if (!imgorg_album_list_add(
                &browser->albums,
                browser->album_dialog_name,
                &album_index
            )) {
            return imgorg_browser_window_error(
                "That album name is already in use"
            );
        }
        browser->library_dirty = true;
        error = imgorg_browser_window_add_selection_to_album(
            browser,
            album_index
        );
        if (error == NULL && browser->library_dirty) {
            error = imgorg_browser_window_save_library(browser);
        }
    } else if (browser->album_dialog_mode == IMGORG_ALBUM_DIALOG_RENAME) {
        album_index = browser->album_dialog_index;
        if (!imgorg_album_list_rename(
                &browser->albums,
                album_index,
                browser->album_dialog_name
            )) {
            return imgorg_browser_window_error(
                "That album name is already in use"
            );
        }
        browser->library_dirty = true;
        error = imgorg_browser_window_save_library(browser);
    } else {
        return NULL;
    }
    if (error != NULL) {
        return error;
    }
    browser->album_dialog_mode = IMGORG_ALBUM_DIALOG_NONE;
    (void) xwimp_close_window(browser->album_dialog_handle);
    return imgorg_browser_window_redraw_browser(browser);
}

os_error *imgorg_browser_window_handle_menu_selection(
    imgorg_browser_window *browser,
    const wimp_selection *selection,
    bool *handled
)
{
    size_t index;

    if (handled != NULL) {
        *handled = false;
    }
    if (browser == NULL || selection == NULL || handled == NULL ||
        !browser->context_menu_open) {
        return NULL;
    }
    browser->context_menu_open = false;
    *handled = true;
    if (browser->context_album_menu) {
        size_t album_index = browser->context_album_index;

        browser->context_album_menu = false;
        if (album_index >= browser->albums.count) {
            return NULL;
        }
        if (selection->items[0] == 0) {
            return imgorg_browser_window_show_album_dialog(
                browser,
                IMGORG_ALBUM_DIALOG_RENAME,
                album_index
            );
        }
        if (selection->items[0] == 1) {
            if (browser->filter_kind == IMGORG_LIBRARY_FILTER_ALBUM) {
                if (browser->filter_album_index == album_index) {
                    browser->filter_kind = IMGORG_LIBRARY_FILTER_ALL;
                } else if (browser->filter_album_index > album_index) {
                    --browser->filter_album_index;
                }
            }
            (void) imgorg_album_list_remove_at(
                &browser->albums,
                album_index
            );
            browser->library_dirty = true;
            if (imgorg_browser_window_save_library(browser) != NULL) {
                return imgorg_browser_window_error(
                    "The album could not be removed"
                );
            }
            if (imgorg_browser_window_update_directory_extent(browser) !=
                NULL) {
                return imgorg_browser_window_error(
                    "The album view could not be refreshed"
                );
            }
            return imgorg_browser_window_redraw_browser(browser);
        }
        return NULL;
    }
    index = browser->context_image_index;
    if (index >= browser->images.count) {
        return NULL;
    }
    switch (selection->items[0]) {
    case 0:
        if (browser->images.items[index].format !=
                IMGORG_IMAGE_FORMAT_PNG &&
            browser->images.items[index].format !=
                IMGORG_IMAGE_FORMAT_JPEG) {
            return NULL;
        }
        return imgorg_browser_window_load_image_mode(
            browser,
            browser->images.items[index].path,
            browser->images.items[index].format,
            browser->handle
        );

    case 1:
        if (selection->items[1] < 0) {
            return NULL;
        }
        if ((size_t) selection->items[1] < browser->albums.count) {
            return imgorg_browser_window_add_selection_to_album(
                browser,
                (size_t) selection->items[1]
            );
        }
        if ((size_t) selection->items[1] == browser->albums.count) {
            return imgorg_browser_window_show_album_dialog(
                browser,
                IMGORG_ALBUM_DIALOG_CREATE,
                0
            );
        }
        return NULL;

    case 2:
        if (selection->items[1] < 0) {
            return NULL;
        }
        if ((size_t) selection->items[1] < browser->tag_count) {
            return imgorg_browser_window_apply_tag_to_selection(
                browser,
                browser->tag_names[selection->items[1]],
                false
            );
        }
        if ((size_t) selection->items[1] == browser->tag_count) {
            return imgorg_browser_window_show_album_dialog(
                browser,
                IMGORG_ALBUM_DIALOG_CREATE_TAG,
                0
            );
        }
        return NULL;

    case 3:
        if (selection->items[1] < 0 ||
            (size_t) selection->items[1] >=
                browser->selection_tag_count) {
            return NULL;
        }
        return imgorg_browser_window_apply_tag_to_selection(
            browser,
            browser->selection_tag_names[selection->items[1]],
            true
        );

    case 4:
        for (index = browser->images.count; index > 0; --index) {
            if (browser->images.items[index - 1].selected) {
                os_error *error =
                    imgorg_browser_window_remove_library_image(
                        browser,
                        index - 1
                    );
                if (error != NULL) {
                    return error;
                }
            }
        }
        return NULL;

    default:
        return NULL;
    }
}

os_error *imgorg_browser_window_handle_drag_end(
    imgorg_browser_window *browser,
    const wimp_dragged *dragged
)
{
    imgorg_viewer_window *viewer = NULL;

    if (browser == NULL || dragged == NULL) {
        return NULL;
    }
    if (browser->thumbnail_image_dragging) {
        wimp_pointer pointer;
        wimp_window_state state;
        size_t album_index;
        int y;
        os_error *error;

        browser->thumbnail_image_dragging = false;
        error = xwimp_get_pointer_info(&pointer);
        if (error != NULL || pointer.w != browser->handle) {
            return error;
        }
        state.w = browser->handle;
        error = xwimp_get_window_state(&state);
        if (error != NULL ||
            pointer.pos.x >=
                state.visible.x0 + WORKSPACE_LEFT_PANEL_WIDTH) {
            return error;
        }
        y = imgorg_browser_window_first_album_baseline(
            browser,
            state.visible.y1
        );
        for (album_index = 0;
             album_index < browser->albums.count && album_index < 6;
             ++album_index) {
            if (pointer.pos.y >= y - 12 && pointer.pos.y < y + 24) {
                return imgorg_browser_window_add_selection_to_album(
                    browser,
                    album_index
                );
            }
            y -= 36;
        }
        return NULL;
    }
    if (browser->thumbnail_slider_dragging) {
        browser->thumbnail_slider_dragging = false;
        return imgorg_browser_window_set_thumbnail_width_from_pointer(
            browser,
            dragged->final.x0
        );
    }
    for (viewer = browser->viewers; viewer != NULL; viewer = viewer->next) {
        if (viewer->dragging) {
            break;
        }
    }
    if (viewer == NULL) {
        return NULL;
    }
    viewer->dragging = false;
    viewer->pan_x = viewer->drag_pan_x +
        dragged->final.x0 - viewer->drag_start.x;
    viewer->pan_y = viewer->drag_pan_y +
        dragged->final.y0 - viewer->drag_start.y;
    return imgorg_browser_window_update_drag(viewer);
}

os_error *imgorg_browser_window_handle_drag_update(
    imgorg_browser_window *browser
)
{
    wimp_pointer pointer;
    os_error *error;
    int pan_x;
    int pan_y;
    imgorg_viewer_window *viewer = NULL;

    if (browser == NULL) {
        return NULL;
    }
    if (browser->thumbnail_slider_dragging) {
        error = xwimp_get_pointer_info(&pointer);
        if (error != NULL) {
            return error;
        }
        return imgorg_browser_window_set_thumbnail_width_from_pointer(
            browser,
            pointer.pos.x
        );
    }
    for (viewer = browser->viewers; viewer != NULL; viewer = viewer->next) {
        if (viewer->dragging) {
            break;
        }
    }
    if (viewer == NULL) {
        return NULL;
    }

    error = xwimp_get_pointer_info(&pointer);
    if (error != NULL) {
        return error;
    }

    if ((pointer.buttons & wimp_CLICK_SELECT) == 0) {
        return NULL;
    }

    pan_x = viewer->drag_pan_x + pointer.pos.x - viewer->drag_start.x;
    pan_y = viewer->drag_pan_y + pointer.pos.y - viewer->drag_start.y;
    if (pan_x == viewer->pan_x && pan_y == viewer->pan_y) {
        return NULL;
    }

    viewer->pan_x = pan_x;
    viewer->pan_y = pan_y;
    return imgorg_browser_window_update_drag(viewer);
}

os_error *imgorg_browser_window_handle_scroll(
    imgorg_browser_window *browser,
    const wimp_scroll *scroll
)
{
    bool zoom_in;
    imgorg_viewer_window *viewer;

    if (browser == NULL || scroll == NULL ||
        scroll->ymin == wimp_SCROLL_NONE) {
        return NULL;
    }

    if (scroll->w == browser->handle) {
        wimp_open open;
        int visible_height;
        int minimum_scroll;
        int amount;

        if (browser->images.count == 0) {
            return NULL;
        }

        open.w = scroll->w;
        open.visible = scroll->visible;
        open.xscroll = 0;
        open.yscroll = scroll->yscroll;
        open.next = scroll->next;
        visible_height = scroll->visible.y1 - scroll->visible.y0;
        minimum_scroll =
            imgorg_browser_window_directory_extent_y0(browser) +
            visible_height;
        if (minimum_scroll > 0) {
            minimum_scroll = 0;
        }

        amount = (scroll->ymin == wimp_SCROLL_PAGE_UP ||
                  scroll->ymin == wimp_SCROLL_PAGE_DOWN) ?
            visible_height - 64 : 64;
        if (amount < 64) {
            amount = 64;
        }
        if (scroll->ymin > 0) {
            open.yscroll += amount;
        } else {
            open.yscroll -= amount;
        }
        if (open.yscroll > 0) {
            open.yscroll = 0;
        } else if (open.yscroll < minimum_scroll) {
            open.yscroll = minimum_scroll;
        }
        imgorg_browser_window_set_thumbnail_priority(
            browser,
            open.yscroll,
            visible_height
        );
        {
            os_error *error = xwimp_open_window(&open);

            if (error == NULL && open.yscroll != scroll->yscroll) {
                error = xwimp_force_redraw(
                    browser->handle,
                    open.xscroll + WORKSPACE_LEFT_PANEL_WIDTH,
                    scroll->yscroll - WORKSPACE_HEADER_HEIGHT,
                    open.xscroll +
                        (open.visible.x1 - open.visible.x0) -
                        WORKSPACE_RIGHT_PANEL_WIDTH,
                    scroll->yscroll
                );
            }
            return error == NULL ?
                imgorg_browser_window_update_fixed_chrome(browser) : error;
        }
    }
    viewer = imgorg_browser_window_find_viewer(browser, scroll->w);
    if (viewer == NULL || viewer->sprite_area == NULL) {
        return NULL;
    }

    switch (scroll->ymin) {
    case wimp_SCROLL_LINE_UP:
    case wimp_SCROLL_PAGE_UP:
    case wimp_SCROLL_AUTO_UP:
    case wimp_SCROLL_SINGLE_EXTENDED_UP:
    case wimp_SCROLL_DOUBLE_EXTENDED_UP:
        zoom_in = true;
        break;

    case wimp_SCROLL_LINE_DOWN:
    case wimp_SCROLL_PAGE_DOWN:
    case wimp_SCROLL_AUTO_DOWN:
    case wimp_SCROLL_SINGLE_EXTENDED_DOWN:
    case wimp_SCROLL_DOUBLE_EXTENDED_DOWN:
        zoom_in = false;
        break;

    default:
        return NULL;
    }

    return imgorg_browser_window_apply_zoom(
        viewer,
        &scroll->visible,
        zoom_in
    );
}

static os_error *imgorg_browser_window_plot_toolbar(
    const imgorg_viewer_window *viewer
)
{
    int icon;
    os_error *error = NULL;

    if (!viewer->toolbar_visible) {
        return NULL;
    }
    for (icon = 0;
         icon < VIEWER_TOOLBAR_ICON_COUNT && error == NULL;
         ++icon) {
        wimp_icon_state state;

        state.w = viewer->handle;
        state.i = icon;
        error = xwimp_get_icon_state(&state);
        if (error == NULL) {
            error = xwimp_plot_icon(&state.icon);
        }
    }
    return error;
}

static os_error *imgorg_browser_window_plot_image(
    const imgorg_viewer_window *viewer,
    const wimp_draw *draw,
    os_box *image_box
)
{
    int x_eigen;
    int y_eigen;
    int available_width;
    int available_height;
    int content_y1;
    int target_width;
    int target_height;
    os_factors factors;

    imgorg_browser_window_read_eigen_factors(&x_eigen, &y_eigen);
    content_y1 = draw->box.y1 -
        (viewer->toolbar_visible ? VIEWER_TOOLBAR_HEIGHT : 0);

    if (viewer->fit_to_window) {
        available_width =
            (draw->box.x1 - draw->box.x0 - (2 * IMAGE_BORDER)) >> x_eigen;
        available_height =
            (content_y1 - draw->box.y0 - (2 * IMAGE_BORDER)) >> y_eigen;

        if (available_width <= 0 || available_height <= 0) {
            memset(image_box, 0, sizeof(*image_box));
            return NULL;
        }

        if ((long long) available_width * viewer->image_height <=
            (long long) available_height * viewer->image_width) {
            target_width = available_width;
            target_height = (int) (
                (long long) viewer->image_height * target_width /
                viewer->image_width
            );
        } else {
            target_height = available_height;
            target_width = (int) (
                (long long) viewer->image_width * target_height /
                viewer->image_height
            );
        }
    } else {
        target_width = (int) (
            (long long) viewer->image_width * viewer->zoom_percent / 100
        );
        target_height = (int) (
            (long long) viewer->image_height * viewer->zoom_percent / 100
        );
    }

    if (target_width <= 0 || target_height <= 0) {
        memset(image_box, 0, sizeof(*image_box));
        return NULL;
    }

    factors.xmul = target_width;
    factors.ymul = target_height;
    factors.xdiv = viewer->image_width;
    factors.ydiv = viewer->image_height;

    image_box->x0 = draw->box.x0 +
        ((draw->box.x1 - draw->box.x0 -
          (target_width << x_eigen)) / 2) + viewer->pan_x;
    image_box->y0 = draw->box.y0 +
        ((content_y1 - draw->box.y0 -
          (target_height << y_eigen)) / 2) + viewer->pan_y;
    image_box->x1 = image_box->x0 + (target_width << x_eigen);
    image_box->y1 = image_box->y0 + (target_height << y_eigen);

    return xosspriteop_put_sprite_scaled(
        osspriteop_PTR,
        viewer->sprite_area,
        (osspriteop_id) viewer->sprite,
        image_box->x0,
        image_box->y0,
        os_ACTION_OVERWRITE,
        &factors,
        NULL
    );
}

static void imgorg_browser_window_fill_box(const os_box *box)
{
    if (box->x0 >= box->x1 || box->y0 >= box->y1) {
        return;
    }

    os_plot(os_MOVE_TO, box->x0, box->y0);
    os_plot(
        os_PLOT_RECTANGLE | os_PLOT_TO,
        box->x1 - 1,
        box->y1 - 1
    );
}

static os_error *imgorg_browser_window_plot_panel(
    const wimp_draw *draw,
    const os_box *screen_box,
    wimp_colour background,
    int border_type,
    bool filled
)
{
    wimp_icon icon;
    char *validation;
    int origin_x = draw->box.x0 - draw->xscroll;
    int origin_y = draw->box.y1 - draw->yscroll;

    switch (border_type) {
    case 1:
        validation = IMGORG_BORDER_SLAB_OUT;
        break;
    case 2:
        validation = IMGORG_BORDER_SLAB_IN;
        break;
    case 3:
        validation = IMGORG_BORDER_RIDGE;
        break;
    default:
        validation = (char *) -1;
        break;
    }
    memset(&icon, 0, sizeof(icon));
    icon.extent.x0 = screen_box->x0 - origin_x;
    icon.extent.x1 = screen_box->x1 - origin_x;
    icon.extent.y0 = screen_box->y0 - origin_y;
    icon.extent.y1 = screen_box->y1 - origin_y;
    icon.flags =
        wimp_ICON_TEXT |
        wimp_ICON_INDIRECTED |
        (filled ? wimp_ICON_FILLED : 0) |
        (border_type != 0 ? wimp_ICON_BORDER : 0) |
        (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT) |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (background << wimp_ICON_BG_COLOUR_SHIFT);
    icon.data.indirected_text.text = IMGORG_EMPTY_ICON_TEXT;
    icon.data.indirected_text.validation = validation;
    icon.data.indirected_text.size = sizeof(IMGORG_EMPTY_ICON_TEXT);
    return xwimp_plot_icon(&icon);
}

static os_error *imgorg_browser_window_plot_action_button(
    const wimp_draw *draw,
    const os_box *screen_box,
    char *label,
    bool selected
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
        wimp_ICON_TEXT |
        wimp_ICON_BORDER |
        wimp_ICON_HCENTRED |
        wimp_ICON_VCENTRED |
        wimp_ICON_FILLED |
        wimp_ICON_INDIRECTED |
        (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT) |
        (selected ? wimp_ICON_SELECTED : 0) |
        (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
        (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT);
    icon.data.indirected_text.text = label;
    icon.data.indirected_text.validation = IMGORG_BORDER_ACTION;
    icon.data.indirected_text.size = strlen(label) + 1;
    return xwimp_plot_icon(&icon);
}

static void imgorg_browser_window_inspector_rating_box(
    const os_box *right,
    int rating_baseline,
    unsigned int rating,
    os_box *box
)
{
    box->x0 = right->x0 + WORKSPACE_PANEL_PADDING + 88 +
        (int) (rating - 1) *
        (INSPECTOR_RATING_BUTTON_WIDTH + INSPECTOR_RATING_BUTTON_GAP);
    box->x1 = box->x0 + INSPECTOR_RATING_BUTTON_WIDTH;
    box->y0 = rating_baseline - 12;
    box->y1 = box->y0 + INSPECTOR_BUTTON_HEIGHT;
}

static void imgorg_browser_window_inspector_favourite_box(
    const os_box *right,
    int favourite_baseline,
    os_box *box
)
{
    box->x0 = right->x0 + WORKSPACE_PANEL_PADDING + 112;
    box->x1 = right->x1 - WORKSPACE_PANEL_PADDING;
    box->y0 = favourite_baseline - 12;
    box->y1 = box->y0 + INSPECTOR_BUTTON_HEIGHT;
}

static bool imgorg_browser_window_point_in_box(
    const os_coord *point,
    const os_box *box
)
{
    return point->x >= box->x0 && point->x < box->x1 &&
        point->y >= box->y0 && point->y < box->y1;
}

static bool imgorg_browser_window_boxes_intersect(
    const os_box *first,
    const os_box *second
)
{
    return first->x0 < second->x1 && first->x1 > second->x0 &&
        first->y0 < second->y1 && first->y1 > second->y0;
}

static const char *imgorg_browser_window_format_name(
    imgorg_image_format format
)
{
    switch (format) {
    case IMGORG_IMAGE_FORMAT_SPRITE:
        return "Sprite";
    case IMGORG_IMAGE_FORMAT_JPEG:
        return "JPEG";
    case IMGORG_IMAGE_FORMAT_PNG:
        return "PNG";
    default:
        return "Image";
    }
}

static os_error *imgorg_browser_window_plot_thumbnail(
    const imgorg_thumbnail *thumbnail,
    const os_box *preview
)
{
    int x_eigen;
    int y_eigen;
    int available_width;
    int available_height;
    int target_width;
    int target_height;
    int x;
    int y;
    os_factors factors;

    if (thumbnail == NULL || thumbnail->sprite_area == NULL ||
        thumbnail->width <= 0 || thumbnail->height <= 0) {
        return NULL;
    }

    imgorg_browser_window_read_eigen_factors(&x_eigen, &y_eigen);
    available_width = (preview->x1 - preview->x0) >> x_eigen;
    available_height = (preview->y1 - preview->y0) >> y_eigen;
    if ((long long) available_width * thumbnail->height <=
        (long long) available_height * thumbnail->width) {
        target_width = available_width;
        target_height = (int) ((long long) thumbnail->height *
            target_width / thumbnail->width);
    } else {
        target_height = available_height;
        target_width = (int) ((long long) thumbnail->width *
            target_height / thumbnail->height);
    }
    if (target_width <= 0 || target_height <= 0) {
        return NULL;
    }

    x = preview->x0 +
        ((preview->x1 - preview->x0 - (target_width << x_eigen)) / 2);
    y = preview->y0 +
        ((preview->y1 - preview->y0 - (target_height << y_eigen)) / 2);
    factors.xmul = target_width;
    factors.ymul = target_height;
    factors.xdiv = thumbnail->width;
    factors.ydiv = thumbnail->height;
    return xosspriteop_put_sprite_scaled(
        osspriteop_PTR,
        thumbnail->sprite_area,
        (osspriteop_id) thumbnail->sprite,
        x,
        y,
        os_ACTION_OVERWRITE,
        &factors,
        NULL
    );
}

static os_error *imgorg_browser_window_plot_directory(
    const imgorg_browser_window *browser,
    const wimp_draw *draw
)
{
    int origin_x = draw->box.x0 - draw->xscroll;
    int origin_y = draw->box.y1 - draw->yscroll;
    int cell_width = imgorg_browser_window_thumbnail_cell_width(browser);
    int cell_height = imgorg_browser_window_thumbnail_cell_height(browser);
    size_t columns = imgorg_browser_window_thumbnail_columns_for_width(
        browser,
        draw->box.x1 - draw->box.x0
    );
    int grid_x0 = imgorg_browser_window_thumbnail_grid_x0(
        browser,
        draw->box.x1 - draw->box.x0,
        columns
    );
    size_t visible_count =
        imgorg_browser_window_visible_image_count(browser);
    size_t visible_index;
    os_error *error;

    error = xwimptextop_set_colour(os_COLOUR_BLACK, os_COLOUR_WHITE);
    if (error != NULL) {
        return error;
    }

    for (visible_index = 0;
         visible_index < visible_count;
         ++visible_index) {
        size_t index = imgorg_browser_window_actual_image_index(
            browser,
            visible_index
        );
        const imgorg_image_entry *entry =
            imgorg_image_list_get(&browser->images, index);
        size_t row = visible_index / columns;
        size_t column = visible_index % columns;
        os_box cell;
        os_box preview;

        cell.x0 = origin_x + grid_x0 + (int) column *
            (cell_width + THUMBNAIL_GAP);
        cell.x1 = cell.x0 + cell_width;
        cell.y1 = origin_y - WORKSPACE_HEADER_HEIGHT - THUMBNAIL_MARGIN -
            (int) row *
            (cell_height + THUMBNAIL_GAP);
        cell.y0 = cell.y1 - cell_height;
        if (!imgorg_browser_window_boxes_intersect(&cell, &draw->clip)) {
            continue;
        }

        error = imgorg_browser_window_plot_panel(
            draw,
            &cell,
            entry->selected ? wimp_COLOUR_LIGHT_BLUE :
                wimp_COLOUR_VERY_LIGHT_GREY,
            2,
            entry->selected
        );
        if (error != NULL) {
            return error;
        }

        preview.x0 = cell.x0 + THUMBNAIL_IMAGE_INSET;
        preview.x1 = cell.x1 - THUMBNAIL_IMAGE_INSET;
        preview.y0 = cell.y0 + THUMBNAIL_IMAGE_INSET;
        preview.y1 = cell.y1 - THUMBNAIL_IMAGE_INSET;
        (void) colourtrans_set_gcol(
            os_COLOUR_VERY_LIGHT_GREY,
            0,
            os_ACTION_OVERWRITE,
            NULL
        );
        imgorg_browser_window_fill_box(&preview);

        if (index < browser->thumbnail_count &&
            browser->thumbnails[index].sprite_area != NULL) {
            error = imgorg_browser_window_plot_thumbnail(
                &browser->thumbnails[index],
                &preview
            );
        } else {
            error = xwimptextop_paint(
                0,
                imgorg_browser_window_format_name(entry->format),
                preview.x0 + 16,
                preview.y0 + ((preview.y1 - preview.y0) / 2)
            );
        }
        if (error != NULL) {
            return error;
        }

    }

    return NULL;
}

static const imgorg_image_entry *imgorg_browser_window_selected_entry(
    const imgorg_browser_window *browser
)
{
    size_t index;

    for (index = 0; index < browser->images.count; ++index) {
        const imgorg_image_entry *entry =
            imgorg_image_list_get(&browser->images, index);

        if (entry->selected) {
            return entry;
        }
    }
    return NULL;
}

static os_error *imgorg_browser_window_paint_workspace_text(
    const char *text,
    int x,
    int y
)
{
    return xwimptextop_paint(0, text, x, y);
}

static os_error *imgorg_browser_window_plot_navigation_selection(
    const wimp_draw *draw,
    const os_box *left,
    int baseline
)
{
    os_box selection;

    selection.x0 = left->x0 + 8;
    selection.x1 = left->x1 - 8;
    selection.y0 = baseline - 12;
    selection.y1 = baseline + 28;
    return imgorg_browser_window_plot_panel(
        draw,
        &selection,
        wimp_COLOUR_CREAM,
        2,
        true
    );
}

static os_error *imgorg_browser_window_plot_workspace_chrome(
    const imgorg_browser_window *browser,
    const wimp_draw *draw
)
{
    const imgorg_image_entry *selected =
        imgorg_browser_window_selected_entry(browser);
    os_box left;
    os_box right;
    os_box header;
    os_box slider_track;
    os_box slider_knob;
    os_error *error;
    char line[64];
    char filter_name[64];
    int x;
    int y;
    size_t folder_index;
    size_t tag_index;
    size_t visible_count =
        imgorg_browser_window_visible_image_count(browser);

    imgorg_browser_window_collect_tags(
        (imgorg_browser_window *) browser,
        false
    );

    left.x0 = draw->box.x0;
    left.y0 = draw->box.y0;
    left.x1 = left.x0 + WORKSPACE_LEFT_PANEL_WIDTH;
    left.y1 = draw->box.y1;
    right.x0 = draw->box.x1 - WORKSPACE_RIGHT_PANEL_WIDTH;
    right.y0 = draw->box.y0;
    right.x1 = draw->box.x1;
    right.y1 = draw->box.y1;
    header.x0 = left.x1;
    header.y0 = draw->box.y1 - WORKSPACE_HEADER_HEIGHT;
    header.x1 = right.x0;
    header.y1 = draw->box.y1;
    imgorg_browser_window_thumbnail_slider_track(
        &draw->box,
        &slider_track
    );
    imgorg_browser_window_thumbnail_slider_knob(
        browser,
        &draw->box,
        &slider_knob
    );

    error = imgorg_browser_window_plot_panel(
        draw,
        &left,
        wimp_COLOUR_VERY_LIGHT_GREY,
        3,
        true
    );
    if (error == NULL) {
        error = imgorg_browser_window_plot_panel(
            draw,
            &right,
            wimp_COLOUR_VERY_LIGHT_GREY,
            3,
            true
        );
    }
    if (error == NULL) {
        error = imgorg_browser_window_plot_panel(
            draw,
            &header,
            wimp_COLOUR_LIGHT_GREY,
            1,
            true
        );
    }
    if (error == NULL) {
        error = imgorg_browser_window_plot_panel(
            draw,
            &slider_track,
            wimp_COLOUR_MID_LIGHT_GREY,
            2,
            true
        );
    }
    if (error == NULL) {
        error = imgorg_browser_window_plot_panel(
            draw,
            &slider_knob,
            wimp_COLOUR_LIGHT_GREY,
            1,
            true
        );
    }
    if (error != NULL) {
        return error;
    }

    error = xwimptextop_set_colour(
        os_COLOUR_BLACK,
        os_COLOUR_VERY_LIGHT_GREY
    );
    if (error != NULL) {
        return error;
    }

    x = left.x0 + WORKSPACE_PANEL_PADDING;
    y = left.y1 - 36;
    error = imgorg_browser_window_paint_workspace_text("LIBRARY", x, y);
    if (error != NULL) {
        return error;
    }
    y -= 52;
    if (browser->filter_kind == IMGORG_LIBRARY_FILTER_ALL) {
        error = imgorg_browser_window_plot_navigation_selection(
            draw,
            &left,
            y
        );
        if (error != NULL) {
            return error;
        }
    }
    error = imgorg_browser_window_paint_workspace_text(
        "All photographs",
        x,
        y
    );
    if (error != NULL) {
        return error;
    }
    y -= 48;
    error = imgorg_browser_window_paint_workspace_text("FOLDERS", x, y);
    if (error != NULL) {
        return error;
    }
    y -= 44;
    if (browser->folders.count == 0) {
        error = imgorg_browser_window_paint_workspace_text(
            "No folders added",
            x + 12,
            y
        );
        if (error != NULL) {
            return error;
        }
        y -= 36;
    } else {
        for (folder_index = 0;
             folder_index < browser->folders.count && folder_index < 6;
             ++folder_index) {
            char folder_name[IMGORG_LEAFNAME_CAPACITY];

            imgorg_browser_window_copy_leafname(
                folder_name,
                sizeof(folder_name),
                browser->folders.items[folder_index]
            );
            snprintf(line, sizeof(line), "%.25s", folder_name);
            if (browser->filter_kind == IMGORG_LIBRARY_FILTER_FOLDER &&
                browser->filter_folder_index == folder_index) {
                error = imgorg_browser_window_plot_navigation_selection(
                    draw,
                    &left,
                    y
                );
                if (error != NULL) {
                    return error;
                }
            }
            error = imgorg_browser_window_paint_workspace_text(
                line,
                x + 12,
                y
            );
            if (error != NULL) {
                return error;
            }
            y -= 36;
        }
        if (browser->folders.count > 6) {
            snprintf(
                line,
                sizeof(line),
                "+ %lu more",
                (unsigned long) (browser->folders.count - 6)
            );
            error = imgorg_browser_window_paint_workspace_text(
                line,
                x + 12,
                y
            );
            if (error != NULL) {
                return error;
            }
            y -= 36;
        }
    }
    if (browser->images.count > 0) {
        snprintf(
            line,
            sizeof(line),
            "%lu photograph%s",
            (unsigned long) browser->images.count,
            browser->images.count == 1 ? "" : "s"
        );
        error = imgorg_browser_window_paint_workspace_text(
            line,
            x + 12,
            y
        );
        if (error != NULL) {
            return error;
        }
        y -= 36;
    }
    y -= 28;
    error = imgorg_browser_window_paint_workspace_text("ORGANISE", x, y);
    if (error != NULL) {
        return error;
    }
    y -= 44;
    {
        unsigned int rating;

        for (rating = 1; rating <= 5; ++rating) {
            if (browser->filter_kind == IMGORG_LIBRARY_FILTER_RATING &&
                browser->filter_rating == rating) {
                error = imgorg_browser_window_plot_navigation_selection(
                    draw,
                    &left,
                    y
                );
                if (error != NULL) {
                    return error;
                }
            }
            snprintf(
                line,
                sizeof(line),
                rating == 1 ? "%u star" : "%u stars",
                rating
            );
            error = imgorg_browser_window_paint_workspace_text(
                line,
                x + 12,
                y
            );
            if (error != NULL) {
                return error;
            }
            y -= 36;
        }
    }
    if (browser->filter_kind == IMGORG_LIBRARY_FILTER_FAVOURITES) {
        error = imgorg_browser_window_plot_navigation_selection(
            draw,
            &left,
            y
        );
        if (error != NULL) {
            return error;
        }
    }
    error = imgorg_browser_window_paint_workspace_text(
        "Favourites",
        x + 12,
        y
    );
    if (error != NULL) {
        return error;
    }
    y -= 52;
    error = imgorg_browser_window_paint_workspace_text(
        "TAGS",
        x,
        y
    );
    if (error != NULL) {
        return error;
    }
    y -= 44;
    if (browser->tag_count == 0) {
        error = imgorg_browser_window_paint_workspace_text(
            "No tags",
            x + 12,
            y
        );
        if (error != NULL) {
            return error;
        }
        y -= 36;
    } else {
        size_t tag_count = browser->tag_count < 5 ?
            browser->tag_count : 5;

        for (tag_index = 0; tag_index < tag_count; ++tag_index) {
            if (browser->filter_kind == IMGORG_LIBRARY_FILTER_TAG) {
                imgorg_image_entry tag_entry;

                memset(&tag_entry, 0, sizeof(tag_entry));
                snprintf(tag_entry.tags, sizeof(tag_entry.tags), "%s",
                    browser->tag_names[tag_index]);
                if (imgorg_image_entry_has_tag(
                        &tag_entry, browser->filter_tag)) {
                    error =
                        imgorg_browser_window_plot_navigation_selection(
                            draw, &left, y);
                    if (error != NULL) {
                        return error;
                    }
                }
            }
            snprintf(line, sizeof(line), "%.22s",
                browser->tag_names[tag_index]);
            error = imgorg_browser_window_paint_workspace_text(
                line, x + 12, y);
            if (error != NULL) {
                return error;
            }
            y -= 36;
        }
        if (browser->tag_count > 5) {
            snprintf(line, sizeof(line), "+ %lu more",
                (unsigned long) (browser->tag_count - 5));
            error = imgorg_browser_window_paint_workspace_text(
                line, x + 12, y);
            if (error != NULL) {
                return error;
            }
            y -= 36;
        }
    }
    y -= 52;
    error = imgorg_browser_window_paint_workspace_text("ALBUMS", x, y);
    if (error != NULL) {
        return error;
    }
    y -= 44;
    if (browser->albums.count == 0) {
        error = imgorg_browser_window_paint_workspace_text(
            "No albums",
            x + 12,
            y
        );
        if (error != NULL) {
            return error;
        }
    } else {
        size_t album_index;
        size_t album_count =
            browser->albums.count < 6 ? browser->albums.count : 6;

        for (album_index = 0; album_index < album_count; ++album_index) {
            if (browser->filter_kind == IMGORG_LIBRARY_FILTER_ALBUM &&
                browser->filter_album_index == album_index) {
                error = imgorg_browser_window_plot_navigation_selection(
                    draw,
                    &left,
                    y
                );
                if (error != NULL) {
                    return error;
                }
            }
            snprintf(line, sizeof(line), "%.22s",
                browser->albums.items[album_index].name);
            error = imgorg_browser_window_paint_workspace_text(
                line,
                x + 12,
                y
            );
            if (error != NULL) {
                return error;
            }
            y -= 36;
        }
    }

    error = xwimptextop_set_colour(os_COLOUR_BLACK, os_COLOUR_LIGHT_GREY);
    if (error != NULL) {
        return error;
    }
    error = imgorg_browser_window_paint_workspace_text(
        "Thumbnail Size",
        slider_track.x0 - 208,
        header.y0 + 22
    );
    if (error != NULL) {
        return error;
    }
    switch (browser->filter_kind) {
    case IMGORG_LIBRARY_FILTER_FOLDER:
        if (browser->filter_folder_index < browser->folders.count) {
            imgorg_browser_window_copy_leafname(
                filter_name,
                sizeof(filter_name),
                browser->folders.items[browser->filter_folder_index]
            );
        } else {
            snprintf(filter_name, sizeof(filter_name), "Folder");
        }
        break;

    case IMGORG_LIBRARY_FILTER_RATING:
        snprintf(
            filter_name,
            sizeof(filter_name),
            browser->filter_rating == 1 ?
                "1-star photographs" : "%u-star photographs",
            browser->filter_rating
        );
        break;

    case IMGORG_LIBRARY_FILTER_FAVOURITES:
        snprintf(filter_name, sizeof(filter_name), "Favourites");
        break;

    case IMGORG_LIBRARY_FILTER_ALBUM:
        if (browser->filter_album_index < browser->albums.count) {
            snprintf(filter_name, sizeof(filter_name), "%s",
                browser->albums.items[browser->filter_album_index].name);
        } else {
            snprintf(filter_name, sizeof(filter_name), "Album");
        }
        break;

    case IMGORG_LIBRARY_FILTER_TAG:
        snprintf(filter_name, sizeof(filter_name), "%s",
            browser->filter_tag);
        break;

    case IMGORG_LIBRARY_FILTER_ALL:
    default:
        snprintf(filter_name, sizeof(filter_name), "All photographs");
        break;
    }
    snprintf(
        line,
        sizeof(line),
        "%.24s - %lu",
        filter_name,
        (unsigned long) visible_count
    );
    error = imgorg_browser_window_paint_workspace_text(
        line,
        header.x0 + WORKSPACE_PANEL_PADDING,
        header.y0 + 22
    );
    if (error != NULL) {
        return error;
    }

    error = xwimptextop_set_colour(
        os_COLOUR_BLACK,
        os_COLOUR_VERY_LIGHT_GREY
    );
    if (error != NULL) {
        return error;
    }
    x = right.x0 + WORKSPACE_PANEL_PADDING;
    y = right.y1 - 36;
    error = imgorg_browser_window_paint_workspace_text("INSPECTOR", x, y);
    if (error != NULL) {
        return error;
    }
    y -= 52;
    if (selected == NULL) {
        error = imgorg_browser_window_paint_workspace_text(
            "Select a thumbnail",
            x,
            y
        );
        if (error != NULL) {
            return error;
        }
        y -= 36;
        error = imgorg_browser_window_paint_workspace_text(
            "to inspect it",
            x,
            y
        );
        if (error != NULL) {
            return error;
        }
    } else {
        snprintf(line, sizeof(line), "%.24s", selected->leafname);
        error = imgorg_browser_window_paint_workspace_text(line, x, y);
        if (error != NULL) {
            return error;
        }
        y -= 44;
        snprintf(
            line,
            sizeof(line),
            "Format: %s",
            imgorg_browser_window_format_name(selected->format)
        );
        error = imgorg_browser_window_paint_workspace_text(line, x, y);
        if (error != NULL) {
            return error;
        }
        y -= 36;
        snprintf(
            line,
            sizeof(line),
            "Size: %lu KB",
            (unsigned long) ((selected->size_bytes + 1023) / 1024)
        );
        error = imgorg_browser_window_paint_workspace_text(line, x, y);
        if (error != NULL) {
            return error;
        }
        y -= 36;
        snprintf(
            line,
            sizeof(line),
            "Filetype: &%03lX",
            (unsigned long) selected->riscos_filetype
        );
        error = imgorg_browser_window_paint_workspace_text(line, x, y);
        if (error != NULL) {
            return error;
        }
        y -= 64;
        error = imgorg_browser_window_paint_workspace_text(
            "ORGANISATION",
            x,
            y
        );
        if (error != NULL) {
            return error;
        }
        y -= 44;
        error = imgorg_browser_window_paint_workspace_text("Rating:", x, y);
        if (error != NULL) {
            return error;
        }
        {
            unsigned int rating;

            for (rating = 1; rating <= 5; ++rating) {
                os_box button;

                imgorg_browser_window_inspector_rating_box(
                    &right,
                    y,
                    rating,
                    &button
                );
                error = imgorg_browser_window_plot_action_button(
                    draw,
                    &button,
                    IMGORG_RATING_LABELS[rating - 1],
                    rating <= selected->rating
                );
                if (error != NULL) {
                    return error;
                }
            }
        }
        y -= 60;
        error = imgorg_browser_window_paint_workspace_text(
            "Favourite:",
            x,
            y
        );
        if (error != NULL) {
            return error;
        }
        {
            os_box button;

            imgorg_browser_window_inspector_favourite_box(
                &right,
                y,
                &button
            );
            error = imgorg_browser_window_plot_action_button(
                draw,
                &button,
                selected->favourite ?
                    IMGORG_FAVOURITE_REMOVE_LABEL :
                    IMGORG_FAVOURITE_ADD_LABEL,
                selected->favourite
            );
            if (error != NULL) {
                return error;
            }
        }
        y -= 60;
        error = imgorg_browser_window_paint_workspace_text(
            "Tags:",
            x,
            y
        );
        if (error != NULL) {
            return error;
        }
        y -= 36;
        snprintf(line, sizeof(line), "%.30s",
            selected->tags[0] != '\0' ? selected->tags : "None");
        error = imgorg_browser_window_paint_workspace_text(line, x, y);
        if (error != NULL) {
            return error;
        }
        y -= 52;
        {
            os_box button;

            imgorg_browser_window_inspector_favourite_box(
                &right,
                y,
                &button
            );
            error = imgorg_browser_window_plot_action_button(
                draw,
                &button,
                IMGORG_INSPECTOR_ADD_TAG_LABEL,
                false
            );
            if (error != NULL) {
                return error;
            }
        }
    }

    if (visible_count == 0) {
        int centre_x = header.x0 + WORKSPACE_PANEL_PADDING + 48;
        int centre_y = draw->box.y1 - WORKSPACE_HEADER_HEIGHT - 128;

        error = xwimptextop_set_colour(os_COLOUR_BLACK, os_COLOUR_WHITE);
        if (error != NULL) {
            return error;
        }
        error = imgorg_browser_window_paint_workspace_text(
            browser->images.count == 0 ?
                "Drop folders or images here to add them to your library" :
                "No photographs match this library view",
            centre_x,
            centre_y
        );
        if (error != NULL) {
            return error;
        }
        return imgorg_browser_window_paint_workspace_text(
            browser->images.count == 0 ?
                "They will remain available the next time Aural starts" :
                "Choose another section from the Library panel",
            centre_x,
            centre_y - 52
        );
    }
    return NULL;
}

static void imgorg_browser_window_clear_around_image(
    const os_box *visible,
    const os_box *image
)
{
    os_box box;
    int middle_x0 = image->x0 > visible->x0 ? image->x0 : visible->x0;
    int middle_x1 = image->x1 < visible->x1 ? image->x1 : visible->x1;

    (void) colourtrans_set_gcol(
        os_COLOUR_WHITE,
        0,
        os_ACTION_OVERWRITE,
        NULL
    );

    box.x0 = visible->x0;
    box.y0 = visible->y0;
    box.x1 = image->x0 < visible->x1 ? image->x0 : visible->x1;
    box.y1 = visible->y1;
    imgorg_browser_window_fill_box(&box);

    box.x0 = image->x1 > visible->x0 ? image->x1 : visible->x0;
    box.x1 = visible->x1;
    imgorg_browser_window_fill_box(&box);

    box.x0 = middle_x0;
    box.x1 = middle_x1;
    box.y0 = visible->y0;
    box.y1 = image->y0 < visible->y1 ? image->y0 : visible->y1;
    imgorg_browser_window_fill_box(&box);

    box.y0 = image->y1 > visible->y0 ? image->y1 : visible->y0;
    box.y1 = visible->y1;
    imgorg_browser_window_fill_box(&box);
}

static os_error *imgorg_browser_window_update_drag(
    const imgorg_viewer_window *viewer
)
{
    wimp_window_state state;
    wimp_draw update;
    osbool more;
    os_error *error;
    int width;
    int height;

    state.w = viewer->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }
    width = state.visible.x1 - state.visible.x0;
    height = state.visible.y1 - state.visible.y0;
    memset(&update, 0, sizeof(update));
    update.w = viewer->handle;
    update.box.x0 = state.xscroll;
    update.box.y0 = state.yscroll - height;
    update.box.x1 = state.xscroll + width;
    update.box.y1 = state.yscroll;

    error = xwimp_update_window(&update, &more);
    while (error == NULL && more) {
        os_box image_box;

        error = imgorg_browser_window_plot_image(viewer, &update, &image_box);
        if (error == NULL) {
            imgorg_browser_window_clear_around_image(
                &update.box,
                &image_box
            );
            error = imgorg_browser_window_plot_toolbar(viewer);
        }
        if (error == NULL) {
            error = xwimp_get_rectangle(&update, &more);
        }
    }

    return error;
}

os_error *imgorg_browser_window_redraw(
    const imgorg_browser_window *browser,
    wimp_draw *redraw
)
{
    osbool more;
    os_error *error;
    const imgorg_viewer_window *viewer = NULL;

    if (browser == NULL || redraw == NULL) {
        return NULL;
    }
    if (redraw->w != browser->handle) {
        for (viewer = browser->viewers; viewer != NULL;
             viewer = viewer->next) {
            if (viewer->created && viewer->handle == redraw->w) {
                break;
            }
        }
        if (viewer == NULL) {
            return NULL;
        }
    }

    error = xwimp_redraw_window(redraw, &more);
    while (error == NULL && more) {
        if (viewer != NULL && viewer->sprite_area != NULL) {
            os_box image_box;

            error = imgorg_browser_window_plot_image(
                viewer,
                redraw,
                &image_box
            );
            if (error == NULL) {
                error = imgorg_browser_window_plot_toolbar(viewer);
            }
        } else if (redraw->w == browser->handle) {
            if (browser->images.count > 0) {
                error = imgorg_browser_window_plot_directory(browser, redraw);
            }
            if (error == NULL) {
                error = imgorg_browser_window_plot_workspace_chrome(
                    browser,
                    redraw
                );
            }
        } else {
            int origin_x = redraw->box.x0 - redraw->xscroll;
            int origin_y = redraw->box.y1 - redraw->yscroll;

            os_set_colour(0, os_COLOUR_BLACK);
            os_plot(os_MOVE_TO, origin_x + 48, origin_y - 72);
            os_plot(os_PLOT_BY, 360, 0);
            os_plot(os_PLOT_BY, 0, -220);
            os_plot(os_PLOT_BY, -360, 0);
            os_plot(os_PLOT_BY, 0, 220);
        }

        if (error == NULL) {
            error = xwimp_get_rectangle(redraw, &more);
        }
    }

    return error;
}

void imgorg_browser_window_destroy(imgorg_browser_window *browser)
{
    imgorg_viewer_window *viewer;

    if (browser == NULL) {
        return;
    }
    if (browser->library_dirty) {
        (void) imgorg_browser_window_save_library(browser);
    }

    if (browser->loading_created) {
        (void) xwimp_delete_window(browser->loading_handle);
        browser->loading_handle = 0;
        browser->loading_created = false;
    }
    if (browser->album_dialog_created) {
        (void) xwimp_delete_window(browser->album_dialog_handle);
        browser->album_dialog_handle = 0;
        browser->album_dialog_created = false;
    }

    if (browser->created) {
        (void) xwimp_delete_window(browser->handle);
    }
    while (browser->viewers != NULL) {
        viewer = browser->viewers;
        browser->viewers = viewer->next;
        if (viewer->created) {
            (void) xwimp_delete_window(viewer->handle);
        }
        free(viewer->sprite_area);
        viewer->sprite_area = NULL;
        viewer->created = false;
        free(viewer);
    }
    imgorg_browser_window_clear_thumbnails(browser);
    imgorg_folder_list_destroy(&browser->folders);
    imgorg_album_list_destroy(&browser->albums);
    imgorg_image_list_destroy(&browser->images);
    free(imgorg_album_submenu);
    imgorg_album_submenu = NULL;
    free(imgorg_add_tag_submenu);
    imgorg_add_tag_submenu = NULL;
    free(imgorg_remove_tag_submenu);
    imgorg_remove_tag_submenu = NULL;
    imgorg_directory_scanner_init(&browser->scanner);
    browser->viewer_image_bytes = 0;
    browser->created = false;
}
