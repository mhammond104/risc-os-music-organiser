#include <stdio.h>
#include <stdlib.h>

#include "imgorg/application.h"
#include "oslib/os.h"

static void report_error(const os_error *error)
{
    if (error != NULL) {
        fprintf(stderr, "Focal: %s\n", error->errmess);
    }
}

int main(void)
{
    imgorg_application application;
    os_error *error;

    error = imgorg_application_initialise(&application);
    if (error == NULL) {
        error = imgorg_application_run(&application);
    }

    imgorg_application_finalise(&application);
    report_error(error);

    return error == NULL ? EXIT_SUCCESS : EXIT_FAILURE;
}
