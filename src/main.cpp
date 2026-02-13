#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef RELEASE
#include "./impl.c"
#else
#define RGFW_NATIVE
#define RGFW_IMPORT
#define RGFW_UNIX
#include "../3rd/RGFW.h"
#include "../3rd/stb_image.h"
#endif

#define array_count(array) (sizeof(array) / sizeof((array)[0]))
#define kilobytes(n) (1024LL * (n))
#define megabytes(n) (1024LL * kilobytes(n))
#define gigabytes(n) (1024LL * megabytes(n))
#define terabytes(n) (1024LL * gigabytes(n))

#define minimum(A, B) (((A) < (B)) ? (A) : (B))
#define maximum(A, B) (((A) > (B)) ? (A) : (B))
#define clamp(N, MIN, MAX) (maximum(minimum((N), (MAX)), (MIN)))

u32 icon[3 * 3] = {
    0xff0000ff, 0xff0000ff, 0xff0000ff,
    0x000000ff, 0xff00ffff, 0xff00ffff,
    0xff0000ff, 0xff0000ff, 0xff0000ff
};

struct timespec init_ts = {};

struct timespec get_timespec() {
    struct timespec ts;
    assert(timespec_get(&ts, TIME_UTC) == TIME_UTC);
    return ts;
}

double get_real_time_seconds() {
    struct timespec ts = get_timespec();
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

double get_time_seconds() {
    struct timespec now_ts = get_timespec();
    double from_start = double(now_ts.tv_sec - init_ts.tv_sec) + (double)(now_ts.tv_nsec - init_ts.tv_nsec) / 1e9;
    return from_start;
}

char *read_entire_file(const char *path) {
    assert(path && "path must not be null");
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "ERROR: %s\n", strerror(errno));
        assert(!"cannot open file");
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "ERROR: %s\n", strerror(errno));
        assert(!"cannot seek file");
    };
    long size = ftell(file);
    if (size < 0) {
        fprintf(stderr, "ERROR: %s\n", strerror(errno));
        assert(!"cannot get file size");
    }
    rewind(file);
    char *buffer = (char *)malloc((size_t)size + 1);
    assert(buffer && "fail to allocate memory for the file");
    size_t read_size = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    assert(read_size == (size_t)size);
    buffer[size] = '\0';
    return buffer;
}

void print_help_msg() {
    printf("USAGE:\n");
    printf("    pin <image_path>\n");
}

const char *get_event_name(RGFW_eventType type) {
    switch (type) {
        case RGFW_eventNone: return "RGFW_eventNone";
        case RGFW_keyPressed: return "RGFW_keyPressed";
        case RGFW_keyReleased: return "RGFW_keyReleased";
        case RGFW_mouseButtonPressed: return "RGFW_mouseButtonPressed";
        case RGFW_mouseButtonReleased: return "RGFW_mouseButtonReleased";
        case RGFW_mouseScroll: return "RGFW_mouseScroll";
        case RGFW_mousePosChanged: return "RGFW_mousePosChanged";
        case RGFW_windowMoved: return "RGFW_windowMoved";
        case RGFW_windowResized: return "RGFW_windowResized";
        case RGFW_focusIn: return "RGFW_focusIn";
        case RGFW_focusOut: return "RGFW_focusOut";
        case RGFW_mouseEnter: return "RGFW_mouseEnter";
        case RGFW_mouseLeave: return "RGFW_mouseLeave";
        case RGFW_windowRefresh: return "RGFW_windowRefresh";
        case RGFW_quit: return "RGFW_quit";
        case RGFW_dataDrop: return "RGFW_dataDrop";
        case RGFW_dataDrag: return "RGFW_dataDrag";
        case RGFW_windowMaximized: return "RGFW_windowMaximized";
        case RGFW_windowMinimized: return "RGFW_windowMinimized";
        case RGFW_windowRestored: return "RGFW_windowRestored";
        case RGFW_scaleUpdated: return "RGFW_scaleUpdated";
        default: return "UNKNOWN";
    }
}

void copy_image(u32 *dest, const u32 *src, int dest_width, int dest_height, int src_width, int src_height) {
    if (dest_width == src_width && dest_height == src_height) {
        memcpy(dest, src, (src_width * src_height) * 4);
        return;
    }
    u32 *row_dest = dest;
    for (int y = 0; y < dest_height; y++) {
        int src_y = (float)y / (float)dest_height * src_height;
        const u32 *row_src = src + (src_y * src_width);
        u32 *pixel_dest = row_dest;
        for (int x = 0; x < dest_width; x++) {
            int src_x = (float)x / (float)dest_width * src_width;
            const u32 *pixel_src = row_src + src_x;
            *pixel_dest = *pixel_src;
            pixel_dest++;
        }
        row_dest += dest_width;
    }
}

void init() {
    init_ts = get_timespec();
}

int main(int argc, char **argv) {
    if (argc != 2) {
        print_help_msg();
        return 1;
    }
    const char *path = argv[1];

    init();

    int image_width, image_height;
    u8 *image_data = stbi_load(path, &image_width, &image_height, 0, 0);
    if (!image_data) {
        fprintf(stderr, "Failed to load %s", path);
        return 1;
    }

    u32 *pixels = (u32 *)malloc(megabytes(128)); // can hold 8K resolution
    if (!pixels) {
        fprintf(stderr, "Failed to allocate pixels");
        return 1;
    }

    RGFW_window *win = RGFW_createWindow("name", 0, 0, image_width, image_height, RGFW_windowNoBorder | RGFW_windowNoResize | RGFW_windowCenter | RGFW_windowFloating);

    RGFW_event event;

    RGFW_window_setExitKey(win, RGFW_escape);
    RGFW_window_setIcon(win, (u8 *)icon, 3, 3, RGFW_formatRGBA8);

    RGFW_surface surface;
    long long frame = 0;
    int scale_level = 0;
    while (RGFW_window_shouldClose(win) == false) {
        frame++;
        RGFW_waitForEvent(-1);

        bool need_redraw = false;

        while (RGFW_window_checkEvent(win, &event)) {
            if (event.type == RGFW_quit)
                break;
            switch (event.type) {
                case RGFW_windowResized: {
                } break;
                case RGFW_keyPressed: {
                } break;
                case RGFW_keyReleased: {
                } break;
                case RGFW_mousePosChanged: {
                } break;
                case RGFW_windowRefresh: {
                    need_redraw = true;
                } break;
                case RGFW_mouseScroll: {
                    scale_level += event.scroll.y;
                    scale_level = clamp(scale_level, -9, 10);
                } break;
            }
        }

        float scale_factor = 1.0f + (float)scale_level / 10.0f;
        int new_width = image_width * scale_factor;
        int new_height = image_height * scale_factor;
        if (new_width != win->w || new_height != win->h) {
            printf("window resizing %lld\n", frame);
            RGFW_window_resize(win, new_width, new_height);
        }

        printf("winsize: (%d, %d) %lld\n", win->w, win->h, frame);
        if (need_redraw) {
            printf("redraw %lld\n", frame);
            double start = get_time_seconds();
            copy_image(pixels, (u32 *)image_data, win->w, win->h, image_width, image_height);
            printf("copy_image took %fms\n", (get_time_seconds() - start) * 1000.0f);
            start = get_time_seconds();
            RGFW_createSurfacePtr((u8 *)pixels, win->w, win->h, RGFW_formatRGBA8, &surface);
            printf("RGFW_createSurfacePtr took %fms\n", (get_time_seconds() - start) * 1000.0f);
            start = get_time_seconds();
            RGFW_window_blitSurface(win, &surface);
            printf("RGFW_window_blitSurface took %fms\n", (get_time_seconds() - start) * 1000.0f);
        }
    }

    RGFW_window_close(win);
    return 0;
}
