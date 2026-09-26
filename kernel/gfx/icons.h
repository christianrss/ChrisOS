#ifndef CHRIS_ICONS_H
#define CHRIS_ICONS_H

#include <stdint.h>

typedef struct RgbaImage {
    const uint32_t *px;
    int w;
    int h;
} RgbaImage;

enum {
    ICON_SHELL = 0,
    ICON_FILES,
    ICON_EDITOR,
    ICON_TASKS,
    ICON_START,
    ICON_LOGO,
    ICON_MINE,
    ICON_N
};

const RgbaImage *icon_by_id(int id);
const RgbaImage *icon_wallpaper(void);

#endif
