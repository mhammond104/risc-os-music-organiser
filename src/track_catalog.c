#include "aural/track_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    AURAL_CATALOG_VERSION = 2,
    AURAL_CATALOG_MAXIMUM_SOURCES = 4096,
    AURAL_CATALOG_MAXIMUM_TRACKS = 200000
};

static const unsigned char aural_catalog_magic[8] = {
    'A', 'U', 'R', 'A', 'L', 'L', 'I', 'B'
};

void aural_track_list_init(aural_track_list *tracks)
{
    if (tracks != NULL) {
        tracks->items = NULL;
        tracks->count = 0;
        tracks->capacity = 0;
    }
}

void aural_track_list_destroy(aural_track_list *tracks)
{
    if (tracks == NULL) {
        return;
    }
    free(tracks->items);
    aural_track_list_init(tracks);
}

size_t aural_track_list_find_path(
    const aural_track_list *tracks,
    const char *path
)
{
    size_t index;

    if (tracks == NULL || path == NULL) {
        return SIZE_MAX;
    }
    for (index = 0; index < tracks->count; ++index) {
        if (strcmp(tracks->items[index].path, path) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

bool aural_track_list_append_unique(
    aural_track_list *tracks,
    const aural_track_entry *entry,
    bool *added
)
{
    aural_track_entry *items;
    size_t capacity;

    if (added != NULL) {
        *added = false;
    }
    if (tracks == NULL || entry == NULL || added == NULL ||
        entry->path[0] == '\0') {
        return false;
    }
    if (aural_track_list_find_path(tracks, entry->path) != SIZE_MAX) {
        return true;
    }
    if (tracks->count == tracks->capacity) {
        capacity = tracks->capacity == 0 ? 32 : tracks->capacity * 2;
        if (capacity < tracks->capacity ||
            capacity > SIZE_MAX / sizeof(*items)) {
            return false;
        }
        items = realloc(tracks->items, capacity * sizeof(*items));
        if (items == NULL) {
            return false;
        }
        tracks->items = items;
        tracks->capacity = capacity;
    }
    tracks->items[tracks->count++] = *entry;
    *added = true;
    return true;
}

bool aural_track_list_remove_at(aural_track_list *tracks, size_t index)
{
    if (tracks == NULL || index >= tracks->count) {
        return false;
    }
    if (index + 1 < tracks->count) {
        memmove(
            &tracks->items[index],
            &tracks->items[index + 1],
            (tracks->count - index - 1) * sizeof(*tracks->items)
        );
    }
    --tracks->count;
    return true;
}

void aural_source_list_init(aural_source_list *sources)
{
    if (sources != NULL) {
        sources->items = NULL;
        sources->count = 0;
        sources->capacity = 0;
    }
}

void aural_source_list_destroy(aural_source_list *sources)
{
    if (sources == NULL) {
        return;
    }
    free(sources->items);
    aural_source_list_init(sources);
}

bool aural_source_list_add(
    aural_source_list *sources,
    const char *path,
    bool *added
)
{
    char (*items)[AURAL_PATH_CAPACITY];
    size_t capacity;
    size_t index;
    int written;

    if (added != NULL) {
        *added = false;
    }
    if (sources == NULL || path == NULL || path[0] == '\0' ||
        added == NULL) {
        return false;
    }
    for (index = 0; index < sources->count; ++index) {
        if (strcmp(sources->items[index], path) == 0) {
            return true;
        }
    }
    if (sources->count == sources->capacity) {
        capacity = sources->capacity == 0 ? 8 : sources->capacity * 2;
        if (capacity < sources->capacity ||
            capacity > SIZE_MAX / sizeof(*items)) {
            return false;
        }
        items = realloc(sources->items, capacity * sizeof(*items));
        if (items == NULL) {
            return false;
        }
        sources->items = items;
        sources->capacity = capacity;
    }
    written = snprintf(
        sources->items[sources->count],
        AURAL_PATH_CAPACITY,
        "%s",
        path
    );
    if (written < 0 || written >= AURAL_PATH_CAPACITY) {
        return false;
    }
    ++sources->count;
    *added = true;
    return true;
}

bool aural_source_list_remove_at(aural_source_list *sources, size_t index)
{
    if (sources == NULL || index >= sources->count) {
        return false;
    }
    if (index + 1 < sources->count) {
        memmove(
            &sources->items[index],
            &sources->items[index + 1],
            (sources->count - index - 1) * sizeof(*sources->items)
        );
    }
    --sources->count;
    return true;
}

static bool aural_catalog_write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char) value;
    bytes[1] = (unsigned char) (value >> 8);
    bytes[2] = (unsigned char) (value >> 16);
    bytes[3] = (unsigned char) (value >> 24);
    return fwrite(bytes, sizeof(bytes), 1, file) == 1;
}

static bool aural_catalog_write_u64(FILE *file, uint64_t value)
{
    return aural_catalog_write_u32(file, (uint32_t) value) &&
        aural_catalog_write_u32(file, (uint32_t) (value >> 32));
}

static bool aural_catalog_read_u32(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];

    if (fread(bytes, sizeof(bytes), 1, file) != 1) {
        return false;
    }
    *value = (uint32_t) bytes[0] |
        ((uint32_t) bytes[1] << 8) |
        ((uint32_t) bytes[2] << 16) |
        ((uint32_t) bytes[3] << 24);
    return true;
}

static bool aural_catalog_read_u64(FILE *file, uint64_t *value)
{
    uint32_t low;
    uint32_t high;

    if (!aural_catalog_read_u32(file, &low) ||
        !aural_catalog_read_u32(file, &high)) {
        return false;
    }
    *value = (uint64_t) low | ((uint64_t) high << 32);
    return true;
}

static bool aural_catalog_write_string(FILE *file, const char *text)
{
    size_t length = strlen(text);

    return length <= UINT32_MAX &&
        aural_catalog_write_u32(file, (uint32_t) length) &&
        (length == 0 || fwrite(text, length, 1, file) == 1);
}

static bool aural_catalog_read_string(
    FILE *file,
    char *text,
    size_t capacity
)
{
    uint32_t length;

    if (!aural_catalog_read_u32(file, &length) ||
        length >= capacity) {
        return false;
    }
    if (length > 0 && fread(text, length, 1, file) != 1) {
        return false;
    }
    text[length] = '\0';
    return true;
}

static bool aural_catalog_write_track(
    FILE *file,
    const aural_track_entry *track
)
{
    return aural_catalog_write_string(file, track->path) &&
        aural_catalog_write_string(file, track->leafname) &&
        aural_catalog_write_string(file, track->title) &&
        aural_catalog_write_string(file, track->artist) &&
        aural_catalog_write_string(file, track->album) &&
        aural_catalog_write_string(file, track->album_artist) &&
        aural_catalog_write_string(file, track->genre) &&
        aural_catalog_write_string(file, track->comment) &&
        aural_catalog_write_string(file, track->tags) &&
        aural_catalog_write_string(file, track->artwork_path) &&
        aural_catalog_write_u64(file, track->size_bytes) &&
        aural_catalog_write_u64(file, track->duration_ms) &&
        aural_catalog_write_u64(file, track->date_added_cs) &&
        aural_catalog_write_u32(file, track->riscos_filetype) &&
        aural_catalog_write_u32(file, track->sample_rate_hz) &&
        aural_catalog_write_u32(file, track->bitrate_bps) &&
        aural_catalog_write_u32(file, track->track_number) &&
        aural_catalog_write_u32(file, track->track_total) &&
        aural_catalog_write_u32(file, track->disc_number) &&
        aural_catalog_write_u32(file, track->disc_total) &&
        aural_catalog_write_u32(file, track->year) &&
        aural_catalog_write_u32(file, track->channels) &&
        aural_catalog_write_u32(file, track->rating) &&
        aural_catalog_write_u32(file, (uint32_t) track->format) &&
        aural_catalog_write_u32(file, track->favourite ? 1u : 0u);
}

static bool aural_catalog_read_track(
    FILE *file,
    aural_track_entry *track,
    uint32_t version
)
{
    uint32_t format;
    uint32_t favourite;

    memset(track, 0, sizeof(*track));
    if (!aural_catalog_read_string(
            file, track->path, sizeof(track->path)) ||
        !aural_catalog_read_string(
            file, track->leafname, sizeof(track->leafname)) ||
        !aural_catalog_read_string(
            file, track->title, sizeof(track->title)) ||
        !aural_catalog_read_string(
            file, track->artist, sizeof(track->artist)) ||
        !aural_catalog_read_string(
            file, track->album, sizeof(track->album)) ||
        !aural_catalog_read_string(
            file, track->album_artist, sizeof(track->album_artist)) ||
        !aural_catalog_read_string(
            file, track->genre, sizeof(track->genre)) ||
        !aural_catalog_read_string(
            file, track->comment, sizeof(track->comment)) ||
        !aural_catalog_read_string(
            file, track->tags, sizeof(track->tags)) ||
        (version >= 2 && !aural_catalog_read_string(
            file, track->artwork_path, sizeof(track->artwork_path))) ||
        !aural_catalog_read_u64(file, &track->size_bytes) ||
        !aural_catalog_read_u64(file, &track->duration_ms) ||
        !aural_catalog_read_u64(file, &track->date_added_cs) ||
        !aural_catalog_read_u32(file, &track->riscos_filetype) ||
        !aural_catalog_read_u32(file, &track->sample_rate_hz) ||
        !aural_catalog_read_u32(file, &track->bitrate_bps) ||
        !aural_catalog_read_u32(file, &track->track_number) ||
        !aural_catalog_read_u32(file, &track->track_total) ||
        !aural_catalog_read_u32(file, &track->disc_number) ||
        !aural_catalog_read_u32(file, &track->disc_total) ||
        !aural_catalog_read_u32(file, &track->year) ||
        !aural_catalog_read_u32(file, &track->channels) ||
        !aural_catalog_read_u32(file, &track->rating) ||
        !aural_catalog_read_u32(file, &format) ||
        !aural_catalog_read_u32(file, &favourite)) {
        return false;
    }
    if (track->path[0] == '\0' ||
        track->rating > 5 ||
        format > AURAL_AUDIO_FORMAT_MIDI ||
        favourite > 1) {
        return false;
    }
    track->riscos_filetype &= 0xFFFu;
    track->format = (aural_audio_format) format;
    track->favourite = favourite != 0;
    track->selected = false;
    return true;
}

bool aural_track_catalog_save(
    const char *file_name,
    const aural_source_list *sources,
    const aural_track_list *tracks
)
{
    char temporary[AURAL_PATH_CAPACITY + 8];
    FILE *file;
    size_t index;
    bool success;
    int temporary_length;

    if (file_name == NULL || sources == NULL || tracks == NULL ||
        sources->count > UINT32_MAX || tracks->count > UINT32_MAX) {
        return false;
    }
    temporary_length = snprintf(
        temporary, sizeof(temporary), "%sTmp", file_name);
    if (temporary_length < 0 ||
        (size_t) temporary_length >= sizeof(temporary)) {
        return false;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        return false;
    }
    success =
        fwrite(aural_catalog_magic, sizeof(aural_catalog_magic), 1, file) ==
            1 &&
        aural_catalog_write_u32(file, AURAL_CATALOG_VERSION) &&
        aural_catalog_write_u32(file, (uint32_t) sources->count) &&
        aural_catalog_write_u32(file, (uint32_t) tracks->count);
    for (index = 0; success && index < sources->count; ++index) {
        success = aural_catalog_write_string(file, sources->items[index]);
    }
    for (index = 0; success && index < tracks->count; ++index) {
        success = aural_catalog_write_track(file, &tracks->items[index]);
    }
    success = fclose(file) == 0 && success;
    if (!success) {
        (void) remove(temporary);
        return false;
    }
    (void) remove(file_name);
    if (rename(temporary, file_name) != 0) {
        (void) remove(temporary);
        return false;
    }
    return true;
}

bool aural_track_catalog_load(
    const char *file_name,
    aural_source_list *sources,
    aural_track_list *tracks
)
{
    unsigned char magic[sizeof(aural_catalog_magic)];
    aural_source_list loaded_sources;
    aural_track_list loaded_tracks;
    uint32_t version;
    uint32_t source_count;
    uint32_t track_count;
    uint32_t index;
    FILE *file;
    bool success;

    if (file_name == NULL || sources == NULL || tracks == NULL) {
        return false;
    }
    file = fopen(file_name, "rb");
    if (file == NULL) {
        return true;
    }
    aural_source_list_init(&loaded_sources);
    aural_track_list_init(&loaded_tracks);
    success =
        fread(magic, sizeof(magic), 1, file) == 1 &&
        memcmp(magic, aural_catalog_magic, sizeof(magic)) == 0 &&
        aural_catalog_read_u32(file, &version) &&
        (version == 1 || version == AURAL_CATALOG_VERSION) &&
        aural_catalog_read_u32(file, &source_count) &&
        source_count <= AURAL_CATALOG_MAXIMUM_SOURCES &&
        aural_catalog_read_u32(file, &track_count) &&
        track_count <= AURAL_CATALOG_MAXIMUM_TRACKS;
    for (index = 0; success && index < source_count; ++index) {
        char path[AURAL_PATH_CAPACITY];
        bool added;

        success = aural_catalog_read_string(file, path, sizeof(path)) &&
            aural_source_list_add(&loaded_sources, path, &added) &&
            added;
    }
    for (index = 0; success && index < track_count; ++index) {
        aural_track_entry track;
        bool added;

        success = aural_catalog_read_track(file, &track, version) &&
            aural_track_list_append_unique(
                &loaded_tracks, &track, &added) &&
            added;
    }
    success = fclose(file) == 0 && success;
    if (!success) {
        aural_source_list_destroy(&loaded_sources);
        aural_track_list_destroy(&loaded_tracks);
        return false;
    }
    aural_source_list_destroy(sources);
    aural_track_list_destroy(tracks);
    *sources = loaded_sources;
    *tracks = loaded_tracks;
    return true;
}
