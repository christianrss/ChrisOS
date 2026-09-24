#include "icons.h"

extern const uint32_t icon_bin_shell[];
extern const uint32_t icon_bin_files[];
extern const uint32_t icon_bin_editor[];
extern const uint32_t icon_bin_tasks[];
extern const uint32_t icon_bin_start[];
extern const uint32_t icon_bin_logo[];
extern const uint32_t icon_bin_mine[];
extern const uint32_t icon_bin_wall[];

static const RgbaImage g_icons[ICON_N] = {
    {icon_bin_shell, 48, 48},
    {icon_bin_files, 48, 48},
    {icon_bin_editor, 48, 48},
    {icon_bin_tasks, 48, 48},
    {icon_bin_start, 48, 48},
    {icon_bin_logo, 48, 48},
    {icon_bin_mine, 48, 48},
};
static const RgbaImage g_wall = {icon_bin_wall, 1440, 1080};

const RgbaImage *icon_by_id(int id) {
    if (id < 0 || id >= ICON_N) {
        return 0;
    }
    return &g_icons[id];
}

const RgbaImage *icon_wallpaper(void) {
    return &g_wall;
}
