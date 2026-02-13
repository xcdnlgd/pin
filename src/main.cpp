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
#include "../3rd/stb_image.h"
#include "../3rd/glad/gl.h"
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

int success;
char infoLog[512];

float vertices[] = {
    -1.0f, -1.0f, 0.0f,     0.0f, 0.0f,
     1.0f, -1.0f, 0.0f,     1.0f, 0.0f,
     1.0f,  1.0f, 0.0f,     1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f,     0.0f, 1.0f,
};

unsigned int indices[] = {
    0, 1, 2,
    0, 3, 2,
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

u32 load_shader(const char *path, GLenum type) {
    const char *shader_source = read_entire_file(path);
    u32 shader = glCreateShader(type);
    glShaderSource(shader, 1, &shader_source, 0);
    glCompileShader(shader);
    free((void *)shader_source);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, sizeof(infoLog), NULL, infoLog);
        fprintf(stderr, "ERROR: %s:%s\n", path, infoLog);
        return 0;
    }
    return shader;
}

u32 load_shader_program(const char *vert_path, const char *frag_path) {
    u32 vertex_shader = load_shader(vert_path, GL_VERTEX_SHADER);
    u32 fragment_shader = load_shader(frag_path, GL_FRAGMENT_SHADER);

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

    RGFW_window *win = RGFW_createWindow("name", 0, 0, image_width, image_height,
                                         RGFW_windowNoBorder | RGFW_windowNoResize | RGFW_windowCenter | RGFW_windowFloating | RGFW_windowOpenGL);

    RGFW_event event;

    int version = gladLoadGL(RGFW_getProcAddress_OpenGL);
    if (version == 0) {
        fprintf(stderr, "Failed to initialize OpenGL context\n");
        return -1;
    }
    printf("Loaded OpenGL %d.%d\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

    RGFW_window_setExitKey(win, RGFW_escape);
    RGFW_window_setIcon(win, (u8 *)icon, 3, 3, RGFW_formatRGBA8);

    u32 shader_program = load_shader_program("./src/v.vert", "./src/f.frag");

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

    RGFW_window_swapInterval_OpenGL(win, 1);

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
            RGFW_window_resize(win, new_width, new_height);
            glViewport(0, 0, new_width, new_height);
        }

        if (need_redraw) {
            glBindVertexArray(vao);
            glDrawElements(GL_TRIANGLES, array_count(indices), GL_UNSIGNED_INT, 0);
            RGFW_window_swapBuffers_OpenGL(win);
            glFlush();
        }
    }

    RGFW_window_close(win);
    return 0;
}
