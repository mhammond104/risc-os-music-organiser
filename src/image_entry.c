#include "imgorg/image_entry.h"

#include <ctype.h>
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
    uint32_t load_addr,
    uint32_t exec_addr,
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

    if (path_length < 0 || (size_t) path_length >= sizeof(entry->path) ||
        leafname_length < 0 ||
        (size_t) leafname_length >= sizeof(entry->leafname)) {
        return false;
    }

    entry->size_bytes = size_bytes;
    entry->load_addr = load_addr;
    entry->exec_addr = exec_addr;
    entry->riscos_filetype = riscos_filetype & 0xFFFu;
    entry->format = imgorg_image_format_from_filetype(riscos_filetype);
    return true;
}

static bool imgorg_tag_name_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char) *left) !=
            tolower((unsigned char) *right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

bool imgorg_tag_name_normalise(
    char *destination,
    size_t capacity,
    const char *source
)
{
    const char *start;
    const char *end;
    size_t length;
    size_t index;

    if (destination == NULL || capacity == 0 || source == NULL) {
        return false;
    }
    start = source;
    while (*start != '\0' && isspace((unsigned char) *start)) {
        ++start;
    }
    end = start + strlen(start);
    while (end > start && isspace((unsigned char) end[-1])) {
        --end;
    }
    length = (size_t) (end - start);
    if (length == 0 || length >= capacity ||
        length >= IMGORG_TAG_NAME_CAPACITY) {
        return false;
    }
    for (index = 0; index < length; ++index) {
        unsigned char character = (unsigned char) start[index];

        if (character == ',' || character < 32 || character == 127) {
            return false;
        }
    }
    memmove(destination, start, length);
    destination[length] = '\0';
    return true;
}

static bool imgorg_image_entry_next_tag(
    const char **cursor,
    char *tag,
    size_t capacity
)
{
    const char *start;
    const char *end;
    size_t length;

    if (cursor == NULL || *cursor == NULL || **cursor == '\0') {
        return false;
    }
    start = *cursor;
    end = strchr(start, ',');
    if (end == NULL) {
        end = start + strlen(start);
        *cursor = end;
    } else {
        *cursor = end + 1;
    }
    while (start < end && isspace((unsigned char) *start)) {
        ++start;
    }
    while (end > start && isspace((unsigned char) end[-1])) {
        --end;
    }
    length = (size_t) (end - start);
    if (length == 0 || length >= capacity) {
        tag[0] = '\0';
        return true;
    }
    memcpy(tag, start, length);
    tag[length] = '\0';
    return true;
}

bool imgorg_image_entry_has_tag(
    const imgorg_image_entry *entry,
    const char *tag
)
{
    char normalised[IMGORG_TAG_NAME_CAPACITY];
    char existing[IMGORG_TAG_NAME_CAPACITY];
    const char *cursor;

    if (entry == NULL ||
        !imgorg_tag_name_normalise(normalised, sizeof(normalised), tag)) {
        return false;
    }
    cursor = entry->tags;
    while (imgorg_image_entry_next_tag(
            &cursor, existing, sizeof(existing))) {
        if (existing[0] != '\0' &&
            imgorg_tag_name_equal(existing, normalised)) {
            return true;
        }
    }
    return false;
}

bool imgorg_image_entry_add_tag(
    imgorg_image_entry *entry,
    const char *tag,
    bool *changed
)
{
    char normalised[IMGORG_TAG_NAME_CAPACITY];
    size_t used;
    size_t required;

    if (changed != NULL) {
        *changed = false;
    }
    if (entry == NULL ||
        !imgorg_tag_name_normalise(normalised, sizeof(normalised), tag)) {
        return false;
    }
    if (imgorg_image_entry_has_tag(entry, normalised)) {
        return true;
    }
    used = strlen(entry->tags);
    required = used + (used > 0 ? 1 : 0) + strlen(normalised) + 1;
    if (required > sizeof(entry->tags)) {
        return false;
    }
    if (used > 0) {
        entry->tags[used++] = ',';
    }
    strcpy(entry->tags + used, normalised);
    if (changed != NULL) {
        *changed = true;
    }
    return true;
}

bool imgorg_image_entry_remove_tag(
    imgorg_image_entry *entry,
    const char *tag,
    bool *changed
)
{
    char normalised[IMGORG_TAG_NAME_CAPACITY];
    char existing[IMGORG_TAG_NAME_CAPACITY];
    char result[IMGORG_TAGS_CAPACITY];
    const char *cursor;
    size_t used = 0;
    bool removed = false;

    if (changed != NULL) {
        *changed = false;
    }
    if (entry == NULL ||
        !imgorg_tag_name_normalise(normalised, sizeof(normalised), tag)) {
        return false;
    }
    result[0] = '\0';
    cursor = entry->tags;
    while (imgorg_image_entry_next_tag(
            &cursor, existing, sizeof(existing))) {
        size_t length;

        if (existing[0] == '\0') {
            continue;
        }
        if (imgorg_tag_name_equal(existing, normalised)) {
            removed = true;
            continue;
        }
        length = strlen(existing);
        if (used + (used > 0 ? 1 : 0) + length + 1 > sizeof(result)) {
            return false;
        }
        if (used > 0) {
            result[used++] = ',';
        }
        memcpy(result + used, existing, length + 1);
        used += length;
    }
    if (removed) {
        strcpy(entry->tags, result);
        if (changed != NULL) {
            *changed = true;
        }
    }
    return true;
}
