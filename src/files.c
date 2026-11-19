#include "files.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// Extensions FFmpeg (and libwebp) open; others are left out of listings
static const char *const MEDIA_EXT[] = {
    "png", "jpg", "jpeg", "jpe", "jfif", "webp", "avif", "gif", "apng",
    "bmp", "tif", "tiff", "tga", "pnm", "ppm", "pgm", "pbm", "qoi", "jxl",
    "heic", "heif", "ico", "exr", "dds", "mp4", "m4v", "mkv", "webm", "mov",
    "avi", "flv", "wmv", "ogv", "mpg", "mpeg", "ts", "mts", "m2ts", "3gp",
};
#define MEDIA_EXT_COUNT ((int)(sizeof(MEDIA_EXT) / sizeof(MEDIA_EXT[0])))

// Prototype
static int add_entry(FileList *list, const char *name, int is_dir,
                     long long size);
static int entry_compare(const void *a, const void *b);

int is_media_name(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) {
        return 0;
    }
    for (int i = 0; i < MEDIA_EXT_COUNT; i++) {
        if (strcasecmp(dot + 1, MEDIA_EXT[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int files_compare(const char *a, const char *b) {
    while (*a && *b) {
        // Runs of digits compare as numbers: img2 before img10
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            while (*a == '0') {
                a++;
            }
            while (*b == '0') {
                b++;
            }
            size_t la = 0, lb = 0;
            while (isdigit((unsigned char)a[la])) {
                la++;
            }
            while (isdigit((unsigned char)b[lb])) {
                lb++;
            }
            if (la != lb) {
                return la < lb ? -1 : 1;
            }
            int c = strncmp(a, b, la);
            if (c) {
                return c;
            }
            a += la;
            b += lb;
            continue;
        }
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        a++;
        b++;
    }
    return *a ? 1 : *b ? -1 : 0;
}

static int entry_compare(const void *a, const void *b) {
    const FileEntry *x = a, *y = b;
    if (x->is_dir != y->is_dir) {
        return x->is_dir ? -1 : 1;
    }
    if (strcmp(x->name, "..") == 0) {
        return -1;
    }
    if (strcmp(y->name, "..") == 0) {
        return 1;
    }
    return files_compare(x->name, y->name);
}

static int add_entry(FileList *list, const char *name, int is_dir,
                     long long size) {
    if (list->count == list->cap) {
        int cap = list->cap ? list->cap * 2 : 64;
        FileEntry *items = realloc(list->items, sizeof(FileEntry) * (size_t)cap);
        if (!items) {
            return -1;
        }
        list->items = items;
        list->cap = cap;
    }
    FileEntry *e = &list->items[list->count++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->is_dir = is_dir;
    e->size = size;
    return 0;
}

int files_list(const char *dir, int with_dirs, FileList *out) {
    memset(out, 0, sizeof(*out));
    DIR *d = opendir(dir);
    if (!d) {
        return -1;
    }
    if (with_dirs && strcmp(dir, "/") != 0) {
        add_entry(out, "..", 1, 0);
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(out->items->name)) {
            continue;
        }
        // stat follows links, so linked folders and files work too
        char full[4352];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        if (stat(full, &st) != 0) {
            continue;
        }
        int is_dir = S_ISDIR(st.st_mode);
        if ((is_dir && with_dirs) ||
            (S_ISREG(st.st_mode) && is_media_name(e->d_name))) {
            if (add_entry(out, e->d_name, is_dir, (long long)st.st_size) != 0) {
                break;
            }
        }
    }
    closedir(d);
    if (out->count > 1) {
        qsort(out->items, (size_t)out->count, sizeof(FileEntry), entry_compare);
    }
    return 0;
}

void files_free(FileList *list) {
    free(list->items);
    memset(list, 0, sizeof(*list));
}

void path_dir(const char *path, char *out, int n) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        snprintf(out, (size_t)n, ".");
    } else if (slash == path) {
        snprintf(out, (size_t)n, "/");
    } else {
        snprintf(out, (size_t)n, "%.*s", (int)(slash - path), path);
    }
}

const char *path_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int files_neighbor(const char *path, int dir, char *out, int n, int *index,
                   int *count) {
    char folder[4096];
    FileList list;
    path_dir(path, folder, sizeof(folder));
    if (files_list(folder, 0, &list) != 0 || list.count == 0) {
        files_free(&list);
        return -1;
    }

    // Where the current file sits; a file missing from the list (deleted,
    // odd extension) counts as sitting just before its sort position
    const char *name = path_name(path);
    int at = -1, before = 0;
    for (int i = 0; i < list.count; i++) {
        if (strcmp(list.items[i].name, name) == 0) {
            at = i;
        }
        if (files_compare(list.items[i].name, name) < 0) {
            before = i + 1;
        }
    }
    int pick;
    if (at >= 0) {
        pick = ((at + dir) % list.count + list.count) % list.count;
    } else {
        pick = dir > 0 ? before % list.count
                       : ((before - 1) % list.count + list.count) % list.count;
    }
    if (strcmp(folder, ".") == 0 && path[0] != '.') {
        snprintf(out, (size_t)n, "%s", list.items[pick].name);
    } else {
        snprintf(out, (size_t)n, "%s/%s", folder, list.items[pick].name);
    }
    *index = pick + 1;
    *count = list.count;
    files_free(&list);
    return 0;
}

void format_size(long long bytes, char *out, int n) {
    const char *units = "KMGT";
    if (bytes < 1024) {
        snprintf(out, (size_t)n, "%lld", bytes);
        return;
    }
    double v = (double)bytes;
    int u = -1;
    while (v >= 1024 && u < 3) {
        v /= 1024;
        u++;
    }
    snprintf(out, (size_t)n, v < 10 ? "%.1f%c" : "%.0f%c", v, units[u]);
}
