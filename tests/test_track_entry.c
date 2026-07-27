#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "aural/audio_probe.h"
#include "aural/metadata_write.h"
#include "aural/playlist.h"
#include "aural/track_catalog.h"
#include "aural/track_entry.h"

static void test_track_entry(void)
{
    aural_track_entry track;

    assert(aural_track_entry_init(
        &track,
        "ADFS::HardDisc4.$.Music.Artist.Album.01 Track",
        "01 Track",
        12345678,
        0x1AD
    ));
    assert(track.size_bytes == 12345678);
    assert(track.riscos_filetype == 0x1AD);
    assert(track.format == AURAL_AUDIO_FORMAT_UNKNOWN);
    assert(strcmp(
        aural_track_entry_display_title(&track),
        "01 Track"
    ) == 0);

    snprintf(track.title, sizeof(track.title), "A Proper Title");
    snprintf(track.artist, sizeof(track.artist), "An Artist");
    snprintf(track.album, sizeof(track.album), "An Album");
    track.track_number = 1;
    track.track_total = 10;
    track.duration_ms = 245000;
    track.rating = 4;
    track.favourite = true;

    assert(strcmp(
        aural_track_entry_display_title(&track),
        "A Proper Title"
    ) == 0);
    assert(track.track_number == 1);
    assert(track.duration_ms == 245000);
    assert(track.rating == 4);
    assert(track.favourite);

}

static void test_track_catalog(void)
{
    static const char file_name[] = "build/host/aural_catalog_test";
    aural_source_list sources;
    aural_source_list loaded_sources;
    aural_track_list tracks;
    aural_track_list loaded_tracks;
    aural_track_entry track;
    bool added;

    (void) remove(file_name);
    aural_source_list_init(&sources);
    aural_source_list_init(&loaded_sources);
    aural_track_list_init(&tracks);
    aural_track_list_init(&loaded_tracks);

    assert(aural_source_list_add(
        &sources, "ADFS::HardDisc4.$.Music", &added));
    assert(added);
    assert(aural_source_list_add(
        &sources, "ADFS::HardDisc4.$.Music", &added));
    assert(!added);

    assert(aural_track_entry_init(
        &track,
        "ADFS::HardDisc4.$.Music.Artist.Album.01 Track",
        "01 Track",
        12345678,
        0x1AD
    ));
    snprintf(track.title, sizeof(track.title), "A Proper Title");
    snprintf(track.artist, sizeof(track.artist), "An Artist");
    snprintf(track.album, sizeof(track.album), "An Album");
    snprintf(track.album_artist, sizeof(track.album_artist), "Various");
    snprintf(track.genre, sizeof(track.genre), "Electronic");
    snprintf(track.comment, sizeof(track.comment), "Catalogue comment");
    snprintf(track.tags, sizeof(track.tags), "night,widescreen");
    snprintf(track.artwork_path, sizeof(track.artwork_path),
        "ADFS::HardDisc4.$.Music.Artist.Album.cover/jpg");
    track.duration_ms = 245678;
    track.date_added_cs = 399999999999ULL;
    track.sample_rate_hz = 44100;
    track.bitrate_bps = 320000;
    track.track_number = 1;
    track.track_total = 10;
    track.disc_number = 1;
    track.disc_total = 2;
    track.year = 2026;
    track.channels = 2;
    track.rating = 5;
    track.format = AURAL_AUDIO_FORMAT_MP3;
    track.favourite = true;

    assert(aural_track_list_append_unique(&tracks, &track, &added));
    assert(added);
    assert(aural_track_list_append_unique(&tracks, &track, &added));
    assert(!added);
    assert(tracks.count == 1);
    assert(aural_track_list_find_path(&tracks, track.path) == 0);

    assert(aural_track_catalog_save(file_name, &sources, &tracks));
    assert(aural_track_catalog_load(
        file_name, &loaded_sources, &loaded_tracks));
    assert(loaded_sources.count == 1);
    assert(strcmp(
        loaded_sources.items[0], "ADFS::HardDisc4.$.Music") == 0);
    assert(loaded_tracks.count == 1);
    assert(strcmp(loaded_tracks.items[0].title, "A Proper Title") == 0);
    assert(strcmp(loaded_tracks.items[0].artist, "An Artist") == 0);
    assert(strcmp(loaded_tracks.items[0].album, "An Album") == 0);
    assert(strcmp(loaded_tracks.items[0].album_artist, "Various") == 0);
    assert(strcmp(loaded_tracks.items[0].genre, "Electronic") == 0);
    assert(strcmp(loaded_tracks.items[0].comment, "Catalogue comment") == 0);
    assert(strcmp(loaded_tracks.items[0].tags, "night,widescreen") == 0);
    assert(strcmp(loaded_tracks.items[0].artwork_path,
        "ADFS::HardDisc4.$.Music.Artist.Album.cover/jpg") == 0);
    assert(loaded_tracks.items[0].duration_ms == 245678);
    assert(loaded_tracks.items[0].date_added_cs == 399999999999ULL);
    assert(loaded_tracks.items[0].sample_rate_hz == 44100);
    assert(loaded_tracks.items[0].bitrate_bps == 320000);
    assert(loaded_tracks.items[0].track_number == 1);
    assert(loaded_tracks.items[0].track_total == 10);
    assert(loaded_tracks.items[0].disc_number == 1);
    assert(loaded_tracks.items[0].disc_total == 2);
    assert(loaded_tracks.items[0].year == 2026);
    assert(loaded_tracks.items[0].channels == 2);
    assert(loaded_tracks.items[0].rating == 5);
    assert(loaded_tracks.items[0].format == AURAL_AUDIO_FORMAT_MP3);
    assert(loaded_tracks.items[0].favourite);
    assert(!loaded_tracks.items[0].selected);

    assert(aural_track_list_remove_at(&loaded_tracks, 0));
    assert(loaded_tracks.count == 0);
    assert(aural_source_list_remove_at(&loaded_sources, 0));
    assert(loaded_sources.count == 0);

    aural_source_list_destroy(&sources);
    aural_source_list_destroy(&loaded_sources);
    aural_track_list_destroy(&tracks);
    aural_track_list_destroy(&loaded_tracks);
    (void) remove(file_name);
}

static void test_playlists(void)
{
    static const char file_name[] = "build/host/aural_playlists_test";
    aural_playlist_list playlists;
    aural_playlist_list loaded;
    size_t index;

    (void) remove(file_name);
    aural_playlist_list_init(&playlists);
    aural_playlist_list_init(&loaded);
    assert(aural_playlist_list_add(&playlists, "Road Trip", &index));
    assert(index == 0);
    assert(!aural_playlist_list_add(&playlists, "Road Trip", &index));
    assert(aural_playlist_add_path(
        &playlists.items[0], "ADFS::HardDisc4.$.Music.Track1"));
    assert(aural_playlist_add_path(
        &playlists.items[0], "ADFS::HardDisc4.$.Music.Track2"));
    assert(aural_playlist_add_path(
        &playlists.items[0], "ADFS::HardDisc4.$.Music.Track1"));
    assert(playlists.items[0].count == 2);
    assert(aural_playlist_move(&playlists.items[0], 1, 0));
    assert(strcmp(playlists.items[0].paths[0],
        "ADFS::HardDisc4.$.Music.Track2") == 0);
    assert(aural_playlist_list_rename(&playlists, 0, "Driving"));
    assert(aural_playlist_catalog_save(file_name, &playlists));
    assert(aural_playlist_catalog_load(file_name, &loaded));
    assert(loaded.count == 1);
    assert(strcmp(loaded.items[0].name, "Driving") == 0);
    assert(loaded.items[0].count == 2);
    assert(strcmp(loaded.items[0].paths[0],
        "ADFS::HardDisc4.$.Music.Track2") == 0);
    assert(aural_playlist_remove_at(&loaded.items[0], 0));
    assert(loaded.items[0].count == 1);
    assert(aural_playlist_list_remove_at(&loaded, 0));
    assert(loaded.count == 0);
    aural_playlist_list_destroy(&playlists);
    aural_playlist_list_destroy(&loaded);
    (void) remove(file_name);
}

static void write_le16(unsigned char *bytes, unsigned int value)
{
    bytes[0] = (unsigned char) value;
    bytes[1] = (unsigned char) (value >> 8);
}

static void write_le32(unsigned char *bytes, unsigned int value)
{
    bytes[0] = (unsigned char) value;
    bytes[1] = (unsigned char) (value >> 8);
    bytes[2] = (unsigned char) (value >> 16);
    bytes[3] = (unsigned char) (value >> 24);
}

static void test_audio_probe(void)
{
    static const char wav_name[] = "build/host/aural_probe.wav";
    static const char mp3_name[] = "build/host/aural_probe.mp3";
    unsigned char wav[44 + 176400];
    unsigned char mp3[4 + 1000 + 128];
    aural_track_entry track;
    FILE *file;

    assert(aural_audio_format_from_file(0x1AD, "Track") ==
        AURAL_AUDIO_FORMAT_MP3);
    assert(aural_audio_format_from_file(0xFB1, "Track") ==
        AURAL_AUDIO_FORMAT_WAV);
    assert(aural_audio_format_from_file(0xFFF, "Track.flac") ==
        AURAL_AUDIO_FORMAT_FLAC);
    assert(aural_audio_format_from_file(0xFFF, "Track.ogg") ==
        AURAL_AUDIO_FORMAT_OGG_VORBIS);

    memset(wav, 0, sizeof(wav));
    memcpy(wav, "RIFF", 4);
    write_le32(wav + 4, sizeof(wav) - 8);
    memcpy(wav + 8, "WAVEfmt ", 8);
    write_le32(wav + 16, 16);
    write_le16(wav + 20, 1);
    write_le16(wav + 22, 2);
    write_le32(wav + 24, 44100);
    write_le32(wav + 28, 176400);
    write_le16(wav + 32, 4);
    write_le16(wav + 34, 16);
    memcpy(wav + 36, "data", 4);
    write_le32(wav + 40, sizeof(wav) - 44);
    file = fopen(wav_name, "wb");
    assert(file != NULL);
    assert(fwrite(wav, sizeof(wav), 1, file) == 1);
    assert(fclose(file) == 0);
    assert(aural_audio_probe_file(
        wav_name, "Track.wav", sizeof(wav), 0xFB1, &track));
    assert(track.format == AURAL_AUDIO_FORMAT_WAV);
    assert(track.sample_rate_hz == 44100);
    assert(track.channels == 2);
    assert(track.bitrate_bps == 1411200);
    assert(track.duration_ms == 1000);
    assert(strcmp(track.title, "Track") == 0);

    memset(mp3, 0, sizeof(mp3));
    memcpy(mp3, "ID3", 3);
    mp3[3] = 3;
    mp3[9] = 15;
    memcpy(mp3 + 10, "APIC", 4);
    mp3[17] = 5;
    memcpy(mp3 + 20, "cover", 5);
    mp3[25] = 0xFF;
    mp3[26] = 0xFB;
    mp3[27] = 0x90;
    mp3[28] = 0x00;
    memcpy(mp3 + sizeof(mp3) - 128, "TAG", 3);
    memcpy(mp3 + sizeof(mp3) - 125, "Night Drive", 11);
    memcpy(mp3 + sizeof(mp3) - 95, "The Drivers", 11);
    memcpy(mp3 + sizeof(mp3) - 65, "Road Music", 10);
    memcpy(mp3 + sizeof(mp3) - 35, "2026", 4);
    mp3[sizeof(mp3) - 3] = 0;
    mp3[sizeof(mp3) - 2] = 7;
    file = fopen(mp3_name, "wb");
    assert(file != NULL);
    assert(fwrite(mp3, sizeof(mp3), 1, file) == 1);
    assert(fclose(file) == 0);
    assert(aural_audio_probe_file(
        mp3_name, "07 Night Drive.mp3", sizeof(mp3), 0x1AD, &track));
    assert(track.format == AURAL_AUDIO_FORMAT_MP3);
    assert(strcmp(track.title, "Night Drive") == 0);
    assert(strcmp(track.artist, "The Drivers") == 0);
    assert(strcmp(track.album, "Road Music") == 0);
    assert(track.year == 2026);
    assert(track.track_number == 7);
    assert(track.sample_rate_hz == 44100);
    assert(track.channels == 2);
    assert(track.bitrate_bps == 128000);

    snprintf(track.title, sizeof(track.title), "Edited Night Drive");
    snprintf(track.artist, sizeof(track.artist), "Edited Drivers");
    snprintf(track.album, sizeof(track.album), "Edited Road Music");
    snprintf(track.album_artist, sizeof(track.album_artist), "Various");
    snprintf(track.genre, sizeof(track.genre), "Electronic");
    snprintf(track.comment, sizeof(track.comment), "Written by Aural");
    snprintf(track.tags, sizeof(track.tags), "night,driving");
    track.track_number = 3;
    track.disc_number = 2;
    track.year = 2025;
    track.rating = 4;
    assert(aural_metadata_write_file(&track) == AURAL_METADATA_WRITE_OK);
    file = fopen(mp3_name, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    track.size_bytes = (uint64_t) ftell(file);
    assert(fclose(file) == 0);
    assert(aural_audio_probe_file(
        mp3_name, "03 Edited.mp3", track.size_bytes, 0x1AD, &track));
    assert(strcmp(track.title, "Edited Night Drive") == 0);
    assert(strcmp(track.artist, "Edited Drivers") == 0);
    assert(strcmp(track.album, "Edited Road Music") == 0);
    assert(strcmp(track.album_artist, "Various") == 0);
    assert(strcmp(track.genre, "Electronic") == 0);
    assert(strcmp(track.comment, "Written by Aural") == 0);
    assert(strcmp(track.tags, "night,driving") == 0);
    assert(track.track_number == 3);
    assert(track.disc_number == 2);
    assert(track.year == 2025);
    assert(track.rating == 4);
    assert(track.sample_rate_hz == 44100);
    file = fopen(mp3_name, "rb");
    assert(file != NULL);
    {
        unsigned char rewritten[2048];
        size_t rewritten_size = fread(
            rewritten, 1, sizeof(rewritten), file);
        size_t index;
        bool found_artwork = false;

        for (index = 0; index + 4 <= rewritten_size; ++index) {
            if (memcmp(rewritten + index, "APIC", 4) == 0) {
                found_artwork = true;
                break;
            }
        }
        assert(found_artwork);
    }
    assert(fclose(file) == 0);

    (void) remove(wav_name);
    (void) remove(mp3_name);
}

int main(void)
{
    test_track_entry();
    test_track_catalog();
    test_playlists();
    test_audio_probe();
    puts("All Aural track-catalogue tests passed.");
    return 0;
}
