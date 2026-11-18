#include "buf.h"
#include "test.h"

// Append to a buffer and read it back as a C string
static const char *buf_str(Buf *b) {
    buf_putc(b, '\0');
    b->len--;
    return b->data;
}

void test_buf(void) {
    Buf b;
    buf_init(&b);

    buf_puts(&b, "ab");
    buf_putu(&b, 0);
    buf_putu(&b, 4096);
    CHECK_STR(buf_str(&b), "ab04096");

    // Growing past the first allocation keeps the contents
    buf_clear(&b);
    for (int i = 0; i < 1000; i++) {
        buf_putc(&b, 'x');
    }
    CHECK_INT(b.len, 1000);
    CHECK(b.data[999] == 'x');

    // UTF-8 round trip for 1-4 byte code points
    uint32_t cps[] = {'A', 0xE9, 0x2580, 0x1F525};
    int lens[] = {1, 2, 3, 4};
    for (int i = 0; i < 4; i++) {
        buf_clear(&b);
        buf_utf8(&b, cps[i]);
        CHECK_INT(b.len, lens[i]);
        uint32_t cp = 0;
        CHECK_INT(utf8_decode(buf_str(&b), &cp), lens[i]);
        CHECK_INT(cp, cps[i]);
    }

    uint32_t cp;
    CHECK_INT(utf8_decode("", &cp), 0);

    buf_free(&b);
    CHECK(b.data == NULL);
}
