#ifndef CHARCOAL_MEDIA_H
#define CHARCOAL_MEDIA_H

#include <stdint.h>

// One decoded RGB24 image
typedef struct Frame {
    int width;
    int height;
    uint8_t *rgb;       // width * height * 3
    int64_t pts_us;     // position in the media, microseconds
    int64_t duration_us;
    int64_t play_us;    // playback time: pts plus the length of earlier loops
} Frame;

// What kind of input a file turned out to be
typedef enum MediaKind {
    MEDIA_STILL,
    MEDIA_ANIMATED,
    MEDIA_VIDEO
} MediaKind;

// Allocate a frame with uninitialized pixels, NULL on failure
Frame *frame_new(int width, int height);

// Free a frame and its pixels
void frame_free(Frame *f);

// An opened media file (FFmpeg state stays inside decoder.c)
typedef struct MediaFile MediaFile;

// Open any FFmpeg-readable file and find its video stream
// On failure returns NULL and writes a message to err
MediaFile *media_open(const char *path, char *err, int err_len);

// Short codec name for display, such as "mjpeg", "png" or "h264"
const char *media_codec_name(const MediaFile *m);

// Full-resolution size of the image or video
void media_size(const MediaFile *m, int *width, int *height);

// Still, animated or video; known once media_decode_first has run
MediaKind media_kind(const MediaFile *m);

// Length in microseconds (0 when unknown) and frame rate (0 when unknown)
int64_t media_duration_us(const MediaFile *m);
double media_fps(const MediaFile *m);

// Decode the first frame, at least need_w x need_h pixels when the codec can
// decode smaller (0 means full size), NULL on failure with a message in err
Frame *media_decode_first(MediaFile *m, int need_w, int need_h, char *err,
                          int err_len);

// Decode the frame shown at t_us, or the index-th frame when index >= 0
// Past the end gives the last frame
Frame *media_decode_at(MediaFile *m, int64_t t_us, int64_t index, int need_w,
                       int need_h, char *err, int err_len);

// Close the file, NULL is allowed
void media_close(MediaFile *m);

// Library versions for --version, such as "FFmpeg 62.12.103, libwebp 1.6.0"
void media_versions(char *out, int n);

// Plays animations and video: a thread decodes ahead into a small queue of
// frames already scaled to the size the viewer asked for
typedef struct Player Player;

// Open a file for playback (the thread starts once a size is set)
Player *player_open(const char *path, int loop, char *err, int err_len);

// Stop the thread and free everything, NULL is allowed
void player_close(Player *p);

// Becomes readable whenever a new frame is queued; drain with player_drain
int player_fd(const Player *p);
void player_drain(Player *p);

// Size to scale frames to; queued frames of another size are dropped
void player_set_target(Player *p, int width, int height);

// Oldest queued frame (still owned by the player), NULL when none
Frame *player_peek(Player *p);

// Take the oldest queued frame, the caller frees it
Frame *player_pop(Player *p);

// Jump to t_us; frames from there on replace the queue
void player_seek(Player *p, int64_t t_us);

void player_set_loop(Player *p, int loop);

// The end was reached (not looping) and every frame was taken
int player_at_end(Player *p);

#endif // CHARCOAL_MEDIA_H
