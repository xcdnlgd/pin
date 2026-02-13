#version 330 core
out vec4 frag_color;
in vec2 tex_coord;
uniform sampler2D texture1;
uniform float opacity;
uniform float border_width;
uniform float width;
uniform float height;

float sdBox( in vec2 p, in vec2 b )
{
    vec2 d = abs(p)-b;
    return length(max(d,0.0)) + min(max(d.x,d.y),0.0);
}

void main()
{
    float x_scale = (width+2*border_width)/width;
    float y_scale = (height+2*border_width)/height;

    vec2 scaled_uv;
    scaled_uv.x = (tex_coord.x - 0.5) * x_scale + 0.5;
    scaled_uv.y = (tex_coord.y - 0.5) * y_scale + 0.5;

    bool is_image = (scaled_uv.x >= 0.0 && scaled_uv.x <= 1.0 &&
                     scaled_uv.y >= 0.0 && scaled_uv.y <= 1.0);

    if (is_image) {
        frag_color = texture(texture1, scaled_uv) * opacity;
    } else {
        vec2 b = vec2(width/2, height/2);
        vec2 p;
        p.x = (tex_coord.x-0.5)*(width+2*border_width);
        p.y = (tex_coord.y-0.5)*(height+2*border_width);
        float t = sdBox(p, b) / border_width;
        frag_color = mix(vec4(0.3098, 0.7058, 0.9176, 1.0) * opacity, vec4(0, 0, 0, 0), t);
    }
}
