#ifndef AURAL_AUDIO_PROBE_H
#define AURAL_AUDIO_PROBE_H

#include <stdbool.h>
#include <stdint.h>

#include "aural/track_entry.h"

aural_audio_format aural_audio_format_from_file(
    uint32_t riscos_filetype,
    const char *leafname
);

bool aural_audio_probe_file(
    const char *path,
    const char *leafname,
    uint64_t size_bytes,
    uint32_t riscos_filetype,
    aural_track_entry *entry
);

#endif
