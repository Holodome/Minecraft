#include "renderer.hh"
#include "game.hh"

#include "mips.hh"

#define GLPROC(_name, _type) \
_type _name;
#include "gl_procs.inc"
#undef GLPROC

static void APIENTRY opengl_error_callback(GLenum source, GLenum type, GLenum id, GLenum severity, GLsizei length,
    const GLchar* message, const void *_) {
    (void)_;
    (void)length;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

    char *source_str;
    switch(source) {
        case GL_DEBUG_SOURCE_API_ARB: {
		    source_str = "Calls to OpenGL API";
        } break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB: {
		    source_str = "Calls to window-system API";
        } break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB: {
		    source_str = "A compiler for shading language"; 
        } break;
        case GL_DEBUG_SOURCE_THIRD_PARTY_ARB: {
		    source_str = "Application associated with OpenGL"; 
        } break;
        case GL_DEBUG_SOURCE_APPLICATION_ARB: {
		    source_str = "Generated by user"; 
        } break;
        case GL_DEBUG_SOURCE_OTHER_ARB: {
		    source_str = "Other"; 
        } break;
        default: {
		    source_str = "Unknown"; 
        } break;
    }

    char *type_str;
    switch (type) {
        case GL_DEBUG_TYPE_ERROR_ARB: {
		    type_str = "ERROR"; 
        } break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB: {
		    type_str = "DEPRECATED_BEHAVIOR"; 
        } break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB: {
		    type_str = "UNDEFINED_BEHAVIOR"; 
        } break;
        case GL_DEBUG_TYPE_PORTABILITY_ARB: {
		    type_str = "PORTABILITY";
        } break;
        case GL_DEBUG_TYPE_PERFORMANCE_ARB: {
		    type_str = "PERFORMANCE"; 
        } break;
        case GL_DEBUG_TYPE_OTHER_ARB: {
		    type_str = "OTHER"; 
        } break;
        default: {
		    type_str = "UNKNOWN"; 
        } break;
    }

    char *severity_str;
    switch(severity) {
        case GL_DEBUG_SEVERITY_NOTIFICATION: {
		    severity_str = "NOTIFICATION"; 
        } break;
        case GL_DEBUG_SEVERITY_LOW_ARB: {
		    severity_str = "LOW"; 
        } break;
        case GL_DEBUG_SEVERITY_MEDIUM_ARB: {
		    severity_str = "MEDIUM"; 
        } break;
        case GL_DEBUG_SEVERITY_HIGH_ARB: {
		    severity_str = "HIGH"; 
        } break;
        default: {
		    severity_str = "UNKNOWN"; 
        } break;
    }

    fprintf(stderr, "OpenGL Error Callback\n<Source: %s, type: %s, Severity: %s, ID: %u>:::\n%s\n",
			source_str, type_str, severity_str, id, message);
}

RendererSetup setup_3d(u32 framebuffer, Mat4x4 view, Mat4x4 projection) {
    RendererSetup result;
    result.view = view;
    result.projection = projection;
    result.mvp = projection * view;
    result.framebuffer = framebuffer;
    return result;
}

RendererSetup setup_2d(u32 framebuffer, Mat4x4 projection) {
    RendererSetup result;
    result.projection = projection;
    result.view = Mat4x4::identity();
    result.mvp = projection;
    result.framebuffer = framebuffer;
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
    return id;
}

static RendererFramebuffer create_framebuffer(Renderer *renderer, Vec2 size, bool has_depth, bool filtered) {
    RendererFramebuffer result = {};
    result.has_depth = has_depth;
    result.size = size;
    
    glGenFramebuffers(1, &result.id);
    glBindFramebuffer(GL_FRAMEBUFFER, result.id);
    glGenTextures(1, &result.texture_id);
    glBindTexture(GL_TEXTURE_2D, result.texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    GLenum filter = filtered ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, result.texture_id, 0);
    renderer->video_memory_used += size.x * size.y * 4;
    if (has_depth) {
        glGenRenderbuffers(1, &result.depth_id);
        glBindRenderbuffer(GL_RENDERBUFFER, result.depth_id);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, size.x, size.y);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, result.depth_id);
        renderer->video_memory_used += size.x * size.y * 3;
    }
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return result;
}

static void free_framebuffer(Renderer *renderer, RendererFramebuffer *framebuffer) {
    if (framebuffer->id) {
        if (framebuffer->has_depth) {
            renderer->video_memory_used -= framebuffer->size.x * framebuffer->size.y * 3;
            glDeleteRenderbuffers(1, &framebuffer->depth_id);
        }
        glDeleteTextures(1, &framebuffer->texture_id);
        renderer->video_memory_used -= framebuffer->size.x * framebuffer->size.y * 4;
        glDeleteFramebuffers(1, &framebuffer->id);
    }
}

void init_renderer_for_settings(Renderer *renderer, RendererSettings settings) {
    renderer->texture_count = 0;
    GLenum min_filter, mag_filter;
    if (settings.filtered) {
        mag_filter = GL_LINEAR;
        if (settings.mipmapping) {
            min_filter = GL_LINEAR_MIPMAP_LINEAR;
        }  else {
            min_filter = GL_LINEAR;
        }
    } else {
        mag_filter = GL_NEAREST;
        if (settings.mipmapping) {
            min_filter = GL_NEAREST_MIPMAP_NEAREST;
        }  else {
            min_filter = GL_NEAREST;
        }
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, renderer->texture_array); 
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, mag_filter);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    
#if 1
    size_t white_data_size = get_total_size_for_mips(RENDERER_TEXTURE_DIM, RENDERER_TEXTURE_DIM);
    TempMemory white_temp = begin_temp_memory(&renderer->arena);
    void *white_data = alloc(&renderer->arena, white_data_size);
    memset(white_data, 0xFF, white_data_size);
    renderer->commands.white_texture = renderer_create_texture_mipmaps(renderer, white_data, RENDERER_TEXTURE_DIM, RENDERER_TEXTURE_DIM);
    end_temp_memory(white_temp);
#else   
    // ...
    u32 white_data = 0xFFFFFFFF;
    renderer->commands.white_texture = renderer_create_texture_mipmaps(renderer, &white_data, 1, 1);
#endif   
      
    // @TODO optimize how we do this - there is probably no need to delete framebuffer handles
    // and its attachment handles
    free_framebuffer(renderer, renderer->framebuffers + RENDERER_FRAMEBUFFER_GAME_WORLD);
    free_framebuffer(renderer, renderer->framebuffers + RENDERER_FRAMEBUFFER_GAME_INTERFACE);
    free_framebuffer(renderer, renderer->framebuffers + RENDERER_FRAMEBUFFER_BLUR1);
    free_framebuffer(renderer, renderer->framebuffers + RENDERER_FRAMEBUFFER_BLUR2);
    renderer->framebuffers[RENDERER_FRAMEBUFFER_GAME_WORLD] = create_framebuffer(renderer, settings.display_size, true, settings.filtered);
    renderer->framebuffers[RENDERER_FRAMEBUFFER_GAME_INTERFACE] = create_framebuffer(renderer, settings.display_size, false, settings.filtered);
    renderer->framebuffers[RENDERER_FRAMEBUFFER_BLUR1] = create_framebuffer(renderer, settings.display_size * 0.5, false, settings.filtered);
    renderer->framebuffers[RENDERER_FRAMEBUFFER_BLUR2] = create_framebuffer(renderer, settings.display_size * 0.5, false, settings.filtered);
    // renderer->framebuffers[RENDERER_FRAMEBUFFER_BLUR1] = create_framebuffer(renderer, settings.display_size, false, settings.filtered);
    // renderer->framebuffers[RENDERER_FRAMEBUFFER_BLUR2] = create_framebuffer(renderer, settings.display_size, false, settings.filtered);
    
    
    renderer->settings = settings;
}

void renderer_init(Renderer *renderer, RendererSettings settings) {
#define RENDERER_ARENA_SIZE MEGABYTES(256)
    arena_init(&renderer->arena, os_alloc(RENDERER_ARENA_SIZE), RENDERER_ARENA_SIZE);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
    glDebugMessageCallback(opengl_error_callback, 0);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
    
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
}      
#else 

in vec4 rect_color;        
in vec2 frag_uv;       
flat in int frag_texture_index;        
uniform sampler2DArray tex;        
out vec4 out_color;        
void main()        
{      
    vec3 array_uv = vec3(frag_uv.x, frag_uv.y, frag_texture_index);        
    vec4 texture_sample = texture(tex, array_uv);      
    
    out_color = texture_sample * rect_color;       
}      
#endif)FOO";
    renderer->standard_shader = create_shader(standard_shader_code);
    renderer->projection_location = glGetUniformLocation(renderer->standard_shader, "projection_matrix");
    assert(renderer->projection_location != (GLuint)-1);
    renderer->tex_location = glGetUniformLocation(renderer->standard_shader, "tex");
    assert(renderer->tex_location != (GLuint)-1);
    renderer->view_location = glGetUniformLocation(renderer->standard_shader, "view_matrix");
    assert(renderer->view_location != (GLuint)-1);
    const char *render_framebuffer_shader_code = R"FOO(#ifdef VERTEX_SHADER
layout(location = 0) in vec2 position;

out vec2 frag_uv;

void main() {
    gl_Position = vec4(position.x, position.y, 0.0, 1.0);
    frag_uv = (position + vec2(1)) * 0.5;
}
#else     

in vec2 frag_uv;
out vec4 out_color;

uniform sampler2D tex;

void main() {
    out_color = texture(tex, frag_uv);
}

#endif)FOO";
    renderer->render_framebuffer_shader = create_shader(render_framebuffer_shader_code);
    renderer->render_framebuffer_tex_location = glGetUniformLocation(renderer->render_framebuffer_shader, "tex");
    assert(renderer->render_framebuffer_tex_location != (GLuint)-1);
    
    const char *horizontal_gaussian_blur_shader_code = R"FOO(#ifdef VERTEX_SHADER
layout(location = 0) in vec2 position;

out vec2 blur_uvs[11];
uniform float target_width;

void main() {
    gl_Position = vec4(position.x, position.y, 0.0, 1.0);
    vec2 center_uv = (position + vec2(1)) * 0.5;
    float px_size = 1.0 / target_width;
    for (int i = -5; i <= 5; ++i) {
        blur_uvs[i + 5] = center_uv + vec2(px_size * i, 0);
    }
}

#else 

out vec4 out_color;
in vec2 blur_uvs[11];

uniform sampler2D tex;

void main() {
    out_color = vec4(0.01) * texture(tex, vec2(0));
    out_color.xy = blur_uvs[5];
    // out_color += texture(tex, blur_uvs[0]) * 0.0093;
    // out_color += texture(tex, blur_uvs[1]) * 0.028002;
    // out_color += texture(tex, blur_uvs[2]) * 0.065984;
    // out_color += texture(tex, blur_uvs[3]) * 0.121703;
    // out_color += texture(tex, blur_uvs[4]) * 0.175713;
    // out_color += texture(tex, blur_uvs[5]) * 0.198596;
    // out_color += texture(tex, blur_uvs[6]) * 0.175713;
    // out_color += texture(tex, blur_uvs[7]) * 0.121703;
    // out_color += texture(tex, blur_uvs[8]) * 0.065984;
    // out_color += texture(tex, blur_uvs[9]) * 0.028002;
    // out_color += texture(tex, blur_uvs[10]) * 0.0093;
}

#endif)FOO";
    renderer->horizontal_gaussian_blur_shader = create_shader(horizontal_gaussian_blur_shader_code);
    renderer->horizontal_gaussian_blur_tex_location = glGetUniformLocation(renderer->horizontal_gaussian_blur_shader, "tex");
    renderer->horizontal_gaussian_blur_target_width = glGetUniformLocation(renderer->horizontal_gaussian_blur_shader, "target_width");
    assert(renderer->horizontal_gaussian_blur_tex_location != (GLuint)-1);
    assert(renderer->horizontal_gaussian_blur_target_width != (GLuint)-1);
    
    const char *vertical_gaussian_blur_shader_code = R"FOO(#ifdef VERTEX_SHADER
layout(location = 0) in vec2 position;

out vec2 blur_uvs[11];
uniform float target_height;

void main() {
    gl_Position = vec4(position.x, position.y, 0.0, 1.0);
    vec2 center_uv = (position + vec2(1)) * 0.5;
    float px_size = 1.0 / target_height;
    for (int i = -5; i <= 5; ++i) {
        blur_uvs[i + 5] = center_uv + vec2(0, px_size * i);
    }
}

#else 

out vec4 out_color;
in vec2 blur_uvs[11];

uniform sampler2D tex;

void main() {
    out_color = vec4(0.01)* texture(tex, vec2(0));
    out_color.xy = blur_uvs[5];
    // out_color += texture(tex, blur_uvs[0]) * 0.0093;
    // out_color += texture(tex, blur_uvs[1]) * 0.028002;
    // out_color += texture(tex, blur_uvs[2]) * 0.065984;
    // out_color += texture(tex, blur_uvs[3]) * 0.121703;
    // out_color += texture(tex, blur_uvs[4]) * 0.175713;
    // out_color += texture(tex, blur_uvs[5]) * 0.198596;
    // out_color += texture(tex, blur_uvs[6]) * 0.175713;
    // out_color += texture(tex, blur_uvs[7]) * 0.121703;
    // out_color += texture(tex, blur_uvs[8]) * 0.065984;
    // out_color += texture(tex, blur_uvs[9]) * 0.028002;
    // out_color += texture(tex, blur_uvs[10]) * 0.0093;
}

#endif)FOO";
    renderer->vertical_gaussian_blur_shader = create_shader(vertical_gaussian_blur_shader_code);
    renderer->vertical_gaussian_blur_tex_location = glGetUniformLocation(renderer->vertical_gaussian_blur_shader, "tex");
    renderer->vertical_gaussian_blur_target_height = glGetUniformLocation(renderer->vertical_gaussian_blur_shader, "target_height");
    assert(renderer->vertical_gaussian_blur_tex_location != (GLuint)-1);
    assert(renderer->vertical_gaussian_blur_target_height != (GLuint)-1);
    
    // Generate vertex arrays
    glGenVertexArrays(1, &renderer->render_framebuffer_vao);
    glBindVertexArray(renderer->render_framebuffer_vao);
    GLuint renderer_framebuffer_vbo;
    f32 render_framebuffer_data[] = {
        -1.0f, -1.0f, 
        -1.0f, 1.0f, 
        1.0f, -1.0f, 
        1.0f, 1.0f, 
    };
    glGenBuffers(1, &renderer_framebuffer_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, renderer_framebuffer_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(render_framebuffer_data), render_framebuffer_data, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    
    glGenVertexArrays(1, &renderer->vertex_array);
    glBindVertexArray(renderer->vertex_array);

    glGenBuffers(1, &renderer->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, renderer->commands.max_vertex_count * sizeof(Vertex), 0, GL_STREAM_DRAW);
    renderer->video_memory_used += renderer->commands.max_vertex_count * sizeof(Vertex);

    glGenBuffers(1, &renderer->index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, renderer->commands.max_index_count * sizeof(RENDERER_INDEX_TYPE), 0, GL_STREAM_DRAW);
    renderer->video_memory_used += renderer->commands.max_index_count * sizeof(RENDERER_INDEX_TYPE);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)STRUCT_OFFSET(Vertex, p));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)STRUCT_OFFSET(Vertex, uv));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)STRUCT_OFFSET(Vertex, n));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)STRUCT_OFFSET(Vertex, c));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(4, 1, GL_UNSIGNED_SHORT, sizeof(Vertex), (void *)STRUCT_OFFSET(Vertex, tex));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);
     
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
        renderer->video_memory_used += iter.width * iter.height * 4;
    }
    init_renderer_for_settings(renderer, settings);
}

RendererCommands *renderer_begin_frame(Renderer *renderer) {
    RendererCommands *commands = &renderer->commands;
    commands->quads_count = 0;
    commands->vertex_count = 0;
    commands->index_count = 0;
    commands->last_quads = 0;
    commands->perform_blur = false;
    return commands;
}

void renderer_end_frame(Renderer *renderer) {
    TIMED_FUNCTION();
    // Upload data from vertex array to OpenGL buffers
    glBindVertexArray(renderer->vertex_array);
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->commands.vertex_count * sizeof(Vertex), renderer->commands.vertices);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, renderer->commands.index_count * sizeof(RENDERER_INDEX_TYPE), renderer->commands.indices);
    glBindVertexArray(0);
    
    // Prepare framebuffers
    for (size_t i = 0; i < ARRAY_SIZE(renderer->framebuffers); ++i) {
        GLuint id = renderer->framebuffers[i].id;
        glBindFramebuffer(GL_FRAMEBUFFER, id);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        u32 flags = GL_COLOR_BUFFER_BIT;
        if (renderer->framebuffers[i].has_depth) {
            flags |= GL_DEPTH_BUFFER_BIT;
        }
        glClear(flags);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // Do renderer commands
    u64 DEBUG_draw_call_count = 0;
    u64 DEBUG_quads_dispatched = 0;
    for (size_t i = 0; i < renderer->commands.quads_count; ++i) {
        RenderQuads *quads = renderer->commands.quads + i;
        RendererFramebuffer *framebuffer = renderer->framebuffers + quads->setup.framebuffer;
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer->id);
        glBindVertexArray(renderer->vertex_array);
        glUseProgram(renderer->standard_shader);
        glUniformMatrix4fv(renderer->view_location, 1, false, quads->setup.view.value_ptr());
        glUniformMatrix4fv(renderer->projection_location, 1, false, quads->setup.projection.value_ptr());
        glUniform1i(renderer->tex_location, 0); 
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, renderer->texture_array);
        glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)(6 * quads->quad_count), GL_INDEX_TYPE,
            (GLvoid *)(sizeof(RENDERER_INDEX_TYPE) * quads->index_array_offset),
            (GLint)quads->vertex_array_offset);
        glUseProgram(0);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        ++DEBUG_draw_call_count;
        DEBUG_quads_dispatched += quads->quad_count;       
    }
    
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    if (renderer->commands.perform_blur) {
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->framebuffers[RENDERER_FRAMEBUFFER_BLUR1].id);
        glUseProgram(renderer->horizontal_gaussian_blur_shader);
        glBindVertexArray(renderer->render_framebuffer_vao);
        glUniform1f(renderer->horizontal_gaussian_blur_target_width, renderer->framebuffers[RENDERER_FRAMEBUFFER_BLUR2].size.x);
        glBindTexture(GL_TEXTURE_2D, renderer->framebuffers[RENDERER_FRAMEBUFFER_GAME_WORLD].texture_id);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->framebuffers[RENDERER_FRAMEBUFFER_BLUR2].id);
        glUseProgram(renderer->vertical_gaussian_blur_shader);
        glBindVertexArray(renderer->render_framebuffer_vao);
        glUniform1f(renderer->vertical_gaussian_blur_target_height, renderer->framebuffers[RENDERER_FRAMEBUFFER_BLUR2].size.y);
        glBindTexture(GL_TEXTURE_2D, renderer->framebuffers[RENDERER_FRAMEBUFFER_BLUR1].texture_id);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->framebuffers[RENDERER_FRAMEBUFFER_GAME_WORLD].id);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(renderer->render_framebuffer_shader);
        glBindVertexArray(renderer->render_framebuffer_vao);
        glBindTexture(GL_TEXTURE_2D, renderer->framebuffers[RENDERER_FRAMEBUFFER_BLUR2].texture_id);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    // Render all framebuffers to default one
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, (GLsizei)renderer->settings.display_size.x, (GLsizei)renderer->settings.display_size.y);
    glClearColor(0.2, 0.2, 0.2, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(renderer->render_framebuffer_shader);
    glBindVertexArray(renderer->render_framebuffer_vao);
    glActiveTexture(GL_TEXTURE0);
    
    RendererFramebuffer *framebuffer = renderer->framebuffers + RENDERER_FRAMEBUFFER_GAME_WORLD;
    glBindTexture(GL_TEXTURE_2D, framebuffer->texture_id);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    framebuffer = renderer->framebuffers + RENDERER_FRAMEBUFFER_GAME_INTERFACE;
    glBindTexture(GL_TEXTURE_2D, framebuffer->texture_id);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    glBindVertexArray(0);
    glUseProgram(0);
    
    {DEBUG_VALUE_BLOCK("Renderer")
        DEBUG_VALUE(renderer->video_memory_used >> 20, "Video memory used");
        DEBUG_VALUE(DEBUG_draw_call_count, "Draw call count");
        DEBUG_VALUE(DEBUG_quads_dispatched, "Quads dispatched");
        DEBUG_VALUE(renderer->texture_count, "Texture count");
        DEBUG_VALUE((f32)renderer->commands.index_count / renderer->commands.max_index_count * 100, "Index buffer");
        DEBUG_VALUE((f32)renderer->commands.vertex_count / renderer->commands.max_vertex_count * 100, "Vertex buffer");
    }
}

Texture renderer_create_texture_mipmaps(Renderer *renderer, void *data, u32 width, u32 height) {
    assert(width <= RENDERER_TEXTURE_DIM && height <= RENDERER_TEXTURE_DIM);
    Texture tex;
    assert(renderer->texture_count + 1 < renderer->max_texture_count);
    tex.index = (u32)renderer->texture_count++;
    tex.width  = (u16)width;
    tex.height = (u16)height;
    assert(tex.width == width && tex.height == height);
    glBindTexture(GL_TEXTURE_2D_ARRAY, renderer->texture_array);
    for (MipIterator iter = iterate_mips(width, height);
         is_valid(&iter);
         advance(&iter)) { 
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, iter.level, 0, 0,
            tex.index, iter.width, iter.height, 1,
            GL_RGBA, GL_UNSIGNED_BYTE, (u32 *)data + iter.pixel_offset);    
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return tex;
}