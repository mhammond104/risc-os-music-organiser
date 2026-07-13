#include "imgorg/browser_window.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>

#include "oslib/colourtrans.h"
#include "oslib/os.h"
#include "oslib/wimpspriteop.h"

enum {
    BROWSER_VISIBLE_WIDTH = 960,
    BROWSER_VISIBLE_HEIGHT = 640
};

enum {
    MAXIMUM_PNG_SIZE = 64 * 1024 * 1024,
    MAXIMUM_IMAGE_DIMENSION = 8192,
    IMAGE_BORDER = 32,
    SPRITE_DPI = 90
};

static os_error browser_error;

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

static osspriteop_area *imgorg_browser_window_decode_png(
    const byte *data,
    size_t data_size,
    int *width_out,
    int *height_out
)
{
    png_image image;
    byte *pixels;
    osspriteop_area *area;
    osspriteop_header *sprite;
    size_t pixel_count;
    size_t pixel_bytes;
    size_t area_size;
    size_t index;

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
    pixel_bytes = PNG_IMAGE_SIZE(image);
    pixels = malloc(pixel_bytes);
    if (pixels == NULL) {
        png_image_free(&image);
        return NULL;
    }

    if (!png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        free(pixels);
        png_image_free(&image);
        return NULL;
    }

    pixel_count = (size_t) image.width * image.height;
    for (index = 0; index < pixel_count; ++index) {
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

    area_size = sizeof(*area) + sizeof(*sprite) + pixel_bytes;
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
    sprite->size = sizeof(*sprite) + (int) pixel_bytes;
    memcpy(sprite->name, "imgorg", 7);
    sprite->width = (int) image.width - 1;
    sprite->height = (int) image.height - 1;
    sprite->left_bit = 0;
    sprite->right_bit = 31;
    sprite->image = sizeof(*sprite);
    sprite->mask = sizeof(*sprite);
    sprite->mode = (os_mode) imgorg_browser_window_sprite_mode();

    memcpy((byte *) sprite + sprite->image, pixels, pixel_bytes);
    free(pixels);

    *width_out = (int) image.width;
    *height_out = (int) image.height;
    png_image_free(&image);
    return area;
}

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

os_error *imgorg_browser_window_load_png(
    imgorg_browser_window *browser,
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
        &width,
        &height
    );
    free(data);
    if (new_area == NULL) {
        return imgorg_browser_window_error("The PNG image could not be decoded");
    }

    free(browser->image_sprite_area);
    browser->image_sprite_area = new_area;
    browser->image_sprite = (osspriteop_header *)
        ((byte *) new_area + new_area->first);
    browser->image_width = width;
    browser->image_height = height;

    if (browser->created) {
        return xwimp_force_redraw(browser->handle, 0, -2048, 2048, 0);
    }

    return NULL;
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
        if (browser->image_sprite_area != NULL) {
            int x_eigen = 1;
            int y_eigen = 1;
            int available_width;
            int available_height;
            int target_width;
            int target_height;
            int x;
            int y;
            os_factors factors;

            (void) xos_read_mode_variable(
                os_CURRENT_MODE,
                os_MODEVAR_XEIG_FACTOR,
                &x_eigen,
                NULL
            );
            (void) xos_read_mode_variable(
                os_CURRENT_MODE,
                os_MODEVAR_YEIG_FACTOR,
                &y_eigen,
                NULL
            );

            available_width =
                (redraw->box.x1 - redraw->box.x0 - (2 * IMAGE_BORDER)) >>
                x_eigen;
            available_height =
                (redraw->box.y1 - redraw->box.y0 - (2 * IMAGE_BORDER)) >>
                y_eigen;

            if ((long long) available_width * browser->image_height <=
                (long long) available_height * browser->image_width) {
                target_width = available_width;
                target_height = (int) (
                    (long long) browser->image_height * target_width /
                    browser->image_width
                );
            } else {
                target_height = available_height;
                target_width = (int) (
                    (long long) browser->image_width * target_height /
                    browser->image_height
                );
            }

            if (target_width > 0 && target_height > 0) {
                factors.xmul = target_width;
                factors.ymul = target_height;
                factors.xdiv = browser->image_width;
                factors.ydiv = browser->image_height;

                x = redraw->box.x0 +
                    ((redraw->box.x1 - redraw->box.x0 -
                      (target_width << x_eigen)) / 2);
                y = redraw->box.y0 +
                    ((redraw->box.y1 - redraw->box.y0 -
                      (target_height << y_eigen)) / 2);

                error = xosspriteop_put_sprite_scaled(
                    osspriteop_PTR,
                    browser->image_sprite_area,
                    (osspriteop_id) browser->image_sprite,
                    x,
                    y,
                    os_ACTION_OVERWRITE,
                    &factors,
                    NULL
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
    if (browser == NULL || !browser->created) {
        return;
    }

    (void) xwimp_delete_window(browser->handle);
    free(browser->image_sprite_area);
    browser->image_sprite_area = NULL;
    browser->image_sprite = NULL;
    browser->image_width = 0;
    browser->image_height = 0;
    browser->created = false;
}
