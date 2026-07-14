#include "imgorg/application.h"

#include <string.h>

#include "oslib/os.h"
#include "oslib/osfile.h"
#include "oslib/wimpspriteop.h"

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

static const wimp_MESSAGE_LIST(3) imgorg_messages = {
    {message_DATA_LOAD, message_QUIT, 0}
};

static os_error *imgorg_application_create_iconbar_icon(
    imgorg_application *application
)
{
    static char icon_sprite[] = "!imgorg";
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
            return imgorg_browser_window_open(&application->browser);
        }
    }

    return imgorg_browser_window_handle_pointer(
        &application->browser,
        pointer
    );
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
    os_error *error;

    if (!((transfer->w == wimp_ICON_BAR &&
           transfer->i == application->iconbar_icon) ||
          transfer->w == application->browser.handle)) {
        return NULL;
    }

    error = xosfile_read_stamped(
        transfer->file_name,
        &object_type,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (error == NULL && object_type == fileswitch_IS_DIR) {
        error = imgorg_browser_window_load_directory(
            &application->browser,
            transfer->file_name
        );
    } else if (error == NULL) {
        error = imgorg_browser_window_load_png(
            &application->browser,
            transfer->file_name
        );
    }
    if (error != NULL) {
        (void) wimp_report_error(
            error,
            wimp_ERROR_BOX_OK_ICON,
            "Image Organiser"
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

    return imgorg_browser_window_open(&application->browser);
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
        (wimp_message_list *) &imgorg_messages,
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
        if (application->browser.dragging ||
            imgorg_browser_window_has_background_work(
                &application->browser
            )) {
            os_t now;

            error = xos_read_monotonic_time(&now);
            if (error == NULL) {
                error = xwimp_poll_idle(
                    0,
                    &block,
                    now + 2,
                    NULL,
                    &event
                );
            }
            if (error != NULL) {
                break;
            }
        } else {
            event = wimp_poll(wimp_MASK_NULL, &block, NULL);
        }

        switch (event) {
        case wimp_NULL_REASON_CODE:
            error = imgorg_browser_window_handle_drag_update(
                &application->browser
            );
            if (error == NULL) {
                error = imgorg_browser_window_scan_step(
                    &application->browser
                );
            }
            break;

        case wimp_REDRAW_WINDOW_REQUEST:
            error = imgorg_browser_window_redraw(
                &application->browser,
                &block.redraw
            );
            break;

        case wimp_OPEN_WINDOW_REQUEST:
        {
            bool handled = false;

            error = imgorg_browser_window_handle_open_request(
                &application->browser,
                &block.open,
                &handled
            );
            if (error == NULL && !handled) {
                error = xwimp_open_window(&block.open);
            }
            break;
        }

        case wimp_CLOSE_WINDOW_REQUEST:
            error = xwimp_close_window(block.close.w);
            break;

        case wimp_MOUSE_CLICK:
            error = imgorg_application_handle_mouse_click(
                application,
                &block.pointer
            );
            break;

        case wimp_USER_DRAG_BOX:
            error = imgorg_browser_window_handle_drag_end(
                &application->browser,
                &block.dragged
            );
            break;

        case wimp_SCROLL_REQUEST:
            error = imgorg_browser_window_handle_scroll(
                &application->browser,
                &block.scroll
            );
            break;

        case wimp_MENU_SELECTION:
            if (block.selection.items[0] == 0) {
                application->quit = true;
            }
            break;

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

    imgorg_browser_window_destroy(&application->browser);

    if (application->task_handle != 0) {
        (void) xwimp_close_down(application->task_handle);
        application->task_handle = 0;
    }
}
