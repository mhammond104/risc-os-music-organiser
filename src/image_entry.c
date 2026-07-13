#include "imgorg/image_entry.h"

#include <stdio.h>
#include <string.h>

enum {
    RISCOS_FILETYPE_SPRITE = 0xFF9,
    RISCOS_FILETYPE_JPEG = 0xC85,
    RISCOS_FILETYPE_PNG = 0xB60
};

imgorg_image_format imgorg_image_format_from_filetype(uint32_t filetype)
{
    switch (filetype & 0xFFFu) {
    case RISCOS_FILETYPE_SPRITE:
        return IMGORG_IMAGE_FORMAT_SPRITE;
    case RISCOS_FILETYPE_JPEG:
        return IMGORG_IMAGE_FORMAT_JPEG;
    case RISCOS_FILETYPE_PNG:
        return IMGORG_IMAGE_FORMAT_PNG;
    default:
        return IMGORG_IMAGE_FORMAT_UNKNOWN;
    }
}

bool imgorg_image_entry_init(
    imgorg_image_entry *entry,
    const char *path,
    const char *leafname,
    uint64_t size_bytes,
    uint32_t riscos_filetype
)
{
    int path_length;
    int leafname_length;

    if (entry == NULL || path == NULL || leafname == NULL) {
        return false;
    }

    path_length = snprintf(entry->path, sizeof(entry->path), "%s", path);
    leafname_length = snprintf(
        entry->leafname,
        sizeof(entry->leafname),
        "%s",
        leafname
    );

    if (path_length < 0 || (size_t) path_length >= sizeof(entry->path) ||
        leafname_length < 0 ||
        (size_t) leafname_length >= sizeof(entry->leafname)) {
        memset(entry, 0, sizeof(*entry));
        return false;
    }

    entry->size_bytes = size_bytes;
    entry->riscos_filetype = riscos_filetype & 0xFFFu;
    entry->format = imgorg_image_format_from_filetype(riscos_filetype);
    entry->selected = false;

    return true;
}
