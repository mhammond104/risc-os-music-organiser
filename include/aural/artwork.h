#ifndef AURAL_ARTWORK_H
#define AURAL_ARTWORK_H

#include <stdbool.h>

#include "oslib/os.h"
#include "oslib/osspriteop.h"

typedef struct aural_artwork {
    osspriteop_area *area;
    osspriteop_header *sprite;
    int width;
    int height;
} aural_artwork;

bool aural_artwork_load(
    aural_artwork *artwork,
    const char *path,
    int maximum_width,
    int maximum_height
);
os_error *aural_artwork_plot(
    const aural_artwork *artwork,
    const os_box *box
);
void aural_artwork_destroy(aural_artwork *artwork);

#endif
