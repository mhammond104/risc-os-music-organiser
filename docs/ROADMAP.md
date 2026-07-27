# !Aural roadmap

## 0.1 — native application foundation

- [x] fork the proven !Focal OSLib/Wimp application shell
- [x] separate !Aural application identity and Choices catalogue
- [x] replace the inherited image workspace with a dedicated music UI
- [x] initial three-pane artist → album-art → track-list workspace
- [ ] alternate Songs and album-grid library views

## 0.2 — music catalogue

- [x] portable track-entry and dynamic collection model
- [x] versioned atomic music catalogue with complete metadata round-tripping
- [x] persistent recursive drag-directory and individual-track import inherited from
  !Focal
- [x] RISC OS filetype and extension-based audio format detection
- [x] editable title, artist, album, album artist, track/disc number, year,
  genre and comments
- [x] atomic MP3 ID3v2 metadata write-back with unrelated-frame preservation
- [x] initial duration, sample rate, channel count, bitrate and source-path
  extraction for MP3 and WAV
- [x] ratings and favourites
- [x] persistent manual playlists with drag/drop, menus and ordering
- [ ] smart playlists
- [x] multi-term catalogue search across music metadata
- [x] Adjust-click track multi-selection and bulk library actions

## 0.3 — artwork and browsing

- [ ] embedded and sidecar cover-art discovery
- [x] persistent manual PNG/JPEG album-art assignment by Filer drag-and-drop
- [x] managed Choices artwork copies and album-level artwork removal
- [x] in-memory native sprite artwork thumbnail cache
- [x] adjustable album thumbnail sizing and responsive grid
- [ ] album grouping and compilation handling
- [x] Artists, All Albums, Genres and Recently Added navigation
- [x] ordered, selectable album track list with number, title and duration
- [x] independently scrollable long navigation, album and track views
- [ ] sortable columns in the alternate Songs view
- [ ] multi-selection metadata editing
- [x] album-wide title, album artist, year and genre editor
- [x] at-a-glance selected-track metadata inspector

## 0.4 — playback

- [x] initial native MP3 backend using AMPlayer
- [x] play, pause/resume, stop, previous and next
- [x] seek and volume controls
- [x] persistent play queue
- [x] shuffle and repeat modes
- [ ] now-playing display with artwork and metadata (metadata complete)
- [x] bottom transport and now-playing strip
- [x] playback continues while the library is browsed
- [x] authoritative AMPlayer status, position, duration and volume polling

## 0.5 — library safety

- [ ] catalogue backup and recovery
- [x] missing-file detection and relinking
- [x] reveal track in Filer
- [x] remove from library without deleting source audio
- [ ] optional metadata write-back with explicit confirmation

## Later

- gapless playback
- ReplayGain
- internet radio
- CD import
- duplicate detection
- plugin or converter integration
