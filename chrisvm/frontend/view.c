#include "chrisvm.h"

#include <stddef.h>
#include <stdint.h>

#ifdef CHRIS_HAVE_SDL
#include <SDL.h>
#endif

int chris_view_show(const ChrisMachine *m, int milliseconds) {
#ifdef CHRIS_HAVE_SDL
    uint32_t *copy;
    uint32_t y;
    uint32_t x;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Event ev;
    uint32_t start;
    if (!m || milliseconds < 0) {
        return -1;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return -1;
    }
    copy = (uint32_t *)SDL_malloc((size_t)CHRIS_FB_WIDTH * (size_t)CHRIS_FB_HEIGHT * 4u);
    if (!copy) {
        SDL_Quit();
        return -1;
    }
    for (y = 0; y < CHRIS_FB_HEIGHT; ++y) {
        for (x = 0; x < CHRIS_FB_WIDTH; ++x) {
            uint32_t p = 0;
            if (chris_fb_get(m, x, y, &p) != 0) {
                p = 0;
            }
            copy[y * CHRIS_FB_WIDTH + x] = p;
        }
    }
    window = SDL_CreateWindow("ChrisOS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)CHRIS_FB_WIDTH,
                              (int)CHRIS_FB_HEIGHT, SDL_WINDOW_SHOWN);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : 0;
    texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                           (int)CHRIS_FB_WIDTH, (int)CHRIS_FB_HEIGHT)
                       : 0;
    if (!texture) {
        SDL_free(copy);
        if (renderer) {
            SDL_DestroyRenderer(renderer);
        }
        if (window) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
        return -1;
    }
    SDL_UpdateTexture(texture, 0, copy, (int)CHRIS_FB_WIDTH * 4);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, 0, 0);
    SDL_RenderPresent(renderer);
    start = SDL_GetTicks();
    while ((int)(SDL_GetTicks() - start) < milliseconds) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                milliseconds = 0;
            }
        }
        SDL_Delay(16);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_free(copy);
    SDL_Quit();
    return 0;
#else
    (void)m;
    (void)milliseconds;
    return -1;
#endif
}
