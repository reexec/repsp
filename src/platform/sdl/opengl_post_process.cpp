// Adapted from re2DJ (BSD 3-Clause License; see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, re2DJ contributors

#include "opengl_post_process.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace repsp::platform::sdl_internal
{

#if defined(_WIN32) && !defined(_WIN64)
#define REPSP_POST_APIENTRY __stdcall
#else
#define REPSP_POST_APIENTRY
#endif

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLfloat = float;
using GLsizeiptr = std::ptrdiff_t;

namespace
{

constexpr GLboolean kGlFalse = 0;
constexpr GLboolean kGlTrue = 1;
constexpr GLenum kGlNoError = 0;
constexpr GLenum kGlFloat = 0x1406;
constexpr GLenum kGlTriangleStrip = 0x0005;
constexpr GLenum kGlTexture2d = 0x0DE1;
constexpr GLenum kGlRgba = 0x1908;
constexpr GLenum kGlUnsignedByte = 0x1401;
constexpr GLint kGlNearest = 0x2600;
constexpr GLint kGlLinear = 0x2601;
constexpr GLenum kGlTextureMagFilter = 0x2800;
constexpr GLenum kGlTextureMinFilter = 0x2801;
constexpr GLenum kGlTextureWrapS = 0x2802;
constexpr GLenum kGlTextureWrapT = 0x2803;
constexpr GLint kGlClampToEdge = 0x812F;
constexpr GLenum kGlArrayBuffer = 0x8892;
constexpr GLenum kGlStaticDraw = 0x88E4;
constexpr GLenum kGlVertexShader = 0x8B31;
constexpr GLenum kGlFragmentShader = 0x8B30;
constexpr GLenum kGlCompileStatus = 0x8B81;
constexpr GLenum kGlLinkStatus = 0x8B82;
constexpr GLenum kGlInfoLogLength = 0x8B84;
constexpr GLenum kGlFramebuffer = 0x8D40;
constexpr GLenum kGlColorAttachment0 = 0x8CE0;
constexpr GLenum kGlFramebufferComplete = 0x8CD5;
constexpr GLenum kGlTexture0 = 0x84C0;
constexpr GLint kGlRgba16f = 0x881A;
constexpr GLint kGlRgba8 = 0x8058;

constexpr GLint kTextureUnit = 0;
constexpr GLint kOrigUnit = 1;
constexpr GLint kFirstPreviousUnit = 2;

constexpr GLfloat kUnitSquareProjection[16] = {
    2.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 2.0f, 0.0f, 0.0f,
    0.0f, 0.0f, -1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
};

constexpr const char* kStockShader = R"glsl(
#if defined(VERTEX)
attribute vec4 VertexCoord;
attribute vec4 TexCoord;
uniform mat4 MVPMatrix;
varying vec2 tex_coord;
void main()
{
    gl_Position = MVPMatrix * VertexCoord;
    tex_coord = TexCoord.xy;
}
#elif defined(FRAGMENT)
uniform sampler2D Texture;
varying vec2 tex_coord;
void main()
{
    gl_FragColor = vec4(texture2D(Texture, tex_coord).rgb, 1.0);
}
#endif
)glsl";

template <typename Function>
bool ResolveOpenGlFunction(const char* name, Function* function)
{
    const SDL_FunctionPointer address = SDL_GL_GetProcAddress(name);
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    if (address == nullptr || value <= 3U || value == ~std::uintptr_t{0})
    {
        return false;
    }
    *function = reinterpret_cast<Function>(address);
    return true;
}

} // namespace

struct OpenGlPostProcess::Impl
{
    GLuint (REPSP_POST_APIENTRY *create_shader)(GLenum) = nullptr;
    void (REPSP_POST_APIENTRY *shader_source)(GLuint, GLsizei, const char* const*, const GLint*) = nullptr;
    void (REPSP_POST_APIENTRY *compile_shader)(GLuint) = nullptr;
    void (REPSP_POST_APIENTRY *get_shader_iv)(GLuint, GLenum, GLint*) = nullptr;
    void (REPSP_POST_APIENTRY *get_shader_info_log)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
    void (REPSP_POST_APIENTRY *delete_shader)(GLuint) = nullptr;
    GLuint (REPSP_POST_APIENTRY *create_program)() = nullptr;
    void (REPSP_POST_APIENTRY *attach_shader)(GLuint, GLuint) = nullptr;
    void (REPSP_POST_APIENTRY *bind_attrib_location)(GLuint, GLuint, const char*) = nullptr;
    void (REPSP_POST_APIENTRY *link_program)(GLuint) = nullptr;
    void (REPSP_POST_APIENTRY *get_program_iv)(GLuint, GLenum, GLint*) = nullptr;
    void (REPSP_POST_APIENTRY *get_program_info_log)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
    void (REPSP_POST_APIENTRY *delete_program)(GLuint) = nullptr;
    void (REPSP_POST_APIENTRY *use_program)(GLuint) = nullptr;
    GLint (REPSP_POST_APIENTRY *get_uniform_location)(GLuint, const char*) = nullptr;
    void (REPSP_POST_APIENTRY *uniform_1i)(GLint, GLint) = nullptr;
    void (REPSP_POST_APIENTRY *uniform_1f)(GLint, GLfloat) = nullptr;
    void (REPSP_POST_APIENTRY *uniform_2f)(GLint, GLfloat, GLfloat) = nullptr;
    void (REPSP_POST_APIENTRY *uniform_matrix_4fv)(GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;

    void (REPSP_POST_APIENTRY *gen_vertex_arrays)(GLsizei, GLuint*) = nullptr;
    void (REPSP_POST_APIENTRY *delete_vertex_arrays)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_POST_APIENTRY *bind_vertex_array)(GLuint) = nullptr;
    void (REPSP_POST_APIENTRY *gen_buffers)(GLsizei, GLuint*) = nullptr;
    void (REPSP_POST_APIENTRY *delete_buffers)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_POST_APIENTRY *bind_buffer)(GLenum, GLuint) = nullptr;
    void (REPSP_POST_APIENTRY *buffer_data)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (REPSP_POST_APIENTRY *enable_vertex_attrib_array)(GLuint) = nullptr;
    void (REPSP_POST_APIENTRY *vertex_attrib_pointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;

    void (REPSP_POST_APIENTRY *gen_framebuffers)(GLsizei, GLuint*) = nullptr;
    void (REPSP_POST_APIENTRY *delete_framebuffers)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_POST_APIENTRY *bind_framebuffer)(GLenum, GLuint) = nullptr;
    void (REPSP_POST_APIENTRY *framebuffer_texture_2d)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    GLenum (REPSP_POST_APIENTRY *check_framebuffer_status)(GLenum) = nullptr;
    void (REPSP_POST_APIENTRY *gen_textures)(GLsizei, GLuint*) = nullptr;
    void (REPSP_POST_APIENTRY *delete_textures)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_POST_APIENTRY *bind_texture)(GLenum, GLuint) = nullptr;
    void (REPSP_POST_APIENTRY *tex_image_2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (REPSP_POST_APIENTRY *tex_parameter_i)(GLenum, GLenum, GLint) = nullptr;
    void (REPSP_POST_APIENTRY *active_texture)(GLenum) = nullptr;
    void (REPSP_POST_APIENTRY *viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (REPSP_POST_APIENTRY *draw_arrays)(GLenum, GLint, GLsizei) = nullptr;
    GLenum (REPSP_POST_APIENTRY *get_error)() = nullptr;

    bool multipass = false;
    bool is_es = false;

    struct Pass
    {
        PostShaderPass settings;
        GLuint program = 0;
        GLint mvp_matrix = -1;
        GLint texture = -1;
        GLint input_size = -1;
        GLint texture_size = -1;
        GLint logical_size = -1;
        GLint output_size = -1;
        GLint frame_count = -1;
        GLint frame_direction = -1;
        GLint orig_texture = -1;
        GLint orig_texture_size = -1;
        GLint orig_input_size = -1;
        std::vector<std::array<GLint, 3>> previous;
        std::vector<GLint> parameter_locations;
        GLuint framebuffer = 0;
        GLuint color = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    bool initialized = false;
    GLuint quad_vao = 0;
    GLuint quad_vbo = 0;
    bool last_quad_flip_y = false;
    std::filesystem::path directory;
    std::string directory_text;
    std::vector<PostShaderEntry> catalog;
    std::string active_id = kPostShaderNoneId;
    std::string last_error;
    std::vector<PostShaderParameter> parameters;
    std::vector<Pass> passes;
    Pass stock;
    // A chain's copy of a top-down (flip_y) input, drawn upright, so that
    // every pass, OrigTexture and PassPrevN included, reads one orientation.
    Pass upright;
    std::uint32_t frames = 0;
};

namespace
{

using Pass = OpenGlPostProcess::Impl::Pass;

bool ResolveFunctions(OpenGlPostProcess::Impl* gl)
{
    return ResolveOpenGlFunction("glCreateShader", &gl->create_shader) &&
           ResolveOpenGlFunction("glShaderSource", &gl->shader_source) &&
           ResolveOpenGlFunction("glCompileShader", &gl->compile_shader) &&
           ResolveOpenGlFunction("glGetShaderiv", &gl->get_shader_iv) &&
           ResolveOpenGlFunction("glGetShaderInfoLog", &gl->get_shader_info_log) &&
           ResolveOpenGlFunction("glDeleteShader", &gl->delete_shader) &&
           ResolveOpenGlFunction("glCreateProgram", &gl->create_program) &&
           ResolveOpenGlFunction("glAttachShader", &gl->attach_shader) &&
           ResolveOpenGlFunction("glBindAttribLocation", &gl->bind_attrib_location) &&
           ResolveOpenGlFunction("glLinkProgram", &gl->link_program) &&
           ResolveOpenGlFunction("glGetProgramiv", &gl->get_program_iv) &&
           ResolveOpenGlFunction("glGetProgramInfoLog", &gl->get_program_info_log) &&
           ResolveOpenGlFunction("glDeleteProgram", &gl->delete_program) &&
           ResolveOpenGlFunction("glUseProgram", &gl->use_program) &&
           ResolveOpenGlFunction("glGetUniformLocation", &gl->get_uniform_location) &&
           ResolveOpenGlFunction("glUniform1i", &gl->uniform_1i) &&
           ResolveOpenGlFunction("glUniform1f", &gl->uniform_1f) &&
           ResolveOpenGlFunction("glUniform2f", &gl->uniform_2f) &&
           ResolveOpenGlFunction("glUniformMatrix4fv", &gl->uniform_matrix_4fv) &&
           ResolveOpenGlFunction("glGenVertexArrays", &gl->gen_vertex_arrays) &&
           ResolveOpenGlFunction("glDeleteVertexArrays", &gl->delete_vertex_arrays) &&
           ResolveOpenGlFunction("glBindVertexArray", &gl->bind_vertex_array) &&
           ResolveOpenGlFunction("glGenBuffers", &gl->gen_buffers) &&
           ResolveOpenGlFunction("glDeleteBuffers", &gl->delete_buffers) &&
           ResolveOpenGlFunction("glBindBuffer", &gl->bind_buffer) &&
           ResolveOpenGlFunction("glBufferData", &gl->buffer_data) &&
           ResolveOpenGlFunction("glEnableVertexAttribArray", &gl->enable_vertex_attrib_array) &&
           ResolveOpenGlFunction("glVertexAttribPointer", &gl->vertex_attrib_pointer) &&
           ResolveOpenGlFunction("glDrawArrays", &gl->draw_arrays);
}

bool ResolveMultipassFunctions(OpenGlPostProcess::Impl* gl)
{
    return ResolveOpenGlFunction("glGenFramebuffers", &gl->gen_framebuffers) &&
           ResolveOpenGlFunction("glDeleteFramebuffers", &gl->delete_framebuffers) &&
           ResolveOpenGlFunction("glBindFramebuffer", &gl->bind_framebuffer) &&
           ResolveOpenGlFunction("glFramebufferTexture2D", &gl->framebuffer_texture_2d) &&
           ResolveOpenGlFunction("glCheckFramebufferStatus", &gl->check_framebuffer_status) &&
           ResolveOpenGlFunction("glGenTextures", &gl->gen_textures) &&
           ResolveOpenGlFunction("glDeleteTextures", &gl->delete_textures) &&
           ResolveOpenGlFunction("glBindTexture", &gl->bind_texture) &&
           ResolveOpenGlFunction("glTexImage2D", &gl->tex_image_2d) &&
           ResolveOpenGlFunction("glTexParameteri", &gl->tex_parameter_i) &&
           ResolveOpenGlFunction("glActiveTexture", &gl->active_texture) &&
           ResolveOpenGlFunction("glViewport", &gl->viewport) &&
           ResolveOpenGlFunction("glGetError", &gl->get_error);
}

std::string TrimLog(std::string log)
{
    while (!log.empty() && (log.back() == '\0' || log.back() == '\n' || log.back() == '\r'))
    {
        log.pop_back();
    }
    return log;
}

std::string ShaderLog(OpenGlPostProcess::Impl* gl, GLuint shader)
{
    GLint length = 0;
    gl->get_shader_iv(shader, kGlInfoLogLength, &length);
    std::string log(length > 0 ? static_cast<std::size_t>(length) : 0U, '\0');
    if (length > 0)
    {
        gl->get_shader_info_log(shader, length, nullptr, log.data());
    }
    return TrimLog(std::move(log));
}

std::string ProgramLog(OpenGlPostProcess::Impl* gl, GLuint program)
{
    GLint length = 0;
    gl->get_program_iv(program, kGlInfoLogLength, &length);
    std::string log(length > 0 ? static_cast<std::size_t>(length) : 0U, '\0');
    if (length > 0)
    {
        gl->get_program_info_log(program, length, nullptr, log.data());
    }
    return TrimLog(std::move(log));
}

GLuint CompileStage(OpenGlPostProcess::Impl* gl, GLenum type, const std::string& source, std::string* error)
{
    const GLuint shader = gl->create_shader(type);
    if (shader == 0)
    {
        *error = "cannot create an OpenGL shader";
        return 0;
    }
    const char* text = source.c_str();
    gl->shader_source(shader, 1, &text, nullptr);
    gl->compile_shader(shader);
    GLint compiled = kGlFalse;
    gl->get_shader_iv(shader, kGlCompileStatus, &compiled);
    if (compiled != kGlTrue)
    {
        *error = std::string(type == kGlVertexShader ? "vertex" : "fragment") +
                 " stage failed to compile: " + ShaderLog(gl, shader);
        gl->delete_shader(shader);
        return 0;
    }
    return shader;
}

GLuint LinkProgram(OpenGlPostProcess::Impl* gl, GLuint vertex, GLuint fragment, std::string* error)
{
    const GLuint program = gl->create_program();
    if (program == 0)
    {
        *error = "cannot create an OpenGL program";
        return 0;
    }
    gl->attach_shader(program, vertex);
    gl->attach_shader(program, fragment);
    gl->bind_attrib_location(program, OpenGlPostProcess::kVertexCoordSlot, "VertexCoord");
    gl->bind_attrib_location(program, OpenGlPostProcess::kColorSlot, "COLOR");
    gl->bind_attrib_location(program, OpenGlPostProcess::kTexCoordSlot, "TexCoord");
    gl->link_program(program);
    GLint linked = kGlFalse;
    gl->get_program_iv(program, kGlLinkStatus, &linked);
    if (linked != kGlTrue)
    {
        *error = "link failed: " + ProgramLog(gl, program);
        gl->delete_program(program);
        return 0;
    }
    return program;
}

bool BuildPass(OpenGlPostProcess::Impl* gl,
               std::size_t index,
               Pass* pass,
               std::vector<PostShaderParameter>* parameters,
               std::string* error)
{
    PostShaderProgramSource source;
    if (!BuildPostShaderProgramSource(pass->settings.text, &source, error, gl->is_es))
    {
        *error = pass->settings.path + ": " + *error;
        return false;
    }
    const GLuint vertex = CompileStage(gl, kGlVertexShader, source.vertex, error);
    const GLuint fragment = vertex == 0 ? 0 : CompileStage(gl, kGlFragmentShader, source.fragment, error);
    if (vertex != 0 && fragment != 0)
    {
        pass->program = LinkProgram(gl, vertex, fragment, error);
    }
    if (vertex != 0)
    {
        gl->delete_shader(vertex);
    }
    if (fragment != 0)
    {
        gl->delete_shader(fragment);
    }
    if (pass->program == 0)
    {
        *error = pass->settings.path + ": " + *error;
        return false;
    }
    const auto uniform = [gl, pass](const std::string& name)
    {
        return gl->get_uniform_location(pass->program, name.c_str());
    };
    pass->mvp_matrix = uniform("MVPMatrix");
    pass->texture = uniform("Texture");
    pass->input_size = uniform("InputSize");
    pass->texture_size = uniform("TextureSize");
    pass->logical_size = uniform("LogicalSize");
    pass->output_size = uniform("OutputSize");
    pass->frame_count = uniform("FrameCount");
    pass->frame_direction = uniform("FrameDirection");
    pass->orig_texture = uniform("OrigTexture");
    pass->orig_texture_size = uniform("OrigTextureSize");
    pass->orig_input_size = uniform("OrigInputSize");
    for (std::size_t back = 1; back <= index + 1; ++back)
    {
        const std::string prefix = "PassPrev" + std::to_string(back);
        pass->previous.push_back(
            {uniform(prefix + "Texture"), uniform(prefix + "TextureSize"), uniform(prefix + "InputSize")});
    }
    for (PostShaderParameter& parameter : source.parameters)
    {
        bool known = false;
        for (const PostShaderParameter& existing : *parameters)
        {
            known = known || existing.name == parameter.name;
        }
        if (!known)
        {
            parameters->push_back(std::move(parameter));
        }
    }
    return true;
}

void ReleasePass(OpenGlPostProcess::Impl* gl, Pass* pass)
{
    if (pass->program != 0 && gl->delete_program != nullptr)
    {
        gl->delete_program(pass->program);
    }
    if (pass->framebuffer != 0 && gl->delete_framebuffers != nullptr)
    {
        gl->delete_framebuffers(1, &pass->framebuffer);
    }
    if (pass->color != 0 && gl->delete_textures != nullptr)
    {
        gl->delete_textures(1, &pass->color);
    }
    *pass = Pass{};
}

void ReleaseProgram(OpenGlPostProcess::Impl* gl)
{
    for (Pass& pass : gl->passes)
    {
        ReleasePass(gl, &pass);
    }
    gl->passes.clear();
    ReleasePass(gl, &gl->stock);
    ReleasePass(gl, &gl->upright);
    gl->parameters.clear();
    gl->active_id = kPostShaderNoneId;
}

void SetFilter(OpenGlPostProcess::Impl* gl, GLuint texture, bool linear)
{
    const GLint filter = linear ? kGlLinear : kGlNearest;
    gl->bind_texture(kGlTexture2d, texture);
    gl->tex_parameter_i(kGlTexture2d, kGlTextureMinFilter, filter);
    gl->tex_parameter_i(kGlTexture2d, kGlTextureMagFilter, filter);
}

bool EnsureTarget(OpenGlPostProcess::Impl* gl, Pass* pass, std::uint32_t width, std::uint32_t height, std::string* error)
{
    if (pass->framebuffer != 0 && pass->width == width && pass->height == height)
    {
        return true;
    }
    if (pass->color == 0)
    {
        gl->gen_textures(1, &pass->color);
    }
    if (pass->framebuffer == 0)
    {
        gl->gen_framebuffers(1, &pass->framebuffer);
    }
    gl->bind_texture(kGlTexture2d, pass->color);
    gl->tex_parameter_i(kGlTexture2d, kGlTextureWrapS, kGlClampToEdge);
    gl->tex_parameter_i(kGlTexture2d, kGlTextureWrapT, kGlClampToEdge);
    gl->tex_parameter_i(kGlTexture2d, kGlTextureMinFilter, kGlNearest);
    gl->tex_parameter_i(kGlTexture2d, kGlTextureMagFilter, kGlNearest);
    gl->tex_image_2d(kGlTexture2d, 0, pass->settings.float_framebuffer ? kGlRgba16f : kGlRgba8,
                     static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, kGlRgba,
                     pass->settings.float_framebuffer ? kGlFloat : kGlUnsignedByte, nullptr);
    gl->bind_framebuffer(kGlFramebuffer, pass->framebuffer);
    gl->framebuffer_texture_2d(kGlFramebuffer, kGlColorAttachment0, kGlTexture2d, pass->color, 0);
    if (gl->check_framebuffer_status(kGlFramebuffer) != kGlFramebufferComplete)
    {
        *error = pass->settings.path + ": cannot draw into a " + std::to_string(width) + "x" + std::to_string(height) +
                 (pass->settings.float_framebuffer ? " floating-point" : "") + " target";
        return false;
    }
    pass->width = width;
    pass->height = height;
    return true;
}

void SetUniforms(OpenGlPostProcess::Impl* gl,
                 const Pass& pass,
                 std::uint32_t input_width,
                 std::uint32_t input_height,
                 std::uint32_t logical_width,
                 std::uint32_t logical_height,
                 std::uint32_t output_width,
                 std::uint32_t output_height,
                 std::uint32_t orig_width,
                 std::uint32_t orig_height,
                 const std::vector<std::array<std::uint32_t, 2>>& previous_sizes)
{
    gl->use_program(pass.program);
    if (pass.mvp_matrix >= 0)
    {
        gl->uniform_matrix_4fv(pass.mvp_matrix, 1, kGlFalse, kUnitSquareProjection);
    }
    const auto size = [gl](GLint location, std::uint32_t width, std::uint32_t height)
    {
        if (location >= 0)
        {
            gl->uniform_2f(location, static_cast<GLfloat>(width), static_cast<GLfloat>(height));
        }
    };
    if (pass.texture >= 0)
    {
        gl->uniform_1i(pass.texture, kTextureUnit);
    }
    size(pass.input_size, input_width, input_height);
    size(pass.texture_size, input_width, input_height);
    size(pass.logical_size, logical_width, logical_height);
    size(pass.output_size, output_width, output_height);
    if (pass.orig_texture >= 0)
    {
        gl->uniform_1i(pass.orig_texture, kOrigUnit);
    }
    size(pass.orig_texture_size, orig_width, orig_height);
    size(pass.orig_input_size, orig_width, orig_height);
    for (std::size_t back = 0; back < pass.previous.size() && back < previous_sizes.size(); ++back)
    {
        if (pass.previous[back][0] >= 0)
        {
            gl->uniform_1i(pass.previous[back][0], kFirstPreviousUnit + static_cast<GLint>(back));
        }
        size(pass.previous[back][1], previous_sizes[back][0], previous_sizes[back][1]);
        size(pass.previous[back][2], previous_sizes[back][0], previous_sizes[back][1]);
    }
    if (pass.frame_count >= 0)
    {
        gl->uniform_1i(pass.frame_count, static_cast<GLint>(gl->frames));
    }
    if (pass.frame_direction >= 0)
    {
        gl->uniform_1i(pass.frame_direction, 1);
    }
    for (std::size_t index = 0; index < pass.parameter_locations.size(); ++index)
    {
        if (pass.parameter_locations[index] >= 0)
        {
            gl->uniform_1f(pass.parameter_locations[index], gl->parameters[index].value);
        }
    }
}

void DrawUnitSquareVao(OpenGlPostProcess::Impl* gl, bool flip_y)
{
    gl->bind_vertex_array(gl->quad_vao);
    gl->draw_arrays(kGlTriangleStrip, flip_y ? 4 : 0, 4);
    gl->bind_vertex_array(0);
}

void InitQuadBuffers(OpenGlPostProcess::Impl* gl)
{
    // Vertices 0..3: standard bottom-up GL texture coordinates.
    // Vertices 4..7: flip_y for top-down CPU RGBA8 uploads.
    static constexpr GLfloat kVertices[8][10] = {
        {0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
    };
    constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(kVertices[0]));
    gl->gen_vertex_arrays(1, &gl->quad_vao);
    gl->gen_buffers(1, &gl->quad_vbo);
    gl->bind_vertex_array(gl->quad_vao);
    gl->bind_buffer(kGlArrayBuffer, gl->quad_vbo);
    gl->buffer_data(kGlArrayBuffer, static_cast<GLsizeiptr>(sizeof(kVertices)), kVertices, kGlStaticDraw);
    gl->enable_vertex_attrib_array(OpenGlPostProcess::kVertexCoordSlot);
    gl->vertex_attrib_pointer(OpenGlPostProcess::kVertexCoordSlot, 4, kGlFloat, kGlFalse, kStride,
                              reinterpret_cast<const void*>(0));
    gl->enable_vertex_attrib_array(OpenGlPostProcess::kColorSlot);
    gl->vertex_attrib_pointer(OpenGlPostProcess::kColorSlot, 4, kGlFloat, kGlFalse, kStride,
                              reinterpret_cast<const void*>(4 * sizeof(GLfloat)));
    gl->enable_vertex_attrib_array(OpenGlPostProcess::kTexCoordSlot);
    gl->vertex_attrib_pointer(OpenGlPostProcess::kTexCoordSlot, 2, kGlFloat, kGlFalse, kStride,
                              reinterpret_cast<const void*>(8 * sizeof(GLfloat)));
    gl->bind_vertex_array(0);
    gl->bind_buffer(kGlArrayBuffer, 0);
}

std::filesystem::path ExecutableDirectory()
{
    const char* base = SDL_GetBasePath();
    return base != nullptr ? std::filesystem::path(base) : std::filesystem::path();
}

} // namespace

OpenGlPostProcess::OpenGlPostProcess() : impl_(std::make_unique<Impl>()) {}

OpenGlPostProcess::~OpenGlPostProcess() = default;

bool OpenGlPostProcess::Initialize(const std::filesystem::path& shader_dir, bool is_es, std::string* message)
{
    Impl* gl = impl_.get();
    if (gl->initialized)
    {
        return true;
    }
    if (!ResolveFunctions(gl))
    {
        if (message != nullptr)
        {
            *message = "OpenGL shader entry points are unavailable";
        }
        return false;
    }
    gl->is_es = is_es;
    gl->multipass = ResolveMultipassFunctions(gl);
    InitQuadBuffers(gl);
    gl->directory = shader_dir.empty() ? ResolvePostShaderDirectory(ExecutableDirectory()) : shader_dir;
    gl->directory_text = gl->directory.string();
    gl->catalog = ListPostShaders(gl->directory);
    gl->initialized = true;
    return true;
}

void OpenGlPostProcess::Shutdown()
{
    Impl* gl = impl_.get();
    if (!gl->initialized)
    {
        return;
    }
    ReleaseProgram(gl);
    if (gl->quad_vbo != 0 && gl->delete_buffers != nullptr)
    {
        gl->delete_buffers(1, &gl->quad_vbo);
        gl->quad_vbo = 0;
    }
    if (gl->quad_vao != 0 && gl->delete_vertex_arrays != nullptr)
    {
        gl->delete_vertex_arrays(1, &gl->quad_vao);
        gl->quad_vao = 0;
    }
    gl->initialized = false;
}

bool OpenGlPostProcess::active() const
{
    return !impl_->passes.empty();
}

const std::vector<PostShaderEntry>& OpenGlPostProcess::catalog() const
{
    return impl_->catalog;
}

const std::string& OpenGlPostProcess::shader_directory() const
{
    return impl_->directory_text;
}

const std::string& OpenGlPostProcess::active_id() const
{
    return impl_->active_id;
}

const std::string& OpenGlPostProcess::last_error() const
{
    return impl_->last_error;
}

std::vector<PostShaderParameter>& OpenGlPostProcess::parameters()
{
    return impl_->parameters;
}

std::size_t OpenGlPostProcess::pass_count() const
{
    return impl_->passes.size();
}

bool OpenGlPostProcess::Select(std::string_view id)
{
    Impl* gl = impl_.get();
    gl->last_error.clear();
    if (!gl->initialized)
    {
        gl->last_error = "post-processing is not initialized";
        return false;
    }
    ReleaseProgram(gl);
    if (id.empty() || id == kPostShaderNoneId)
    {
        return true;
    }

    const PostShaderEntry* entry = FindPostShader(gl->catalog, id);
    PostShaderPreset preset;
    if (entry == nullptr)
    {
        gl->last_error = "no shader named '" + std::string(id) + "' in " + gl->directory_text + " or built in";
        return false;
    }
    if (!LoadPostShaderPreset(*entry, &preset, &gl->last_error))
    {
        return false;
    }
    const bool chained = preset.passes.size() > 1 || !preset.passes.front().to_window;
    if (chained && !gl->multipass)
    {
        gl->last_error = "this OpenGL has no framebuffer objects for a multi-pass shader";
        return false;
    }
    std::vector<Pass> passes(preset.passes.size());
    std::vector<PostShaderParameter> parameters;
    bool built = true;
    for (std::size_t index = 0; built && index < passes.size(); ++index)
    {
        passes[index].settings = std::move(preset.passes[index]);
        built = BuildPass(gl, index, &passes[index], &parameters, &gl->last_error);
    }
    if (built && !passes.back().settings.to_window)
    {
        gl->stock.settings = SinglePassPreset("stock", kStockShader).passes.front();
        built = BuildPass(gl, passes.size(), &gl->stock, &parameters, &gl->last_error);
    }
    if (built && chained)
    {
        gl->upright.settings = SinglePassPreset("upright", kStockShader).passes.front();
        gl->upright.settings.to_window = false;
        gl->upright.settings.float_framebuffer = false;
        built = BuildPass(gl, passes.size() + 1, &gl->upright, &parameters, &gl->last_error);
    }
    gl->passes = std::move(passes);
    if (!built)
    {
        const std::string error = gl->last_error;
        ReleaseProgram(gl);
        gl->last_error = error;
        return false;
    }
    gl->parameters = std::move(parameters);
    const auto locate = [gl](Pass* pass)
    {
        if (pass->program == 0)
        {
            return;
        }
        for (const PostShaderParameter& parameter : gl->parameters)
        {
            pass->parameter_locations.push_back(gl->get_uniform_location(pass->program, parameter.name.c_str()));
        }
    };
    for (Pass& pass : gl->passes)
    {
        locate(&pass);
    }
    locate(&gl->stock);
    gl->active_id = std::string(id);
    gl->frames = 0;
    return true;
}

bool OpenGlPostProcess::Reload()
{
    Impl* gl = impl_.get();
    if (!gl->initialized)
    {
        return false;
    }
    const std::string id = gl->active_id;
    gl->catalog = ListPostShaders(gl->directory);
    return Select(id);
}

bool OpenGlPostProcess::UseForPresent(std::uint32_t source_texture,
                                      std::uint32_t input_width,
                                      std::uint32_t input_height,
                                      std::uint32_t logical_width,
                                      std::uint32_t logical_height,
                                      std::int32_t viewport_x,
                                      std::int32_t viewport_y,
                                      std::uint32_t output_width,
                                      std::uint32_t output_height,
                                      bool flip_y)
{
    Impl* gl = impl_.get();
    if (gl->passes.empty())
    {
        return false;
    }
    if (gl->passes.size() == 1 && gl->passes.front().settings.to_window)
    {
        const Pass& pass = gl->passes.front();
        if (pass.settings.filter_specified && gl->multipass)
        {
            SetFilter(gl, source_texture, pass.settings.filter_linear);
        }
        SetUniforms(gl, pass, input_width, input_height, logical_width, logical_height,
                    output_width, output_height, input_width, input_height, {{input_width, input_height}});
        if (pass.orig_texture >= 0)
        {
            gl->uniform_1i(pass.orig_texture, kTextureUnit);
        }
        if (!pass.previous.empty() && pass.previous.front()[0] >= 0)
        {
            gl->uniform_1i(pass.previous.front()[0], kTextureUnit);
        }
        gl->last_quad_flip_y = flip_y;
        ++gl->frames;
        return true;
    }

    std::string error;
    const auto fail = [&]()
    {
        gl->bind_framebuffer(kGlFramebuffer, 0);
        gl->active_texture(kGlTexture0);
        gl->bind_texture(kGlTexture2d, source_texture);
        gl->viewport(viewport_x, viewport_y, static_cast<GLsizei>(output_width), static_cast<GLsizei>(output_height));
        ReleaseProgram(gl);
        gl->use_program(0);
        while (gl->get_error() != kGlNoError)
        {
        }
        gl->last_error = error;
        return false;
    };

    // A top-down input is drawn upright first: later passes read the
    // original (OrigTexture, PassPrevN) with their own texture coordinates,
    // which a flip in pass 0's quad alone would leave upside down.
    GLuint source = static_cast<GLuint>(source_texture);
    if (flip_y && gl->upright.program != 0)
    {
        if (!EnsureTarget(gl, &gl->upright, input_width, input_height, &error))
        {
            return fail();
        }
        gl->active_texture(kGlTexture0);
        SetFilter(gl, source, false);
        SetUniforms(gl, gl->upright, input_width, input_height, logical_width, logical_height, input_width,
                    input_height, input_width, input_height, {{input_width, input_height}});
        gl->bind_framebuffer(kGlFramebuffer, gl->upright.framebuffer);
        gl->viewport(0, 0, static_cast<GLsizei>(input_width), static_cast<GLsizei>(input_height));
        DrawUnitSquareVao(gl, true);
        source = gl->upright.color;
        flip_y = false;
    }

    std::vector<GLuint> outputs = {source};
    std::vector<std::array<std::uint32_t, 2>> sizes = {{input_width, input_height}};
    gl->active_texture(kGlTexture0 + kOrigUnit);
    SetFilter(gl, outputs.front(), gl->passes.front().settings.filter_linear);
    const std::size_t count = gl->passes.size();
    for (std::size_t index = 0; index <= count; ++index)
    {
        const bool stock = index == count;
        if (stock && gl->stock.program == 0)
        {
            break;
        }
        Pass& pass = stock ? gl->stock : gl->passes[index];
        const std::uint32_t pass_input_width = sizes.back()[0];
        const std::uint32_t pass_input_height = sizes.back()[1];
        std::uint32_t width = output_width;
        std::uint32_t height = output_height;
        const bool to_window = stock || pass.settings.to_window;
        if (!to_window)
        {
            PostShaderPassSize(pass.settings, pass_input_width, pass_input_height, output_width, output_height, &width,
                               &height);
            if (!EnsureTarget(gl, &pass, width, height, &error))
            {
                return fail();
            }
        }
        for (std::size_t back = 1; back <= outputs.size() && back <= pass.previous.size(); ++back)
        {
            if (pass.previous[back - 1][0] >= 0)
            {
                gl->active_texture(kGlTexture0 + kFirstPreviousUnit + static_cast<GLenum>(back - 1));
                gl->bind_texture(kGlTexture2d, outputs[outputs.size() - back]);
            }
        }
        gl->active_texture(kGlTexture0 + kOrigUnit);
        gl->bind_texture(kGlTexture2d, outputs.front());
        gl->active_texture(kGlTexture0);
        SetFilter(gl, outputs.back(), stock || pass.settings.filter_linear);

        std::vector<std::array<std::uint32_t, 2>> previous_sizes;
        for (std::size_t back = 1; back <= sizes.size(); ++back)
        {
            previous_sizes.push_back(sizes[sizes.size() - back]);
        }
        SetUniforms(gl, pass, pass_input_width, pass_input_height, logical_width, logical_height,
                    width, height, input_width, input_height, previous_sizes);
        if (to_window)
        {
            gl->bind_framebuffer(kGlFramebuffer, 0);
            gl->viewport(viewport_x, viewport_y, static_cast<GLsizei>(output_width),
                         static_cast<GLsizei>(output_height));
            gl->last_quad_flip_y = (index == 0) && flip_y;
            ++gl->frames;
            return true;
        }
        gl->bind_framebuffer(kGlFramebuffer, pass.framebuffer);
        gl->viewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        DrawUnitSquareVao(gl, (index == 0) && flip_y);
        outputs.push_back(pass.color);
        sizes.push_back({width, height});
    }
    return false;
}

void OpenGlPostProcess::DrawLastQuad()
{
    Impl* gl = impl_.get();
    DrawUnitSquareVao(gl, gl->last_quad_flip_y);
    gl->active_texture(kGlTexture0);
    gl->bind_texture(kGlTexture2d, 0);
    gl->use_program(0);
}

} // namespace repsp::platform::sdl_internal
