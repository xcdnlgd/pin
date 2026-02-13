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
    "\n"
    "void main()\n"
    "{\n"
    "    vec4 tex_color = texture(texture1, tex_coord);\n"
    "    frag_color = tex_color * opacity;\n"
    "}\n";

float vertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
    1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
    -1.0f, 1.0f, 0.0f, 0.0f, 1.0f
};

unsigned int indices[] = {
    0,
    1,
    2,
    0,
    3,
    2,
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

u32 load_texture(const char *path, GLenum color_format) {
    stbi_set_flip_vertically_on_load(true);
    int width, height, nrChannels;
    u8 *data = stbi_load(path, &width, &height, &nrChannels, 0);
    if (!data) {
        fprintf(stderr, "ERROR: cannot load %s\n", path);
        return 0;
    }
    printf("loaded %s\n", path);
    u32 texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, color_format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    free(data);
    return texture;
}

void set_uniform_int(u32 shader_program, const char *name, int value) {
    glUniform1i(glGetUniformLocation(shader_program, name), value);
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
    RGFW_setClassName("pin");
    RGFW_window *win = RGFW_createWindow("pin", 0, 0, image_width, image_height,
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
    u32 shader_program = load_shader_program_path("/home/xcdnlgd/dev/cpp/pin/src/v.vert", "/home/xcdnlgd/dev/cpp/pin/src/f.frag");
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
    u32 texture1 = load_texture(path, GL_RGBA);
    set_uniform_int(shader_program, "texture1", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture1);

    int opacity_loc = glGetUniformLocation(shader_program, "opacity");

    RGFW_window_swapInterval_OpenGL(win, 1);

    long long frame = 0;
    int scale_level = 0;
    int opacity_level = 0;
    bool dragging = false;
    int drag_start_local_x = 0;
    int drag_start_local_y = 0;
    bool ctrl_down = false;
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
                        dragging = true;
                        assert(RGFW_window_getMouse(win, &drag_start_local_x, &drag_start_local_y));
                        RGFW_window_setMouseStandard(win, RGFW_mouseResizeAll);
                    } else if (event.button.value == RGFW_mouseMiddle) {
                        return 0;
                    }
                } break;
                case RGFW_mouseButtonReleased: {
                    if (event.button.value == RGFW_mouseLeft) {
                        dragging = false;
                        RGFW_window_setMouseDefault(win);
                    }
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
        int new_width = image_width * scale_factor;
        int new_height = image_height * scale_factor;
        if (new_width != win->w || new_height != win->h) {
            RGFW_window_resize(win, new_width, new_height);
            glViewport(0, 0, new_width, new_height);
        }

        if (dragging) {
            int x;
            int y;
            assert(RGFW_getGlobalMouse(&x, &y));
            int new_window_x = x - drag_start_local_x;
            int new_window_y = y - drag_start_local_y;
            if (win->x != new_window_x || win->y != new_window_y) {
                RGFW_window_move(win, new_window_x, new_window_y);
            }
        }

        if (need_redraw) {
            glBindVertexArray(vao);
            float opacity = 1.0f + (float)opacity_level / 10.0f;
            glUniform1f(opacity_loc, opacity);
            glDrawElements(GL_TRIANGLES, array_count(indices), GL_UNSIGNED_INT, 0);
            RGFW_window_swapBuffers_OpenGL(win);
            glFlush();
        }
    }

    RGFW_window_close(win);
    return 0;
}
