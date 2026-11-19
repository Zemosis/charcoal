#include "files.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Create an empty file in dir
static void touch(const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("x", f);
        fclose(f);
    }
}

void test_files(void) {
    CHECK(is_media_name("a.JPG"));
    CHECK(is_media_name("clip.webm"));
    CHECK(!is_media_name("notes.txt"));
    CHECK(!is_media_name(".png"));
    CHECK(!is_media_name("png"));

    CHECK(files_compare("img2.png", "img10.png") < 0);
    CHECK(files_compare("IMG_5.jpg", "img_5.jpg") == 0);
    CHECK(files_compare("a007", "a7") == 0);
    CHECK(files_compare("b", "a10") > 0);

    char out[64];
    path_dir("/x/y/z.png", out, sizeof(out));
    CHECK_STR(out, "/x/y");
    path_dir("z.png", out, sizeof(out));
    CHECK_STR(out, ".");
    path_dir("/z.png", out, sizeof(out));
    CHECK_STR(out, "/");
    CHECK_STR(path_name("/x/y/z.png"), "z.png");

    format_size(812, out, sizeof(out));
    CHECK_STR(out, "812");
    format_size(4200, out, sizeof(out));
    CHECK_STR(out, "4.1K");
    format_size(23LL * 1024 * 1024, out, sizeof(out));
    CHECK_STR(out, "23M");

    // A folder with media, other files, a subfolder and a hidden file
    char dir[] = "/tmp/charcoal-test-XXXXXX";
    if (!mkdtemp(dir)) {
        CHECK(0);
        return;
    }
    touch(dir, "img10.png");
    touch(dir, "img2.png");
    touch(dir, "Clip.mp4");
    touch(dir, "notes.txt");
    touch(dir, ".hidden.png");
    char sub[600];
    snprintf(sub, sizeof(sub), "%s/album", dir);
    mkdir(sub, 0755);

    FileList list;
    CHECK_INT(files_list(dir, 0, &list), 0);
    CHECK_INT(list.count, 3);
    CHECK_STR(list.items[0].name, "Clip.mp4");
    CHECK_STR(list.items[1].name, "img2.png");
    CHECK_STR(list.items[2].name, "img10.png");
    files_free(&list);

    CHECK_INT(files_list(dir, 1, &list), 0);
    CHECK_INT(list.count, 5);
    CHECK_STR(list.items[0].name, "..");
    CHECK_STR(list.items[1].name, "album");
    CHECK(list.items[1].is_dir);
    files_free(&list);

    // Next and previous wrap around the folder
    char path[700], next[700];
    int index, count;
    snprintf(path, sizeof(path), "%s/img10.png", dir);
    CHECK_INT(files_neighbor(path, 1, next, sizeof(next), &index, &count), 0);
    CHECK_STR(path_name(next), "Clip.mp4");
    CHECK_INT(index, 1);
    CHECK_INT(count, 3);
    CHECK_INT(files_neighbor(path, -1, next, sizeof(next), &index, &count), 0);
    CHECK_STR(path_name(next), "img2.png");
    CHECK_INT(index, 2);

    // A file that is not in the list still finds its neighbors
    snprintf(path, sizeof(path), "%s/img5.png", dir);
    files_neighbor(path, 1, next, sizeof(next), &index, &count);
    CHECK_STR(path_name(next), "img10.png");
    files_neighbor(path, -1, next, sizeof(next), &index, &count);
    CHECK_STR(path_name(next), "img2.png");

    // Clean up
    const char *names[] = {"img10.png", "img2.png", "Clip.mp4", "notes.txt",
                           ".hidden.png"};
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        unlink(path);
    }
    rmdir(sub);
    rmdir(dir);
}
