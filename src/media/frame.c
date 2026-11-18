#include "media.h"

#include <stdlib.h>

Frame *frame_new(int width, int height) {
    if (width < 1 || height < 1) {
        return NULL;
    }
    Frame *f = malloc(sizeof(*f));
    if (!f) {
        return NULL;
    }
    f->width = width;
    f->height = height;
    f->pts_us = 0;
    f->rgb = malloc((size_t)width * (size_t)height * 3);
    if (!f->rgb) {
        free(f);
        return NULL;
    }
    return f;
}

void frame_free(Frame *f) {
    if (f) {
        free(f->rgb);
        free(f);
    }
}
