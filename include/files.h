#ifndef CHARCOAL_FILES_H
#define CHARCOAL_FILES_H

// One entry of a folder listing
typedef struct FileEntry {
    char name[256];
    int is_dir;
    long long size;     // bytes, files only
} FileEntry;

// A sorted folder listing: folders first, then files, in natural order
// (img2 before img10, case ignored)
typedef struct FileList {
    FileEntry *items;
    int count;
    int cap;
} FileList;

// The name ends in an image, animation or video extension charcoal opens
int is_media_name(const char *name);

// List media files in dir, plus its folders (and ".." unless dir is /) when
// with_dirs is set; hidden entries are skipped. Returns 0 or -1
int files_list(const char *dir, int with_dirs, FileList *out);

void files_free(FileList *list);

// Natural, case-insensitive name order
int files_compare(const char *a, const char *b);

// Folder part of a path ("." for a bare name) and the name part
void path_dir(const char *path, char *out, int n);
const char *path_name(const char *path);

// "~" or "~/x" becomes $HOME/x; anything else is copied as it is
void path_expand(const char *path, char *out, int n);

// $HOME/x becomes ~/x, for showing paths
void path_abbrev(const char *path, char *out, int n);

// 1 when path is a folder that exists
int path_is_dir(const char *path);

// The media file dir media files away from path in its folder, wrapping
// around; fills out, and the new position and count (1-based). Returns 0,
// or -1 when the folder has no media files
int files_neighbor(const char *path, int dir, char *out, int n, int *index,
                   int *count);

// Human size for the picker: 812, 4.1K, 23M, 1.2G
void format_size(long long bytes, char *out, int n);

#endif // CHARCOAL_FILES_H
