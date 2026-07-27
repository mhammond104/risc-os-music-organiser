#include "aural/audio_probe.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    RISCOS_FILETYPE_AMPEG = 0x1AD,
    RISCOS_FILETYPE_WAVEFORM = 0xFB1,
    RISCOS_FILETYPE_AIFF = 0xFC2,
    RISCOS_FILETYPE_MIDI = 0xFD4,
    ID3_MAXIMUM_TAG_SIZE = 4 * 1024 * 1024,
    MP3_SCAN_SIZE = 1024 * 1024
};

static uint32_t aural_read_le32(const unsigned char *bytes)
{
    return (uint32_t) bytes[0] |
        ((uint32_t) bytes[1] << 8) |
        ((uint32_t) bytes[2] << 16) |
        ((uint32_t) bytes[3] << 24);
}

static uint32_t aural_read_be32(const unsigned char *bytes)
{
    return ((uint32_t) bytes[0] << 24) |
        ((uint32_t) bytes[1] << 16) |
        ((uint32_t) bytes[2] << 8) |
        (uint32_t) bytes[3];
}

static uint16_t aural_read_le16(const unsigned char *bytes)
{
    return (uint16_t) ((uint16_t) bytes[0] |
        ((uint16_t) bytes[1] << 8));
}

static bool aural_text_ends_with(
    const char *text,
    const char *suffix
)
{
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    size_t index;

    if (suffix_length > text_length) {
        return false;
    }
    text += text_length - suffix_length;
    for (index = 0; index < suffix_length; ++index) {
        if (tolower((unsigned char) text[index]) !=
            tolower((unsigned char) suffix[index])) {
            return false;
        }
    }
    return true;
}

aural_audio_format aural_audio_format_from_file(
    uint32_t riscos_filetype,
    const char *leafname
)
{
    switch (riscos_filetype & 0xFFFu) {
    case RISCOS_FILETYPE_AMPEG:
        return AURAL_AUDIO_FORMAT_MP3;
    case RISCOS_FILETYPE_WAVEFORM:
        return AURAL_AUDIO_FORMAT_WAV;
    case RISCOS_FILETYPE_AIFF:
        return AURAL_AUDIO_FORMAT_AIFF;
    case RISCOS_FILETYPE_MIDI:
        return AURAL_AUDIO_FORMAT_MIDI;
    default:
        break;
    }
    if (leafname == NULL) {
        return AURAL_AUDIO_FORMAT_UNKNOWN;
    }
    if (aural_text_ends_with(leafname, ".mp3") ||
        aural_text_ends_with(leafname, "/mp3")) {
        return AURAL_AUDIO_FORMAT_MP3;
    }
    if (aural_text_ends_with(leafname, ".wav") ||
        aural_text_ends_with(leafname, ".wave") ||
        aural_text_ends_with(leafname, "/wav")) {
        return AURAL_AUDIO_FORMAT_WAV;
    }
    if (aural_text_ends_with(leafname, ".flac") ||
        aural_text_ends_with(leafname, "/flac")) {
        return AURAL_AUDIO_FORMAT_FLAC;
    }
    if (aural_text_ends_with(leafname, ".ogg") ||
        aural_text_ends_with(leafname, "/ogg")) {
        return AURAL_AUDIO_FORMAT_OGG_VORBIS;
    }
    if (aural_text_ends_with(leafname, ".aif") ||
        aural_text_ends_with(leafname, ".aiff")) {
        return AURAL_AUDIO_FORMAT_AIFF;
    }
    if (aural_text_ends_with(leafname, ".mid") ||
        aural_text_ends_with(leafname, ".midi")) {
        return AURAL_AUDIO_FORMAT_MIDI;
    }
    return AURAL_AUDIO_FORMAT_UNKNOWN;
}

static void aural_copy_trimmed(
    char *destination,
    size_t capacity,
    const unsigned char *source,
    size_t length
)
{
    size_t start = 0;
    size_t output = 0;

    while (start < length &&
           (source[start] == '\0' || isspace(source[start]))) {
        ++start;
    }
    while (length > start &&
           (source[length - 1] == '\0' ||
            isspace(source[length - 1]))) {
        --length;
    }
    while (start < length && output + 1 < capacity) {
        unsigned char character = source[start++];

        if (character >= 32 && character != 127) {
            destination[output++] = (char) character;
        }
    }
    destination[output] = '\0';
}

static void aural_copy_id3_text(
    char *destination,
    size_t capacity,
    const unsigned char *data,
    size_t length
)
{
    unsigned int encoding;

    if (length == 0) {
        return;
    }
    encoding = data[0];
    ++data;
    --length;
    if (encoding == 0 || encoding == 3) {
        aural_copy_trimmed(destination, capacity, data, length);
    } else {
        size_t input = 0;
        size_t output = 0;
        bool little_endian = length >= 2 &&
            data[0] == 0xFF && data[1] == 0xFE;

        if (length >= 2 &&
            ((data[0] == 0xFF && data[1] == 0xFE) ||
             (data[0] == 0xFE && data[1] == 0xFF))) {
            input = 2;
        }
        while (input + 1 < length && output + 1 < capacity) {
            unsigned char character = little_endian ?
                data[input] : data[input + 1];

            if (character >= 32 && character != 127) {
                destination[output++] = (char) character;
            }
            input += 2;
        }
        destination[output] = '\0';
    }
}

static void aural_parse_number_pair(
    const char *text,
    unsigned int *number,
    unsigned int *total
)
{
    unsigned int first = 0;
    unsigned int second = 0;

    if (sscanf(text, "%u/%u", &first, &second) >= 1) {
        *number = first;
        *total = second;
    }
}

static size_t aural_id3_terminated_length(
    const unsigned char *data,
    size_t length,
    unsigned int encoding
)
{
    size_t index;
    size_t step = encoding == 1 || encoding == 2 ? 2 : 1;

    for (index = 0; index + step <= length; index += step) {
        if (data[index] == 0 &&
            (step == 1 || data[index + 1] == 0)) {
            return index + step;
        }
    }
    return length;
}

static size_t aural_syncsafe_size(const unsigned char *bytes)
{
    return ((size_t) (bytes[0] & 0x7F) << 21) |
        ((size_t) (bytes[1] & 0x7F) << 14) |
        ((size_t) (bytes[2] & 0x7F) << 7) |
        (size_t) (bytes[3] & 0x7F);
}

static size_t aural_probe_id3v2(
    FILE *file,
    aural_track_entry *entry
)
{
    unsigned char header[10];
    unsigned char *tag;
    size_t tag_size;
    size_t offset = 0;
    unsigned int version;

    if (fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, sizeof(header), 1, file) != 1 ||
        memcmp(header, "ID3", 3) != 0) {
        return 0;
    }
    version = header[3];
    tag_size = aural_syncsafe_size(header + 6);
    if ((version != 3 && version != 4) ||
        tag_size == 0 || tag_size > ID3_MAXIMUM_TAG_SIZE) {
        return 10 + tag_size;
    }
    tag = malloc(tag_size);
    if (tag == NULL || fread(tag, tag_size, 1, file) != 1) {
        free(tag);
        return 10 + tag_size;
    }
    while (offset + 10 <= tag_size) {
        const unsigned char *frame = tag + offset;
        size_t frame_size;
        const unsigned char *data;
        char number_text[32];

        if (frame[0] == 0) {
            break;
        }
        frame_size = version == 4 ?
            aural_syncsafe_size(frame + 4) :
            aural_read_be32(frame + 4);
        if (frame_size == 0 || frame_size > tag_size - offset - 10) {
            break;
        }
        data = frame + 10;
        if (memcmp(frame, "TIT2", 4) == 0) {
            aural_copy_id3_text(
                entry->title, sizeof(entry->title), data, frame_size);
        } else if (memcmp(frame, "TPE1", 4) == 0) {
            aural_copy_id3_text(
                entry->artist, sizeof(entry->artist), data, frame_size);
        } else if (memcmp(frame, "TALB", 4) == 0) {
            aural_copy_id3_text(
                entry->album, sizeof(entry->album), data, frame_size);
        } else if (memcmp(frame, "TPE2", 4) == 0) {
            aural_copy_id3_text(
                entry->album_artist, sizeof(entry->album_artist),
                data, frame_size);
        } else if (memcmp(frame, "TCON", 4) == 0) {
            aural_copy_id3_text(
                entry->genre, sizeof(entry->genre), data, frame_size);
        } else if (memcmp(frame, "TYER", 4) == 0 ||
                   memcmp(frame, "TDRC", 4) == 0) {
            char year[16];

            aural_copy_id3_text(year, sizeof(year), data, frame_size);
            entry->year = (unsigned int) strtoul(year, NULL, 10);
        } else if (memcmp(frame, "TRCK", 4) == 0) {
            aural_copy_id3_text(
                number_text, sizeof(number_text), data, frame_size);
            aural_parse_number_pair(
                number_text, &entry->track_number, &entry->track_total);
        } else if (memcmp(frame, "TPOS", 4) == 0) {
            aural_copy_id3_text(
                number_text, sizeof(number_text), data, frame_size);
            aural_parse_number_pair(
                number_text, &entry->disc_number, &entry->disc_total);
        } else if (memcmp(frame, "COMM", 4) == 0 &&
                   frame_size > 4) {
            unsigned int encoding = data[0];
            size_t description = aural_id3_terminated_length(
                data + 4, frame_size - 4, encoding);

            if (description < frame_size - 4) {
                unsigned char *text = malloc(
                    frame_size - 4 - description + 1);

                if (text != NULL) {
                    text[0] = (unsigned char) encoding;
                    memcpy(text + 1, data + 4 + description,
                        frame_size - 4 - description);
                    aural_copy_id3_text(
                        entry->comment, sizeof(entry->comment),
                        text, frame_size - 3 - description);
                    free(text);
                }
            }
        } else if (memcmp(frame, "TXXX", 4) == 0 &&
                   frame_size > 1) {
            unsigned int encoding = data[0];
            size_t description = aural_id3_terminated_length(
                data + 1, frame_size - 1, encoding);

            if (description == 11 &&
                memcmp(data + 1, "Aural Tags", 10) == 0 &&
                description < frame_size - 1) {
                unsigned char *text = malloc(
                    frame_size - 1 - description + 1);

                if (text != NULL) {
                    text[0] = (unsigned char) encoding;
                    memcpy(text + 1, data + 1 + description,
                        frame_size - 1 - description);
                    aural_copy_id3_text(
                        entry->tags, sizeof(entry->tags),
                        text, frame_size - description);
                    free(text);
                }
            }
        } else if (memcmp(frame, "POPM", 4) == 0 &&
                   frame_size > 1) {
            size_t owner = aural_id3_terminated_length(
                data, frame_size, 0);

            if (owner < frame_size &&
                owner == 6 && memcmp(data, "Aural", 5) == 0) {
                unsigned int value = data[owner];

                entry->rating = value == 0 ? 0 :
                    (value + 50) / 51;
                if (entry->rating > 5) {
                    entry->rating = 5;
                }
            }
        }
        offset += 10 + frame_size;
    }
    free(tag);
    return 10 + tag_size;
}

static void aural_probe_id3v1(FILE *file, aural_track_entry *entry)
{
    unsigned char tag[128];

    if (fseek(file, -128, SEEK_END) != 0 ||
        fread(tag, sizeof(tag), 1, file) != 1 ||
        memcmp(tag, "TAG", 3) != 0) {
        return;
    }
    if (entry->title[0] == '\0') {
        aural_copy_trimmed(entry->title, sizeof(entry->title), tag + 3, 30);
    }
    if (entry->artist[0] == '\0') {
        aural_copy_trimmed(
            entry->artist, sizeof(entry->artist), tag + 33, 30);
    }
    if (entry->album[0] == '\0') {
        aural_copy_trimmed(
            entry->album, sizeof(entry->album), tag + 63, 30);
    }
    if (entry->year == 0) {
        char year[5];

        memcpy(year, tag + 93, 4);
        year[4] = '\0';
        entry->year = (unsigned int) strtoul(year, NULL, 10);
    }
    if (entry->comment[0] == '\0') {
        aural_copy_trimmed(
            entry->comment, sizeof(entry->comment), tag + 97, 30);
    }
    if (entry->track_number == 0 && tag[125] == 0) {
        entry->track_number = tag[126];
    }
}

static void aural_probe_mp3_frames(
    FILE *file,
    size_t audio_offset,
    aural_track_entry *entry
)
{
    static const unsigned int bitrates[16] = {
        0, 32, 40, 48, 56, 64, 80, 96,
        112, 128, 160, 192, 224, 256, 320, 0
    };
    static const unsigned int sample_rates[4] = {
        44100, 48000, 32000, 0
    };
    unsigned char *data;
    size_t read_size;
    size_t offset;

    data = malloc(MP3_SCAN_SIZE);
    if (data == NULL || fseek(file, (long) audio_offset, SEEK_SET) != 0) {
        free(data);
        return;
    }
    read_size = fread(data, 1, MP3_SCAN_SIZE, file);
    for (offset = 0; offset + 4 <= read_size; ++offset) {
        unsigned int header;
        unsigned int bitrate_index;
        unsigned int sample_index;

        if (data[offset] != 0xFF ||
            (data[offset + 1] & 0xFE) != 0xFA) {
            continue;
        }
        header = aural_read_be32(data + offset);
        bitrate_index = (header >> 12) & 0xF;
        sample_index = (header >> 10) & 0x3;
        if (bitrates[bitrate_index] == 0 ||
            sample_rates[sample_index] == 0) {
            continue;
        }
        entry->bitrate_bps = bitrates[bitrate_index] * 1000;
        entry->sample_rate_hz = sample_rates[sample_index];
        entry->channels = ((header >> 6) & 0x3) == 3 ? 1 : 2;
        if (entry->size_bytes > audio_offset &&
            entry->bitrate_bps > 0) {
            entry->duration_ms =
                (entry->size_bytes - audio_offset) * 8000 /
                entry->bitrate_bps;
        }
        break;
    }
    free(data);
}

static void aural_probe_mp3(FILE *file, aural_track_entry *entry)
{
    size_t audio_offset = aural_probe_id3v2(file, entry);

    aural_probe_id3v1(file, entry);
    aural_probe_mp3_frames(file, audio_offset, entry);
}

static void aural_probe_wav(FILE *file, aural_track_entry *entry)
{
    unsigned char header[12];
    uint32_t data_size = 0;
    uint32_t byte_rate = 0;

    if (fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, sizeof(header), 1, file) != 1 ||
        memcmp(header, "RIFF", 4) != 0 ||
        memcmp(header + 8, "WAVE", 4) != 0) {
        return;
    }
    for (;;) {
        unsigned char chunk[8];
        uint32_t size;
        long next;

        if (fread(chunk, sizeof(chunk), 1, file) != 1) {
            break;
        }
        size = aural_read_le32(chunk + 4);
        next = ftell(file) + (long) size + (long) (size & 1u);
        if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            unsigned char format[16];

            if (fread(format, sizeof(format), 1, file) != 1) {
                break;
            }
            entry->channels = aural_read_le16(format + 2);
            entry->sample_rate_hz = aural_read_le32(format + 4);
            byte_rate = aural_read_le32(format + 8);
            entry->bitrate_bps = byte_rate * 8;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_size = size;
        }
        if (fseek(file, next, SEEK_SET) != 0) {
            break;
        }
    }
    if (byte_rate > 0) {
        entry->duration_ms = (uint64_t) data_size * 1000 / byte_rate;
    }
}

static void aural_remove_extension(char *text)
{
    char *dot = strrchr(text, '.');
    char *slash = strrchr(text, '/');
    char *separator = dot != NULL && (slash == NULL || dot > slash) ?
        dot : slash;

    if (separator != NULL) {
        *separator = '\0';
    }
}

static void aural_fill_path_fallbacks(aural_track_entry *entry)
{
    char path[AURAL_PATH_CAPACITY];
    char *leaf;
    char *album;
    char *artist;

    if (entry->title[0] == '\0') {
        char *cursor;
        unsigned long number;

        snprintf(entry->title, sizeof(entry->title), "%s", entry->leafname);
        aural_remove_extension(entry->title);
        number = strtoul(entry->title, &cursor, 10);
        if (cursor != entry->title && number > 0 && number <= 999) {
            while (*cursor == ' ' || *cursor == '-' ||
                   *cursor == '_' || *cursor == '.') {
                ++cursor;
            }
            if (*cursor != '\0') {
                if (entry->track_number == 0) {
                    entry->track_number = (unsigned int) number;
                }
                memmove(entry->title, cursor, strlen(cursor) + 1);
            }
        }
    }
    snprintf(path, sizeof(path), "%s", entry->path);
    leaf = strrchr(path, '.');
    if (leaf == NULL) {
        return;
    }
    *leaf = '\0';
    album = strrchr(path, '.');
    if (album != NULL) {
        *album++ = '\0';
        if (entry->album[0] == '\0') {
            snprintf(entry->album, sizeof(entry->album), "%s", album);
        }
        artist = strrchr(path, '.');
        if (artist != NULL && entry->artist[0] == '\0') {
            snprintf(entry->artist, sizeof(entry->artist), "%s", artist + 1);
        }
    }
}

bool aural_audio_probe_file(
    const char *path,
    const char *leafname,
    uint64_t size_bytes,
    uint32_t riscos_filetype,
    aural_track_entry *entry
)
{
    FILE *file;
    aural_audio_format format =
        aural_audio_format_from_file(riscos_filetype, leafname);

    if (format == AURAL_AUDIO_FORMAT_UNKNOWN ||
        !aural_track_entry_init(
            entry, path, leafname, size_bytes, riscos_filetype)) {
        return false;
    }
    entry->format = format;
    file = fopen(path, "rb");
    if (file != NULL) {
        if (format == AURAL_AUDIO_FORMAT_MP3) {
            aural_probe_mp3(file, entry);
        } else if (format == AURAL_AUDIO_FORMAT_WAV) {
            aural_probe_wav(file, entry);
        }
        fclose(file);
    }
    aural_fill_path_fallbacks(entry);
    return true;
}
