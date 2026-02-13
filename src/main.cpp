#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef RELEASE
#include "./impl.c"
#else
#define RGFW_NATIVE
#define RGFW_OPENGL
#define RGFW_IMPORT
#define RGFW_UNIX
#include "../3rd/RGFW.h"
#include "../3rd/glad/gl.h"
#include "../3rd/stb_image.h"
#endif
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#define array_count(array) (sizeof(array) / sizeof((array)[0]))
#define kilobytes(n) (1024LL * (n))
#define megabytes(n) (1024LL * kilobytes(n))
#define gigabytes(n) (1024LL * megabytes(n))
#define terabytes(n) (1024LL * gigabytes(n))

#define minimum(A, B) (((A) < (B)) ? (A) : (B))
#define maximum(A, B) (((A) > (B)) ? (A) : (B))
#define clamp(N, MIN, MAX) (maximum(minimum((N), (MAX)), (MIN)))

int success;
char infoLog[512];

int border_width = 5;

const char *vertex_source =
    "#version 330 core\n"
    "layout (location = 0) in vec3 a_pos;\n"
    "layout (location = 1) in vec2 a_tex_coord;\n"
    "\n"
    "out vec2 tex_coord;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    gl_Position = vec4(a_pos, 1.0);\n"
    "    tex_coord = a_tex_coord;\n"
    "}\n";

const char *fragment_source =
    "#version 330 core\n"
    "out vec4 frag_color;\n"
    "in vec2 tex_coord;\n"
    "uniform sampler2D texture1;\n"
    "uniform float opacity;\n"
    "uniform float border_width;\n"
    "uniform float width;\n"
    "uniform float height;\n"
    "\n"
    "float sdBox( in vec2 p, in vec2 b )\n"
    "{\n"
    "    vec2 d = abs(p)-b;\n"
    "    return length(max(d,0.0)) + min(max(d.x,d.y),0.0);\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "    float x_scale = (width+2*border_width)/width;\n"
    "    float y_scale = (height+2*border_width)/height;\n"
    "\n"
    "    vec2 scaled_uv;\n"
    "    scaled_uv.x = (tex_coord.x - 0.5) * x_scale + 0.5;\n"
    "    scaled_uv.y = (tex_coord.y - 0.5) * y_scale + 0.5;\n"
    "\n"
    "    bool is_image = (scaled_uv.x >= 0.0 && scaled_uv.x <= 1.0 &&\n"
    "                     scaled_uv.y >= 0.0 && scaled_uv.y <= 1.0);\n"
    "\n"
    "    if (is_image) {\n"
    "        frag_color = texture(texture1, scaled_uv) * opacity;\n"
    "    } else {\n"
    "        vec2 b = vec2(width/2, height/2);\n"
    "        vec2 p;\n"
    "        p.x = (tex_coord.x-0.5)*(width+2*border_width);\n"
    "        p.y = (tex_coord.y-0.5)*(height+2*border_width);\n"
    "        float t = sdBox(p, b) / border_width;\n"
    "        frag_color = mix(vec4(0.3098, 0.7058, 0.9176, 1.0) * opacity, vec4(0, 0, 0, 0), t);\n"
    "    }\n"
    "}\n";

float vertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
    1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
    -1.0f, 1.0f, 0.0f, 0.0f, 1.0f
};

unsigned int indices[] = {
    0, 1, 2,
    0, 3, 2
};

const u32 vertex_stride = 5 * sizeof(float);

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

u32 load_shader_source(const char *source, GLenum type) {
    u32 shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, 0);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, sizeof(infoLog), NULL, infoLog);
        fprintf(stderr, "ERROR: %s\n", infoLog);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

u32 load_shader_path(const char *path, GLenum type) {
    const char *shader_source = read_entire_file(path);
    u32 shader = load_shader_source(shader_source, type);
    free((void *)shader_source);
    return shader;
}

u32 load_load_shader_program(u32 vertex_shader, u32 fragment_shader) {
    u32 shader_program = glCreateProgram();

    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader_program, sizeof(infoLog), NULL, infoLog);
        fprintf(stderr, "ERROR: linking shader: %s\n", infoLog);
        glDeleteProgram(shader_program);
        return 0;
    }
    return shader_program;
}

u32 load_shader_program_path(const char *vert_path, const char *frag_path) {
    u32 vertex_shader = load_shader_path(vert_path, GL_VERTEX_SHADER);
    u32 fragment_shader = load_shader_path(frag_path, GL_FRAGMENT_SHADER);
    u32 shader_program = load_load_shader_program(vertex_shader, fragment_shader);
    return shader_program;
}

u32 load_shader_program_source(const char *vert_source, const char *frag_source) {
    u32 vertex_shader = load_shader_source(vert_source, GL_VERTEX_SHADER);
    u32 fragment_shader = load_shader_source(frag_source, GL_FRAGMENT_SHADER);
    u32 shader_program = load_load_shader_program(vertex_shader, fragment_shader);
    return shader_program;
}

u32 load_texture(const char *path) {
    stbi_set_flip_vertically_on_load(true);
    int width, height, nrChannels;
    u8 *data = stbi_load(path, &width, &height, &nrChannels, 4);
    if (!data) {
        fprintf(stderr, "ERROR: cannot load %s\n", path);
        return 0;
    }
    GLenum color_format = GL_RGBA;
    printf("loaded %s\n", path);
    u32 texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, color_format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    free(data);
    return texture;
}

void set_uniform_int(u32 shader_program, const char *name, int value) {
    glUniform1i(glGetUniformLocation(shader_program, name), value);
}

void set_uniform_float(u32 shader_program, const char *name, float value) {
    glUniform1f(glGetUniformLocation(shader_program, name), value);
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

void init() {
    init_ts = get_timespec();
}

void requestSystemMove(RGFW_window *win) {
    Display *display = (Display *)RGFW_getDisplay_X11();
    Atom moveResizeAtom = XInternAtom(display, "_NET_WM_MOVERESIZE", False);

    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.xclient.type = ClientMessage;
    xev.xclient.message_type = moveResizeAtom;
    xev.xclient.display = (Display *)RGFW_getDisplay_X11();
    xev.xclient.window = win->src.window;
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

    RGFW_monitor monitor = RGFW_getPrimaryMonitor();
    border_width *= monitor.scaleX;

    RGFW_setClassName("pin");
    RGFW_window *win = RGFW_createWindow("pin", 0, 0, image_width + 2 * border_width, image_height + 2 * border_width,
                                         RGFW_windowNoBorder | RGFW_windowNoResize | RGFW_windowCenter | RGFW_windowFloating | RGFW_windowOpenGL | RGFW_windowTransparent);

    RGFW_event event;

    int version = gladLoadGL(RGFW_getProcAddress_OpenGL);
    if (version == 0) {
        fprintf(stderr, "Failed to initialize OpenGL context\n");
        return -1;
    }
    printf("Loaded OpenGL %d.%d\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

#ifdef RELEASE
    u32 shader_program = load_shader_program_source(vertex_source, fragment_source);
#else
    u32 shader_program = load_shader_program_path("./src/v.vert", "./src/f.frag");
#endif

    u32 vao;
    glGenVertexArrays(1, &vao);
    u32 vbo;
    glGenBuffers(1, &vbo);
    u32 ebo;
    glGenBuffers(1, &ebo);
    {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_stride, 0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vertex_stride, (void *)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }

    glUseProgram(shader_program);
    u32 texture1 = load_texture(path);
    set_uniform_int(shader_program, "texture1", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture1);

    RGFW_window_swapInterval_OpenGL(win, 1);

    long long frame = 0;
    int scale_level = 0;
    int opacity_level = 0;
    bool ctrl_down = false;
    float x_scale = 0;
    float y_scale = 0;
    while (RGFW_window_shouldClose(win) == false) {
        frame++;
        RGFW_waitForEvent(-1);

        bool need_redraw = false;

        while (RGFW_window_checkEvent(win, &event)) {
            if (event.type == RGFW_quit) return 0;
            switch (event.type) {
                case RGFW_windowResized: {
                } break;
                case RGFW_keyPressed: {
                    if (event.key.value == RGFW_controlL || event.key.value == RGFW_controlR) {
                        ctrl_down = true;
                    }
                } break;
                case RGFW_keyReleased: {
                    if (event.key.value == RGFW_controlL || event.key.value == RGFW_controlR) {
                        ctrl_down = false;
                    }
                } break;
                case RGFW_mousePosChanged: {
                } break;
                case RGFW_mouseButtonPressed: {
                    if (event.button.value == RGFW_mouseLeft) {
                        requestSystemMove(win);
                    } else if (event.button.value == RGFW_mouseMiddle) {
                        return 0;
                    } else if (event.button.value == RGFW_mouseRight) {
                        opacity_level = 0;
                        scale_level = 0;
                    }
                } break;
                case RGFW_mouseButtonReleased: {
                } break;
                case RGFW_windowRefresh: {
                    need_redraw = true;
                } break;
                case RGFW_mouseScroll: {
                    if (ctrl_down) {
                        opacity_level += event.scroll.y;
                        opacity_level = clamp(opacity_level, -9, 0);
                        need_redraw = true;
                    } else {
                        scale_level += event.scroll.y;
                        scale_level = clamp(scale_level, -9, 10);
                    }
                } break;
            }
        }

        float scale_factor = 1.0f + (float)scale_level / 10.0f;
        int new_width = image_width * scale_factor + 2 * border_width;
        int new_height = image_height * scale_factor + 2 * border_width;
        if (new_width != win->w || new_height != win->h) {
            RGFW_window_resize(win, new_width, new_height);
            glViewport(0, 0, new_width, new_height);
        }

        if (need_redraw) {
            glBindVertexArray(vao);
            float opacity = 1.0f + (float)opacity_level / 10.0f;
            set_uniform_float(shader_program, "opacity", opacity);
            set_uniform_float(shader_program, "border_width", border_width);
            set_uniform_float(shader_program, "width", image_width * scale_factor);
            set_uniform_float(shader_program, "height", image_height * scale_factor);
            glDrawElements(GL_TRIANGLES, array_count(indices), GL_UNSIGNED_INT, 0);
            RGFW_window_swapBuffers_OpenGL(win);
            glFlush();
        }
    }

    RGFW_window_close(win);
    return 0;
}
