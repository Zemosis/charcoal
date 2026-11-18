#include "test.h"

int test_checks = 0;
int test_failures = 0;

// Run one suite and report it
static void run(const char *name, void (*suite)(void)) {
    int before = test_failures;
    suite();
    printf("%-8s %s\n", name, test_failures == before ? "ok" : "FAILED");
}

int main(void) {
    run("buf", test_buf);
    run("resize", test_resize);
    run("glyph", test_glyph);
    run("export", test_export);

    printf("\n%d checks, %d failed\n", test_checks, test_failures);
    return test_failures ? 1 : 0;
}
