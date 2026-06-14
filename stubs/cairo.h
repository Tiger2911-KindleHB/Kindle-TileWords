#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _cairo cairo_t;
typedef struct {
    double x_bearing;
    double y_bearing;
    double width;
    double height;
    double x_advance;
    double y_advance;
} cairo_text_extents_t;

#define CAIRO_FONT_SLANT_NORMAL 0
#define CAIRO_FONT_WEIGHT_BOLD 1
#define CAIRO_FONT_WEIGHT_NORMAL 0

#ifdef __cplusplus
}
#endif
