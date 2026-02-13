#version 330 core
out vec4 frag_color;
in vec2 tex_coord;
uniform sampler2D texture1;
uniform float opacity;

void main()
{
    vec4 tex_color = texture(texture1, tex_coord);
    frag_color = tex_color * opacity;
}
