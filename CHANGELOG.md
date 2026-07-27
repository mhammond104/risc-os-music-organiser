# Changelog

## Unreleased

### Added

- All Albums, Recently Added, artist and genre navigation.
- Persistent playlists, a playlist track-table view, New/Rename/Remove
  Playlist actions, Add to Playlist menus, drag-to-playlist and manual track
  ordering.
- Real added timestamps on new imports for Recently Added ordering.
- Comma-separated AND search across the music catalogue, presented in the
  expanded central track table.
- Rating and favourite track actions and matching left-panel views.
- Functional AMPlayer seeking and volume controls, plus shuffle and repeat.
- A separately persisted play queue with Play Next and Add to Queue actions.
- Missing-source marking, typed-path relinking, Filer reveal, and persistent
  catalogue-only removal that never deletes source audio.
- Independent wheel scrolling for long artist, genre, playlist, album and
  track views, without moving the neighbouring panes.
- AMPlayer_Info-backed playback state, elapsed time, duration and volume,
  replacing the transport's estimated playback clock.
- Filer-style Adjust-click track multi-selection with bulk playlist, queue,
  rating, favourite and safe library-removal actions.
- Initial `!Aural` project fork using the proven native OSLib/Wimp foundation
  from `!Focal`.
- Independent Aural task identity, application directory, build target and
  `<Choices$Write>.Aural` catalogue namespace.
- Music-first product roadmap covering metadata, artwork, browsing, playlists,
  search, playback and library safety.
- Primary artist → album-art → track-list browser design, with metadata editing
  separated from the main three-pane navigation hierarchy.
- Portable track and source-folder collections with duplicate-path prevention.
- Versioned `<Choices$Write>.Aural.Tracks` catalogue with atomic replacement
  and complete editable/source music metadata round-tripping.
- Dedicated music-only Wimp window with artist navigation, a responsive album
  artwork grid, selected-album track list and persistent playback-control
  strip.
- Removed Focal's image viewer, thumbnail browser and PNG/JPEG dependencies
  from the Aural application build.
- Incremental recursive music-source scanning with persistent refresh,
  duplicate-safe track insertion and live library redraw.
- RISC OS AMPEG (`&1AD`), WaveForm (`&FB1`), AIFF (`&FC2`) and MIDI (`&FD4`)
  detection, plus extension fallback for MP3, WAV, FLAC, Ogg, AIFF and MIDI.
- MP3 ID3v2/ID3v1 metadata and frame-property probing, WAV RIFF technical
  metadata probing, and artist/album/title fallback from directory structure.
- Alphabetically ordered artists/albums and disc/track ordered album contents.
- Wimp theme texture retained beneath the centre album-art browser.
- Selectable track rows with a distinct selected and currently-playing state.
- Native AMPlayer MP3 playback on double-click, with previous, play/pause,
  next and stop controls plus a now-playing readout.
- Bottom-aligned transport strip and fixed at-a-glance metadata inspector
  beneath the selected album's track list.
- Centred Track Information editor for persistent title, artist, album,
  album artist, numbering, year, genre, rating, tags and comments.
- Atomic MP3 ID3v2 source-file write-back from Track Information, preserving
  unrelated frames such as embedded artwork and retaining the original file
  until the complete replacement has been written successfully.
- Persistent album-art assignment by dropping a PNG or JPEG from the Filer
  onto an album tile, with native sprite decoding and cached rendering.
- Labelled Thumbnail Size slider and responsive album-grid reflow.
- Dedicated Album Information dialogue for applying album title, album
  artist, year and genre changes across every track in the selected album.
- Album-wide MP3 tag write-back, with catalogue-only updates retained for
  formats that do not yet have a source metadata writer.
- Managed artwork storage under `<Choices$Write>.Aural.Artwork`, plus album
  artwork replacement and removal.

### Fixed

- Track Information Save and Cancel buttons now recognise Wimp
  `CLICK_SELECT` events instead of silently ignoring them.
- Restored the main window's bottom-right resize furniture.
- Increased spacing between the Thumbnail Size label and slider.
- Returned the main library window to a fixed centred size after resize-time
  redraw glitches proved too disruptive.
- Artist navigation now groups by Album Artist when present and immediately
  follows album-artist changes made in Album Information.

- Catalogue saves now use a RISC OS-safe sibling `TracksTmp` filename rather
  than treating `.tmp` as a pathname component.

### Notes

- Image-specific browser and viewer internals remain temporarily during the
  bootstrap and will be replaced incrementally by the track catalogue,
  track-list/album views and audio player.
