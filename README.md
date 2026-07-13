# RISC OS Image Organiser

A native RISC OS image browser, viewer and lightweight collection organiser.

## Current status

This repository contains the first application milestone:

- native OSLib Wimp application
- iconbar icon
- iconbar menu with Quit
- browser window shell
- drag-and-drop loading and fit-to-window display of one PNG image
- clean `Message_Quit` handling
- portable image-list and directory-entry model
- host-side unit tests for the portable model
- RISC OS application-directory packaging

PNG display is decoded by the statically linked libpng library and plotted as a
native RISC OS sprite. The next milestone is directory drag-and-drop, directory
enumeration, and placeholder thumbnail cells.

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
src/               Native application source
include/imgorg/     Public project headers
tests/             Portable host-side tests
docs/              Architecture and roadmap
Makefile            RISC OS cross-build
Makefile.host       Host-side tests
```

The application directory is currently named `!ImgOrg` because classic RISC OS
leafnames are limited. The user-facing name is **Image Organiser**.

## Requirements

For a RISC OS build:

- GCCSDK cross-compiler
- OSLib
- libpng 1.6 and zlib built for the GCCSDK environment
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

## Open a PNG

Launch Image Organiser, then drag a PNG file onto either its iconbar icon or its
browser window. The image is loaded into memory and scaled to fit the window.
Dropping another PNG replaces the current image. The window title shows the
image name and the current zoom percentage, or `Fit` in fit-to-window mode.

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
