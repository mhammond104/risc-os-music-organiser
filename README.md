# Aural

`!Aural` is a native RISC OS music-library browser, manager, organiser and
player inspired by the clarity of classic iTunes while retaining a proper
RISC OS Wimp appearance.

The project is based on the proven application and catalogue architecture of
`!Focal`. Shared ideas include the fixed three-pane workspace, persistent
non-destructive library, ratings, favourites, multi-selection and
collections. Aural is being given a music-specific catalogue and playback
model rather than treating audio files as another image format.

## Intended workspace

- Left: artist navigation, plus Library, Genres, Playlists, ratings and
  favourites.
- Centre: album artwork for the selected artist or library section.
- Right: the selected album's ordered track list.
- Playback strip: previous, play/pause, next, progress, volume and now-playing
  information.

Metadata editing is opened explicitly for the selected track or album rather
than permanently occupying the track-list panel.

The primary action is to play or queue a track. Library removal is
catalogue-only and must never delete the source audio file.

All Albums, Recently Added, artist and genre selections retain the album-art
browser. Selecting a playlist switches the centre pane to an ordered track
table. Tracks can be added through their Menu-click submenu or by dragging
them onto a playlist; playlists can be created, renamed and removed without
affecting source audio.

Search accepts comma-separated terms and matches every term across title,
artist, album artist, album, genre, comments and source path. Search results,
ratings, favourites, playlists and the persistent play queue use the expanded
central track-table view. Long navigation, album and track lists scroll
independently according to the pane beneath the pointer.

Tracks support Filer-style Adjust-click multi-selection. Playlist and queue
addition, ratings, favourites, playlist removal and safe library removal
apply to the selected group.

The transport reads authoritative status, elapsed time, duration and volume
from AMPlayer and provides seeking, volume, shuffle and repeat. Track
menus also provide Play Next, Add to Queue, safe catalogue-only removal,
Filer reveal and relinking for missing source files.

As in `!Focal`, dragging a directory onto the iconbar icon or main window adds
it persistently to the library. Aural scans imported music sources
incrementally and prevents duplicate path entries.

## Current status

The repository has been forked from the stable `!Focal` Wimp foundation.
`!Aural` has its own application identity, application directory and
`<Choices$Write>.Aural` catalogue namespace. Work is beginning on the portable
music metadata model before the inherited image-browser components are
replaced.

See [docs/ROADMAP.md](docs/ROADMAP.md) for the implementation sequence.

## Building

The RISC OS cross-build uses GCCSDK, OSLib and the existing static application
pipeline:

```sh
make all
```

Portable host-side model tests use:

```sh
make -f Makefile.host test
```

The RISC OS application directory is produced at `app/!Aural`.
