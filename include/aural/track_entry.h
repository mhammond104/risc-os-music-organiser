#ifndef AURAL_TRACK_ENTRY_H
#define AURAL_TRACK_ENTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURAL_PATH_CAPACITY 1024
#define AURAL_LEAFNAME_CAPACITY 256
#define AURAL_TITLE_CAPACITY 256
#define AURAL_ARTIST_CAPACITY 256
#define AURAL_ALBUM_CAPACITY 256
#define AURAL_GENRE_CAPACITY 128
#define AURAL_COMMENT_CAPACITY 512
#define AURAL_TAGS_CAPACITY 256

typedef enum aural_audio_format {
    AURAL_AUDIO_FORMAT_UNKNOWN = 0,
    AURAL_AUDIO_FORMAT_MP3,
    AURAL_AUDIO_FORMAT_FLAC,
    AURAL_AUDIO_FORMAT_OGG_VORBIS,
    AURAL_AUDIO_FORMAT_WAV,
    AURAL_AUDIO_FORMAT_AIFF,
    AURAL_AUDIO_FORMAT_MIDI
} aural_audio_format;

typedef struct aural_track_entry {
    char path[AURAL_PATH_CAPACITY];
    char leafname[AURAL_LEAFNAME_CAPACITY];
    char title[AURAL_TITLE_CAPACITY];
    char artist[AURAL_ARTIST_CAPACITY];
    char album[AURAL_ALBUM_CAPACITY];
    char album_artist[AURAL_ARTIST_CAPACITY];
    char genre[AURAL_GENRE_CAPACITY];
    char comment[AURAL_COMMENT_CAPACITY];
    char tags[AURAL_TAGS_CAPACITY];
    char artwork_path[AURAL_PATH_CAPACITY];
    uint64_t size_bytes;
    uint64_t duration_ms;
    uint64_t date_added_cs;
    uint32_t riscos_filetype;
    uint32_t sample_rate_hz;
    uint32_t bitrate_bps;
    unsigned int track_number;
    unsigned int track_total;
    unsigned int disc_number;
    unsigned int disc_total;
    unsigned int year;
    unsigned int channels;
    unsigned int rating;
    aural_audio_format format;
    bool favourite;
    bool selected;
} aural_track_entry;

bool aural_track_entry_init(
    aural_track_entry *entry,
    const char *path,
    const char *leafname,
    uint64_t size_bytes,
    uint32_t riscos_filetype
);

const char *aural_track_entry_display_title(
    const aural_track_entry *entry
);

#endif
