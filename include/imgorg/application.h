#ifndef IMGORG_APPLICATION_H
#define IMGORG_APPLICATION_H

#include <stdbool.h>

#include "imgorg/browser_window.h"
#include "oslib/wimp.h"

typedef struct imgorg_application {
    wimp_t task_handle;
    wimp_i iconbar_icon;
    bool quit;
    imgorg_browser_window browser;
} imgorg_application;

os_error *imgorg_application_initialise(imgorg_application *application);
os_error *imgorg_application_run(imgorg_application *application);
void imgorg_application_finalise(imgorg_application *application);

#endif
