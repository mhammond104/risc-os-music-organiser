#ifndef IMGORG_BROWSER_WINDOW_H
#define IMGORG_BROWSER_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include "imgorg/directory_scanner.h"
#include "oslib/osspriteop.h"
#include "oslib/wimp.h"

typedef struct imgorg_thumbnail {
    osspriteop_area *sprite_area;
    osspriteop_header *sprite;
    int width;
    int height;
    bool attempted;
} imgorg_thumbnail;

typedef struct imgorg_browser_window {
    wimp_w handle;
    wimp_w loading_handle;
    bool created;
    bool loading_created;
    char title[160];
    char loading_text[32];
    char image_name[112];
    char directory_name[112];
    char directory_path[IMGORG_PATH_CAPACITY];
    imgorg_image_list images;
    imgorg_directory_scanner scanner;
    imgorg_thumbnail *thumbnails;
    size_t thumbnail_count;
    size_t thumbnail_capacity;
    size_t thumbnail_cursor;
    size_t thumbnail_priority_start;
    size_t thumbnail_priority_end;
    osspriteop_area *image_sprite_area;
    osspriteop_header *image_sprite;
    int image_width;
    int image_height;
    bool fit_to_window;
    int zoom_percent;
    int pan_x;
    int pan_y;
    bool dragging;
    bool return_to_directory;
    os_coord drag_start;
    int drag_pan_x;
    int drag_pan_y;
} imgorg_browser_window;

os_error *imgorg_browser_window_create(imgorg_browser_window *browser);
os_error *imgorg_browser_window_open(imgorg_browser_window *browser);
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
os_error *imgorg_browser_window_load_directory(
    imgorg_browser_window *browser,
    const char *directory_path
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
os_error *imgorg_browser_window_redraw(
    const imgorg_browser_window *browser,
    wimp_draw *redraw
);
void imgorg_browser_window_destroy(imgorg_browser_window *browser);

#endif
