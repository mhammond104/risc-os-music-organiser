#include "imgorg/thumbnail_cache.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oslib/osfile.h"

enum {
    CACHE_MAGIC = 0x54474D49,
    CACHE_VERSION = 1,
    MAXIMUM_CACHE_SPRITE_SIZE = 4 * 1024 * 1024
};

typedef struct imgorg_thumbnail_cache_header {
    uint32_t magic;
    uint32_t version;
    uint32_t load_addr;
    uint32_t exec_addr;
    uint32_t size_low;
    uint32_t size_high;
    uint32_t width;
    uint32_t height;
    uint32_t area_size;
    uint32_t hash_low;
    uint32_t hash_high;
} imgorg_thumbnail_cache_header;

static uint64_t imgorg_thumbnail_cache_hash(const imgorg_image_entry *entry)
{
    const unsigned char *cursor =
        (const unsigned char *) entry->path;
    uint64_t hash = UINT64_C(1469598103934665603);

    while (*cursor != '\0') {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    hash ^= entry->size_bytes;
    hash *= UINT64_C(1099511628211);
    hash ^= entry->load_addr;
    hash *= UINT64_C(1099511628211);
    hash ^= entry->exec_addr;
    hash *= UINT64_C(1099511628211);
    return hash;
}

static bool imgorg_thumbnail_cache_path(
    const imgorg_image_entry *entry,
    char *path,
    size_t path_size,
    uint64_t *hash_out
)
{
    uint64_t hash = imgorg_thumbnail_cache_hash(entry);
    int written = snprintf(
        path,
        path_size,
        "<Choices$Write>.ImgOrg.Thumbs.%08lX.%08lX",
        (unsigned long) (hash >> 32),
        (unsigned long) (hash & UINT32_MAX)
    );

    *hash_out = hash;
    return written >= 0 && (size_t) written < path_size;
}

static bool imgorg_thumbnail_cache_valid_area(
    const osspriteop_area *area,
    size_t area_size
)
{
    const osspriteop_header *sprite;

    if (area_size < sizeof(*area) + sizeof(*sprite) ||
        area_size > MAXIMUM_CACHE_SPRITE_SIZE ||
        area->size != (int) area_size || area->used != (int) area_size ||
        area->sprite_count != 1 ||
        area->first < (int) sizeof(*area) ||
        (size_t) area->first + sizeof(*sprite) > area_size) {
        return false;
    }
    sprite = (const osspriteop_header *)
        ((const byte *) area + area->first);
    return sprite->size >= (int) sizeof(*sprite) &&
        (size_t) area->first + (size_t) sprite->size <= area_size;
}

osspriteop_area *imgorg_thumbnail_cache_load(
    const imgorg_image_entry *entry,
    int *width_out,
    int *height_out
)
{
    char path[IMGORG_PATH_CAPACITY];
    uint64_t hash;
    imgorg_thumbnail_cache_header header;
    FILE *file;
    osspriteop_area *area;

    if (entry == NULL || width_out == NULL || height_out == NULL ||
        !imgorg_thumbnail_cache_path(entry, path, sizeof(path), &hash)) {
        return NULL;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fread(&header, sizeof(header), 1, file) != 1 ||
        header.magic != CACHE_MAGIC || header.version != CACHE_VERSION ||
        header.load_addr != entry->load_addr ||
        header.exec_addr != entry->exec_addr ||
        header.size_low != (uint32_t) entry->size_bytes ||
        header.size_high != (uint32_t) (entry->size_bytes >> 32) ||
        header.hash_low != (uint32_t) hash ||
        header.hash_high != (uint32_t) (hash >> 32) ||
        header.width == 0 || header.height == 0 ||
        header.width > INT_MAX || header.height > INT_MAX ||
        header.area_size < sizeof(osspriteop_area) ||
        header.area_size > MAXIMUM_CACHE_SPRITE_SIZE) {
        fclose(file);
        return NULL;
    }

    area = malloc(header.area_size);
    if (area == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(area, header.area_size, 1, file) != 1 ||
        !imgorg_thumbnail_cache_valid_area(area, header.area_size)) {
        free(area);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *width_out = (int) header.width;
    *height_out = (int) header.height;
    return area;
}

bool imgorg_thumbnail_cache_save(
    const imgorg_image_entry *entry,
    const osspriteop_area *area,
    int width,
    int height
)
{
    char path[IMGORG_PATH_CAPACITY];
    char bucket[IMGORG_PATH_CAPACITY];
    uint64_t hash;
    imgorg_thumbnail_cache_header header;
    FILE *file;
    int bucket_length;
    bool written;

    if (entry == NULL || area == NULL || width <= 0 || height <= 0 ||
        area->size <= 0 || area->size > MAXIMUM_CACHE_SPRITE_SIZE ||
        !imgorg_thumbnail_cache_valid_area(area, (size_t) area->size) ||
        !imgorg_thumbnail_cache_path(entry, path, sizeof(path), &hash)) {
        return false;
    }

    (void) xosfile_create_dir("<Choices$Write>.ImgOrg", 0);
    (void) xosfile_create_dir("<Choices$Write>.ImgOrg.Thumbs", 0);
    bucket_length = snprintf(
        bucket,
        sizeof(bucket),
        "<Choices$Write>.ImgOrg.Thumbs.%08lX",
        (unsigned long) (hash >> 32)
    );
    if (bucket_length < 0 || (size_t) bucket_length >= sizeof(bucket)) {
        return false;
    }
    (void) xosfile_create_dir(bucket, 0);

    memset(&header, 0, sizeof(header));
    header.magic = CACHE_MAGIC;
    header.version = CACHE_VERSION;
    header.load_addr = entry->load_addr;
    header.exec_addr = entry->exec_addr;
    header.size_low = (uint32_t) entry->size_bytes;
    header.size_high = (uint32_t) (entry->size_bytes >> 32);
    header.width = (uint32_t) width;
    header.height = (uint32_t) height;
    header.area_size = (uint32_t) area->size;
    header.hash_low = (uint32_t) hash;
    header.hash_high = (uint32_t) (hash >> 32);

    file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    written = fwrite(&header, sizeof(header), 1, file) == 1 &&
        fwrite(area, (size_t) area->size, 1, file) == 1;
    return fclose(file) == 0 && written;
}
