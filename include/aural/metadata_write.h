#ifndef AURAL_METADATA_WRITE_H
#define AURAL_METADATA_WRITE_H

#include "aural/track_entry.h"

typedef enum aural_metadata_write_result {
    AURAL_METADATA_WRITE_OK = 0,
    AURAL_METADATA_WRITE_UNSUPPORTED_FORMAT,
    AURAL_METADATA_WRITE_UNSUPPORTED_TAG,
    AURAL_METADATA_WRITE_IO_ERROR,
    AURAL_METADATA_WRITE_NO_MEMORY
} aural_metadata_write_result;

aural_metadata_write_result aural_metadata_write_file(
    const aural_track_entry *track
);

#endif
