#include "imgorg/application.h"

#include <string.h>

#include "oslib/os.h"

#define IMGORG_MENU_ICON_FLAGS \
    (wimp_ICON_TEXT | (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT))

static wimp_MENU(1) imgorg_iconbar_menu = {
    {"ImgOrg"},
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

static os_error *imgorg_application_create_iconbar_icon(
    imgorg_application *application
)
{
    static char icon_text[] = "ImgOrg";
    static char validation[] = "";
    wimp_icon_create icon;

    memset(&icon, 0, sizeof(icon));

    icon.w = wimp_ICON_BAR_RIGHT;
    icon.icon.extent.x0 = 0;
    icon.icon.extent.y0 = 0;
    icon.icon.extent.x1 = 120;
    icon.icon.extent.y1 = 68;

    icon.icon.flags =
        wimp_ICON_TEXT |
        wimp_ICON_BORDER |
        wimp_ICON_HCENTRED |
        wimp_ICON_VCENTRED |
        wimp_ICON_INDIRECTED |
        (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);

    icon.icon.data.indirected_text.text = icon_text;
    icon.icon.data.indirected_text.validation = validation;
    icon.icon.data.indirected_text.size = sizeof(icon_text);

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
            return imgorg_browser_window_open(&application->browser);
        }
    }

    return NULL;
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

os_error *imgorg_application_initialise(imgorg_application *application)
{
    os_error *error;

    if (application == NULL) {
        return NULL;
    }

    memset(application, 0, sizeof(*application));
    application->iconbar_icon = wimp_ICON_WINDOW;

    error = xwimp_initialise(
        wimp_VERSION_RO3,
        "Image Organiser",
        NULL,
        NULL,
        &application->task_handle
    );
    if (error != NULL) {
        return error;
    }

    error = imgorg_browser_window_create(&application->browser);
    if (error != NULL) {
        return error;
    }

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
        event = wimp_poll(wimp_MASK_NULL, &block, NULL);

        switch (event) {
        case wimp_REDRAW_WINDOW_REQUEST:
            error = imgorg_browser_window_redraw(
                &application->browser,
                &block.redraw
            );
            break;

        case wimp_OPEN_WINDOW_REQUEST:
            error = xwimp_open_window(&block.open);
            break;

        case wimp_CLOSE_WINDOW_REQUEST:
            error = xwimp_close_window(block.close.w);
            break;

        case wimp_MOUSE_CLICK:
            error = imgorg_application_handle_mouse_click(
                application,
                &block.pointer
            );
            break;

        case wimp_MENU_SELECTION:
            if (block.selection.items[0] == 0) {
                application->quit = true;
            }
            break;

        case wimp_USER_MESSAGE:
        case wimp_USER_MESSAGE_RECORDED:
            imgorg_application_handle_message(application, &block.message);
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

    imgorg_browser_window_destroy(&application->browser);

    if (application->task_handle != 0) {
        (void) xwimp_close_down(application->task_handle);
        application->task_handle = 0;
    }
}
