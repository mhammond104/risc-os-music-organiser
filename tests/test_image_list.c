#include <assert.h>
#include <stdio.h>

#include "imgorg/image_entry.h"
#include "imgorg/image_list.h"

static void test_filetype_mapping(void)
{
    assert(imgorg_image_format_from_filetype(0xFF9) ==
           IMGORG_IMAGE_FORMAT_SPRITE);
    assert(imgorg_image_format_from_filetype(0xC85) ==
           IMGORG_IMAGE_FORMAT_JPEG);
    assert(imgorg_image_format_from_filetype(0xB60) ==
           IMGORG_IMAGE_FORMAT_PNG);
    assert(imgorg_image_format_from_filetype(0xFFF) ==
           IMGORG_IMAGE_FORMAT_UNKNOWN);
}

static void test_append_and_read(void)
{
    imgorg_image_list list;
    imgorg_image_entry entry;
    const imgorg_image_entry *stored;

    imgorg_image_list_init(&list);

    assert(imgorg_image_entry_init(
        &entry,
        "ADFS::HardDisc4.$.Pictures.Sample",
        "Sample",
        123456,
        0xC85
    ));
    assert(imgorg_image_list_append(&list, &entry));
    assert(list.count == 1);

    stored = imgorg_image_list_get(&list, 0);
    assert(stored != NULL);
    assert(stored->format == IMGORG_IMAGE_FORMAT_JPEG);
    assert(stored->size_bytes == 123456);
    assert(imgorg_image_list_get(&list, 1) == NULL);

    imgorg_image_list_clear(&list);
    assert(list.count == 0);

    imgorg_image_list_destroy(&list);
}

int main(void)
{
    test_filetype_mapping();
    test_append_and_read();

    puts("All image-list tests passed.");
    return 0;
}
