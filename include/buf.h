#ifndef CHARCOAL_BUF_H
#define CHARCOAL_BUF_H

#include <stddef.h>
#include <stdint.h>

// Growable byte buffer used to batch terminal and file output
typedef struct Buf {
    char *data;
    size_t len;
    size_t cap;
} Buf;

// Initialize an empty buffer
void buf_init(Buf *b);

// Free the buffer's memory and reset it to empty
void buf_free(Buf *b);

// Drop the contents but keep the memory
void buf_clear(Buf *b);

// Append n bytes
void buf_push(Buf *b, const char *s, size_t n);

// Append a NUL-terminated string
void buf_puts(Buf *b, const char *s);

// Append one byte
void buf_putc(Buf *b, char c);

// Append an unsigned integer in decimal
void buf_putu(Buf *b, unsigned v);

// Append a Unicode code point encoded as UTF-8
void buf_utf8(Buf *b, uint32_t cp);

// Decode one UTF-8 code point from s, returns bytes used (0 at end of string)
int utf8_decode(const char *s, uint32_t *cp);

#endif // CHARCOAL_BUF_H
