#include "renderer.hh"
#include "game/game.hh"

#include "mips.hh"

#define GLPROC(_name, _type) \
_type _name;
#include "framework/gl_procs.inc"
#undef GLPROC

#include "renderer_internal.cc"

RendererSetup setup_3d(Mat4x4 view, Mat4x4 projection) {
    RendererSetup result;
    result.view = view;
    result.projection = projection;
    result.has_depth = true;
    result.mvp = projection * view;
    return result;
}

RendererSetup setup_2d(Mat4x4 projection) {
    RendererSetup result;
    result.projection = projection;
    result.view = Mat4x4::identity();
    result.has_depth = false;
    result.mvp = projection;
    return result;
}


static GLuint create_shader(const char *source) {
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    const char *const vertex_source[] = { "#version 330\n", "#define VERTEX_SHADER\n", source };
    const char *const fragment_source[] = { "#version 330\n", "", source };
    glShaderSource(vertex_shader, ARRAY_SIZE(vertex_source), vertex_source, 0);
    glShaderSource(fragment_shader, ARRAY_SIZE(fragment_source), fragment_source, 0);
    glCompileShader(vertex_shader);
    glCompileShader(fragment_shader);
    
    GLint vertex_compiled, fragment_compiled;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &vertex_compiled);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &fragment_compiled);
    
    bool shader_failed = false;
    if (!(vertex_compiled && fragment_compiled)) {
        char shader_log[4096];
        if (!vertex_compiled) {
            glGetShaderInfoLog(vertex_shader, sizeof(shader_log), 0, shader_log);
            fprintf(stderr, "[ERROR] OpenGL vertex shader compilation failed: %s\n", shader_log);
            shader_failed = true;
        }
        if (!fragment_compiled) {
            glGetShaderInfoLog(fragment_shader, sizeof(shader_log), 0, shader_log);
            fprintf(stderr, "[ERROR] OpenGL fragment shader compilation failed: %s\n", shader_log);
            shader_failed = true;
        }
    }
    
    GLuint id = glCreateProgram();
    glAttachShader(id, vertex_shader);
    glAttachShader(id, fragment_shader);
    glLinkProgram(id);
    
    GLint link_success;
    glGetProgramiv(id, GL_LINK_STATUS, &link_success);
    if (!link_success) {
        char program_log[4096];
        glGetProgramInfoLog(id, sizeof(program_log), 0, program_log);
        fprintf(stderr, "[ERROR] OpenGL shader compilation failed: %s\n", program_log);
        shader_failed = true;
    }    
    
    assert(!shader_failed);
    logprintln("Renderer", "Shader compiled");
    return id;
}

void renderer_init(Renderer *renderer) {
#define RENDERER_ARENA_SIZE MEGABYTES(256)
    arena_init(&renderer->arena, os_alloc(RENDERER_ARENA_SIZE), RENDERER_ARENA_SIZE);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
    glDebugMessageCallback(opengl_error_callback, 0);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    
#define MAX_QUADS_COUNT 1024
#define MAX_VERTEX_COUNT (1 << 16)
#define MAX_INDEX_COUNT (MAX_VERTEX_COUNT / 2 * 3)
    renderer->commands.max_quads_count = MAX_QUADS_COUNT;
    renderer->commands.quads = alloc_arr(&renderer->arena, MAX_QUADS_COUNT, RenderQuads);
    renderer->commands.max_vertex_count = MAX_VERTEX_COUNT;
    renderer->commands.vertices = alloc_arr(&renderer->arena, MAX_VERTEX_COUNT, Vertex);
    renderer->commands.max_index_count = MAX_INDEX_COUNT;
    renderer->commands.indices = alloc_arr(&renderer->arena, MAX_INDEX_COUNT, RENDERER_INDEX_TYPE);
    
    const char *standard_shader_code = R"FOO(#ifdef VERTEX_SHADER       
layout(location = 0) in vec4 position;     
layout(location = 1) in vec2 uv;       
layout(location = 2) in vec3 n;        
layout(location = 3) in vec4 color;        
layout(location = 4) in int texture_index;     
       
out vec4 rect_color;       
out vec2 frag_uv;      
       
flat out int frag_texture_index;       
// out float visibility;

uniform mat4 view_matrix = mat4(1);     
uniform mat4 projection_matrix = mat4(1);

void main() {             
    vec4 world_space = position;       
    vec4 cam_space = view_matrix * world_space;
    vec4 clip_space = projection_matrix * cam_space;        
    gl_Position = clip_space;      
       
    rect_color = color;        
    frag_uv = uv;      
    frag_texture_index = texture_index;        
    
    float d = length(cam_space.xyz);
// #define DENSITY 0.007 * 5
// #define GRADIENT 1.5
//     visibility = exp(-pow((d * DENSITY), GRADIENT));
//     visibility = clamp(visibility, 0, 1);    
}      
#else 

in vec4 rect_color;        
in vec2 frag_uv;       
flat in int frag_texture_index;        
// in float visibility;
uniform sampler2DArray tex;        
out vec4 out_color;        
void main()        
{      
    vec3 array_uv = vec3(frag_uv.x, frag_uv.y, frag_texture_index);        
    vec4 texture_sample = texture(tex, array_uv);      
    if (texture_sample.a == 0) {
        discard;
    } 
    
    out_color = texture_sample * rect_color;       
// #define FOG_COLOR vec4(0.5, 0.5, 0.5, 1)
//     out_color = mix(FOG_COLOR, out_color, visibility);
}      
#endif)FOO";
    renderer->standard_shader = create_shader(standard_shader_code);
    renderer->projection_location = glGetUniformLocation(renderer->standard_shader, "projection_matrix");
    assert(renderer->projection_location != (GLuint)-1);
    renderer->tex_location = glGetUniformLocation(renderer->standard_shader, "tex");
    assert(renderer->tex_location != (GLuint)-1);
    renderer->view_location = glGetUniformLocation(renderer->standard_shader, "view_matrix");
    assert(renderer->view_location != (GLuint)-1);
    // Generate vertex arrays
    glGenVertexArrays(1, &renderer->vertex_array);
    glBindVertexArray(renderer->vertex_array);

    glGenBuffers(1, &renderer->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, renderer->commands.max_vertex_count * sizeof(Vertex), 0, GL_STREAM_DRAW);

    glGenBuffers(1, &renderer->index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, renderer->commands.max_index_count * sizeof(RENDERER_INDEX_TYPE), 0, GL_STREAM_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, p));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, uv));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, n));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, c));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(4, 1, GL_UNSIGNED_SHORT, sizeof(Vertex), (void *)offsetof(Vertex, tex));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);
    // Generate textures
#define MAX_TEXTURE_COUNT 256
    renderer->max_texture_count = MAX_TEXTURE_COUNT;
    glGenTextures(1, &renderer->texture_array);
    glBindTexture(GL_TEXTURE_2D_ARRAY, renderer->texture_array);
    for (MipIterator iter = iterate_mips(RENDERER_TEXTURE_DIM, RENDERER_TEXTURE_DIM);
         is_valid(&iter);
         advance(&iter)) {
        glTexImage3D(GL_TEXTURE_2D_ARRAY, iter.level, GL_RGBA8, 
            iter.width, iter.height, MAX_TEXTURE_COUNT,
            0, GL_RGBA, GL_UNSIGNED_BYTE, 0);        
    }
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    size_t white_data_size = get_total_size_for_mips(RENDERER_TEXTURE_DIM, RENDERER_TEXTURE_DIM);
    void *white_data = malloc(white_data_size);
    memset(white_data, 0xFF, white_data_size);
    renderer->commands.white_texture = renderer_create_texture_mipmaps(renderer, white_data, Vec2i(RENDERER_TEXTURE_DIM, RENDERER_TEXTURE_DIM));
    free(white_data);
}

RendererCommands *renderer_begin_frame(Renderer *renderer, Vec2 display_size, Vec4 clear_color) {
    renderer->display_size = display_size;
    renderer->clear_color = clear_color;
    RendererCommands *commands = &renderer->commands;
    commands->quads_count = 0;
    commands->vertex_count = 0;
    commands->index_count = 0;
    commands->last_quads = 0;
    return commands;
}

void renderer_end_frame(Renderer *renderer) {
    TIMED_FUNCTION();
    glViewport(0, 0, (GLsizei)renderer->display_size.x, (GLsizei)renderer->display_size.y);
    glScissor(0, 0, (GLsizei)renderer->display_size.x, (GLsizei)renderer->display_size.y);
    // Upload data from vertex array to OpenGL buffer
    glBindVertexArray(renderer->vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vertex_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->commands.vertex_count * sizeof(Vertex), renderer->commands.vertices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->index_buffer);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, renderer->commands.index_count * sizeof(RENDERER_INDEX_TYPE), renderer->commands.indices);

    glClearColor(renderer->clear_color.r, renderer->clear_color.g,
                 renderer->clear_color.b, renderer->clear_color.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(renderer->standard_shader);
    u64 DEBUG_draw_call_count = 0;
    u64 DEBUG_quads_dispatched = 0;
    for (size_t i = 0; i < renderer->commands.quads_count; ++i) {
        RenderQuads *quads = renderer->commands.quads + i;
        
        if (quads->setup.has_depth) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glUniformMatrix4fv(renderer->view_location, 1, false, quads->setup.view.value_ptr());
        glUniformMatrix4fv(renderer->projection_location, 1, false, quads->setup.projection.value_ptr());
        glUniform1i(renderer->tex_location, 0); 
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, renderer->texture_array);
        glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)(6 * quads->quad_count), GL_INDEX_TYPE,
            (GLvoid *)(sizeof(RENDERER_INDEX_TYPE) * quads->index_array_offset),
            (GLint)quads->vertex_array_offset);
            
        ++DEBUG_draw_call_count;
        DEBUG_quads_dispatched += quads->quad_count;       
    }
    
    {DEBUG_VALUE_BLOCK("Renderer")
        DEBUG_VALUE(DEBUG_draw_call_count, "Draw call count");
        DEBUG_VALUE(DEBUG_quads_dispatched, "Quads dispatched");
        DEBUG_VALUE(renderer->texture_count, "Texture count");
        DEBUG_VALUE((f32)renderer->commands.index_count / renderer->commands.max_index_count * 100, "Index buffer");
        DEBUG_VALUE((f32)renderer->commands.vertex_count / renderer->commands.max_vertex_count * 100, "Vertex buffer");
    }
}

Texture renderer_create_texture_mipmaps(Renderer *renderer, void *data, Vec2i size) {
    assert(size.x <= RENDERER_TEXTURE_DIM && size.y <= RENDERER_TEXTURE_DIM);
    Texture tex;
    assert(renderer->texture_count + 1 < renderer->max_texture_count);
    tex.index = (u32)renderer->texture_count++;
    tex.width  = (u16)size.x;
    tex.height = (u16)size.y;
    assert(tex.width == size.x && tex.height == size.y);
    glBindTexture(GL_TEXTURE_2D_ARRAY, renderer->texture_array);
    for (MipIterator iter = iterate_mips(size.x, size.y);
         is_valid(&iter);
         advance(&iter)) { 
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, iter.level, 0, 0,
            tex.index, iter.width, iter.height, 1,
            GL_RGBA, GL_UNSIGNED_BYTE, (u32 *)data + iter.pixel_offset);    
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return tex;
}