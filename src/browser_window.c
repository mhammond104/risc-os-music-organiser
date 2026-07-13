#include "imgorg/browser_window.h"

#include <string.h>

#include "oslib/colourtrans.h"
#include "oslib/os.h"
#include "oslib/wimpspriteop.h"

enum {
    BROWSER_VISIBLE_WIDTH = 960,
    BROWSER_VISIBLE_HEIGHT = 640
};

os_error *imgorg_browser_window_create(imgorg_browser_window *browser)
{
    wimp_window definition;
    os_error *error;

    if (browser == NULL) {
        return NULL;
    }

    memset(browser, 0, sizeof(*browser));
    memset(&definition, 0, sizeof(definition));

    definition.visible.x0 = 128;
    definition.visible.y0 = 128;
    definition.visible.x1 = 128 + BROWSER_VISIBLE_WIDTH;
    definition.visible.y1 = 128 + BROWSER_VISIBLE_HEIGHT;
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
        wimp_WINDOW_VSCROLL |
        wimp_WINDOW_NEW_FORMAT;

    definition.title_fg = wimp_COLOUR_BLACK;
    definition.title_bg = wimp_COLOUR_LIGHT_GREY;
    definition.work_fg = wimp_COLOUR_BLACK;
    definition.work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
    definition.scroll_outer = wimp_COLOUR_MID_LIGHT_GREY;
    definition.scroll_inner = wimp_COLOUR_VERY_LIGHT_GREY;
    definition.highlight_bg = wimp_COLOUR_CREAM;

    definition.extent.x0 = 0;
    definition.extent.y0 = -2048;
    definition.extent.x1 = 2048;
    definition.extent.y1 = 0;

    definition.title_flags =
        wimp_ICON_TEXT |
        wimp_ICON_HCENTRED |
        wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED;

    definition.work_flags = wimp_BUTTON_CLICK_DRAG << wimp_ICON_BUTTON_TYPE_SHIFT;

    definition.sprite_area = wimpspriteop_AREA;
    definition.xmin = 320;
    definition.ymin = 240;

    definition.title_data.indirected_text.text = "Image Organiser";
    definition.title_data.indirected_text.validation = (char *) -1;
    definition.title_data.indirected_text.size = 16;

    definition.icon_count = 0;

    error = xwimp_create_window(&definition, &browser->handle);
    if (error == NULL) {
        browser->created = true;
    }

    return error;
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

os_error *imgorg_browser_window_redraw(
    const imgorg_browser_window *browser,
    wimp_draw *redraw
)
{
    osbool more;
    os_error *error;

    if (browser == NULL || redraw == NULL || redraw->w != browser->handle) {
        return NULL;
    }

    error = xwimp_redraw_window(redraw, &more);
    while (error == NULL && more) {
        int origin_x = redraw->box.x0 - redraw->xscroll;
        int origin_y = redraw->box.y1 - redraw->yscroll;

        os_set_colour(0, os_COLOUR_BLACK);
        os_plot(os_MOVE_TO, origin_x + 48, origin_y - 72);
        os_plot(os_PLOT_BY, 360, 0);
        os_plot(os_PLOT_BY, 0, -220);
        os_plot(os_PLOT_BY, -360, 0);
        os_plot(os_PLOT_BY, 0, 220);

        error = xwimp_get_rectangle(redraw, &more);
    }

    return error;
}

void imgorg_browser_window_destroy(imgorg_browser_window *browser)
{
    if (browser == NULL || !browser->created) {
        return;
    }

    (void) xwimp_delete_window(browser->handle);
    browser->created = false;
}
