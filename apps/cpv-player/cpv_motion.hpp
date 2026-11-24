#ifndef CPV_MOTION_HPP
#define CPV_MOTION_HPP

#include <stdint.h>

/*
 * Apply CPV v1.3 global motion in place to an RGB565 raster framebuffer.
 *
 * No second full-frame buffer, heap allocation, floating point, or recursion
 * is required. Copy order is chosen so that source pixels are not overwritten
 * before being read.
 *
 * dx > 0 moves right; dx < 0 moves left.
 * dy > 0 moves down;  dy < 0 moves up.
 */
static inline bool cpv_apply_global_motion(
    uint16_t *pixels,
    int width,
    int height,
    int dx,
    int dy,
    uint16_t fill_colour
) {
    if (pixels == 0 || width <= 0 || height <= 0) {
        return false;
    }

    if ((dx == 0 && dy == 0) ||
        dx < -8 || dx > 8 || dy < -8 || dy > 8 ||
        dx <= -width || dx >= width ||
        dy <= -height || dy >= height) {
        return false;
    }

    /* Horizontal translation: copy backwards when moving right, forwards
       when moving left, then fill the newly exposed columns. */
    if (dx > 0) {
        for (int y = 0; y < height; ++y) {
            uint16_t *row = pixels + y * width;
            for (int x = width - 1; x >= dx; --x) {
                row[x] = row[x - dx];
            }
            for (int x = 0; x < dx; ++x) {
                row[x] = fill_colour;
            }
        }
    } else if (dx < 0) {
        const int shift = -dx;
        for (int y = 0; y < height; ++y) {
            uint16_t *row = pixels + y * width;
            for (int x = 0; x < width - shift; ++x) {
                row[x] = row[x + shift];
            }
            for (int x = width - shift; x < width; ++x) {
                row[x] = fill_colour;
            }
        }
    }

    /* Vertical translation operates on the horizontally shifted rows.
       Copy bottom-up when moving down and top-down when moving up. */
    if (dy > 0) {
        for (int y = height - 1; y >= dy; --y) {
            uint16_t *dst = pixels + y * width;
            const uint16_t *src = pixels + (y - dy) * width;
            for (int x = 0; x < width; ++x) {
                dst[x] = src[x];
            }
        }
        for (int y = 0; y < dy; ++y) {
            uint16_t *row = pixels + y * width;
            for (int x = 0; x < width; ++x) {
                row[x] = fill_colour;
            }
        }
    } else if (dy < 0) {
        const int shift = -dy;
        for (int y = 0; y < height - shift; ++y) {
            uint16_t *dst = pixels + y * width;
            const uint16_t *src = pixels + (y + shift) * width;
            for (int x = 0; x < width; ++x) {
                dst[x] = src[x];
            }
        }
        for (int y = height - shift; y < height; ++y) {
            uint16_t *row = pixels + y * width;
            for (int x = 0; x < width; ++x) {
                row[x] = fill_colour;
            }
        }
    }

    return true;
}

#endif
