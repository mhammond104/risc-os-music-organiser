#include "aural/track_entry.h"

#include <stdio.h>
#include <string.h>

bool aural_track_entry_init(
    aural_track_entry *entry,
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
    memset(entry, 0, sizeof(*entry));
    path_length = snprintf(entry->path, sizeof(entry->path), "%s", path);
    leafname_length = snprintf(
        entry->leafname,
        sizeof(entry->leafname),
        "%s",
        leafname
    );
    if (path_length < 0 ||
        (size_t) path_length >= sizeof(entry->path) ||
        leafname_length < 0 ||
        (size_t) leafname_length >= sizeof(entry->leafname)) {
        return false;
    }
    entry->size_bytes = size_bytes;
    entry->riscos_filetype = riscos_filetype & 0xFFFu;
    entry->format = AURAL_AUDIO_FORMAT_UNKNOWN;
    return true;
}

const char *aural_track_entry_display_title(
    const aural_track_entry *entry
)
{
    if (entry == NULL) {
        return "";
    }
    return entry->title[0] != '\0' ? entry->title : entry->leafname;
}
