#include "aural/artwork.h"

#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>
#include <png.h>

enum {
    AURAL_ARTWORK_MAXIMUM_FILE_SIZE = 64 * 1024 * 1024,
    AURAL_ARTWORK_MAXIMUM_DIMENSION = 8192,
    AURAL_SPRITE_DPI = 90
};

typedef struct aural_jpeg_error {
    struct jpeg_error_mgr manager;
    jmp_buf escape;
} aural_jpeg_error;

static void aural_jpeg_error_exit(j_common_ptr decoder)
{
    aural_jpeg_error *error = (aural_jpeg_error *) decoder->err;

    longjmp(error->escape, 1);
}

static osspriteop_mode_word aural_artwork_sprite_mode(void)
{
    return osspriteop_NEW_STYLE |
        (AURAL_SPRITE_DPI << osspriteop_XRES_SHIFT) |
        (AURAL_SPRITE_DPI << osspriteop_YRES_SHIFT) |
        (osspriteop_TYPE32BPP << osspriteop_TYPE_SHIFT);
}

static osspriteop_area *aural_artwork_create_sprite(
    int width,
    int height
)
{
    size_t pixels;
    size_t size;
    osspriteop_area *area;
    osspriteop_header *sprite;

    if (width <= 0 || height <= 0 ||
        (size_t) width > SIZE_MAX / (size_t) height / 4) {
        return NULL;
    }
    pixels = (size_t) width * height * 4;
    size = sizeof(*area) + sizeof(*sprite) + pixels;
    if (size > INT_MAX) {
        return NULL;
    }
    area = calloc(1, size);
    if (area == NULL) {
        return NULL;
    }
    area->size = (int) size;
    area->sprite_count = 1;
    area->first = sizeof(*area);
    area->used = (int) size;
    sprite = (osspriteop_header *) ((unsigned char *) area + area->first);
    sprite->size = sizeof(*sprite) + (int) pixels;
    memcpy(sprite->name, "auralart", 8);
    sprite->width = width - 1;
    sprite->height = height - 1;
    sprite->left_bit = 0;
    sprite->right_bit = 31;
    sprite->image = sizeof(*sprite);
    sprite->mask = sizeof(*sprite);
    sprite->mode = (os_mode) aural_artwork_sprite_mode();
    return area;
}

static void aural_artwork_fit(
    int source_width,
    int source_height,
    int maximum_width,
    int maximum_height,
    int *width,
    int *height
)
{
    *width = source_width;
    *height = source_height;
    if (*width > maximum_width || *height > maximum_height) {
        if ((long long) *width * maximum_height >
            (long long) *height * maximum_width) {
            *height = (int) ((long long) *height *
                maximum_width / *width);
            *width = maximum_width;
        } else {
            *width = (int) ((long long) *width *
                maximum_height / *height);
            *height = maximum_height;
        }
    }
    if (*width < 1) {
        *width = 1;
    }
    if (*height < 1) {
        *height = 1;
    }
}

static bool aural_artwork_decode_png(
    const unsigned char *data,
    size_t size,
    int maximum_width,
    int maximum_height,
    aural_artwork *artwork
)
{
    png_image image;
    unsigned char *pixels;
    osspriteop_area *area;
    osspriteop_header *sprite;
    unsigned char *output;
    int width;
    int height;
    int y;

    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, size) ||
        image.width == 0 || image.height == 0 ||
        image.width > AURAL_ARTWORK_MAXIMUM_DIMENSION ||
        image.height > AURAL_ARTWORK_MAXIMUM_DIMENSION) {
        png_image_free(&image);
        return false;
    }
    image.format = PNG_FORMAT_RGBA;
    pixels = malloc(PNG_IMAGE_SIZE(image));
    if (pixels == NULL ||
        !png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        free(pixels);
        png_image_free(&image);
        return false;
    }
    aural_artwork_fit((int) image.width, (int) image.height,
        maximum_width, maximum_height, &width, &height);
    area = aural_artwork_create_sprite(width, height);
    if (area == NULL) {
        free(pixels);
        png_image_free(&image);
        return false;
    }
    sprite = (osspriteop_header *)
        ((unsigned char *) area + area->first);
    output = (unsigned char *) sprite + sprite->image;
    for (y = 0; y < height; ++y) {
        int source_y = (int) ((long long) y * image.height / height);
        int x;

        for (x = 0; x < width; ++x) {
            int source_x = (int) ((long long) x * image.width / width);
            unsigned char *source = pixels +
                (((size_t) source_y * image.width + source_x) * 4);
            unsigned char *destination = output +
                (((size_t) y * width + x) * 4);
            unsigned int alpha = source[3];

            destination[0] = (unsigned char) (
                (source[0] * alpha + 255u * (255u - alpha) + 127u) / 255u);
            destination[1] = (unsigned char) (
                (source[1] * alpha + 255u * (255u - alpha) + 127u) / 255u);
            destination[2] = (unsigned char) (
                (source[2] * alpha + 255u * (255u - alpha) + 127u) / 255u);
            destination[3] = 0;
        }
    }
    free(pixels);
    png_image_free(&image);
    artwork->area = area;
    artwork->sprite = sprite;
    artwork->width = width;
    artwork->height = height;
    return true;
}

static bool aural_artwork_decode_jpeg(
    FILE *file,
    int maximum_width,
    int maximum_height,
    aural_artwork *artwork
)
{
    struct jpeg_decompress_struct decoder;
    aural_jpeg_error error;
    volatile unsigned char *pixels = NULL;
    volatile bool created = false;
    int decoded_width;
    int decoded_height;
    int width;
    int height;
    osspriteop_area *area;
    osspriteop_header *sprite;
    unsigned char *output;
    int y;

    decoder.err = jpeg_std_error(&error.manager);
    error.manager.error_exit = aural_jpeg_error_exit;
    if (setjmp(error.escape) != 0) {
        if (created) {
            jpeg_destroy_decompress(&decoder);
        }
        free((void *) pixels);
        return false;
    }
    jpeg_create_decompress(&decoder);
    created = true;
    jpeg_stdio_src(&decoder, file);
    if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK ||
        decoder.image_width == 0 || decoder.image_height == 0 ||
        decoder.image_width > AURAL_ARTWORK_MAXIMUM_DIMENSION ||
        decoder.image_height > AURAL_ARTWORK_MAXIMUM_DIMENSION) {
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    decoder.scale_num = 1;
    decoder.scale_denom = 8;
    while (decoder.scale_denom > 1 &&
        decoder.image_width / decoder.scale_denom <
            (unsigned int) maximum_width &&
        decoder.image_height / decoder.scale_denom <
            (unsigned int) maximum_height) {
        decoder.scale_denom /= 2;
    }
    decoder.out_color_space = JCS_RGB;
    decoder.dct_method = JDCT_IFAST;
    decoder.do_fancy_upsampling = FALSE;
    if (!jpeg_start_decompress(&decoder)) {
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    decoded_width = (int) decoder.output_width;
    decoded_height = (int) decoder.output_height;
    if (decoder.output_components != 3 ||
        (size_t) decoded_width > SIZE_MAX / (size_t) decoded_height / 3) {
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    pixels = malloc((size_t) decoded_width * decoded_height * 3);
    if (pixels == NULL) {
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    while (decoder.output_scanline < decoder.output_height) {
        JSAMPROW row = (unsigned char *) pixels +
            (size_t) decoder.output_scanline * decoded_width * 3;

        if (jpeg_read_scanlines(&decoder, &row, 1) != 1) {
            free((void *) pixels);
            jpeg_destroy_decompress(&decoder);
            return false;
        }
    }
    (void) jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    created = false;
    aural_artwork_fit(decoded_width, decoded_height,
        maximum_width, maximum_height, &width, &height);
    area = aural_artwork_create_sprite(width, height);
    if (area == NULL) {
        free((void *) pixels);
        return false;
    }
    sprite = (osspriteop_header *)
        ((unsigned char *) area + area->first);
    output = (unsigned char *) sprite + sprite->image;
    for (y = 0; y < height; ++y) {
        int source_y = (int) ((long long) y * decoded_height / height);
        int x;

        for (x = 0; x < width; ++x) {
            int source_x = (int) ((long long) x * decoded_width / width);
            const unsigned char *source = (const unsigned char *) pixels +
                ((size_t) source_y * decoded_width + source_x) * 3;
            unsigned char *destination = output +
                ((size_t) y * width + x) * 4;

            destination[0] = source[0];
            destination[1] = source[1];
            destination[2] = source[2];
            destination[3] = 0;
        }
    }
    free((void *) pixels);
    artwork->area = area;
    artwork->sprite = sprite;
    artwork->width = width;
    artwork->height = height;
    return true;
}

bool aural_artwork_load(
    aural_artwork *artwork,
    const char *path,
    int maximum_width,
    int maximum_height
)
{
    static const unsigned char png_signature[] =
        {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
    unsigned char signature[8];
    FILE *file;
    long size;
    bool success = false;

    if (artwork == NULL || path == NULL) {
        return false;
    }
    memset(artwork, 0, sizeof(*artwork));
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) <= 0 ||
        size > AURAL_ARTWORK_MAXIMUM_FILE_SIZE ||
        fseek(file, 0, SEEK_SET) != 0 ||
        fread(signature, 1, sizeof(signature), file) != sizeof(signature) ||
        fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return false;
    }
    if (memcmp(signature, png_signature, sizeof(signature)) == 0) {
        unsigned char *data = malloc((size_t) size);

        if (data != NULL &&
            fread(data, 1, (size_t) size, file) == (size_t) size) {
            success = aural_artwork_decode_png(
                data, (size_t) size, maximum_width, maximum_height,
                artwork);
        }
        free(data);
    } else if (signature[0] == 0xFF && signature[1] == 0xD8) {
        success = aural_artwork_decode_jpeg(
            file, maximum_width, maximum_height, artwork);
    }
    fclose(file);
    return success;
}

os_error *aural_artwork_plot(
    const aural_artwork *artwork,
    const os_box *box
)
{
    int x_eigen = 1;
    int y_eigen = 1;
    int available_width;
    int available_height;
    int width;
    int height;
    int x;
    int y;
    os_factors factors;

    if (artwork == NULL || artwork->area == NULL) {
        return NULL;
    }
    (void) xos_read_mode_variable(
        os_CURRENT_MODE, os_MODEVAR_XEIG_FACTOR, &x_eigen, NULL);
    (void) xos_read_mode_variable(
        os_CURRENT_MODE, os_MODEVAR_YEIG_FACTOR, &y_eigen, NULL);
    available_width = (box->x1 - box->x0) >> x_eigen;
    available_height = (box->y1 - box->y0) >> y_eigen;
    if ((long long) available_width * artwork->height <=
        (long long) available_height * artwork->width) {
        width = available_width;
        height = (int) ((long long) artwork->height *
            width / artwork->width);
    } else {
        height = available_height;
        width = (int) ((long long) artwork->width *
            height / artwork->height);
    }
    x = box->x0 + (box->x1 - box->x0 - (width << x_eigen)) / 2;
    y = box->y0 + (box->y1 - box->y0 - (height << y_eigen)) / 2;
    factors = (os_factors) {
        width, height, artwork->width, artwork->height
    };
    return xosspriteop_put_sprite_scaled(
        osspriteop_PTR, artwork->area,
        (osspriteop_id) artwork->sprite, x, y,
        os_ACTION_OVERWRITE, &factors, NULL);
}

void aural_artwork_destroy(aural_artwork *artwork)
{
    if (artwork != NULL) {
        free(artwork->area);
        memset(artwork, 0, sizeof(*artwork));
    }
}
