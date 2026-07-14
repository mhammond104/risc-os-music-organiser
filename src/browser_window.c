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
    SPRITE_DPI = 90,
    MINIMUM_ZOOM_PERCENT = 10,
    MAXIMUM_ZOOM_PERCENT = 800
};

enum {
    THUMBNAIL_COLUMNS = 3,
    THUMBNAIL_CELL_WIDTH = 280,
    THUMBNAIL_CELL_HEIGHT = 240,
    THUMBNAIL_GAP = 24,
    THUMBNAIL_MARGIN = 32,
    THUMBNAIL_IMAGE_INSET = 16,
    THUMBNAIL_LABEL_HEIGHT = 48,
    THUMBNAIL_MAXIMUM_WIDTH = 160,
    THUMBNAIL_MAXIMUM_HEIGHT = 112
};

static os_error browser_error;

static os_error *imgorg_browser_window_update_drag(
    const imgorg_browser_window *browser
);

static int imgorg_browser_window_directory_extent_y0(
    const imgorg_browser_window *browser
)
{
    size_t rows = (browser->images.count + THUMBNAIL_COLUMNS - 1) /
        THUMBNAIL_COLUMNS;
    int height = THUMBNAIL_MARGIN + (int) rows *
        (THUMBNAIL_CELL_HEIGHT + THUMBNAIL_GAP);

    if (height < BROWSER_VISIBLE_HEIGHT) {
        height = BROWSER_VISIBLE_HEIGHT;
    }
    return -height;
}

static os_error *imgorg_browser_window_update_directory_extent(
    const imgorg_browser_window *browser
)
{
    os_box extent;

    extent.x0 = 0;
    extent.y0 = imgorg_browser_window_directory_extent_y0(browser);
    extent.x1 = BROWSER_VISIBLE_WIDTH;
    extent.y1 = 0;
    return xwimp_set_extent(browser->handle, &extent);
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

static int imgorg_browser_window_fit_zoom(
    const imgorg_browser_window *browser,
    const os_box *visible
)
{
    int x_eigen;
    int y_eigen;
    int available_width;
    int available_height;
    int x_percent;
    int y_percent;

    if (browser->image_width <= 0 || browser->image_height <= 0) {
        return 100;
    }

    imgorg_browser_window_read_eigen_factors(&x_eigen, &y_eigen);
    available_width =
        (visible->x1 - visible->x0 - (2 * IMAGE_BORDER)) >> x_eigen;
    available_height =
        (visible->y1 - visible->y0 - (2 * IMAGE_BORDER)) >> y_eigen;

    if (available_width <= 0 || available_height <= 0) {
        return MINIMUM_ZOOM_PERCENT;
    }

    x_percent = (int) ((long long) available_width * 100 /
                       browser->image_width);
    y_percent = (int) ((long long) available_height * 100 /
                       browser->image_height);
    return x_percent < y_percent ? x_percent : y_percent;
}

static os_error *imgorg_browser_window_redraw_all(
    const imgorg_browser_window *browser
)
{
    return xwimp_force_redraw(browser->handle, 0, -2048, 2048, 0);
}

static os_error *imgorg_browser_window_update_title(
    imgorg_browser_window *browser
)
{
    if (browser->image_name[0] != '\0' && browser->fit_to_window) {
        snprintf(
            browser->title,
            sizeof(browser->title),
            "%s - Fit",
            browser->image_name
        );
    } else if (browser->image_name[0] != '\0') {
        snprintf(
            browser->title,
            sizeof(browser->title),
            "%s - %d%%",
            browser->image_name,
            browser->zoom_percent
        );
    } else if (browser->directory_name[0] != '\0') {
        if (browser->scanner.active) {
            snprintf(
                browser->title,
                sizeof(browser->title),
                "%s - %lu image%s - Scanning",
                browser->directory_name,
                (unsigned long) browser->images.count,
                browser->images.count == 1 ? "" : "s"
            );
        } else {
            snprintf(
                browser->title,
                sizeof(browser->title),
                "%s - %lu image%s",
                browser->directory_name,
                (unsigned long) browser->images.count,
                browser->images.count == 1 ? "" : "s"
            );
        }
    } else {
        snprintf(browser->title, sizeof(browser->title), "Image Organiser");
    }

    if (browser->created) {
        return xwimp_force_redraw_title(browser->handle);
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

static os_error *imgorg_browser_window_apply_zoom(
    imgorg_browser_window *browser,
    const os_box *visible,
    bool zoom_in
)
{
    os_error *error;
    int zoom = browser->fit_to_window ?
        imgorg_browser_window_fit_zoom(browser, visible) :
        browser->zoom_percent;

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

    browser->fit_to_window = false;
    browser->zoom_percent = zoom;
    error = imgorg_browser_window_update_title(browser);
    if (error != NULL) {
        return error;
    }
    return imgorg_browser_window_redraw_all(browser);
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
    memcpy(sprite->name, "imgorg", 7);
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

os_error *imgorg_browser_window_create(imgorg_browser_window *browser)
{
    wimp_window definition;
    os_error *error;

    if (browser == NULL) {
        return NULL;
    }

    memset(browser, 0, sizeof(*browser));
    imgorg_image_list_init(&browser->images);
    imgorg_directory_scanner_init(&browser->scanner);
    memset(&definition, 0, sizeof(definition));
    (void) imgorg_browser_window_update_title(browser);

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
        wimp_WINDOW_SCROLL |
        wimp_WINDOW_VSCROLL |
        wimp_WINDOW_NEW_FORMAT;

    definition.title_fg = wimp_COLOUR_BLACK;
    definition.title_bg = wimp_COLOUR_LIGHT_GREY;
    definition.work_fg = wimp_COLOUR_BLACK;
    definition.work_bg = wimp_COLOUR_WHITE;
    definition.scroll_outer = wimp_COLOUR_MID_LIGHT_GREY;
    definition.scroll_inner = wimp_COLOUR_VERY_LIGHT_GREY;
    definition.highlight_bg = wimp_COLOUR_CREAM;
    definition.extra_flags = wimp_WINDOW_USE_EXTENDED_SCROLL_REQUEST;

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

    definition.title_data.indirected_text.text = browser->title;
    definition.title_data.indirected_text.validation = (char *) -1;
    definition.title_data.indirected_text.size = sizeof(browser->title);

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

os_error *imgorg_browser_window_handle_open_request(
    imgorg_browser_window *browser,
    const wimp_open *open,
    bool *handled
)
{
    wimp_window_state state;
    os_error *error;

    if (handled != NULL) {
        *handled = false;
    }

    if (browser == NULL || open == NULL || handled == NULL ||
        open->w != browser->handle ||
        browser->image_sprite_area == NULL) {
        return NULL;
    }

    state.w = browser->handle;
    error = xwimp_get_window_state(&state);
    if (error != NULL) {
        return error;
    }

    if (memcmp(&open->visible, &state.visible, sizeof(open->visible)) == 0 &&
        open->xscroll == state.xscroll &&
        open->yscroll != state.yscroll) {
        *handled = true;
        return imgorg_browser_window_apply_zoom(
            browser,
            &state.visible,
            open->yscroll > state.yscroll
        );
    }

    return NULL;
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
        MAXIMUM_IMAGE_DIMENSION,
        MAXIMUM_IMAGE_DIMENSION,
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
    browser->fit_to_window = true;
    browser->zoom_percent = 100;
    browser->pan_x = 0;
    browser->pan_y = 0;
    browser->directory_name[0] = '\0';
    browser->directory_path[0] = '\0';
    browser->scanner.active = false;
    imgorg_browser_window_clear_thumbnails(browser);
    imgorg_image_list_clear(&browser->images);
    imgorg_browser_window_copy_leafname(
        browser->image_name,
        sizeof(browser->image_name),
        file_name
    );

    if (browser->created) {
        os_error *error = imgorg_browser_window_update_title(browser);
        if (error != NULL) {
            return error;
        }
        return imgorg_browser_window_redraw_all(browser);
    }

    return NULL;
}

os_error *imgorg_browser_window_load_directory(
    imgorg_browser_window *browser,
    const char *directory_path
)
{
    int path_length;
    os_error *error;

    if (browser == NULL || directory_path == NULL || directory_path[0] == '\0') {
        return imgorg_browser_window_error("No directory was supplied");
    }

    path_length = snprintf(
        browser->directory_path,
        sizeof(browser->directory_path),
        "%s",
        directory_path
    );
    if (path_length < 0 ||
        (size_t) path_length >= sizeof(browser->directory_path)) {
        browser->directory_path[0] = '\0';
        return imgorg_browser_window_error("The directory path is too long");
    }

    free(browser->image_sprite_area);
    browser->image_sprite_area = NULL;
    browser->image_sprite = NULL;
    browser->image_width = 0;
    browser->image_height = 0;
    browser->image_name[0] = '\0';
    browser->dragging = false;
    imgorg_browser_window_clear_thumbnails(browser);
    imgorg_image_list_clear(&browser->images);
    if (!imgorg_directory_scanner_start(&browser->scanner, directory_path)) {
        browser->directory_path[0] = '\0';
        return imgorg_browser_window_error("The directory path is too long");
    }
    imgorg_browser_window_copy_leafname(
        browser->directory_name,
        sizeof(browser->directory_name),
        directory_path
    );

    error = imgorg_browser_window_update_title(browser);
    if (error != NULL) {
        return error;
    }
    error = imgorg_browser_window_update_directory_extent(browser);
    if (error != NULL) {
        return error;
    }
    return imgorg_browser_window_redraw_all(browser);
}

bool imgorg_browser_window_has_background_work(
    const imgorg_browser_window *browser
)
{
    return browser != NULL && (browser->scanner.active ||
        browser->thumbnail_cursor < browser->images.count);
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
        if (!imgorg_browser_window_ensure_thumbnail_slots(browser)) {
            return imgorg_browser_window_error(
                "There is not enough memory for the thumbnail list"
            );
        }
        error = imgorg_browser_window_update_directory_extent(browser);
        if (error != NULL) {
            return error;
        }
        error = imgorg_browser_window_redraw_all(browser);
        if (error != NULL) {
            return error;
        }
    }

    while (browser->thumbnail_cursor < browser->images.count) {
        size_t index = browser->thumbnail_cursor++;
        const imgorg_image_entry *entry =
            imgorg_image_list_get(&browser->images, index);
        imgorg_thumbnail *thumbnail;

        if (!imgorg_browser_window_ensure_thumbnail_slots(browser)) {
            return imgorg_browser_window_error(
                "There is not enough memory for the thumbnail list"
            );
        }
        thumbnail = &browser->thumbnails[index];
        thumbnail->attempted = true;
        if (entry->format == IMGORG_IMAGE_FORMAT_PNG) {
            thumbnail->sprite_area =
                imgorg_browser_window_decode_png_file(
                    entry->path,
                    THUMBNAIL_MAXIMUM_WIDTH,
                    THUMBNAIL_MAXIMUM_HEIGHT,
                    &thumbnail->width,
                    &thumbnail->height
                );
            if (thumbnail->sprite_area != NULL) {
                thumbnail->sprite = (osspriteop_header *)
                    ((byte *) thumbnail->sprite_area +
                    thumbnail->sprite_area->first);
                thumbnail_completed = true;
            }
            break;
        }
    }

    if (thumbnail_completed) {
        error = imgorg_browser_window_redraw_all(browser);
        if (error != NULL) {
            return error;
        }
    }

    if (changed || was_active != browser->scanner.active) {
        return imgorg_browser_window_update_title(browser);
    }
    return NULL;
}

os_error *imgorg_browser_window_handle_pointer(
    imgorg_browser_window *browser,
    const wimp_pointer *pointer
)
{
    wimp_window_state state;
    wimp_drag drag;
    os_error *error;

    if (browser == NULL || pointer == NULL ||
        pointer->w != browser->handle ||
        browser->image_sprite_area == NULL) {
        return NULL;
    }

    if ((pointer->buttons & wimp_CLICK_ADJUST) != 0) {
        os_error *title_error;

        browser->fit_to_window = true;
        browser->pan_x = 0;
        browser->pan_y = 0;
        title_error = imgorg_browser_window_update_title(browser);
        if (title_error != NULL) {
            return title_error;
        }
        return imgorg_browser_window_redraw_all(browser);
    }

    if ((pointer->buttons &
         (wimp_CLICK_SELECT | wimp_DRAG_SELECT)) == 0) {
        return NULL;
    }

    if (browser->fit_to_window) {
        state.w = browser->handle;
        error = xwimp_get_window_state(&state);
        if (error != NULL) {
            return error;
        }
        browser->zoom_percent = imgorg_browser_window_fit_zoom(
            browser,
            &state.visible
        );
        browser->fit_to_window = false;
        error = imgorg_browser_window_update_title(browser);
        if (error != NULL) {
            return error;
        }
    }

    memset(&drag, 0, sizeof(drag));
    drag.w = browser->handle;
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
        browser->dragging = true;
        browser->drag_start = pointer->pos;
        browser->drag_pan_x = browser->pan_x;
        browser->drag_pan_y = browser->pan_y;
    }
    return error;
}

os_error *imgorg_browser_window_handle_drag_end(
    imgorg_browser_window *browser,
    const wimp_dragged *dragged
)
{
    if (browser == NULL || dragged == NULL || !browser->dragging) {
        return NULL;
    }

    browser->dragging = false;
    browser->pan_x = browser->drag_pan_x +
        dragged->final.x0 - browser->drag_start.x;
    browser->pan_y = browser->drag_pan_y +
        dragged->final.y0 - browser->drag_start.y;
    return imgorg_browser_window_update_drag(browser);
}

os_error *imgorg_browser_window_handle_drag_update(
    imgorg_browser_window *browser
)
{
    wimp_pointer pointer;
    os_error *error;
    int pan_x;
    int pan_y;

    if (browser == NULL || !browser->dragging) {
        return NULL;
    }

    error = xwimp_get_pointer_info(&pointer);
    if (error != NULL) {
        return error;
    }

    if ((pointer.buttons & wimp_CLICK_SELECT) == 0) {
        return NULL;
    }

    pan_x = browser->drag_pan_x + pointer.pos.x - browser->drag_start.x;
    pan_y = browser->drag_pan_y + pointer.pos.y - browser->drag_start.y;
    if (pan_x == browser->pan_x && pan_y == browser->pan_y) {
        return NULL;
    }

    browser->pan_x = pan_x;
    browser->pan_y = pan_y;
    return imgorg_browser_window_update_drag(browser);
}

os_error *imgorg_browser_window_handle_scroll(
    imgorg_browser_window *browser,
    const wimp_scroll *scroll
)
{
    bool zoom_in;

    if (browser == NULL || scroll == NULL ||
        scroll->w != browser->handle ||
        scroll->ymin == wimp_SCROLL_NONE) {
        return NULL;
    }

    if (browser->image_sprite_area == NULL) {
        wimp_open open;
        int visible_height;
        int minimum_scroll;
        int amount;

        if (browser->directory_path[0] == '\0') {
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
        return xwimp_open_window(&open);
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
        browser,
        &scroll->visible,
        zoom_in
    );
}

static os_error *imgorg_browser_window_plot_image(
    const imgorg_browser_window *browser,
    const wimp_draw *draw,
    os_box *image_box
)
{
    int x_eigen;
    int y_eigen;
    int available_width;
    int available_height;
    int target_width;
    int target_height;
    os_factors factors;

    imgorg_browser_window_read_eigen_factors(&x_eigen, &y_eigen);

    if (browser->fit_to_window) {
        available_width =
            (draw->box.x1 - draw->box.x0 - (2 * IMAGE_BORDER)) >> x_eigen;
        available_height =
            (draw->box.y1 - draw->box.y0 - (2 * IMAGE_BORDER)) >> y_eigen;

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
    } else {
        target_width = (int) (
            (long long) browser->image_width * browser->zoom_percent / 100
        );
        target_height = (int) (
            (long long) browser->image_height * browser->zoom_percent / 100
        );
    }

    if (target_width <= 0 || target_height <= 0) {
        memset(image_box, 0, sizeof(*image_box));
        return NULL;
    }

    factors.xmul = target_width;
    factors.ymul = target_height;
    factors.xdiv = browser->image_width;
    factors.ydiv = browser->image_height;

    image_box->x0 = draw->box.x0 +
        ((draw->box.x1 - draw->box.x0 -
          (target_width << x_eigen)) / 2) + browser->pan_x;
    image_box->y0 = draw->box.y0 +
        ((draw->box.y1 - draw->box.y0 -
          (target_height << y_eigen)) / 2) + browser->pan_y;
    image_box->x1 = image_box->x0 + (target_width << x_eigen);
    image_box->y1 = image_box->y0 + (target_height << y_eigen);

    return xosspriteop_put_sprite_scaled(
        osspriteop_PTR,
        browser->image_sprite_area,
        (osspriteop_id) browser->image_sprite,
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

static void imgorg_browser_window_make_label(
    char *label,
    size_t label_size,
    const char *leafname
)
{
    enum { MAXIMUM_LABEL_CHARACTERS = 29 };
    size_t length = strlen(leafname);

    if (length <= MAXIMUM_LABEL_CHARACTERS) {
        snprintf(label, label_size, "%s", leafname);
    } else {
        snprintf(
            label,
            label_size,
            "%.*s...",
            MAXIMUM_LABEL_CHARACTERS - 3,
            leafname
        );
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
    size_t index;
    os_error *error;

    error = xwimptextop_set_colour(os_COLOUR_BLACK, os_COLOUR_WHITE);
    if (error != NULL) {
        return error;
    }

    for (index = 0; index < browser->images.count; ++index) {
        const imgorg_image_entry *entry =
            imgorg_image_list_get(&browser->images, index);
        size_t row = index / THUMBNAIL_COLUMNS;
        size_t column = index % THUMBNAIL_COLUMNS;
        os_box cell;
        os_box inner;
        os_box preview;
        char label[40];

        cell.x0 = origin_x + THUMBNAIL_MARGIN + (int) column *
            (THUMBNAIL_CELL_WIDTH + THUMBNAIL_GAP);
        cell.x1 = cell.x0 + THUMBNAIL_CELL_WIDTH;
        cell.y1 = origin_y - THUMBNAIL_MARGIN - (int) row *
            (THUMBNAIL_CELL_HEIGHT + THUMBNAIL_GAP);
        cell.y0 = cell.y1 - THUMBNAIL_CELL_HEIGHT;
        if (!imgorg_browser_window_boxes_intersect(&cell, &draw->clip)) {
            continue;
        }

        (void) colourtrans_set_gcol(
            os_COLOUR_MID_LIGHT_GREY,
            0,
            os_ACTION_OVERWRITE,
            NULL
        );
        imgorg_browser_window_fill_box(&cell);

        inner = cell;
        inner.x0 += 2;
        inner.y0 += 2;
        inner.x1 -= 2;
        inner.y1 -= 2;
        (void) colourtrans_set_gcol(
            os_COLOUR_WHITE,
            0,
            os_ACTION_OVERWRITE,
            NULL
        );
        imgorg_browser_window_fill_box(&inner);

        preview.x0 = cell.x0 + THUMBNAIL_IMAGE_INSET;
        preview.x1 = cell.x1 - THUMBNAIL_IMAGE_INSET;
        preview.y0 = cell.y0 + THUMBNAIL_LABEL_HEIGHT;
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

        imgorg_browser_window_make_label(
            label,
            sizeof(label),
            entry->leafname
        );
        error = xwimptextop_paint(
            0,
            label,
            cell.x0 + THUMBNAIL_IMAGE_INSET,
            cell.y0 + 16
        );
        if (error != NULL) {
            return error;
        }
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
    const imgorg_browser_window *browser
)
{
    wimp_draw update;
    osbool more;
    os_error *error;

    memset(&update, 0, sizeof(update));
    update.w = browser->handle;
    update.box.x0 = 0;
    update.box.y0 = -2048;
    update.box.x1 = 2048;
    update.box.y1 = 0;

    error = xwimp_update_window(&update, &more);
    while (error == NULL && more) {
        os_box image_box;

        error = imgorg_browser_window_plot_image(browser, &update, &image_box);
        if (error == NULL) {
            imgorg_browser_window_clear_around_image(
                &update.box,
                &image_box
            );
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

    if (browser == NULL || redraw == NULL || redraw->w != browser->handle) {
        return NULL;
    }

    error = xwimp_redraw_window(redraw, &more);
    while (error == NULL && more) {
        if (browser->image_sprite_area != NULL) {
            os_box image_box;

            error = imgorg_browser_window_plot_image(
                browser,
                redraw,
                &image_box
            );
        } else if (browser->directory_path[0] != '\0') {
            error = imgorg_browser_window_plot_directory(browser, redraw);
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
    if (browser == NULL) {
        return;
    }

    if (browser->created) {
        (void) xwimp_delete_window(browser->handle);
    }
    free(browser->image_sprite_area);
    imgorg_browser_window_clear_thumbnails(browser);
    imgorg_image_list_destroy(&browser->images);
    imgorg_directory_scanner_init(&browser->scanner);
    browser->image_sprite_area = NULL;
    browser->image_sprite = NULL;
    browser->image_width = 0;
    browser->image_height = 0;
    browser->dragging = false;
    browser->created = false;
}
