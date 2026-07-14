#ifndef IMGORG_DIRECTORY_SCANNER_H
#define IMGORG_DIRECTORY_SCANNER_H

#include <stdbool.h>

#include "imgorg/image_entry.h"
#include "imgorg/image_list.h"
#include "oslib/os.h"

typedef struct imgorg_directory_scanner {
    char path[IMGORG_PATH_CAPACITY];
    int context;
    bool active;
} imgorg_directory_scanner;

void imgorg_directory_scanner_init(imgorg_directory_scanner *scanner);
bool imgorg_directory_scanner_start(
    imgorg_directory_scanner *scanner,
    const char *path
);
os_error *imgorg_directory_scanner_step(
    imgorg_directory_scanner *scanner,
    imgorg_image_list *images,
    bool *changed
);

#endif
