#include "media_internal.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Frames decoded ahead of the clock
#define QUEUE_SIZE 8

struct Player {
    MediaFile *m;
    Decoder d;
    AVFrame *av;
    AVFrame *last;          // last frame skipped on the way to a seek target
    int64_t last_pts_us;
    int64_t last_dur_us;
    struct SwsContext *sws;

    pthread_t thread;
    int started;
    pthread_mutex_t mu;
    pthread_cond_t cv;

    // Everything below is shared with the thread, guarded by mu
    Frame *queue[QUEUE_SIZE];
    int head;
    int count;
    int target_w;
    int target_h;
    int generation;         // bumps when queued frames become stale
    int seek_pending;
    int64_t seek_us;
    int64_t skip_until_us;  // after a seek, frames ending before this go
    int loop;
    int eof;
    int quit;
    int64_t loop_offset_us; // added to pts: the length of finished loops
    int64_t loop_len_us;    // end of the last frame seen

    int wake[2];
};

// Prototype
static void wake_ui(Player *p);
static void queue_clear(Player *p);
static void push_frame(Player *p, Frame *f, int64_t pts, int64_t dur);
static void *player_main(void *arg);

static void wake_ui(Player *p) {
    char c = 'f';
    if (write(p->wake[1], &c, 1) < 0) {
        // Pipe full: the UI has a wake-up pending already
    }
}

// Drop every queued frame; called with mu held
static void queue_clear(Player *p) {
    while (p->count > 0) {
        frame_free(p->queue[p->head]);
        p->queue[p->head] = NULL;
        p->head = (p->head + 1) % QUEUE_SIZE;
        p->count--;
    }
}

// Queue a frame for the UI; called with mu held
static void push_frame(Player *p, Frame *f, int64_t pts, int64_t dur) {
    p->skip_until_us = INT64_MIN;
    f->pts_us = pts;
    f->duration_us = dur;
    f->play_us = pts + p->loop_offset_us;
    if (pts + dur > p->loop_len_us) {
        p->loop_len_us = pts + dur;
    }
    p->queue[(p->head + p->count) % QUEUE_SIZE] = f;
    p->count++;
    wake_ui(p);
}

// The decoder thread: decode, scale, queue, repeat
static void *player_main(void *arg) {
    Player *p = arg;
    char err[128];

    pthread_mutex_lock(&p->mu);
    while (!p->quit) {
        if (p->seek_pending) {
            int64_t t = p->seek_us;
            p->seek_pending = 0;
            p->eof = 0;
            p->skip_until_us = t;
            p->loop_offset_us = 0;
            pthread_mutex_unlock(&p->mu);
            decoder_seek(p->m, &p->d, t);
            pthread_mutex_lock(&p->mu);
            continue;
        }
        if (p->count == QUEUE_SIZE || p->eof || p->target_w < 1) {
            pthread_cond_wait(&p->cv, &p->mu);
            continue;
        }

        int w = p->target_w, h = p->target_h, gen = p->generation;
        int64_t skip = p->skip_until_us;
        pthread_mutex_unlock(&p->mu);

        int ret = decoder_next(p->m, &p->d, p->av);
        if (ret == AVERROR_EOF && skip != INT64_MIN && p->last->width > 0) {
            // Sought past the end: settle on the last frame there is
            Frame *f = frame_convert(p->last, w, h, &p->sws, err, sizeof(err));
            av_frame_unref(p->last);
            pthread_mutex_lock(&p->mu);
            if (f && gen == p->generation && !p->seek_pending) {
                push_frame(p, f, p->last_pts_us, p->last_dur_us);
            } else {
                frame_free(f);
            }
            p->skip_until_us = INT64_MIN;
            continue;
        }
        if (ret < 0) {
            pthread_mutex_lock(&p->mu);
            if (ret == AVERROR_EOF && p->loop && p->loop_len_us > 0 &&
                !p->seek_pending) {
                // Start over; the clock keeps counting up across loops
                p->loop_offset_us += p->loop_len_us;
                pthread_mutex_unlock(&p->mu);
                decoder_seek(p->m, &p->d, 0);
                pthread_mutex_lock(&p->mu);
            } else if (!p->seek_pending) {
                p->eof = 1;
                wake_ui(p);
            }
            continue;
        }

        int64_t pts, dur;
        decoder_timing(p->m, &p->d, p->av, &pts, &dur);
        if (pts + dur <= skip) {
            // Decoding forward to a seek target; keep it in case the file
            // ends first
            av_frame_unref(p->last);
            av_frame_move_ref(p->last, p->av);
            p->last_pts_us = pts;
            p->last_dur_us = dur;
            pthread_mutex_lock(&p->mu);
            continue;
        }
        av_frame_unref(p->last);
        Frame *f = frame_convert(p->av, w, h, &p->sws, err, sizeof(err));
        av_frame_unref(p->av);

        pthread_mutex_lock(&p->mu);
        if (!f) {
            p->eof = 1;
            wake_ui(p);
            continue;
        }
        if (gen != p->generation || p->seek_pending) {
            frame_free(f);      // made for an old size or position
            continue;
        }
        push_frame(p, f, pts, dur);
    }
    pthread_mutex_unlock(&p->mu);
    return NULL;
}

Player *player_open(const char *path, int loop, char *err, int err_len) {
    Player *p = calloc(1, sizeof(*p));
    if (!p) {
        snprintf(err, (size_t)err_len, "out of memory");
        return NULL;
    }
    p->wake[0] = p->wake[1] = -1;
    p->skip_until_us = INT64_MIN;
    p->loop = loop;
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->cv, NULL);

    p->m = media_open(path, err, err_len);
    if (!p->m || decoder_open(p->m, &p->d, 0, err, err_len) != 0) {
        player_close(p);
        return NULL;
    }
    p->av = av_frame_alloc();
    p->last = av_frame_alloc();
    if (!p->av || !p->last || pipe(p->wake) != 0) {
        snprintf(err, (size_t)err_len, "out of memory");
        player_close(p);
        return NULL;
    }
    for (int i = 0; i < 2; i++) {
        fcntl(p->wake[i], F_SETFL, fcntl(p->wake[i], F_GETFL) | O_NONBLOCK);
        fcntl(p->wake[i], F_SETFD, FD_CLOEXEC);
    }
    if (pthread_create(&p->thread, NULL, player_main, p) != 0) {
        snprintf(err, (size_t)err_len, "cannot start the decoder thread");
        player_close(p);
        return NULL;
    }
    p->started = 1;
    return p;
}

void player_close(Player *p) {
    if (!p) {
        return;
    }
    if (p->started) {
        pthread_mutex_lock(&p->mu);
        p->quit = 1;
        pthread_cond_signal(&p->cv);
        pthread_mutex_unlock(&p->mu);
        pthread_join(p->thread, NULL);
    }
    queue_clear(p);
    sws_free_context(&p->sws);
    av_frame_free(&p->av);
    av_frame_free(&p->last);
    if (p->d.ctx) {
        decoder_close(&p->d);
    }
    media_close(p->m);
    for (int i = 0; i < 2; i++) {
        if (p->wake[i] >= 0) {
            close(p->wake[i]);
        }
    }
    pthread_mutex_destroy(&p->mu);
    pthread_cond_destroy(&p->cv);
    free(p);
}

int player_fd(const Player *p) {
    return p->wake[0];
}

void player_drain(Player *p) {
    char buf[64];
    while (read(p->wake[0], buf, sizeof(buf)) > 0) {
    }
}

void player_set_target(Player *p, int width, int height) {
    pthread_mutex_lock(&p->mu);
    if (width != p->target_w || height != p->target_h) {
        p->target_w = width;
        p->target_h = height;
        p->generation++;
        queue_clear(p);
        pthread_cond_signal(&p->cv);
    }
    pthread_mutex_unlock(&p->mu);
}

Frame *player_peek(Player *p) {
    pthread_mutex_lock(&p->mu);
    Frame *f = p->count ? p->queue[p->head] : NULL;
    pthread_mutex_unlock(&p->mu);
    return f;
}

Frame *player_pop(Player *p) {
    pthread_mutex_lock(&p->mu);
    Frame *f = NULL;
    if (p->count) {
        f = p->queue[p->head];
        p->queue[p->head] = NULL;
        p->head = (p->head + 1) % QUEUE_SIZE;
        p->count--;
        pthread_cond_signal(&p->cv);
    }
    pthread_mutex_unlock(&p->mu);
    return f;
}

void player_seek(Player *p, int64_t t_us) {
    pthread_mutex_lock(&p->mu);
    p->seek_pending = 1;
    p->seek_us = t_us < 0 ? 0 : t_us;
    p->generation++;
    queue_clear(p);
    pthread_cond_signal(&p->cv);
    pthread_mutex_unlock(&p->mu);
}

void player_set_loop(Player *p, int loop) {
    pthread_mutex_lock(&p->mu);
    p->loop = loop;
    if (loop && p->eof) {
        // Reached the end while not looping: carry on from the start
        p->eof = 0;
        p->seek_pending = 1;
        p->seek_us = 0;
    }
    pthread_cond_signal(&p->cv);
    pthread_mutex_unlock(&p->mu);
}

int player_at_end(Player *p) {
    pthread_mutex_lock(&p->mu);
    int end = p->eof && p->count == 0 && !p->seek_pending;
    pthread_mutex_unlock(&p->mu);
    return end;
}
