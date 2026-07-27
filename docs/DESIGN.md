# !Aural product and technical design

## Product shape

!Aural should feel like a native RISC OS interpretation of classic iTunes:
information-dense, direct and library-centred. It is not a skinned copy.
Standard Wimp furniture, themed surfaces, bevelled controls and RISC OS
selection conventions remain important.

The default main window is an artist → album → track browser with three
persistent areas:

1. The left panel contains the artist list, with Library, Genres, ratings,
   favourites and playlists available as alternate navigation sources.
2. The centre is a cover-art album grid. Selecting an artist limits the grid
   to that artist's albums; selecting a broader Library section can show all
   matching albums.
3. The right panel is a track list for the selected album, ordered by disc and
   track number. It is wider than !Focal's Inspector and shows at least track
   number, title and duration.

This master-detail-detail hierarchy keeps browsing visual while making the
actual playable items immediately available. A dedicated metadata dialogue
edits the selected track or album. Multi-selection editing exposes only fields
that can safely be applied in bulk.

A playback strip remains available while browsing. Double-clicking a track in
the right panel starts playback; Select chooses it; Adjust extends the
selection; Menu exposes queue, playlist, metadata and catalogue actions.

Selecting an album updates the track panel without starting playback.
Double-clicking album art starts the album from its first track. The currently
playing track remains highlighted even when the user browses to another
artist or album.

## Catalogue principles

- The catalogue is persistent and independent of the source files.
- Dragging a directory onto the iconbar icon or main window adds it as a
  persistent library source and starts an incremental recursive track scan.
- Re-importing a known source refreshes it without creating duplicate tracks.
- Individual audio files may also be dragged into the main window to add them.
- Removing a track from the library never deletes the audio file.
- Metadata edits initially update only the catalogue.
- Optional source-file write-back is a later, explicit and confirmable action.
- Paths are retained so missing files can be reported and relinked.
- Catalogue versions and atomic replacement follow the !Focal design.

The track model distinguishes editable descriptive metadata from measured
audio properties:

| Editable metadata | Measured/source data |
| --- | --- |
| title | path and RISC OS filetype |
| artist and album artist | byte size and duration |
| album | audio format |
| track/disc number and totals | sample rate and channel count |
| year, genre and comments | bitrate |
| rating and favourite | artwork source/cache identity |

## Search

Search uses comma-separated AND terms, matching each term against title,
artist, album, album artist, genre, comments and source path. Later smart
playlists reuse the same predicate engine with additional
numeric comparisons for year, rating, duration and date added.

## Playback boundary

The catalogue and user interface must not depend directly on a particular
decoder. Playback sits behind a small backend interface responsible for:

- probing whether a format is playable;
- opening and closing a track;
- play, pause, stop and seek;
- volume;
- current position and duration;
- end-of-track and error notifications.

This keeps the library useful even when a particular codec is unavailable and
allows the initial backend to be replaced without rewriting the Wimp UI.
