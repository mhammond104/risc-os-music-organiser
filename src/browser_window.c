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
#include "oslib/wimpspriteop.h"
#include "imgorg/thumbnail_cache.h"

enum {
    BROWSER_VISIBLE_WIDTH = 960,
    BROWSER_VISIBLE_HEIGHT = 640
};

enum {
    MAXIMUM_PNG_SIZE = 64 * 1024 * 1024,
    MAXIMUM_JPEG_SIZE = 64 * 1024 * 1024,
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
    THUMBNAIL_MAXIMUM_HEIGHT = 112,
    LOADING_WINDOW_WIDTH = 360,
    LOADING_WINDOW_HEIGHT = 120
};

static os_error browser_error;

typedef struct imgorg_jpeg_error_state {
    struct jpeg_error_mgr manager;
    jmp_buf escape;
} imgorg_jpeg_error_state;

static void imgorg_browser_window_jpeg_error_exit(j_common_ptr common)
{
    imgorg_jpeg_error_state *error =
        (imgorg_jpeg_error_state *) common->err;
    longjmp(error->escape, 1);
}

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
    memcpy(sprite->name, "imgorg", 7);
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
    browser->thumbnail_priority_end = THUMBNAIL_COLUMNS * 3;
}

static os_error *imgorg_browser_window_show_image(
    imgorg_browser_window *browser,
    const char *file_name,
    osspriteop_area *new_area,
    int width,
    int height,
    bool preserve_directory
)
{
    os_error *error;

    imgorg_browser_window_copy_leafname(
        browser->image_name,
        sizeof(browser->image_name),
        file_name
    );
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
    browser->dragging = false;
    browser->return_to_directory = preserve_directory;
    if (!preserve_directory) {
        browser->directory_name[0] = '\0';
        browser->directory_path[0] = '\0';
        browser->scanner.active = false;
        imgorg_browser_window_clear_thumbnails(browser);
        imgorg_image_list_clear(&browser->images);
    }

    if (!browser->created) {
        return NULL;
    }
    error = imgorg_browser_window_update_title(browser);
    if (error != NULL) {
        return error;
    }
    return imgorg_browser_window_redraw_all(browser);
}

static void imgorg_browser_window_set_thumbnail_priority(
    imgorg_browser_window *browser,
    int yscroll,
    int visible_height
)
{
    const int row_height = THUMBNAIL_CELL_HEIGHT + THUMBNAIL_GAP;
    size_t first_row;
    size_t visible_rows;

    if (yscroll > 0) {
        yscroll = 0;
    }
    first_row = (size_t) (-yscroll / row_height);
    visible_rows = (size_t) ((visible_height + row_height - 1) / row_height);
    browser->thumbnail_priority_start = first_row * THUMBNAIL_COLUMNS;
    browser->thumbnail_priority_end =
        (first_row + visible_rows + 1) * THUMBNAIL_COLUMNS;
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
    size_t priority_end = browser->thumbnail_priority_end;

    if (priority_end > browser->images.count) {
        priority_end = browser->images.count;
    }
    for (index = browser->thumbnail_priority_start;
         index < priority_end;
         ++index) {
        const imgorg_image_entry *entry;

        if (browser->thumbnails[index].attempted) {
            continue;
        }
        entry = imgorg_image_list_get(&browser->images, index);
        if (entry->format == IMGORG_IMAGE_FORMAT_PNG ||
            entry->format == IMGORG_IMAGE_FORMAT_JPEG) {
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
            entry->format == IMGORG_IMAGE_FORMAT_JPEG) {
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

    definition.work_flags =
        wimp_BUTTON_DOUBLE_CLICK_DRAG << wimp_ICON_BUTTON_TYPE_SHIFT;

    definition.sprite_area = wimpspriteop_AREA;
    definition.xmin = 320;
    definition.ymin = 240;

    definition.title_data.indirected_text.text = browser->title;
    definition.title_data.indirected_text.validation = (char *) -1;
    definition.title_data.indirected_text.size = sizeof(browser->title);

    definition.icon_count = 0;

    error = xwimp_create_window(&definition, &browser->handle);
    if (error != NULL) {
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
        "Image Org"
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
        return error;
    }
    browser->loading_created = true;

    return NULL;
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

static os_error *imgorg_browser_window_show_loading(
    imgorg_browser_window *browser
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
    parent.w = browser->handle;
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

    if (handled != NULL) {
        *handled = false;
    }

    if (browser == NULL || open == NULL || handled == NULL ||
        open->w != browser->handle) {
        return NULL;
    }

    if (browser->image_sprite_area == NULL) {
        if (browser->directory_path[0] != '\0') {
            imgorg_browser_window_set_thumbnail_priority(
                browser,
                open->yscroll,
                open->visible.y1 - open->visible.y0
            );
        }
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

os_error *imgorg_browser_window_handle_close_request(
    imgorg_browser_window *browser,
    wimp_w window,
    bool *handled
)
{
    os_error *error;

    if (handled != NULL) {
        *handled = false;
    }
    if (browser == NULL || handled == NULL || window != browser->handle ||
        browser->image_sprite_area == NULL ||
        !browser->return_to_directory) {
        return NULL;
    }

    free(browser->image_sprite_area);
    browser->image_sprite_area = NULL;
    browser->image_sprite = NULL;
    browser->image_width = 0;
    browser->image_height = 0;
    browser->image_name[0] = '\0';
    browser->dragging = false;
    browser->return_to_directory = false;
    *handled = true;

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

static os_error *imgorg_browser_window_load_png_mode(
    imgorg_browser_window *browser,
    const char *file_name,
    bool preserve_directory
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
        file_name,
        new_area,
        width,
        height,
        preserve_directory
    );
}

os_error *imgorg_browser_window_load_png(
    imgorg_browser_window *browser,
    const char *file_name
)
{
    return imgorg_browser_window_load_png_mode(browser, file_name, false);
}

static os_error *imgorg_browser_window_load_jpeg_mode(
    imgorg_browser_window *browser,
    const char *file_name,
    bool preserve_directory
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
        file_name,
        new_area,
        width,
        height,
        preserve_directory
    );
}

os_error *imgorg_browser_window_load_jpeg(
    imgorg_browser_window *browser,
    const char *file_name
)
{
    return imgorg_browser_window_load_jpeg_mode(browser, file_name, false);
}

static os_error *imgorg_browser_window_load_image_mode(
    imgorg_browser_window *browser,
    const char *file_name,
    imgorg_image_format format,
    bool preserve_directory
)
{
    os_error *error;

    if (browser == NULL) {
        return imgorg_browser_window_error("No browser was supplied");
    }
    (void) imgorg_browser_window_show_loading(browser);
    if (format == IMGORG_IMAGE_FORMAT_PNG) {
        error = imgorg_browser_window_load_png_mode(
            browser,
            file_name,
            preserve_directory
        );
    } else if (format == IMGORG_IMAGE_FORMAT_JPEG) {
        error = imgorg_browser_window_load_jpeg_mode(
            browser,
            file_name,
            preserve_directory
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
        false
    );
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
    browser->return_to_directory = false;
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
    size_t column;
    size_t row;
    size_t index;

    state.w = browser->handle;
    if (xwimp_get_window_state(&state) != NULL) {
        return false;
    }
    work_x = pointer->pos.x - state.visible.x0 + state.xscroll;
    work_y = pointer->pos.y - state.visible.y1 + state.yscroll;
    cell_x = work_x - THUMBNAIL_MARGIN;
    cell_y = -work_y - THUMBNAIL_MARGIN;
    if (cell_x < 0 || cell_y < 0) {
        return false;
    }

    column = (size_t) cell_x /
        (THUMBNAIL_CELL_WIDTH + THUMBNAIL_GAP);
    row = (size_t) cell_y /
        (THUMBNAIL_CELL_HEIGHT + THUMBNAIL_GAP);
    if (column >= THUMBNAIL_COLUMNS ||
        cell_x % (THUMBNAIL_CELL_WIDTH + THUMBNAIL_GAP) >=
            THUMBNAIL_CELL_WIDTH ||
        cell_y % (THUMBNAIL_CELL_HEIGHT + THUMBNAIL_GAP) >=
            THUMBNAIL_CELL_HEIGHT) {
        return false;
    }
    index = row * THUMBNAIL_COLUMNS + column;
    if (index >= browser->images.count) {
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

os_error *imgorg_browser_window_handle_pointer(
    imgorg_browser_window *browser,
    const wimp_pointer *pointer
)
{
    wimp_window_state state;
    wimp_drag drag;
    os_error *error;

    if (browser == NULL || pointer == NULL ||
        pointer->w != browser->handle) {
        return NULL;
    }

    if (browser->image_sprite_area == NULL) {
        size_t index;
        const imgorg_image_entry *entry;
        bool selection_changed;

        if (browser->directory_path[0] == '\0' ||
            (pointer->buttons != wimp_SINGLE_SELECT &&
             pointer->buttons != wimp_DOUBLE_SELECT)) {
            return NULL;
        }
        if (!imgorg_browser_window_thumbnail_at_pointer(
                browser,
                pointer,
                &index
            )) {
            if (pointer->buttons == wimp_SINGLE_SELECT &&
                imgorg_browser_window_select_thumbnail(browser, SIZE_MAX)) {
                return imgorg_browser_window_redraw_all(browser);
            }
            return NULL;
        }
        selection_changed = imgorg_browser_window_select_thumbnail(
            browser,
            index
        );
        if (pointer->buttons == wimp_SINGLE_SELECT) {
            return selection_changed ?
                imgorg_browser_window_redraw_all(browser) : NULL;
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
            true
        );
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
        imgorg_browser_window_set_thumbnail_priority(
            browser,
            open.yscroll,
            visible_height
        );
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
            entry->selected ? os_COLOUR_LIGHT_BLUE :
                os_COLOUR_MID_LIGHT_GREY,
            0,
            os_ACTION_OVERWRITE,
            NULL
        );
        imgorg_browser_window_fill_box(&cell);

        inner = cell;
        inner.x0 += entry->selected ? 6 : 2;
        inner.y0 += entry->selected ? 6 : 2;
        inner.x1 -= entry->selected ? 6 : 2;
        inner.y1 -= entry->selected ? 6 : 2;
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

    if (browser->loading_created) {
        (void) xwimp_delete_window(browser->loading_handle);
        browser->loading_handle = 0;
        browser->loading_created = false;
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
