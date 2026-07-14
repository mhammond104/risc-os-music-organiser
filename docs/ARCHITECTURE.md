# Architecture

## Goals

Image Organiser is intended to feel native to RISC OS rather than imitating a
large cross-platform photo-management suite. Its core operations should follow
normal Wimp conventions: iconbar presence, filer drag-and-drop, menus, separate
viewer windows and cooperative background work.

## Layers

### Wimp application layer

`application.c` owns Wimp registration, polling, iconbar interaction and global
messages. Long-running work must be split into bounded jobs and serviced
between polls.

### Browser window

`browser_window.c` owns the thumbnail-window state, redraw processing, scrolling
and pointer interaction. It decodes one dropped PNG through libpng, retains the
result as a 32-bpp RISC OS sprite and plots it fit-to-window through core
SpriteOp calls; otherwise it draws a placeholder cell. Viewer state stores a
bounded zoom percentage and screen-coordinate pan offset, with Wimp scroll and
drag events driving those controls. While a pan drag is active, timed idle polls
sample the pointer and use immediate, non-clearing Wimp update loops so the image
follows the mouse continuously without flashing the window background first.

### Collection model

`image_entry.c` and `image_list.c` are platform-independent. They represent
discovered files and can be tested on the development host.

### Directory scanner

Directory drops are classified using RISC OS catalogue metadata and stored as
the browser's current source. The scanner enumerates that source in bounded
batches during null-event time and appends only supported filetypes, so large
directories do not block the desktop. Each batch updates the browser's
scrollable placeholder grid immediately.

### Thumbnail pipeline — next milestone

A queue will decode and scale a limited number of images during null-event
time. Completed thumbnails will be stored as sprites and redraw requests will
be issued for affected cells.

### Catalogue — later milestone

Ratings, tags and collections will remain external to image files initially.
The storage format will be versioned and designed so damaged catalogue data
cannot damage the source image collection.

## Error handling

OSLib `x...` calls are used where practical so errors can be returned to the
application boundary. Portable modules report allocation and validation
failure using boolean results.

## Testing

Portable collection, sorting, cache-key and catalogue modules should be covered
by host-side tests. Wimp integration requires testing under RISC OS or RPCEmu.
