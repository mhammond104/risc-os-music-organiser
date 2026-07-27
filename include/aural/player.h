#ifndef AURAL_PLAYER_H
#define AURAL_PLAYER_H

#include <stdbool.h>

#include "aural/track_entry.h"
#include "oslib/os.h"

typedef enum aural_player_state {
    AURAL_PLAYER_STOPPED = 0,
    AURAL_PLAYER_PLAYING,
    AURAL_PLAYER_PAUSED
} aural_player_state;

typedef struct aural_player {
    aural_player_state state;
    char current_path[AURAL_PATH_CAPACITY];
    uint64_t duration_ms;
    uint64_t position_ms;
    os_t started_cs;
    unsigned int volume;
    bool seen_active;
    bool stop_requested;
} aural_player;

void aural_player_init(aural_player *player);
os_error *aural_player_play(
    aural_player *player,
    const aural_track_entry *track
);
os_error *aural_player_toggle_pause(aural_player *player);
os_error *aural_player_stop(aural_player *player);
os_error *aural_player_seek(
    aural_player *player,
    unsigned int percent
);
os_error *aural_player_set_volume(
    aural_player *player,
    unsigned int volume
);
os_error *aural_player_refresh(
    aural_player *player,
    bool *finished
);
uint64_t aural_player_position_ms(const aural_player *player);
bool aural_player_is_current(
    const aural_player *player,
    const aural_track_entry *track
);

#endif
