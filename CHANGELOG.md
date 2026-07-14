# Changelog

## Unreleased

### Added

- Initial OSLib Wimp application shell.
- Text icon on the RISC OS iconbar.
- Browser window shell opened from the iconbar.
- Clean handling of `Message_Quit`.
- Portable image-entry and image-list modules.
- Host-side unit tests.
- RISC OS `!Boot` and `!Run` launch files.
- Initial architecture and development roadmap.
- Iconbar menu with a Quit command.
- Drag-and-drop loading and fit-to-window display of a single PNG image.
- Mouse-wheel zoom, smooth Select-drag panning and Adjust-click fit-to-window
  reset.
- Dynamic viewer titles showing the image name and zoom state.
- Custom resolution-aware application and iconbar sprites.
- Directory drag-and-drop with incremental JPEG, PNG and Sprite enumeration.
- Scrollable labelled placeholder cells populated from real directory entries.
- Static ELF-to-AIF conversion integrated into the normal RISC OS build.
- Progressive PNG thumbnail decoding with a bounded in-memory sprite store.
- Reduced-resolution libjpeg-turbo thumbnails in the in-memory sprite store.
- Embedded EXIF JPEG previews, visible-row prioritisation and a persistent,
  source-validated thumbnail cache.
- Full JPEG drag-and-drop viewing and double-click opening of PNG and JPEG
  thumbnails in the viewer.
- Double-click-aware thumbnail input and restoration of the populated directory
  browser when a thumbnail-opened viewer is closed.
- Single-click thumbnail highlighting and an immediate loading popup for
  full-resolution image decoding.
- Native Sprite-file thumbnails rendered into the bounded progressive thumbnail
  and persistent-cache pipeline.
- A separate image-viewer Wimp window with independent redraw, close, zoom, pan
  and file-drop routing, leaving the thumbnail browser active alongside it.
- Multiple simultaneous viewer windows with duplicate focusing and targeted
  replacement. Viewers are created on demand, with image-header preflight and
  tracked image and thumbnail memory preventing the 128 MB Wimp slot from being
  exceeded.
