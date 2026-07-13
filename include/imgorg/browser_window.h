#ifndef IMGORG_BROWSER_WINDOW_H
#define IMGORG_BROWSER_WINDOW_H

#include <stdbool.h>
#include "oslib/osspriteop.h"
#include "oslib/wimp.h"

typedef struct imgorg_browser_window {
    wimp_w handle;
    bool created;
    osspriteop_area *image_sprite_area;
    osspriteop_header *image_sprite;
    int image_width;
    int image_height;
} imgorg_browser_window;

os_error *imgorg_browser_window_create(imgorg_browser_window *browser);
os_error *imgorg_browser_window_open(imgorg_browser_window *browser);
os_error *imgorg_browser_window_load_png(
    imgorg_browser_window *browser,
    const char *file_name
);
os_error *imgorg_browser_window_redraw(
    const imgorg_browser_window *browser,
    wimp_draw *redraw
);
void imgorg_browser_window_destroy(imgorg_browser_window *browser);

#endif
