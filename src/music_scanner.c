#include "aural/music_scanner.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aural/audio_probe.h"
#include "oslib/fileswitch.h"
#include "oslib/osgbpb.h"

enum {
    AURAL_SCAN_BUFFER_SIZE = 8192,
    AURAL_SCAN_ENTRY_LIMIT = 24,
    AURAL_SCAN_MAXIMUM_DIRECTORIES = 100000
};

static os_error aural_scanner_error;

static os_error *aural_music_scanner_error(const char *message)
{
    aural_scanner_error.errnum = 0x80F102;
    snprintf(
        aural_scanner_error.errmess,
        sizeof(aural_scanner_error.errmess),
        "%s",
        message
    );
    return &aural_scanner_error;
}

static size_t aural_bounded_length(const char *text, size_t capacity)
{
    size_t length;

    for (length = 0;
         length < capacity && text[length] != '\0';
         ++length) {
    }
    return length;
}

static bool aural_make_path(
    char *destination,
    size_t capacity,
    const char *directory,
    const char *leafname
)
{
    size_t length = strlen(directory);
    const char *separator =
        length > 0 && directory[length - 1] == '.' ? "" : ".";
    int written = snprintf(
        destination, capacity, "%s%s%s",
        directory, separator, leafname);

    return written >= 0 && (size_t) written < capacity;
}

static bool aural_music_scanner_add_directory(
    aural_music_scanner *scanner,
    const char *path
)
{
    char (*directories)[AURAL_PATH_CAPACITY];
    size_t capacity;
    size_t index;
    int written;

    for (index = 0; index < scanner->directory_count; ++index) {
        if (strcmp(scanner->directories[index], path) == 0) {
            return true;
        }
    }
    if (scanner->directory_count >= AURAL_SCAN_MAXIMUM_DIRECTORIES) {
        return false;
    }
    if (scanner->directory_count == scanner->directory_capacity) {
        capacity = scanner->directory_capacity == 0 ?
            32 : scanner->directory_capacity * 2;
        if (capacity < scanner->directory_capacity ||
            capacity > SIZE_MAX / sizeof(*directories)) {
            return false;
        }
        directories = realloc(
            scanner->directories,
            capacity * sizeof(*directories));
        if (directories == NULL) {
            return false;
        }
        scanner->directories = directories;
        scanner->directory_capacity = capacity;
    }
    written = snprintf(
        scanner->directories[scanner->directory_count],
        AURAL_PATH_CAPACITY,
        "%s",
        path
    );
    if (written < 0 || written >= AURAL_PATH_CAPACITY) {
        return false;
    }
    ++scanner->directory_count;
    return true;
}

void aural_music_scanner_init(aural_music_scanner *scanner)
{
    if (scanner != NULL) {
        memset(scanner, 0, sizeof(*scanner));
    }
}

void aural_music_scanner_destroy(aural_music_scanner *scanner)
{
    if (scanner == NULL) {
        return;
    }
    free(scanner->directories);
    aural_music_scanner_init(scanner);
}

bool aural_music_scanner_start(
    aural_music_scanner *scanner,
    const char *root_path
)
{
    if (scanner == NULL || root_path == NULL || root_path[0] == '\0') {
        return false;
    }
    aural_music_scanner_destroy(scanner);
    if (!aural_music_scanner_add_directory(scanner, root_path)) {
        aural_music_scanner_destroy(scanner);
        return false;
    }
    scanner->directory_index = 0;
    scanner->context = 0;
    scanner->active = true;
    return true;
}

os_error *aural_music_scanner_step(
    aural_music_scanner *scanner,
    aural_track_list *tracks,
    bool *changed
)
{
    union {
        bits alignment;
        byte bytes[AURAL_SCAN_BUFFER_SIZE];
    } buffer;
    const char *directory;
    os_error *error;
    int read_count = 0;
    int next_context;
    size_t offset = 0;
    int index;

    if (changed != NULL) {
        *changed = false;
    }
    if (scanner == NULL || tracks == NULL || changed == NULL ||
        !scanner->active) {
        return NULL;
    }
    if (scanner->directory_index >= scanner->directory_count) {
        scanner->active = false;
        return NULL;
    }
    directory = scanner->directories[scanner->directory_index];
    error = xosgbpb_dir_entries_info_stamped(
        directory,
        (osgbpb_info_stamped_list *) buffer.bytes,
        AURAL_SCAN_ENTRY_LIMIT,
        scanner->context,
        sizeof(buffer.bytes),
        NULL,
        &read_count,
        &next_context
    );
    if (error != NULL) {
        scanner->active = false;
        return error;
    }

    for (index = 0; index < read_count; ++index) {
        osgbpb_info_stamped *info;
        size_t name_capacity;
        size_t name_length;
        char full_path[AURAL_PATH_CAPACITY];

        if (offset + offsetof(osgbpb_info_stamped, name) >=
            sizeof(buffer.bytes)) {
            scanner->active = false;
            return aural_music_scanner_error(
                "A directory returned invalid catalogue data");
        }
        info = (osgbpb_info_stamped *) (buffer.bytes + offset);
        name_capacity = sizeof(buffer.bytes) - offset -
            offsetof(osgbpb_info_stamped, name);
        name_length = aural_bounded_length(info->name, name_capacity);
        if (name_length == name_capacity) {
            scanner->active = false;
            return aural_music_scanner_error(
                "A directory returned an invalid leafname");
        }
        if (aural_make_path(
                full_path, sizeof(full_path), directory, info->name)) {
            if (info->obj_type == fileswitch_IS_DIR) {
                if (!aural_music_scanner_add_directory(
                        scanner, full_path)) {
                    scanner->active = false;
                    return aural_music_scanner_error(
                        "There is not enough memory to scan this music folder");
                }
            } else if (info->obj_type == fileswitch_IS_FILE &&
                       aural_audio_format_from_file(
                           info->file_type, info->name) !=
                           AURAL_AUDIO_FORMAT_UNKNOWN) {
                aural_track_entry track;
                bool added;

                if (aural_audio_probe_file(
                        full_path,
                        info->name,
                        info->size < 0 ? 0u : (uint64_t) info->size,
                        info->file_type,
                        &track)) {
                    track.date_added_cs =
                        (uint64_t) time(NULL) * 100u;
                    if (!aural_track_list_append_unique(
                            tracks, &track, &added)) {
                        scanner->active = false;
                        return aural_music_scanner_error(
                            "There is not enough memory for the music library");
                    }
                    *changed = *changed || added;
                }
            }
        }
        offset += offsetof(osgbpb_info_stamped, name) + name_length + 1;
        offset = (offset + 3u) & ~3u;
    }

    scanner->context = next_context;
    if (next_context == osgbpb_NO_MORE) {
        ++scanner->directory_index;
        scanner->context = 0;
        if (scanner->directory_index >= scanner->directory_count) {
            scanner->active = false;
        }
    }
    return NULL;
}
