#ifndef IMGORG_BROWSER_WINDOW_H
#define IMGORG_BROWSER_WINDOW_H

#include <stdbool.h>
#include "oslib/wimp.h"

typedef struct imgorg_browser_window {
    wimp_w handle;
    bool created;
} imgorg_browser_window;

os_error *imgorg_browser_window_create(imgorg_browser_window *browser);
os_error *imgorg_browser_window_open(imgorg_browser_window *browser);
os_error *imgorg_browser_window_redraw(
    const imgorg_browser_window *browser,
    wimp_draw *redraw
);
void imgorg_browser_window_destroy(imgorg_browser_window *browser);

#endif
