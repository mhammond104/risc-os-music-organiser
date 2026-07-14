# RISC OS Image Organiser

A native RISC OS image browser, viewer and lightweight collection organiser.

## Current status

This repository contains the first application milestone:

- native OSLib Wimp application
- custom resolution-aware application and iconbar icon
- iconbar menu with Quit
- browser window shell
- directory drag-and-drop
- incremental enumeration of JPEG, PNG and Sprite files
- progressive in-memory PNG and JPEG thumbnails with Sprite placeholders
- drag-and-drop loading and fit-to-window display of PNG and JPEG images
- clean `Message_Quit` handling
- portable image-list and directory-entry model
- host-side unit tests for the portable model
- RISC OS application-directory packaging

Directory contents are enumerated incrementally during null-event time. PNG and
JPEG thumbnails are progressively decoded into bounded in-memory sprites, with
visible rows processed first. JPEGs use an embedded EXIF preview when available;
generated thumbnails are cached below `<Choices$Write>.ImgOrg.Thumbs` for later
visits. Sprite files retain labelled placeholders. Full PNG and JPEG display is
decoded by the statically linked image libraries and plotted as a native RISC OS
sprite.

## Intended features

- scrollable thumbnail browser
- JPEG, PNG and Sprite support
- full-size image viewer
- zoom, pan, fit-to-window and previous/next navigation
- ratings, favourites and tags
- persistent thumbnail cache
- saved collections and search
- file operations through normal RISC OS conventions

## Repository layout

```text
app/!ImgOrg/       RISC OS application directory
assets/            Original artwork used to generate application sprites
src/               Native application source
include/imgorg/     Public project headers
tests/             Portable host-side tests
docs/              Architecture and roadmap
tools/             Windows deployment and sprite-generation helpers
Makefile            RISC OS cross-build
Makefile.host       Host-side tests
```

The application directory is currently named `!ImgOrg` because classic RISC OS
leafnames are limited. The user-facing name is **Image Organiser**.

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
app/!ImgOrg/!RunImage
```

Copy the complete `app/!ImgOrg` directory to a RISC OS filesystem preserving
RISC OS filetypes. How filetypes are represented depends on the transfer method
and emulator/tooling in use.

## Open an image

Launch Image Organiser, then drag a directory, PNG or JPEG file onto either its
iconbar icon or its browser window. A directory becomes the browser's current
source. Double-clicking a PNG or JPEG thumbnail opens it in the same image
viewer. An image is loaded into memory and scaled to fit the window. Dropping
another source replaces the current one. The window title shows the image name
and the current zoom percentage, or `Fit` in fit-to-window mode. Closing an
image opened from a thumbnail returns to the same populated directory browser.
Single-clicking a thumbnail highlights its cell, and a loading window remains
visible while a full-resolution image is decoded.
Directory browsers show real PNG and JPEG thumbnails and labelled format
placeholders for Sprite files while Sprite loading is developed.

With an image open:

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
git commit -m "Add initial RISC OS Image Organiser application shell"
git remote add origin git@github.com:YOUR-USER/risc-os-image-organiser.git
git push -u origin main
```

## Licence

GPL-3.0-or-later. See `LICENSE`.
