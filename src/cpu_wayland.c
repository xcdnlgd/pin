#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-util.h>

#include "../wayland/cursor-shape-v1-client-protocol.h"
#include "../wayland/cursor-shape-v1-protocol.c"
#include "../wayland/fractional-scale-v1-client-protocol.h"
#include "../wayland/fractional-scale-v1-protocol.c"
#include "../wayland/tablet-v2-protocol.c"
#include "../wayland/xdg-shell-client-protocol.h"
#include "../wayland/xdg-shell-protocol.c"
#include "../wayland/viewporter-client-protocol.h"
#include "../wayland/viewporter-protocol.c"

#define kilobytes(n) (1024LL * (n))
#define megabytes(n) (1024LL * kilobytes(n))
#define gigabytes(n) (1024LL * megabytes(n))
#define terabytes(n) (1024LL * gigabytes(n))
#define array_count(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
    uint32_t *pixels;
    int width, height, stride; // in pixels
} OffScreenBuffer;

typedef struct {
    float offset;
} AppState;

typedef struct {
    // Globals
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *xdg_wm_base;
    struct wl_output *wl_output;
    struct wl_seat *wl_seat;
    struct wp_cursor_shape_manager_v1 *wp_cursor_shape_manager;
    struct wp_cursor_shape_device_v1 *wp_cursor_shape_device;
    struct wp_fractional_scale_manager_v1 *wp_fractional_scale_manager;
    struct wp_fractional_scale_v1 *wp_fractional_scale;
    struct wp_viewporter *wp_viewporter;
    struct wp_viewport *wp_viewport;

    int width, height;

    float scale;
    uint8_t *pool_data;
    int offsets[2];
    struct wl_shm_pool *pool;
    struct wl_buffer *buffers[2];
    bool quit;
    bool mouse_in;

    AppState app_state;
} ClientState;

static void
randname(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int
create_shm_file(void) {
    int retries = 100;
    do {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

int allocate_shm_file(size_t size) {
    int fd = create_shm_file();
    if (fd < 0)
        return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    ClientState *state = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, version);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        state->wl_output = wl_registry_bind(registry, name, &wl_output_interface, version);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
    } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        state->wp_cursor_shape_manager = wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, version);
    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        state->wp_fractional_scale_manager = wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, version);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        state->wp_viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, version);
    }
}
static void
registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    // This space deliberately left blank
}
static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    ClientState *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);
}
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

void output_geometry_listener(void *data,
                              struct wl_output *wl_output,
                              int32_t x, int32_t y,
                              int32_t physical_width, int32_t physical_height,
                              int32_t subpixel,
                              const char *make,
                              const char *model,
                              int32_t transform) {
    printf("output_geometry %d, %d, %d, %d, %d, %s, %s, %d\n", x, y, physical_width, physical_height, subpixel, make, model, transform);
}
void output_mode_listener(void *data,
                          struct wl_output *wl_output,
                          uint32_t flags,
                          int32_t width,
                          int32_t height,
                          int32_t refresh) {
    printf("output_mode %ud, %d, %d, %d\n", flags, width, height, refresh);
}
void output_done_listener(void *data, struct wl_output *wl_output) {
}
void output_scale_listener(void *data, struct wl_output *wl_output, int32_t factor) {
}
void output_name_listener(void *data, struct wl_output *wl_output, const char *name) {
}
void output_description_listener(void *data, struct wl_output *wl_output, const char *description) {
}
static const struct wl_output_listener output_listener = {
    .geometry = output_geometry_listener,
    .mode = output_mode_listener,
    .done = output_done_listener,
    .scale = output_scale_listener,
    .name = output_name_listener,
    .description = output_description_listener,
};

void surface_enter_listener(void *data, struct wl_surface *wl_surface, struct wl_output *output) {
    printf("surface_enter\n");
}
void surface_leave_listener(void *data, struct wl_surface *wl_surface, struct wl_output *output) {
    printf("surface_leave\n");
}
void surface_preferred_buffer_scale_listener(void *data, struct wl_surface *wl_surface, int32_t factor) {
}
void surface_preferred_buffer_transform_listener(void *data, struct wl_surface *wl_surface, uint32_t transform) {
}
static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter_listener,
    .leave = surface_leave_listener,
    .preferred_buffer_scale = surface_preferred_buffer_scale_listener,
    .preferred_buffer_transform = surface_preferred_buffer_transform_listener,
};

void update_state_buffers(ClientState *state, int new_width, int new_height) {
    if (new_width != state->width || new_height != state->height) {
        state->width = new_width;
        state->height = new_height;
        int stride = state->width * 4;
        for (int i = 0; i < (int)array_count(state->offsets); i++) {
            int new_offset = state->height * stride * i;
            if (new_offset > state->offsets[i]) {
                state->offsets[i] = new_offset;
            }
            if (state->buffers[i]) {
                wl_buffer_destroy(state->buffers[i]);
            }
            state->buffers[i] = wl_shm_pool_create_buffer(state->pool, state->offsets[i], state->width, state->height, stride, WL_SHM_FORMAT_ARGB8888);
        }
    }
}

void xdg_toplevel_configure_listener(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
    ClientState *state = data;
    printf("width: %d, height: %d\n", width, height);
    update_state_buffers(state, width * state->scale, height * state->scale);
}
void xdg_toplevel_close_listener(void *data, struct xdg_toplevel *xdg_toplevel) {
    ClientState *state = data;
    state->quit = true;
}
void xdg_toplevel_configure_bounds_listener(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) {
}
void xdg_toplevel_wm_capabilities_listener(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities) {
}
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure_listener,
    .close = xdg_toplevel_close_listener,
    .configure_bounds = xdg_toplevel_configure_bounds_listener,
    .wm_capabilities = xdg_toplevel_wm_capabilities_listener,
};

void wl_keyboard_keymap_listener(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size) {
}
void wl_keyboard_enter_listener(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
}
void wl_keyboard_leave_listener(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface) {
}
void wl_keyboard_key_listener(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
}
void wl_keyboard_modifiers_listener(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
}
void wl_keyboard_repeat_info_listener(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {
}
static const struct wl_keyboard_listener wl_keyboard_listener = {
    .keymap = wl_keyboard_keymap_listener,
    .enter = wl_keyboard_enter_listener,
    .leave = wl_keyboard_leave_listener,
    .key = wl_keyboard_key_listener,
    .modifiers = wl_keyboard_modifiers_listener,
    .repeat_info = wl_keyboard_repeat_info_listener,
};

void wl_pointer_enter_listener(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    ClientState *state = data;
    state->mouse_in = true;
    wp_cursor_shape_device_v1_set_shape(state->wp_cursor_shape_device, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
}
void wl_pointer_leave_listener(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface) {
    ClientState *state = data;
    state->mouse_in = false;
}
void wl_pointer_motion_listener(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    double x = wl_fixed_to_double(surface_x);
    double y = wl_fixed_to_double(surface_y);
}
void wl_pointer_button_listener(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    ClientState *clientstate = data;
    if (button == BTN_MIDDLE && state == 0) {
        clientstate->quit = true;
    }
}
void wl_pointer_axis_listener(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
}
void wl_pointer_frame_listener(void *data, struct wl_pointer *wl_pointer) {
}
void wl_pointer_axis_source_listener(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {
}
void wl_pointer_axis_stop_listener(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {
}
void wl_pointer_axis_discrete_listener(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {
}
void wl_pointer_axis_value120_listener(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120) {
    printf("axis: %u, value: %d\n", axis, value120);
}
void wl_pointer_axis_relative_direction_listener(void *data, struct wl_pointer *wl_pointer, uint32_t axis, uint32_t direction) {
}
static const struct wl_pointer_listener wl_pointer_listener = {
    .enter = wl_pointer_enter_listener,
    .leave = wl_pointer_leave_listener,
    .motion = wl_pointer_motion_listener,
    .button = wl_pointer_button_listener,
    .axis = wl_pointer_axis_listener,
    .frame = wl_pointer_frame_listener,
    .axis_source = wl_pointer_axis_source_listener,
    .axis_stop = wl_pointer_axis_stop_listener,
    .axis_discrete = wl_pointer_axis_discrete_listener,
    .axis_value120 = wl_pointer_axis_value120_listener,
    .axis_relative_direction = wl_pointer_axis_relative_direction_listener,
};

void preferred_scale_listener(void *data, struct wp_fractional_scale_v1 *wp_fractional_scale_v1, uint32_t scale) {
    ClientState *state = data;
    printf("scale: %f\n", scale / 120.f);
    state->scale = scale / 120.f;
};

static const struct wp_fractional_scale_v1_listener wp_fractional_scale_v1_listener = {
    .preferred_scale = preferred_scale_listener,
};

void update_and_render(OffScreenBuffer *buffer, AppState *state) {
    uint32_t *pixels = buffer->pixels;
    int height = buffer->height;
    int width = buffer->width;
    int offset = (int)state->offset;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (((x + offset) + (y + offset) / 8 * 8) % 16 < 8) {
                pixels[y * width + x] = 0xFF666666;
            } else {
                pixels[y * width + x] = 0xFFEEEEEE;
            }
        }
    }
    state->offset += 0.1;
    if (state->offset > 8) {
        state->offset -= 8;
    }
}

int main(int argc, char *argv[]) {
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return 1;
    }
    printf("Connection established!\n");
    struct pollfd fds[1] = {};
    fds[0].fd = wl_display_get_fd(display);
    fds[0].events = POLLIN;

    ClientState state = {};

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(display);

    struct wl_keyboard *keyboard = wl_seat_get_keyboard(state.wl_seat);
    wl_keyboard_add_listener(keyboard, &wl_keyboard_listener, &state);
    struct wl_pointer *pointer = wl_seat_get_pointer(state.wl_seat);
    wl_pointer_add_listener(pointer, &wl_pointer_listener, &state);
    state.wp_cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(state.wp_cursor_shape_manager, pointer);

    xdg_wm_base_add_listener(state.xdg_wm_base, &xdg_wm_base_listener, &state);
    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    wl_surface_add_listener(surface, &surface_listener, &state);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, &state);
    wl_output_add_listener(state.wl_output, &output_listener, &state);
    struct xdg_toplevel *xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, &state);
    wl_surface_commit(surface);
    state.wp_viewport = wp_viewporter_get_viewport(state.wp_viewporter, surface);
    state.wp_fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(state.wp_fractional_scale_manager, surface);
    wp_fractional_scale_v1_add_listener(state.wp_fractional_scale, &wp_fractional_scale_v1_listener, &state);
    wl_display_roundtrip(display);

    // wl_surface_set_buffer_scale(surface, state.scale);

    const int shm_pool_size = gigabytes(1);
    int fd = allocate_shm_file(shm_pool_size);
    state.pool_data = mmap(NULL, shm_pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    state.pool = wl_shm_create_pool(state.shm, fd, shm_pool_size);
    const int init_width = 2560, init_height = 1080;

    update_state_buffers(&state, init_width, init_height);

    int back_buffer_index = 0; // 0/1

    printf("frame start\n");
    while (!state.quit) {
        while (wl_display_prepare_read(display) != 0) {
            wl_display_dispatch_pending(display);
        }
        wl_display_flush(display);
        if (poll(fds, 1, 0) > 0) {
            wl_display_read_events(display);
            wl_display_dispatch_pending(display);
        } else {
            wl_display_cancel_read(display);
        }

        OffScreenBuffer off_screen_buffer = {};
        off_screen_buffer.pixels = (uint32_t *)(state.pool_data + state.offsets[back_buffer_index]);
        off_screen_buffer.width = state.width;
        off_screen_buffer.height = state.height;
        off_screen_buffer.stride = state.width;
        update_and_render(&off_screen_buffer, &state.app_state);

        wp_viewport_set_source(state.wp_viewport, wl_fixed_from_double(0), wl_fixed_from_double(0), wl_fixed_from_double(state.width), wl_fixed_from_double(state.height));
        float scale = 1;
        scale = state.scale;
        wp_viewport_set_destination(state.wp_viewport, round(state.width / scale), round(state.height / scale));

        wl_surface_attach(surface, state.buffers[back_buffer_index], 0, 0);
        back_buffer_index = !back_buffer_index;
        wl_surface_damage(surface, 0, 0, INT32_MAX, INT32_MAX);
        wl_surface_commit(surface);
    }

    return 0;
}
