#include "framework/renderer.hh"
#include "framework/assets.hh"
#include "game/game.hh"

#define GLPROC(_name, _type) \
static _type _name;
#include "framework/gl_procs.inc"
#undef GLPROC

Renderer *renderer;

#include "framework/renderer_internal.cc"

void Renderer::init() {
    logprintln("Renderer", "Init start");
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
    glDebugMessageCallback(opengl_error_callback, 0);
    
    Str standard_shader_code = R"FOO(#ifdef VERTEX_SHADER
layout(location = 0) in vec4 p;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 n;
layout(location = 3) in vec4 c;

out vec2 pass_uv;
out vec4 pass_c;
uniform mat4 mvp;

void main() {
    gl_Position = mvp * p;
    pass_uv = uv;
    pass_c = c;
}

#else

out vec4 out_c;

uniform sampler2D tex;

in vec2 pass_uv;
in vec4 pass_c;

void main() {
    //out_c = vec4(1, 0, 1, 0);
    out_c = pass_c * texture(tex, pass_uv);
}

#endif)FOO";
    standard_shader = create_shader(standard_shader_code);
    Str terrain_shader_code = R"FOO(#ifdef VERTEX_SHADER
layout(location = 0) in vec4 p;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 n;
layout(location = 3) in vec4 c;

flat out vec4 pass_c;
uniform mat4 mvp;

void main() {
    gl_Position = mvp * p;
    pass_c = c;
}
#else 
flat in vec4 pass_c;
out vec4 out_c;

void main() {
    out_c = pass_c;
}
#endif 
)FOO";
    terrain_shader = create_shader(terrain_shader_code);
    default_shader = standard_shader;
   
    // glEnable(GL_DEPTH_TEST);
    // glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    
    assert(!::renderer);
    ::renderer = this;
    logprint("Renderer", "Init end\n");
    
    this->vertex_count = 0;
    this->max_vertex_count = 1 << 16;
    this->vertices = (Vertex *)this->arena.alloc(this->max_vertex_count * sizeof(Vertex));
}

void Renderer::cleanup() {
}

void Renderer::begin_frame() {
    statistics = current_statistics;
    current_statistics.begin_frame();
}

void Renderer::clear(Vec4 color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);    
}

void Renderer::set_draw_region(Vec2 window_size) {
    glViewport(0, 0, window_size.x, window_size.y);
    glScissor(0, 0, window_size.x, window_size.y);
}

void Renderer::imm_begin() {
    this->vertex_count = 0;
    this->current_shader = this->default_shader;
    if (immediate_vao == GL_INVALID_ID) {
        glGenVertexArrays(1, &immediate_vao);
        glBindVertexArray(immediate_vao);
        glGenBuffers(1, &immediate_vbo);
    }
}

void Renderer::imm_flush() {
    if (!vertex_count) { return; }
    
    Shader shader = current_shader;
    assert(shader != Shader::invalid());
    bind_shader(&shader);
    glUniformMatrix4fv(glGetUniformLocation(shader.id, "mvp"), 1, false, this->mvp.value_ptr());
    Texture texture = current_texture;
	assert(texture != Texture::invalid());
    bind_texture(&texture);
    glUniform1i(glGetUniformLocation(shader.id, "tex"), 0);
    
    glBindVertexArray(immediate_vao);
    glBindBuffer(GL_ARRAY_BUFFER, immediate_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * this->vertex_count, this->vertices, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, p));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, uv));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, n));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, c));
    glEnableVertexAttribArray(3);
    
    ++current_statistics.draw_call_count;
    glDrawArrays(GL_TRIANGLES, 0, this->vertex_count);
}

void Renderer::imm_vertex(const Vertex &v) {
    assert(this->vertex_count < this->max_vertex_count);
    this->vertices[this->vertex_count++] = v;
}

void Renderer::set_mvp(const Mat4x4 &mvp) {
    this->mvp = mvp;
    this->imvp = Mat4x4::inverse(this->mvp);
}

void Renderer::set_shader(Shader shader) {
    if (shader == Shader::invalid()) {
        shader = this->default_shader;
    }
    
    this->current_shader = shader;
}

void Renderer::set_texture(Texture texture) {
    if (texture.id == GL_INVALID_ID) {
        texture = assets->get_tex("white");
    }
    this->current_texture = texture;
}

void Renderer::imm_draw_quad(Vec3 v00, Vec3 v01, Vec3 v10, Vec3 v11,
                             Vec4 c00, Vec4 c01, Vec4 c10, Vec4 c11,
                             Vec2 uv00, Vec2 uv01, Vec2 uv10, Vec2 uv11,
                             Texture texture) {
    this->set_texture(texture);
    this->imm_begin();
    Vertex v0, v1, v2, v3;
    v0.p = v00;
    v0.uv = uv00;
    v0.c = c00;
    v1.p = v01;
    v1.uv = uv01;
    v1.c = c01;
    v2.p = v10;
    v2.uv = uv10;
    v2.c = c10;
    v3.p = v11;
    v3.uv = uv11;
    v3.c = c11;
    this->imm_vertex(v3);
    this->imm_vertex(v1);
    this->imm_vertex(v0);
    this->imm_vertex(v0);
    this->imm_vertex(v2);
    this->imm_vertex(v3);
    this->imm_flush();                  
}

void Renderer::imm_draw_quad(Vec3 v00, Vec3 v01, Vec3 v10, Vec3 v11,
                             Vec4 c, Texture texture) {
    this->imm_draw_quad(v00, v01, v10, v11, c, c, c, c, Vec2(0, 0), Vec2(0, 1), Vec2(1, 0), Vec2(1, 1), texture);
}

void Renderer::imm_draw_quad(Vec3 v[4], Texture texture) {
    this->imm_draw_quad(v[0], v[1], v[2], v[3], Colors::white, Colors::white, Colors::white, Colors::white,
                        Vec2(0, 0), Vec2(0, 1), Vec2(1, 0), Vec2(1, 1), texture);
}

void Renderer::imm_draw_rect(Rect rect, Vec4 color, Rect uv_rect, Texture texture) {
    Vec3 v[4]; 
    rect.store_points(v);
    Vec2 uvs[4];
    uv_rect.store_points(uvs);
    this->imm_draw_quad(v[0], v[1], v[2], v[3], color, color, color, color, uvs[0], uvs[1], uvs[2], uvs[3], texture);
}

// void Renderer::imm_draw_text(Vec2 p, Vec4 color, const char *text, Font *font, f32 scale) {
//     f32 line_height = font->size * scale;

// 	f32 rwidth  = 1.0f / (f32)font->tex->size.x;
// 	f32 rheight = 1.0f / (f32)font->tex->size.y;

// 	Vec3 offset = Vec3(p, 0);
// 	offset.y += line_height;
    
//     set_shader();
// 	for (const char *scan = text; *scan; ++scan) {
// 		char symbol = *scan;

// 		if ((symbol >= font->first_codepoint) && (symbol < font->first_codepoint + font->glyphs.len)) {
// 			FontGlyph *glyph = &font->glyphs[symbol - font->first_codepoint];

// 			f32 glyph_width  = (glyph->offset2_x - glyph->offset1_x) * scale;
// 			f32 glyph_height = (glyph->offset2_y - glyph->offset1_y) * scale;

// 			f32 y1 = offset.y + glyph->offset1_y * scale;
// 			f32 y2 = y1 + glyph_height;
// 			f32 x1 = offset.x + glyph->offset1_x * scale;
// 			f32 x2 = x1 + glyph_width;

// 			f32 s1 = glyph->min_x * rwidth;
// 			f32 t1 = glyph->min_y * rheight;
// 			f32 s2 = glyph->max_x * rwidth;
// 			f32 t2 = glyph->max_y * rheight;
//             this->imm_draw_rect(Rect(x1, y1, x2 - x1, y2 - y1), color, Rect(s1, t1, s2 - s1, t2 - t1), font->tex);
// 			f32 char_advance = glyph->x_advance * scale;
// 			offset.x += char_advance;
// 		}
// 	}
// }

void Renderer::set_renderering_3d(const Mat4x4 &mvp) {
    this->set_mvp(mvp);
    glEnable(GL_DEPTH_TEST);    
}

void Renderer::set_renderering_2d(Vec2 winsize) {
    Mat4x4 win_proj = Mat4x4::ortographic_2d(0, winsize.x, winsize.y, 0);
    renderer->set_mvp(win_proj);
    glDisable(GL_DEPTH_TEST);
}

void Renderer::imm_draw_line(Vec3 a, Vec3 b, Vec4 color, f32 thickness) {
    // @TODO not behaving properly when ab is close to parallel with cam_z
    // Vec3 cam_z = this->imvp.v[2].xyz;
    Vec3 cam_z = this->mvp.get_z();
    Vec3 line = (b - a);
    line -= cam_z * Math::dot(cam_z, line);
    Vec3 line_perp = Math::cross(line, cam_z);
    // Vec3 other_perp = Math::cross(line_perp, line);
    line_perp = Math::normalize(line_perp);
    // other_perp = Math::normalize(other_perp);
    line_perp *= thickness;
    // other_perp *= thickness;
    this->imm_draw_quad(a - line_perp, a + line_perp, b - line_perp, b + line_perp, color);
    // this->imm_draw_quad(a - other_perp, a + other_perp, b - other_perp, b + other_perp, color);
    
}

void Renderer::imm_draw_quad_outline(Vec3 v00, Vec3 v01, Vec3 v10, Vec3 v11, Vec4 color, f32 thickness) {
    this->imm_draw_line(v00, v01, color, thickness);
    this->imm_draw_line(v01, v11, color, thickness);
    this->imm_draw_line(v11, v10, color, thickness);
    this->imm_draw_line(v10, v00, color, thickness);
}

void Renderer::imm_draw_rect_outline(Rect rect, Vec4 color, f32 thickness) {
    Vec3 v[4]; 
    rect.store_points(v);
    this->imm_draw_quad_outline(v[0], v[1], v[2], v[3], color, thickness);
}

Texture Renderer::create_texture(void *buffer, Vec2i size) {
    Texture tex = create_texture_internal(buffer, size);
    return tex;
}