#ifndef IMGORG_IMAGE_ENTRY_H
#define IMGORG_IMAGE_ENTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IMGORG_LEAFNAME_CAPACITY 256
#define IMGORG_PATH_CAPACITY 1024

typedef enum imgorg_image_format {
    IMGORG_IMAGE_FORMAT_UNKNOWN = 0,
    IMGORG_IMAGE_FORMAT_SPRITE,
    IMGORG_IMAGE_FORMAT_JPEG,
    IMGORG_IMAGE_FORMAT_PNG
} imgorg_image_format;

typedef struct imgorg_image_entry {
    char leafname[IMGORG_LEAFNAME_CAPACITY];
    char path[IMGORG_PATH_CAPACITY];
    uint64_t size_bytes;
    uint32_t load_addr;
    uint32_t exec_addr;
    uint32_t riscos_filetype;
    imgorg_image_format format;
    bool selected;
} imgorg_image_entry;

imgorg_image_format imgorg_image_format_from_filetype(uint32_t filetype);
bool imgorg_image_entry_init(
    imgorg_image_entry *entry,
    const char *path,
    const char *leafname,
    uint64_t size_bytes,
    uint32_t load_addr,
    uint32_t exec_addr,
    uint32_t riscos_filetype
);

#endif
