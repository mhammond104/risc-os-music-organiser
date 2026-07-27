#ifndef IMGORG_BROWSER_WINDOW_H
#define IMGORG_BROWSER_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include "imgorg/directory_scanner.h"
#include "imgorg/library_catalog.h"
#include "oslib/osspriteop.h"
#include "oslib/wimp.h"

typedef struct imgorg_thumbnail {
    osspriteop_area *sprite_area;
    osspriteop_header *sprite;
    int width;
    int height;
    bool attempted;
} imgorg_thumbnail;

typedef enum imgorg_library_filter_kind {
    IMGORG_LIBRARY_FILTER_ALL = 0,
    IMGORG_LIBRARY_FILTER_FOLDER,
    IMGORG_LIBRARY_FILTER_RATING,
    IMGORG_LIBRARY_FILTER_FAVOURITES,
    IMGORG_LIBRARY_FILTER_ALBUM,
    IMGORG_LIBRARY_FILTER_TAG
} imgorg_library_filter_kind;

typedef enum imgorg_album_dialog_mode {
    IMGORG_ALBUM_DIALOG_NONE = 0,
    IMGORG_ALBUM_DIALOG_CREATE,
    IMGORG_ALBUM_DIALOG_RENAME,
    IMGORG_ALBUM_DIALOG_CREATE_TAG
} imgorg_album_dialog_mode;

typedef struct imgorg_viewer_window {
    struct imgorg_viewer_window *next;
    wimp_w handle;
    bool created;
    char title[160];
    char image_name[112];
    char image_path[IMGORG_PATH_CAPACITY];
    osspriteop_area *sprite_area;
    osspriteop_header *sprite;
    int image_width;
    int image_height;
    bool fit_to_window;
    int zoom_percent;
    int pan_x;
    int pan_y;
    bool toolbar_visible;
    bool fullscreen;
    char fullscreen_label[24];
    os_box restore_visible;
    int restore_xscroll;
    int restore_yscroll;
    bool dragging;
    os_coord drag_start;
    int drag_pan_x;
    int drag_pan_y;
} imgorg_viewer_window;

typedef struct imgorg_browser_window {
    wimp_w handle;
    wimp_w loading_handle;
    wimp_w album_dialog_handle;
    bool created;
    bool loading_created;
    bool album_dialog_created;
    char title[160];
    char loading_text[32];
    char directory_name[112];
    char directory_path[IMGORG_PATH_CAPACITY];
    imgorg_image_list images;
    imgorg_folder_list folders;
    imgorg_album_list albums;
    size_t folder_scan_index;
    bool library_dirty;
    imgorg_library_filter_kind filter_kind;
    size_t filter_folder_index;
    unsigned int filter_rating;
    size_t filter_album_index;
    char filter_tag[IMGORG_TAG_NAME_CAPACITY];
    int layout_width;
    int layout_height;
    int thumbnail_cell_width;
    bool thumbnail_slider_dragging;
    bool context_menu_open;
    bool context_album_menu;
    size_t context_image_index;
    size_t context_album_index;
    char tag_names[64][IMGORG_TAG_NAME_CAPACITY];
    size_t tag_count;
    char selection_tag_names[64][IMGORG_TAG_NAME_CAPACITY];
    size_t selection_tag_count;
    imgorg_album_dialog_mode album_dialog_mode;
    size_t album_dialog_index;
    char album_dialog_name[IMGORG_ALBUM_NAME_CAPACITY];
    char album_dialog_title[32];
    char album_dialog_label[32];
    bool thumbnail_image_dragging;
    size_t thumbnail_drag_image_index;
    imgorg_directory_scanner scanner;
    imgorg_thumbnail *thumbnails;
    size_t thumbnail_count;
    size_t thumbnail_capacity;
    size_t thumbnail_cursor;
    size_t thumbnail_priority_start;
    size_t thumbnail_priority_end;
    imgorg_viewer_window *viewers;
    size_t viewer_image_bytes;
} imgorg_browser_window;

os_error *imgorg_browser_window_create(imgorg_browser_window *browser);
os_error *imgorg_browser_window_open(imgorg_browser_window *browser);
bool imgorg_browser_window_owns_window(
    const imgorg_browser_window *browser,
    wimp_w window
);
os_error *imgorg_browser_window_handle_open_request(
    imgorg_browser_window *browser,
    const wimp_open *open,
    bool *handled
);
os_error *imgorg_browser_window_handle_close_request(
    imgorg_browser_window *browser,
    wimp_w window,
    bool *handled
);
os_error *imgorg_browser_window_load_png(
    imgorg_browser_window *browser,
    const char *file_name
);
os_error *imgorg_browser_window_load_jpeg(
    imgorg_browser_window *browser,
    const char *file_name
);
os_error *imgorg_browser_window_load_image(
    imgorg_browser_window *browser,
    const char *file_name,
    imgorg_image_format format
);
os_error *imgorg_browser_window_load_image_into(
    imgorg_browser_window *browser,
    const char *file_name,
    imgorg_image_format format,
    wimp_w target
);
os_error *imgorg_browser_window_load_directory(
    imgorg_browser_window *browser,
    const char *directory_path
);
os_error *imgorg_browser_window_add_image(
    imgorg_browser_window *browser,
    const char *file_name,
    uint64_t size_bytes,
    uint32_t load_addr,
    uint32_t exec_addr,
    uint32_t file_type
);
bool imgorg_browser_window_has_background_work(
    const imgorg_browser_window *browser
);
os_error *imgorg_browser_window_scan_step(
    imgorg_browser_window *browser
);
os_error *imgorg_browser_window_handle_pointer(
    imgorg_browser_window *browser,
    const wimp_pointer *pointer
);
os_error *imgorg_browser_window_handle_drag_end(
    imgorg_browser_window *browser,
    const wimp_dragged *dragged
);
os_error *imgorg_browser_window_handle_drag_update(
    imgorg_browser_window *browser
);
os_error *imgorg_browser_window_handle_scroll(
    imgorg_browser_window *browser,
    const wimp_scroll *scroll
);
os_error *imgorg_browser_window_handle_key(
    imgorg_browser_window *browser,
    const wimp_key *key,
    bool *handled
);
os_error *imgorg_browser_window_handle_menu_selection(
    imgorg_browser_window *browser,
    const wimp_selection *selection,
    bool *handled
);
os_error *imgorg_browser_window_redraw(
    const imgorg_browser_window *browser,
    wimp_draw *redraw
);
void imgorg_browser_window_destroy(imgorg_browser_window *browser);

#endif
