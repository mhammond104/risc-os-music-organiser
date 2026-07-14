#ifndef IMGORG_THUMBNAIL_CACHE_H
#define IMGORG_THUMBNAIL_CACHE_H

#include <stdbool.h>

#include "imgorg/image_entry.h"
#include "oslib/osspriteop.h"

osspriteop_area *imgorg_thumbnail_cache_load(
    const imgorg_image_entry *entry,
    int *width_out,
    int *height_out
);
bool imgorg_thumbnail_cache_save(
    const imgorg_image_entry *entry,
    const osspriteop_area *area,
    int width,
    int height
);

#endif
