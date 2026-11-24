#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../cpv_motion.hpp"

static bool check_shift(int width, int height, int dx, int dy) {
    const uint16_t fill = 0x1234;
    const int count = width * height;
    uint16_t *actual = (uint16_t *)malloc((unsigned long)count * sizeof(uint16_t));
    uint16_t *expected = (uint16_t *)malloc((unsigned long)count * sizeof(uint16_t));
    if (!actual || !expected) {
        free(actual);
        free(expected);
        return false;
    }

    for (int i = 0; i < count; ++i) {
        actual[i] = (uint16_t)(i * 37 + 11);
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int sx = x - dx;
            const int sy = y - dy;
            expected[y * width + x] =
                (sx >= 0 && sx < width && sy >= 0 && sy < height)
                ? (uint16_t)(sy * width + sx) * 37 + 11
                : fill;
        }
    }

    const bool applied = cpv_apply_global_motion(
        actual, width, height, dx, dy, fill
    );
    bool equal = applied;
    for (int i = 0; i < count && equal; ++i) {
        if (actual[i] != expected[i]) {
            equal = false;
        }
    }

    free(actual);
    free(expected);
    return equal;
}

int main() {
    const int shifts[][2] = {
        {3, 2}, {3, -2}, {-3, 2}, {-3, -2},
        {0, 4}, {0, -4}, {5, 0}, {-5, 0},
        {1, 1}, {-1, -1}
    };

    for (unsigned int i = 0; i < sizeof(shifts) / sizeof(shifts[0]); ++i) {
        if (!check_shift(20, 16, shifts[i][0], shifts[i][1])) {
            fprintf(stderr, "global-motion mismatch for (%d, %d)\n",
                    shifts[i][0], shifts[i][1]);
            return 1;
        }
    }

    uint16_t pixel = 0;
    if (cpv_apply_global_motion(&pixel, 1, 1, 1, 0, 0xFFFF)) {
        fprintf(stderr, "invalid width-overlap vector was accepted\n");
        return 1;
    }
    if (cpv_apply_global_motion(&pixel, 1, 1, 0, 0, 0xFFFF)) {
        fprintf(stderr, "zero vector was accepted\n");
        return 1;
    }

    puts("CPV global-motion in-place tests: OK");
    return 0;
}
