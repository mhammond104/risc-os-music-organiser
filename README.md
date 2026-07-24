# Focal

A native RISC OS image browser, viewer and lightweight collection organiser.

## Download

The packaged RISC OS application is available as
[`dist/!Focal.zip`](dist/!Focal.zip). The ZIP preserves the application
directory's RISC OS filetypes.

## Current status

This repository contains the first application milestone:

- native OSLib Wimp application
- custom resolution-aware application and iconbar icon
- iconbar menu with Quit
- browser window shell
- persistent folder and individual-image library imports
- incremental enumeration of JPEG, PNG and Sprite files
- progressive PNG, JPEG and Sprite thumbnails
- responsive three-pane library workspace with selection inspector
- Filer-style multiple selection, ratings, favourites and persistent Albums
- drag-and-drop loading and fit-to-window display of PNG and JPEG images
- clean `Message_Quit` handling
- portable image-list and directory-entry model
- host-side unit tests for the portable model
- RISC OS application-directory packaging

Directory contents are enumerated incrementally during null-event time. PNG and
JPEG thumbnails are progressively decoded into bounded in-memory sprites, with
visible rows processed first. JPEGs use an embedded EXIF preview when available;
generated thumbnails are cached below `<Choices$Write>.Focal.Thumbs` for later
visits. Native Sprite files are loaded through SpriteOp and rendered into the
same bounded thumbnail format. Full PNG and JPEG display is decoded by the
statically linked image libraries and plotted as a native RISC OS sprite.
The large, theme-aware main Focal window places library and organisation
controls to the left of a responsive thumbnail canvas, with selected-file
information on the right. Imported folders, images and future organisation
metadata are stored in `<Choices$Write>.Focal.Library`.

## Intended features

- scrollable thumbnail browser
- JPEG, PNG and Sprite support
- full-size image viewer
- zoom, pan, fit-to-window and previous/next navigation
- ratings, favourites and tags
- persistent thumbnail cache
- albums and search
- file operations through normal RISC OS conventions

## Repository layout

```text
app/!Focal/        RISC OS application directory
dist/              Packaged RISC OS application download
assets/            Original artwork used to generate application sprites
src/               Native application source
include/imgorg/     Public project headers
tests/             Portable host-side tests
docs/              Architecture and roadmap
tools/             Windows deployment and sprite-generation helpers
Makefile            RISC OS cross-build
Makefile.host       Host-side tests
```

The application directory is named `!Focal`. The internal C module namespace
still uses `imgorg` to keep the current public headers stable.

## Requirements

For a RISC OS build:

- GCCSDK cross-compiler
- OSLib
- libpng 1.6, libjpeg-turbo and zlib built for the GCCSDK environment
- GNU Make

The Makefile assumes the compiler is available as:

```sh
arm-unknown-riscos-gcc
```

It also uses `GCCSDK_INSTALL_ENV` to find the installed OSLib headers and
libraries. For a typical shell session, set:

```sh
export GCCSDK_INSTALL_CROSSBIN=/path/to/gccsdk/cross/bin
export GCCSDK_INSTALL_ENV=/path/to/gccsdk/env
export PATH="$GCCSDK_INSTALL_CROSSBIN:$PATH"
```

Override it when necessary:

```sh
make CC=/path/to/arm-unknown-riscos-gcc
```

## Build

```sh
make
```

The executable is written to:

```text
app/!Focal/!RunImage
```

Copy the complete `app/!Focal` directory to a RISC OS filesystem preserving
RISC OS filetypes. How filetypes are represented depends on the transfer method
and emulator/tooling in use.

## Open an image

Launch Focal, then drag one or more directories or supported images into its
library window to add them persistently. Dropping an image on the iconbar opens
it directly in a viewer without adding it to the library. Double-clicking PNG
or JPEG thumbnails opens independent image viewers,
leaving the populated browser open alongside them. Opening an image that is
already displayed brings its viewer to the front. Dropping an image onto an
existing viewer replaces only that viewer; dropping onto the iconbar or browser
opens another. Each viewer has independent fit, zoom and pan state. Closing a
viewer does not disturb the library browser or other viewers.
Single-clicking a thumbnail highlights its cell, and a loading window remains
visible while a full-resolution image is decoded. Viewer windows are created on
demand rather than limited to a fixed count. Before decoding another image, the
application checks its estimated peak memory requirement against the 128 MB Wimp
slot and asks the user to close viewers if the image would not fit safely.
Directory browsers show real PNG, JPEG and Sprite thumbnails.

The Library panel filters the grid by all photographs, imported folder, exact
1–5-star rating or favourites. Menu-click a thumbnail for Open and
Remove from library. Removal changes only Focal's catalogue and never deletes
the source image. A folder disappears from the panel when its last library
image is removed.

With an image open:

- use the toolbar arrows or Left/Right keys to navigate the current directory
- use the `100%` toolbar button for actual size
- use the `Fit` toolbar button for fit-to-window display
- use the `Full Screen` toolbar button to toggle desktop-sized viewing
- press `T` to show or hide that viewer's toolbar
- use the mouse wheel over the window to zoom from 10% to 800%
- Select-drag the image to pan it
- Adjust-click the image to return to fit-to-window

## Run portable tests

The model and image-list code deliberately avoid RISC OS dependencies.

```sh
make -f Makefile.host test
```

## GitHub setup

```sh
git init
git branch -M main
git add .
git commit -m "Add initial Focal application shell"
git remote add origin git@github.com:YOUR-USER/risc-os-image-organiser.git
git push -u origin main
```

## Licence

GPL-3.0-or-later. See `LICENSE`.
