#ifndef CHARCOAL_TEST_H
#define CHARCOAL_TEST_H

#include <stdio.h>
#include <string.h>

// Counters shared by every test file, defined in test_main.c
extern int test_checks;
extern int test_failures;

// Record one check, print the location if it fails
#define CHECK(cond)                                                        \
    do {                                                                   \
        test_checks++;                                                     \
        if (!(cond)) {                                                     \
            test_failures++;                                               \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__,      \
                    #cond);                                                \
        }                                                                  \
    } while (0)

#define CHECK_INT(a, b)                                                    \
    do {                                                                   \
        long long va_ = (a), vb_ = (b);                                    \
        test_checks++;                                                     \
        if (va_ != vb_) {                                                  \
            test_failures++;                                               \
            fprintf(stderr, "  FAIL %s:%d: %s == %lld, expected %lld\n",   \
                    __FILE__, __LINE__, #a, va_, vb_);                     \
        }                                                                  \
    } while (0)

#define CHECK_STR(a, b)                                                    \
    do {                                                                   \
        const char *sa_ = (a), *sb_ = (b);                                 \
        test_checks++;                                                     \
        if (strcmp(sa_, sb_) != 0) {                                       \
            test_failures++;                                               \
            fprintf(stderr, "  FAIL %s:%d: %s\n    got:      \"%s\"\n"     \
                    "    expected: \"%s\"\n", __FILE__, __LINE__, #a,      \
                    sa_, sb_);                                             \
        }                                                                  \
    } while (0)

// Test suites, one per file
void test_buf(void);
void test_resize(void);
void test_glyph(void);
void test_export(void);
void test_color(void);
void test_screen(void);
void test_config(void);

#endif // CHARCOAL_TEST_H
