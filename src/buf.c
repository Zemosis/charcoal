#include "buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Prototype
static void buf_reserve(Buf *b, size_t extra);

// Make room for extra more bytes, aborts if out of memory
static void buf_reserve(Buf *b, size_t extra) {
    if (b->len + extra <= b->cap) {
        return;
    }
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra) {
        cap *= 2;
    }
    char *data = realloc(b->data, cap);
    if (!data) {
        fprintf(stderr, "charcoal: out of memory\n");
        abort();
    }
    b->data = data;
    b->cap = cap;
}

void buf_init(Buf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void buf_free(Buf *b) {
    free(b->data);
    buf_init(b);
}

void buf_clear(Buf *b) {
    b->len = 0;
}

void buf_push(Buf *b, const char *s, size_t n) {
    buf_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

void buf_puts(Buf *b, const char *s) {
    buf_push(b, s, strlen(s));
}

void buf_putc(Buf *b, char c) {
    buf_reserve(b, 1);
    b->data[b->len++] = c;
}

void buf_putu(Buf *b, unsigned v) {
    char tmp[16];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    buf_reserve(b, (size_t)n);
    while (n) {
        b->data[b->len++] = tmp[--n];
    }
}

void buf_utf8(Buf *b, uint32_t cp) {
    char s[4];
    if (cp < 0x80) {
        buf_putc(b, (char)cp);
    } else if (cp < 0x800) {
        s[0] = (char)(0xC0 | (cp >> 6));
        s[1] = (char)(0x80 | (cp & 0x3F));
        buf_push(b, s, 2);
    } else if (cp < 0x10000) {
        s[0] = (char)(0xE0 | (cp >> 12));
        s[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        s[2] = (char)(0x80 | (cp & 0x3F));
        buf_push(b, s, 3);
    } else {
        s[0] = (char)(0xF0 | (cp >> 18));
        s[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        s[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        s[3] = (char)(0x80 | (cp & 0x3F));
        buf_push(b, s, 4);
    }
}

int utf8_decode(const char *s, uint32_t *cp) {
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] == 0) {
        return 0;
    }
    if (u[0] < 0x80) {
        *cp = u[0];
        return 1;
    }
    int n = (u[0] >= 0xF0) ? 4 : (u[0] >= 0xE0) ? 3 : (u[0] >= 0xC0) ? 2 : 1;
    if (n == 1) {
        // Stray continuation byte, pass it through as a replacement char
        *cp = 0xFFFD;
        return 1;
    }
    uint32_t v = u[0] & (0x3F >> (n - 1));
    for (int i = 1; i < n; i++) {
        if ((u[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return i;
        }
        v = (v << 6) | (u[i] & 0x3F);
    }
    *cp = v;
    return n;
}
