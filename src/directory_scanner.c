#include "imgorg/directory_scanner.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "oslib/fileswitch.h"
#include "oslib/osgbpb.h"

enum {
    SCAN_BUFFER_SIZE = 4096,
    SCAN_ENTRY_LIMIT = 16
};

static os_error scanner_error;

static size_t imgorg_directory_scanner_bounded_length(
    const char *text,
    size_t capacity
)
{
    size_t length;

    for (length = 0; length < capacity && text[length] != '\0'; ++length) {
    }
    return length;
}

static os_error *imgorg_directory_scanner_error(const char *message)
{
    scanner_error.errnum = 0x80F002;
    snprintf(
        scanner_error.errmess,
        sizeof(scanner_error.errmess),
        "%s",
        message
    );
    return &scanner_error;
}

static bool imgorg_directory_scanner_make_path(
    char *destination,
    size_t destination_size,
    const char *directory,
    const char *leafname
)
{
    size_t length = strlen(directory);
    const char *separator = length > 0 && directory[length - 1] == '.' ? "" : ".";
    int written = snprintf(
        destination,
        destination_size,
        "%s%s%s",
        directory,
        separator,
        leafname
    );

    return written >= 0 && (size_t) written < destination_size;
}

void imgorg_directory_scanner_init(imgorg_directory_scanner *scanner)
{
    if (scanner == NULL) {
        return;
    }

    memset(scanner, 0, sizeof(*scanner));
}

bool imgorg_directory_scanner_start(
    imgorg_directory_scanner *scanner,
    const char *path
)
{
    int written;

    if (scanner == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    written = snprintf(scanner->path, sizeof(scanner->path), "%s", path);
    if (written < 0 || (size_t) written >= sizeof(scanner->path)) {
        imgorg_directory_scanner_init(scanner);
        return false;
    }

    scanner->context = 0;
    scanner->active = true;
    return true;
}

os_error *imgorg_directory_scanner_step(
    imgorg_directory_scanner *scanner,
    imgorg_image_list *images,
    bool *changed
)
{
    union {
        bits alignment;
        byte bytes[SCAN_BUFFER_SIZE];
    } buffer;
    os_error *error;
    int read_count = 0;
    int next_context;
    size_t offset = 0;
    int index;

    if (changed != NULL) {
        *changed = false;
    }
    if (scanner == NULL || images == NULL || changed == NULL ||
        !scanner->active) {
        return NULL;
    }

    error = xosgbpb_dir_entries_info_stamped(
        scanner->path,
        (osgbpb_info_stamped_list *) buffer.bytes,
        SCAN_ENTRY_LIMIT,
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

        if (offset + offsetof(osgbpb_info_stamped, name) >=
            sizeof(buffer.bytes)) {
            scanner->active = false;
            return imgorg_directory_scanner_error(
                "The directory returned invalid catalogue data"
            );
        }

        info = (osgbpb_info_stamped *) (buffer.bytes + offset);
        name_capacity = sizeof(buffer.bytes) - offset -
            offsetof(osgbpb_info_stamped, name);
        name_length = imgorg_directory_scanner_bounded_length(
            info->name,
            name_capacity
        );
        if (name_length == name_capacity) {
            scanner->active = false;
            return imgorg_directory_scanner_error(
                "The directory returned an invalid leafname"
            );
        }

        if (info->obj_type == fileswitch_IS_FILE &&
            imgorg_image_format_from_filetype(info->file_type) !=
                IMGORG_IMAGE_FORMAT_UNKNOWN) {
            imgorg_image_entry entry;
            char full_path[IMGORG_PATH_CAPACITY];

            if (imgorg_directory_scanner_make_path(
                    full_path,
                    sizeof(full_path),
                    scanner->path,
                    info->name
                ) &&
                imgorg_image_entry_init(
                    &entry,
                    full_path,
                    info->name,
                    info->size < 0 ? 0u : (uint64_t) info->size,
                    info->load_addr,
                    info->exec_addr,
                    info->file_type
                )) {
                bool added;

                if (!imgorg_image_list_append_unique(
                        images,
                        &entry,
                        &added
                    )) {
                    scanner->active = false;
                    return imgorg_directory_scanner_error(
                        "There is not enough memory for the directory listing"
                    );
                }
                *changed = *changed || added;
            }
        }

        offset += offsetof(osgbpb_info_stamped, name) + name_length + 1;
        offset = (offset + 3u) & ~3u;
    }

    scanner->context = next_context;
    if (next_context == osgbpb_NO_MORE) {
        scanner->active = false;
    }
    return NULL;
}
