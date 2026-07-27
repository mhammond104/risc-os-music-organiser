#include "aural/metadata_write.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    AURAL_ID3_MAXIMUM_SIZE = 4 * 1024 * 1024,
    AURAL_COPY_BUFFER_SIZE = 64 * 1024
};

typedef struct aural_byte_buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} aural_byte_buffer;

static size_t aural_syncsafe_read(const unsigned char *bytes)
{
    return ((size_t) (bytes[0] & 0x7F) << 21) |
        ((size_t) (bytes[1] & 0x7F) << 14) |
        ((size_t) (bytes[2] & 0x7F) << 7) |
        (size_t) (bytes[3] & 0x7F);
}

static uint32_t aural_be32_read(const unsigned char *bytes)
{
    return ((uint32_t) bytes[0] << 24) |
        ((uint32_t) bytes[1] << 16) |
        ((uint32_t) bytes[2] << 8) |
        (uint32_t) bytes[3];
}

static void aural_size_write(
    unsigned char *bytes,
    size_t value,
    unsigned int version
)
{
    if (version == 4) {
        bytes[0] = (unsigned char) ((value >> 21) & 0x7F);
        bytes[1] = (unsigned char) ((value >> 14) & 0x7F);
        bytes[2] = (unsigned char) ((value >> 7) & 0x7F);
        bytes[3] = (unsigned char) (value & 0x7F);
    } else {
        bytes[0] = (unsigned char) (value >> 24);
        bytes[1] = (unsigned char) (value >> 16);
        bytes[2] = (unsigned char) (value >> 8);
        bytes[3] = (unsigned char) value;
    }
}

static bool aural_buffer_append(
    aural_byte_buffer *buffer,
    const void *data,
    size_t size
)
{
    size_t required;

    if (size > SIZE_MAX - buffer->size) {
        return false;
    }
    required = buffer->size + size;
    if (required > buffer->capacity) {
        size_t capacity = buffer->capacity != 0 ? buffer->capacity : 1024;
        unsigned char *replacement;

        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        replacement = realloc(buffer->data, capacity);
        if (replacement == NULL) {
            return false;
        }
        buffer->data = replacement;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return true;
}

static bool aural_append_frame(
    aural_byte_buffer *tag,
    const char id[4],
    const unsigned char *data,
    size_t size,
    unsigned int version
)
{
    unsigned char header[10] = {0};

    if (size == 0 || size > 0x0FFFFFFFu) {
        return size == 0;
    }
    memcpy(header, id, 4);
    aural_size_write(header + 4, size, version);
    return aural_buffer_append(tag, header, sizeof(header)) &&
        aural_buffer_append(tag, data, size);
}

static bool aural_append_text(
    aural_byte_buffer *tag,
    const char id[4],
    const char *text,
    unsigned int version
)
{
    aural_byte_buffer data = {0};
    unsigned char encoding = version == 4 ? 3 : 0;
    bool success;

    if (text[0] == '\0') {
        return true;
    }
    success = aural_buffer_append(&data, &encoding, 1) &&
        aural_buffer_append(&data, text, strlen(text)) &&
        aural_append_frame(tag, id, data.data, data.size, version);
    free(data.data);
    return success;
}

static bool aural_append_number(
    aural_byte_buffer *tag,
    const char id[4],
    unsigned int number,
    unsigned int total,
    unsigned int version
)
{
    char text[32];

    if (number == 0) {
        return true;
    }
    if (total != 0) {
        snprintf(text, sizeof(text), "%u/%u", number, total);
    } else {
        snprintf(text, sizeof(text), "%u", number);
    }
    return aural_append_text(tag, id, text, version);
}

static bool aural_append_comment(
    aural_byte_buffer *tag,
    const char *comment,
    unsigned int version
)
{
    aural_byte_buffer data = {0};
    unsigned char prefix[] = {
        version == 4 ? 3 : 0, 'e', 'n', 'g', 0
    };
    bool success;

    if (comment[0] == '\0') {
        return true;
    }
    success = aural_buffer_append(&data, prefix, sizeof(prefix)) &&
        aural_buffer_append(&data, comment, strlen(comment)) &&
        aural_append_frame(tag, "COMM", data.data, data.size, version);
    free(data.data);
    return success;
}

static bool aural_append_tags(
    aural_byte_buffer *tag,
    const char *tags,
    unsigned int version
)
{
    static const char description[] = "Aural Tags";
    aural_byte_buffer data = {0};
    unsigned char encoding = version == 4 ? 3 : 0;
    unsigned char terminator = 0;
    bool success;

    if (tags[0] == '\0') {
        return true;
    }
    success = aural_buffer_append(&data, &encoding, 1) &&
        aural_buffer_append(&data, description, sizeof(description) - 1) &&
        aural_buffer_append(&data, &terminator, 1) &&
        aural_buffer_append(&data, tags, strlen(tags)) &&
        aural_append_frame(tag, "TXXX", data.data, data.size, version);
    free(data.data);
    return success;
}

static bool aural_append_rating(
    aural_byte_buffer *tag,
    unsigned int rating,
    unsigned int version
)
{
    static const unsigned char owner[] = {'A','u','r','a','l',0};
    unsigned char data[sizeof(owner) + 1];

    if (rating == 0) {
        return true;
    }
    memcpy(data, owner, sizeof(owner));
    data[sizeof(owner)] =
        (unsigned char) (rating >= 5 ? 255 : rating * 51);
    return aural_append_frame(tag, "POPM", data, sizeof(data), version);
}

static size_t aural_bounded_length(
    const unsigned char *text,
    size_t capacity
)
{
    size_t length = 0;

    while (length < capacity && text[length] != 0) {
        ++length;
    }
    return length;
}

static bool aural_is_replaced_frame(
    const unsigned char *frame,
    const unsigned char *data,
    size_t size
)
{
    static const char *ids[] = {
        "TIT2", "TPE1", "TALB", "TPE2", "TRCK",
        "TPOS", "TYER", "TDRC", "TCON", "COMM"
    };
    size_t index;

    for (index = 0; index < sizeof(ids) / sizeof(ids[0]); ++index) {
        if (memcmp(frame, ids[index], 4) == 0) {
            return true;
        }
    }
    if (memcmp(frame, "TXXX", 4) == 0 && size > 1) {
        size_t length = aural_bounded_length(data + 1, size - 1);

        return length == 10 &&
            memcmp(data + 1, "Aural Tags", 10) == 0;
    }
    return memcmp(frame, "POPM", 4) == 0 && size > 6 &&
        memcmp(data, "Aural", 6) == 0;
}

static bool aural_preserve_frames(
    aural_byte_buffer *output,
    const unsigned char *old_tag,
    size_t old_size,
    unsigned int version
)
{
    size_t offset = 0;

    while (offset + 10 <= old_size) {
        const unsigned char *frame = old_tag + offset;
        size_t frame_size;

        if (frame[0] == 0) {
            return true;
        }
        frame_size = version == 4 ?
            aural_syncsafe_read(frame + 4) :
            aural_be32_read(frame + 4);
        if (frame_size == 0 || frame_size > old_size - offset - 10) {
            return false;
        }
        if (!aural_is_replaced_frame(frame, frame + 10, frame_size) &&
            !aural_buffer_append(output, frame, frame_size + 10)) {
            return false;
        }
        offset += frame_size + 10;
    }
    return true;
}

static bool aural_write_new_frames(
    aural_byte_buffer *tag,
    const aural_track_entry *track,
    unsigned int version
)
{
    char year[16];

    snprintf(year, sizeof(year), "%u", track->year);
    return aural_append_text(tag, "TIT2", track->title, version) &&
        aural_append_text(tag, "TPE1", track->artist, version) &&
        aural_append_text(tag, "TALB", track->album, version) &&
        aural_append_text(tag, "TPE2", track->album_artist, version) &&
        aural_append_number(tag, "TRCK", track->track_number,
            track->track_total, version) &&
        aural_append_number(tag, "TPOS", track->disc_number,
            track->disc_total, version) &&
        (track->year == 0 ||
         aural_append_text(tag, version == 4 ? "TDRC" : "TYER",
            year, version)) &&
        aural_append_text(tag, "TCON", track->genre, version) &&
        aural_append_comment(tag, track->comment, version) &&
        aural_append_tags(tag, track->tags, version) &&
        aural_append_rating(tag, track->rating, version);
}

static bool aural_copy_remaining(FILE *input, FILE *output)
{
    unsigned char *buffer = malloc(AURAL_COPY_BUFFER_SIZE);
    bool success = buffer != NULL;

    while (success) {
        size_t amount = fread(buffer, 1, AURAL_COPY_BUFFER_SIZE, input);

        if (amount > 0 && fwrite(buffer, 1, amount, output) != amount) {
            success = false;
        }
        if (amount < AURAL_COPY_BUFFER_SIZE) {
            if (ferror(input)) {
                success = false;
            }
            break;
        }
    }
    free(buffer);
    return success;
}

static bool aural_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        return false;
    }
    (void) fclose(file);
    return true;
}

aural_metadata_write_result aural_metadata_write_file(
    const aural_track_entry *track
)
{
    unsigned char header[10] = {0};
    unsigned char output_header[10] =
        {'I','D','3',4,0,0,0,0,0,0};
    unsigned char *old_tag = NULL;
    aural_byte_buffer new_tag = {0};
    char temporary[AURAL_PATH_CAPACITY + 16];
    char backup[AURAL_PATH_CAPACITY + 16];
    FILE *input = NULL;
    FILE *output = NULL;
    size_t old_size = 0;
    unsigned int version = 4;
    bool installed = false;
    aural_metadata_write_result result = AURAL_METADATA_WRITE_IO_ERROR;

    if (track == NULL || track->format != AURAL_AUDIO_FORMAT_MP3) {
        return AURAL_METADATA_WRITE_UNSUPPORTED_FORMAT;
    }
    if (snprintf(temporary, sizeof(temporary), "%sAuralTmp",
            track->path) >= (int) sizeof(temporary) ||
        snprintf(backup, sizeof(backup), "%sAuralBak",
            track->path) >= (int) sizeof(backup)) {
        return result;
    }
    if (aural_file_exists(temporary) || aural_file_exists(backup)) {
        return result;
    }
    input = fopen(track->path, "rb");
    if (input == NULL) {
        goto cleanup;
    }
    if (fread(header, 1, sizeof(header), input) == sizeof(header) &&
        memcmp(header, "ID3", 3) == 0) {
        version = header[3];
        old_size = aural_syncsafe_read(header + 6);
        if ((version != 3 && version != 4) || header[5] != 0 ||
            old_size > AURAL_ID3_MAXIMUM_SIZE) {
            result = AURAL_METADATA_WRITE_UNSUPPORTED_TAG;
            goto cleanup;
        }
        old_tag = malloc(old_size);
        if (old_size > 0 && old_tag == NULL) {
            result = AURAL_METADATA_WRITE_NO_MEMORY;
            goto cleanup;
        }
        if (old_size > 0 &&
            fread(old_tag, 1, old_size, input) != old_size) {
            goto cleanup;
        }
        if (!aural_preserve_frames(&new_tag, old_tag, old_size, version)) {
            result = AURAL_METADATA_WRITE_UNSUPPORTED_TAG;
            goto cleanup;
        }
    } else if (fseek(input, 0, SEEK_SET) != 0) {
        goto cleanup;
    }
    if (!aural_write_new_frames(&new_tag, track, version)) {
        result = AURAL_METADATA_WRITE_NO_MEMORY;
        goto cleanup;
    }
    output_header[3] = (unsigned char) version;
    aural_size_write(output_header + 6, new_tag.size, 4);
    output = fopen(temporary, "wb");
    if (output == NULL ||
        fwrite(output_header, 1, sizeof(output_header), output) !=
            sizeof(output_header) ||
        (new_tag.size > 0 &&
         fwrite(new_tag.data, 1, new_tag.size, output) != new_tag.size) ||
        !aural_copy_remaining(input, output)) {
        goto cleanup;
    }
    if (fclose(output) != 0) {
        output = NULL;
        goto cleanup;
    }
    output = NULL;
    if (fclose(input) != 0) {
        input = NULL;
        goto cleanup;
    }
    input = NULL;
    if (rename(track->path, backup) != 0) {
        goto cleanup;
    }
    if (rename(temporary, track->path) != 0) {
        (void) rename(backup, track->path);
        goto cleanup;
    }
    (void) remove(backup);
    installed = true;
    result = AURAL_METADATA_WRITE_OK;

cleanup:
    if (input != NULL) {
        (void) fclose(input);
    }
    if (output != NULL) {
        (void) fclose(output);
    }
    if (!installed) {
        (void) remove(temporary);
    }
    free(old_tag);
    free(new_tag.data);
    return result;
}
