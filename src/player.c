#include "aural/player.h"

#include <kernel.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    AURAL_AMPLAYER_INFO = 0x52e04,
    AURAL_AMPLAYER_DORMANT = 0,
    AURAL_AMPLAYER_LOCATING = 2,
    AURAL_AMPLAYER_PLAYING = 3,
    AURAL_AMPLAYER_PAUSING = 4,
    AURAL_AMPLAYER_STOPPING = 5,
    AURAL_AMPLAYER_FIB_TOTAL_VALID = 1u << 0,
    AURAL_AMPLAYER_FIB_ELAPSED_VALID = 1u << 1,
    AURAL_AMPLAYER_FIB_TOTAL_CS = 2,
    AURAL_AMPLAYER_FIB_ELAPSED_CS = 3,
    AURAL_AMPLAYER_FIB_VOLUME = 11
};

static os_error aural_player_unsupported = {
    0,
    "Aural can currently play MP3 tracks. Support for the other imported formats will follow."
};

static os_error aural_player_bad_path = {
    0,
    "Aural cannot play a track whose filename contains a quote character."
};

static bool aural_player_command_path(
    char *command,
    size_t capacity,
    const char *prefix,
    const char *path
)
{
    if (strchr(path, '"') != NULL) {
        return false;
    }
    return snprintf(command, capacity, "%s \"%s\"", prefix, path) <
        (int) capacity;
}

void aural_player_init(aural_player *player)
{
    if (player != NULL) {
        memset(player, 0, sizeof(*player));
        player->volume = 113;
    }
}

os_error *aural_player_play(
    aural_player *player,
    const aural_track_entry *track
)
{
    char command[AURAL_PATH_CAPACITY + 32];
    os_error *error;

    if (player == NULL || track == NULL) {
        return NULL;
    }
    if (track->format != AURAL_AUDIO_FORMAT_MP3) {
        return &aural_player_unsupported;
    }
    if (!aural_player_command_path(
            command, sizeof(command), "AMPlay", track->path)) {
        return &aural_player_bad_path;
    }
    error = xos_cli(command);
    if (error == NULL) {
        snprintf(player->current_path, sizeof(player->current_path),
            "%s", track->path);
        player->state = AURAL_PLAYER_PLAYING;
        player->duration_ms = track->duration_ms;
        player->position_ms = 0;
        player->seen_active = false;
        player->stop_requested = false;
        (void) xos_read_monotonic_time(&player->started_cs);
    }
    return error;
}

os_error *aural_player_toggle_pause(aural_player *player)
{
    os_error *error;

    if (player == NULL || player->state == AURAL_PLAYER_STOPPED) {
        return NULL;
    }
    if (player->state == AURAL_PLAYER_PLAYING) {
        player->position_ms = aural_player_position_ms(player);
    }
    error = xos_cli(
        player->state == AURAL_PLAYER_PAUSED ?
            "AMPause -off" : "AMPause"
    );
    if (error == NULL) {
        player->state = player->state == AURAL_PLAYER_PAUSED ?
            AURAL_PLAYER_PLAYING : AURAL_PLAYER_PAUSED;
        if (player->state == AURAL_PLAYER_PLAYING) {
            (void) xos_read_monotonic_time(&player->started_cs);
        }
    }
    return error;
}

os_error *aural_player_stop(aural_player *player)
{
    os_error *error = NULL;

    if (player == NULL) {
        return NULL;
    }
    player->stop_requested = true;
    if (player->state != AURAL_PLAYER_STOPPED) {
        error = xos_cli("AMStop");
    }
    if (error == NULL) {
        player->state = AURAL_PLAYER_STOPPED;
        player->current_path[0] = '\0';
        player->duration_ms = 0;
        player->position_ms = 0;
        player->seen_active = false;
    }
    return error;
}

uint64_t aural_player_position_ms(const aural_player *player)
{
    if (player == NULL || player->state == AURAL_PLAYER_STOPPED) {
        return 0;
    }
    return player->position_ms > player->duration_ms &&
        player->duration_ms > 0 ?
        player->duration_ms : player->position_ms;
}

os_error *aural_player_refresh(
    aural_player *player,
    bool *finished
)
{
    _kernel_swi_regs registers;
    _kernel_oserror *kernel_error;
    const unsigned int *fib;
    int status;

    if (finished != NULL) {
        *finished = false;
    }
    if (player == NULL) {
        return NULL;
    }
    memset(&registers, 0, sizeof(registers));
    kernel_error = _kernel_swi(
        AURAL_AMPLAYER_INFO, &registers, &registers);
    if (kernel_error != NULL) {
        return (os_error *) kernel_error;
    }
    status = registers.r[0];
    fib = (const unsigned int *) (uintptr_t) registers.r[2];
    if ((status == AURAL_AMPLAYER_LOCATING ||
         status == AURAL_AMPLAYER_PLAYING ||
         status == AURAL_AMPLAYER_PAUSING) &&
        fib != NULL) {
        unsigned int flags = fib[0];

        player->seen_active = true;
        player->stop_requested = false;
        if ((flags & AURAL_AMPLAYER_FIB_TOTAL_VALID) != 0) {
            player->duration_ms =
                (uint64_t) fib[AURAL_AMPLAYER_FIB_TOTAL_CS] * 10u;
        }
        if ((flags & AURAL_AMPLAYER_FIB_ELAPSED_VALID) != 0) {
            player->position_ms =
                (uint64_t) fib[AURAL_AMPLAYER_FIB_ELAPSED_CS] * 10u;
        }
        player->volume = fib[AURAL_AMPLAYER_FIB_VOLUME];
        if (player->volume > 127) {
            player->volume = 127;
        }
        player->state = status == AURAL_AMPLAYER_PAUSING ?
            AURAL_PLAYER_PAUSED : AURAL_PLAYER_PLAYING;
        return NULL;
    }
    if ((status == AURAL_AMPLAYER_DORMANT ||
         status == AURAL_AMPLAYER_STOPPING) &&
        player->seen_active && !player->stop_requested &&
        player->current_path[0] != '\0') {
        player->state = AURAL_PLAYER_STOPPED;
        player->seen_active = false;
        if (finished != NULL) {
            *finished = true;
        }
    }
    return NULL;
}

os_error *aural_player_seek(
    aural_player *player,
    unsigned int percent
)
{
    char command[64];
    uint64_t seconds;
    os_error *error;

    if (player == NULL || player->state == AURAL_PLAYER_STOPPED) {
        return NULL;
    }
    if (percent > 100) {
        percent = 100;
    }
    player->position_ms = player->duration_ms * percent / 100u;
    seconds = player->position_ms / 1000u;
    snprintf(command, sizeof(command), "AMLocate %lu:%02lu",
        (unsigned long) (seconds / 60u),
        (unsigned long) (seconds % 60u));
    error = xos_cli(command);
    if (error == NULL) {
        (void) xos_read_monotonic_time(&player->started_cs);
    }
    return error;
}

os_error *aural_player_set_volume(
    aural_player *player,
    unsigned int volume
)
{
    char command[32];
    os_error *error;

    if (player == NULL) {
        return NULL;
    }
    if (volume > 127) {
        volume = 127;
    }
    snprintf(command, sizeof(command), "AMVolume %u", volume);
    error = xos_cli(command);
    if (error == NULL) {
        player->volume = volume;
    }
    return error;
}

bool aural_player_is_current(
    const aural_player *player,
    const aural_track_entry *track
)
{
    return player != NULL && track != NULL &&
        player->current_path[0] != '\0' &&
        strcmp(player->current_path, track->path) == 0;
}
