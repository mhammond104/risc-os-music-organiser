# Architecture

## Goals

Focal is intended to feel native to RISC OS rather than imitating a
large cross-platform photo-management suite. Its core operations should follow
normal Wimp conventions: iconbar presence, filer drag-and-drop, menus, separate
viewer windows and cooperative background work.

## Layers

### Wimp application layer

`application.c` owns Wimp registration, polling, iconbar interaction and global
messages. Long-running work must be split into bounded jobs and serviced
between polls.
The application icon is generated at each supported desktop resolution as a
32-bpp Sprite with an 8-bpp alpha mask, preserving smooth transparent edges.

### Browser window

`browser_window.c` owns a three-pane library workspace and dynamically allocated
image-viewer Wimp windows, their redraw processing, scrolling and pointer interaction.
The workspace opens at almost the full desktop size and keeps library and
organisation navigation fixed on the left,
lays out one to three thumbnail columns responsively in the scrolling centre,
and keeps a selected-file inspector fixed on the right. Panel and thumbnail
frames are Wimp icons so the active desktop theme supplies native texture and
bevelled edges. Scroll and resize operations force a complete workspace redraw,
preventing the Wimp's scroll-copy optimisation from moving either fixed panel.
The browser asks the Wimp to ignore both work-area extents when constraining
window resizing, so neither axis can trap the window at a smaller size. The
fixed centre header is filled before its label is drawn, preventing scrolling
thumbnails from showing through it. Library navigation applies non-destructive
all, folder, exact 1–5-star and favourite filters to the central grid. It decodes
dropped or double-clicked PNG and JPEG images through libpng and libjpeg-turbo,
retains the result as a 32-bpp RISC OS sprite and plots it fit-to-window through
core SpriteOp calls. Viewer state stores a
bounded zoom percentage and screen-coordinate pan offset, with Wimp scroll and
drag events driving those controls. While a pan drag is active, timed idle polls
sample the pointer and use immediate, non-clearing Wimp update loops so the image
follows the mouse continuously without flashing the window background first.
Opening a thumbnail leaves its directory list, decoded thumbnails and scanner
active in the separate browser window; closing the viewer leaves that browser
untouched. A
small Wimp loading window is explicitly redrawn before synchronous full-image
decoding, so lengthy decodes provide feedback even while polling is paused.
Each viewer owns its image, title, fit, zoom, pan and drag state. Duplicate open
requests focus the existing viewer, while drops onto a viewer replace that
viewer's image. A native icon toolbar provides directory navigation, zoom mode
controls and a fullscreen toggle which restores the previous window rectangle
on exit. Viewers open centred and ask the Wimp to ignore both work extents while
resizing. They claim an invisible caret on interaction so Left, Right and T
shortcuts are routed to the intended window. Viewer windows have no fixed
count limit. Before opening one,
the application
reads the image header and estimates both its retained sprite size and the peak
temporary decoder storage. Existing viewer sprites and thumbnail sprite areas
are tracked, with a reserve for the application, libraries and Wimp structures;
an image whose estimated peak would exceed the 128 MB Wimp slot is refused with
a recoverable error box. Closed viewer structures and Wimp windows are reused.

### Collection model

`image_entry.c` and `image_list.c` are platform-independent. They represent
discovered files and can be tested on the development host.

### Directory scanner

Directory drops are classified using RISC OS catalogue metadata and appended
to a persistent folder list. The scanner enumerates queued folders in bounded
batches during null-event time and appends only supported, previously unknown
paths, so large directories do not block the desktop. Individual supported
images can be appended through the same drag-and-drop route. Each batch updates
the browser's scrollable placeholder grid immediately.
Imported folders are retained as catalogue sources but are not automatically
re-imported at startup, ensuring that a catalogue-only image removal remains
removed. Empty folder sources are pruned from navigation. A later explicit
rescan operation can refresh a source on demand.

### Thumbnail pipeline

The browser decodes one queued PNG or JPEG during each null-event slice,
downsamples it to a bounded native sprite and retains it in memory for the
current directory. Visible rows are prioritised over the sequential background
queue. PNGs use the linked libpng decoder. JPEGs use an embedded EXIF preview
when present, otherwise libjpeg-turbo's reduced-resolution IDCT before a final
thumbnail-sized downsample. Completed thumbnails are stored below
`<Choices$Write>.Focal.Thumbs`; cache keys and headers include the source path,
size and RISC OS timestamp so stale entries are ignored. Native Sprite files are
loaded into temporary user areas and rendered through SpriteOp into the same
bounded 32-bpp thumbnail representation before the source area is released.

### Catalogue

`library_catalog.c` stores the folder list, image identities and reserved
rating, favourite and tag fields in the versioned
`<Choices$Write>.Focal.Library` file. Catalogue writes use a temporary sibling
and rename, while malformed input is rejected without touching source images.

## Error handling

OSLib `x...` calls are used where practical so errors can be returned to the
application boundary. Portable modules report allocation and validation
failure using boolean results.

## Testing

Portable collection, sorting, cache-key and catalogue modules should be covered
by host-side tests. Wimp integration requires testing under RISC OS or RPCEmu.
