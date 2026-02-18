#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../3rd/stb_image.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#define array_count(array) (sizeof(array) / sizeof((array)[0]))
#define kilobytes(n) (1024LL * (n))
#define megabytes(n) (1024LL * kilobytes(n))
#define gigabytes(n) (1024LL * megabytes(n))
#define terabytes(n) (1024LL * gigabytes(n))

#define minimum(A, B) (((A) < (B)) ? (A) : (B))
#define maximum(A, B) (((A) > (B)) ? (A) : (B))
#define clamp(N, MIN, MAX) (maximum(minimum((N), (MAX)), (MIN)))

#define APP_NAME "pin"

struct MotifWmHints {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long input_mode;
    unsigned long status;
};

#define MWM_HINTS_DECORATIONS (1L << 1)

typedef struct {
    u32 *pixels;
    int width;
    int height;
    int stride;
} BackBuffer;

// globals
Display *display = 0;
Window window = 0;
GC gc = 0;
int window_width = 0;
int window_height = 0;
int image_width = 0;
int image_height = 0;
int border_width = 0;
Atom wm_delete_window = 0;
u32 *image_data = 0;

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
    double from_start = (double)(now_ts.tv_sec - init_ts.tv_sec) + (double)(now_ts.tv_nsec - init_ts.tv_nsec) / 1e9;
    return from_start;
}

void fill_rect(BackBuffer *buffer, int x, int y, int w, int h, u32 color) {
    int x0 = clamp(x, 0, buffer->width);
    int y0 = clamp(y, 0, buffer->height);
    int x1 = clamp(x + w, 0, buffer->width);
    int y1 = clamp(y + h, 0, buffer->height);
    u32 *row_start = buffer->pixels + y0 * buffer->stride + x0;
    for (int i = y0; i < y1; i++) {
        u32 *pixel = row_start;
        for (int j = x0; j < x1; j++) {
            *pixel = color;
            pixel++;
        }
        row_start += buffer->stride;
    }
}

void clear(BackBuffer *buffer, u32 color) {
    fill_rect(buffer, 0, 0, buffer->width, buffer->height, color);
}

u32 color_apply_opacity(u32 color, float opacity) {
    u8 *part = (u8 *)&color;
    for (int i = 0; i < 4; i++) {
        *part = *part * opacity;
        part++;
    }
    return color;
}

void fill_border(BackBuffer *buffer) {
    u32 border_color = 0xff4fb4ea;
    { // up and bottom
        u32 *up_row_start = buffer->pixels + border_width;
        u32 *bottom_row_start = buffer->pixels + border_width + (buffer->height - 1) * buffer->stride;
        for (int i = 0; i < border_width; i++) {
            u32 color = color_apply_opacity(border_color, (float)(i + 1) / (float)border_width);
            u32 *up_pixel = up_row_start;
            u32 *bottom_pixel = bottom_row_start;
            for (int j = 0; j < buffer->width - 2 * border_width; j++) {
                *up_pixel = color;
                *bottom_pixel = color;
                up_pixel++;
                bottom_pixel++;
            }
            up_row_start += buffer->stride;
            bottom_row_start -= buffer->stride;
        }
    }
    { // left and right
        for (int j = 0; j < border_width; j++) {
            u32 color = color_apply_opacity(border_color, (float)(j + 1) / (float)border_width);
            for (int i = 0; i < buffer->height - 2 * border_width; i++) {
                u32 *left_pixel = buffer->pixels + j + (border_width + i) * buffer->stride;
                u32 *right_pixel = buffer->pixels + (border_width + i + 1) * buffer->stride - 1 - j;
                *left_pixel = color;
                *right_pixel = color;
            }
        }
    }
    { // corners
        for (int y = 0; y < border_width; y++) {
            for (int x = 0; x < border_width; x++) {
                float d = (sqrtf(x * x + y * y)) / border_width;
                u32 color;
                float t = 1.0f - d;
                if (t < 0) {
                    color = 0;
                } else {
                    color = color_apply_opacity(border_color, t);
                }

                u32 *up_left = buffer->pixels + buffer->stride * (border_width - 1 - y) + border_width - 1 - x;
                u32 *up_right = buffer->pixels + buffer->stride * (border_width - y) - border_width + x;
                u32 *bottom_left = buffer->pixels + buffer->stride * (buffer->height - border_width + y) + border_width - 1 - x;
                u32 *bottom_right = buffer->pixels + buffer->stride * (buffer->height - border_width + y + 1) - border_width + x;
                *up_left = color;
                *up_right = color;
                *bottom_left = color;
                *bottom_right = color;
            }
        }
    }
}

void copy_buffer(BackBuffer *dest, BackBuffer *src) {
    u32 *dest_row_start = dest->pixels;
    for (int dest_y = 0; dest_y < dest->height; dest_y++) {
        int src_y = (float)dest_y / dest->height * src->height;
        u32 *dest_pixel = dest_row_start;
        u32 *src_row_start = src->pixels + src_y * src->stride;
        for (int dest_x = 0; dest_x < dest->width; dest_x++) {
            int src_x = (float)dest_x / dest->width * src->width;
            u32 *src_pixel = src_row_start + src_x;
            *dest_pixel = *src_pixel;
            dest_pixel++;
        }
        dest_row_start += dest->stride;
    }
}

void render(BackBuffer *buffer, float opacity) {
    fill_border(buffer);
    BackBuffer src = { 0 };
    src.pixels = image_data;
    src.width = image_width;
    src.height = image_height;
    src.stride = image_width;

    int content_width = buffer->width - 2 * border_width;
    int content_height = buffer->height - 2 * border_width;
    BackBuffer dest = { 0 };
    dest.pixels = buffer->pixels + border_width * buffer->stride + border_width;
    dest.width = content_width;
    dest.height = content_height;
    dest.stride = buffer->stride;
    copy_buffer(&dest, &src);

    { // apply opacity
        u8 *parts = (u8 *)buffer->pixels;
        for (int i = 0; i < buffer->width * buffer->height * 4; i++) {
            parts[i] = (float)parts[i] * opacity;
        }
    }
}

void init() {
    init_ts = get_timespec();
}

void resize(int width, int height) {
    XSizeHints hints = { 0 };

    hints.flags = PMinSize | PMaxSize;
    hints.min_width = width;
    hints.min_height = height;
    hints.max_width = width;
    hints.max_height = height;

    XSetWMNormalHints(display, window, &hints);
    window_width = width;
    window_height = height;
}

void swap_buffer(BackBuffer *buffer) {
    XImage ximage = {
        .format = ZPixmap,
        .data = (char *)buffer->pixels,
        .width = buffer->width,
        .height = buffer->height,
        .xoffset = 0,
        .byte_order = LSBFirst,
        .bitmap_bit_order = MSBFirst,
        .bits_per_pixel = 32,
        .bytes_per_line = buffer->stride * 4,
        .bitmap_unit = 32,
        .bitmap_pad = 32,
        .depth = 32
    };
    XPutImage(display, window, gc, &ximage, 0, 0, 0, 0, buffer->width, buffer->height);
}

void set_title(char *title) {
    XTextProperty prop;

    Xutf8TextListToTextProperty(display, &title, 1, XUTF8StringStyle, &prop);
    XSetWMName(display, window, &prop);
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    XSetTextProperty(display, window, &prop, net_wm_name);
    XFree(prop.value);
}

void set_borderless() {
    struct MotifWmHints hints;
    hints.flags = MWM_HINTS_DECORATIONS;
    hints.decorations = 0;

    Atom mwmHintsProperty = XInternAtom(display, "_MOTIF_WM_HINTS", False);
    XChangeProperty(display, window, mwmHintsProperty, mwmHintsProperty, 32,
                    PropModeReplace, (unsigned char *)&hints, 5);
}

void set_always_on_top(int enabled) {
    Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom wm_above = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);

    XEvent event;
    event.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = wm_state;
    event.xclient.format = 32;

    event.xclient.data.l[0] = enabled;

    event.xclient.data.l[1] = wm_above;
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 1;
    event.xclient.data.l[4] = 0;

    XSendEvent(display, DefaultRootWindow(display), False, SubstructureRedirectMask | SubstructureNotifyMask, &event);

    XFlush(display);
}

// frameless, transparent, always_on_top, not resizable window
void xinit() {
    XVisualInfo vinfo;
    XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &vinfo);

    XSetWindowAttributes attrs;
    attrs.colormap = XCreateColormap(display, DefaultRootWindow(display), vinfo.visual, AllocNone);
    attrs.border_pixel = 0;
    attrs.background_pixel = 0;
    attrs.bit_gravity = NorthWestGravity; // reduce flickering when resizing

    window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, 300, 200, 0,
                           vinfo.depth, InputOutput, vinfo.visual,
                           CWBitGravity | CWColormap | CWBorderPixel | CWBackPixel, &attrs);
    resize(window_width, window_height);

    set_title(APP_NAME);
    set_borderless();

    XClassHint class = { APP_NAME, APP_NAME };
    XWMHints wm = { .flags = InputHint, .input = 1 };
    XSetWMProperties(display, window, NULL, NULL, NULL, 0, 0, &wm, &class);

    gc = XCreateGC(display, window, 0, 0);
    XMapWindow(display, window);
    set_always_on_top(true);
    XSelectInput(display, window, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | ExposureMask);
    wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", false);
    XSetWMProtocols(display, window, &wm_delete_window, 1);
    XSync(display, false);
}

void requestSystemMove() {
    Atom moveResizeAtom = XInternAtom(display, "_NET_WM_MOVERESIZE", False);

    XEvent xev = { 0 };
    xev.xclient.type = ClientMessage;
    xev.xclient.message_type = moveResizeAtom;
    xev.xclient.display = display;
    xev.xclient.window = window;
    xev.xclient.format = 32;

    Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(display, DefaultRootWindow(display),
                  &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);

    xev.xclient.data.l[0] = root_x;
    xev.xclient.data.l[1] = root_y;
    xev.xclient.data.l[2] = 8;
    xev.xclient.data.l[3] = Button1;
    xev.xclient.data.l[4] = 1;

    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &xev);

    XFlush(display);
}

void print_help_msg() {
    printf("USAGE:\n");
    printf("    pin <image_path>\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        print_help_msg();
        return 1;
    }
    const char *path = argv[1];

    init();
    display = XOpenDisplay(0);
    assert(display);
    float system_scale = 1.0f;
    {
        char *resource_manager = XResourceManagerString(display);
        if (resource_manager) {
            XrmDatabase db = XrmGetStringDatabase(resource_manager);
            XrmValue value;
            char *type;

            if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value)) {
                double dpi = atof(value.addr);
                system_scale = dpi / 96.0;
            }
            XrmDestroyDatabase(db);
        }
    }
    printf("system scale: %f\n", system_scale);

    image_data = (u32 *)stbi_load(path, &image_width, &image_height, 0, 4);
    if (!image_data) {
        fprintf(stderr, "Failed to load %s\n", path);
    }
    printf("Loaded %s\n", path);
    { // convert image from 0xaabbggrr to 0xaarrggbb
        u32 *row_start = image_data;
        for (int y = 0; y < image_height; y++) {
            u32 *pixel = row_start;
            for (int x = 0; x < image_width; x++) {
                u8 *r = (u8 *)pixel;
                u8 *b = r + 2;
                u8 temp = *r;
                *r = *b;
                *b = temp;
                pixel++;
            }
            row_start += image_width;
        }
    }

    border_width = 5 * system_scale;
    window_width = image_width + 2 * border_width;
    window_height = image_height + 2 * border_width;
    xinit();

    u32 *pixels = malloc(gigabytes(1) * sizeof(u32));

    BackBuffer back_buffer = { 0 };
    back_buffer.pixels = pixels;
    back_buffer.width = window_width;
    back_buffer.height = window_height;
    back_buffer.stride = window_width;

    render(&back_buffer, 1);

    int scale_level = 0;
    int opacity_level = 0;
    bool quit = false;
    while (!quit) {
        bool redraw = false;
        bool expose_swap = false;
        int old_scale_level = scale_level;
        int old_opacity_level = opacity_level;
        XEvent event;
        XNextEvent(display, &event);
        switch (event.type) {
            case ClientMessage: {
                if (event.xclient.data.l[0] == (long)wm_delete_window) {
                    quit = true;
                }
            } break;
            case ButtonPress: {
                if (event.xbutton.button == Button1) {
                    requestSystemMove();
                }
                if (event.xbutton.button == Button4) {
                    if ((event.xbutton.state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)) == 0) {
                        scale_level += 1;
                    } else if (event.xbutton.state & ControlMask) {
                        opacity_level += 1;
                    }
                }
                if (event.xbutton.button == Button5) {
                    if ((event.xbutton.state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)) == 0) {
                        scale_level -= 1;
                    } else if (event.xbutton.state & ControlMask) {
                        opacity_level -= 1;
                    }
                }
                scale_level = clamp(scale_level, -9, 10);
                opacity_level = clamp(opacity_level, -9, 0);
            } break;
            case ButtonRelease: {
                if (event.xbutton.button == Button2) {
                    quit = true;
                }
                if (event.xbutton.button == Button3) {
                    scale_level = 0;
                    opacity_level = 0;
                }
            } break;
            case Expose: {
                printf("expose\n");
                printf("swap\n");
                swap_buffer(&back_buffer);
            } break;
        }

        if (old_scale_level != scale_level || old_opacity_level != opacity_level) {
            redraw = true;
            if (scale_level > old_scale_level) {
                expose_swap = true;
            }
        }

        if (redraw) {
            float opacity = 1.0f + (float)opacity_level / 10.0f;
            float scale = 1.0f + (float)scale_level / 10.0f;

            int new_width = image_width * scale + 2 * border_width;
            int new_height = image_height * scale + 2 * border_width;
            if (new_width != window_width || new_height != window_height) {
                resize(new_width, new_height);
            }

            back_buffer.width = window_width;
            back_buffer.height = window_height;
            back_buffer.stride = window_width;

            render(&back_buffer, opacity);
            printf("redraw\n");
            if (!expose_swap) {
                printf("swap\n");
                swap_buffer(&back_buffer);
            }
        }
    }

    XCloseDisplay(display);

    return 0;
}
