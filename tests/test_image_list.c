#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "imgorg/image_entry.h"
#include "imgorg/image_list.h"
#include "imgorg/library_catalog.h"

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
        0xFFF12345,
        0x6789ABCD,
        0xC85
    ));
    assert(imgorg_image_list_append(&list, &entry));
    assert(list.count == 1);

    stored = imgorg_image_list_get(&list, 0);
    assert(stored != NULL);
    assert(stored->format == IMGORG_IMAGE_FORMAT_JPEG);
    assert(stored->size_bytes == 123456);
    assert(stored->load_addr == 0xFFF12345);
    assert(stored->exec_addr == 0x6789ABCD);
    assert(imgorg_image_list_get(&list, 1) == NULL);

    imgorg_image_list_clear(&list);
    assert(list.count == 0);

    imgorg_image_list_destroy(&list);
}

static void test_unique_append(void)
{
    imgorg_image_list list;
    imgorg_image_entry entry;
    bool added;

    imgorg_image_list_init(&list);
    assert(imgorg_image_entry_init(
        &entry, "ADFS::HardDisc4.$.Pictures.One", "One",
        42, 1, 2, 0xB60
    ));
    assert(imgorg_image_list_append_unique(&list, &entry, &added));
    assert(added);
    assert(imgorg_image_list_append_unique(&list, &entry, &added));
    assert(!added);
    assert(list.count == 1);
    assert(imgorg_image_list_find_path(&list, entry.path) == 0);
    assert(!imgorg_image_list_remove_at(&list, 1));
    assert(imgorg_image_list_remove_at(&list, 0));
    assert(list.count == 0);
    imgorg_image_list_destroy(&list);
}

static void test_tags(void)
{
    imgorg_image_entry entry;
    char normalised[IMGORG_TAG_NAME_CAPACITY];
    bool changed;

    assert(imgorg_image_entry_init(
        &entry, "ADFS::HardDisc4.$.Pictures.One", "One",
        42, 1, 2, 0xB60
    ));
    assert(imgorg_tag_name_normalise(
        normalised, sizeof(normalised), "  Winter holiday  "
    ));
    assert(strcmp(normalised, "Winter holiday") == 0);
    assert(!imgorg_tag_name_normalise(
        normalised, sizeof(normalised), "bad,tag"
    ));
    assert(imgorg_image_entry_add_tag(&entry, "Winter", &changed));
    assert(changed);
    assert(strcmp(entry.tags, "Winter") == 0);
    assert(imgorg_image_entry_add_tag(&entry, "winter", &changed));
    assert(!changed);
    assert(imgorg_image_entry_add_tag(&entry, "Landscape", &changed));
    assert(changed);
    assert(strcmp(entry.tags, "Winter,Landscape") == 0);
    assert(imgorg_image_entry_has_tag(&entry, " landscape "));
    assert(imgorg_image_entry_remove_tag(&entry, "WINTER", &changed));
    assert(changed);
    assert(strcmp(entry.tags, "Landscape") == 0);
    assert(imgorg_image_entry_remove_tag(&entry, "Portrait", &changed));
    assert(!changed);
}

static void test_catalog_round_trip(void)
{
    static const char file_name[] = "build/host/focal_catalog_test";
    imgorg_folder_list folders;
    imgorg_folder_list loaded_folders;
    imgorg_image_list images;
    imgorg_image_list loaded_images;
    imgorg_album_list albums;
    imgorg_album_list loaded_albums;
    imgorg_image_entry entry;
    bool added;

    (void) remove(file_name);
    imgorg_folder_list_init(&folders);
    imgorg_folder_list_init(&loaded_folders);
    imgorg_image_list_init(&images);
    imgorg_image_list_init(&loaded_images);
    imgorg_album_list_init(&albums);
    imgorg_album_list_init(&loaded_albums);

    assert(imgorg_folder_list_add(
        &folders, "ADFS::HardDisc4.$.Pictures", &added
    ));
    assert(added);
    assert(imgorg_folder_list_add(
        &folders, "ADFS::HardDisc4.$.Pictures", &added
    ));
    assert(!added);
    assert(imgorg_folder_list_add(
        &folders, "ADFS::HardDisc4.$.Other", &added
    ));
    assert(added);
    assert(imgorg_folder_list_remove_at(&folders, 1));
    assert(!imgorg_folder_list_remove_at(&folders, 1));
    assert(imgorg_image_entry_init(
        &entry, "ADFS::HardDisc4.$.Pictures.Sunset", "Sunset",
        987654, 0xFFF00000, 7, 0xC85
    ));
    entry.rating = 4;
    entry.favourite = true;
    snprintf(entry.tags, sizeof(entry.tags), "holiday,sunset");
    assert(imgorg_image_list_append(&images, &entry));
    {
        size_t album_index;

        assert(imgorg_album_list_add(&albums, "Winter", &album_index));
        assert(album_index == 0);
        assert(imgorg_album_add_image(
            &albums.items[album_index], entry.path, &added
        ));
        assert(added);
        assert(imgorg_album_contains(&albums.items[album_index], entry.path));
    }

    assert(imgorg_library_catalog_save(
        file_name, &folders, &images, &albums
    ));
    assert(imgorg_library_catalog_load(
        file_name, &loaded_folders, &loaded_images, &loaded_albums
    ));
    assert(loaded_folders.count == 1);
    assert(strcmp(
        loaded_folders.items[0], "ADFS::HardDisc4.$.Pictures"
    ) == 0);
    assert(loaded_images.count == 1);
    assert(loaded_images.items[0].rating == 4);
    assert(loaded_images.items[0].favourite);
    assert(strcmp(loaded_images.items[0].tags, "holiday,sunset") == 0);
    assert(loaded_albums.count == 1);
    assert(strcmp(loaded_albums.items[0].name, "Winter") == 0);
    assert(imgorg_album_contains(
        &loaded_albums.items[0],
        loaded_images.items[0].path
    ));
    assert(imgorg_album_list_rename(&loaded_albums, 0, "Snow"));
    assert(strcmp(loaded_albums.items[0].name, "Snow") == 0);
    assert(imgorg_album_list_remove_at(&loaded_albums, 0));

    imgorg_folder_list_destroy(&folders);
    imgorg_folder_list_destroy(&loaded_folders);
    imgorg_image_list_destroy(&images);
    imgorg_image_list_destroy(&loaded_images);
    imgorg_album_list_destroy(&albums);
    imgorg_album_list_destroy(&loaded_albums);
    (void) remove(file_name);
}

int main(void)
{
    test_filetype_mapping();
    test_append_and_read();
    test_unique_append();
    test_tags();
    test_catalog_round_trip();

    puts("All image-list tests passed.");
    return 0;
}
