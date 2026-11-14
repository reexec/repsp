#pragma once

#include "repsp/platform/display.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace repsp::gpu
{

#if defined(_WIN32) && !defined(_WIN64)
#define REPSP_GL_APIENTRY __stdcall
#else
#define REPSP_GL_APIENTRY
#endif

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLchar = char;
using GLfloat = float;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;
using GLubyte = unsigned char;

inline constexpr GLboolean kGlFalse = 0;
inline constexpr GLboolean kGlTrue = 1;

inline constexpr GLenum kGlPoints = 0x0000;
inline constexpr GLenum kGlLines = 0x0001;
inline constexpr GLenum kGlTriangles = 0x0004;

inline constexpr GLenum kGlNever = 0x0200;
inline constexpr GLenum kGlLess = 0x0201;
inline constexpr GLenum kGlEqual = 0x0202;
inline constexpr GLenum kGlLequal = 0x0203;
inline constexpr GLenum kGlGreater = 0x0204;
inline constexpr GLenum kGlNotequal = 0x0205;
inline constexpr GLenum kGlGequal = 0x0206;
inline constexpr GLenum kGlAlways = 0x0207;

inline constexpr GLenum kGlZero = 0;
inline constexpr GLenum kGlOne = 1;
inline constexpr GLenum kGlSrcColor = 0x0300;
inline constexpr GLenum kGlOneMinusSrcColor = 0x0301;
inline constexpr GLenum kGlSrcAlpha = 0x0302;
inline constexpr GLenum kGlOneMinusSrcAlpha = 0x0303;
inline constexpr GLenum kGlDstAlpha = 0x0304;
inline constexpr GLenum kGlOneMinusDstAlpha = 0x0305;
inline constexpr GLenum kGlDstColor = 0x0306;
inline constexpr GLenum kGlOneMinusDstColor = 0x0307;
inline constexpr GLenum kGlConstantColor = 0x8001;
inline constexpr GLenum kGlOneMinusConstantColor = 0x8002;

inline constexpr GLenum kGlFuncAdd = 0x8006;
inline constexpr GLenum kGlMin = 0x8007;
inline constexpr GLenum kGlMax = 0x8008;
inline constexpr GLenum kGlFuncSubtract = 0x800A;
inline constexpr GLenum kGlFuncReverseSubtract = 0x800B;

inline constexpr GLenum kGlCullFace = 0x0B44;
inline constexpr GLenum kGlDepthTest = 0x0B71;
inline constexpr GLenum kGlStencilTest = 0x0B90;
inline constexpr GLenum kGlBlend = 0x0BE2;
inline constexpr GLenum kGlScissorTest = 0x0C11;

inline constexpr GLenum kGlPackAlignment = 0x0D05;
inline constexpr GLenum kGlUnpackAlignment = 0x0CF5;

inline constexpr GLenum kGlTexture2d = 0x0DE1;
inline constexpr GLenum kGlUnsignedByte = 0x1401;
inline constexpr GLenum kGlFloat = 0x1406;
inline constexpr GLenum kGlRgba = 0x1908;
inline constexpr GLenum kGlRgba8 = 0x8058;
inline constexpr GLenum kGlDepth24Stencil8 = 0x88F0;

inline constexpr GLenum kGlNearest = 0x2600;
inline constexpr GLenum kGlLinear = 0x2601;
inline constexpr GLenum kGlTextureMagFilter = 0x2800;
inline constexpr GLenum kGlTextureMinFilter = 0x2801;
inline constexpr GLenum kGlTextureWrapS = 0x2802;
inline constexpr GLenum kGlTextureWrapT = 0x2803;
inline constexpr GLenum kGlRepeat = 0x2901;
inline constexpr GLenum kGlClampToEdge = 0x812F;

inline constexpr GLenum kGlTexture0 = 0x84C0;
inline constexpr GLenum kGlArrayBuffer = 0x8892;
inline constexpr GLenum kGlStreamDraw = 0x88E0;

inline constexpr GLenum kGlFragmentShader = 0x8B30;
inline constexpr GLenum kGlVertexShader = 0x8B31;
inline constexpr GLenum kGlCompileStatus = 0x8B81;
inline constexpr GLenum kGlLinkStatus = 0x8B82;

inline constexpr GLenum kGlFramebuffer = 0x8D40;
inline constexpr GLenum kGlRenderbuffer = 0x8D41;
inline constexpr GLenum kGlReadFramebuffer = 0x8CA8;
inline constexpr GLenum kGlDrawFramebuffer = 0x8CA9;
inline constexpr GLenum kGlColorAttachment0 = 0x8CE0;
inline constexpr GLenum kGlDepthStencilAttachment = 0x821A;
inline constexpr GLenum kGlFramebufferComplete = 0x8CD5;

inline constexpr GLbitfield kGlDepthBufferBit = 0x00000100;
inline constexpr GLbitfield kGlStencilBufferBit = 0x00000400;
inline constexpr GLbitfield kGlColorBufferBit = 0x00004000;

struct GlProcs
{
    void (REPSP_GL_APIENTRY *glViewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (REPSP_GL_APIENTRY *glScissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (REPSP_GL_APIENTRY *glEnable)(GLenum) = nullptr;
    void (REPSP_GL_APIENTRY *glDisable)(GLenum) = nullptr;
    void (REPSP_GL_APIENTRY *glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean) = nullptr;
    void (REPSP_GL_APIENTRY *glDepthMask)(GLboolean) = nullptr;
    void (REPSP_GL_APIENTRY *glDepthFunc)(GLenum) = nullptr;
    void (REPSP_GL_APIENTRY *glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum) = nullptr;
    void (REPSP_GL_APIENTRY *glBlendEquationSeparate)(GLenum, GLenum) = nullptr;
    void (REPSP_GL_APIENTRY *glBlendColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (REPSP_GL_APIENTRY *glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (REPSP_GL_APIENTRY *glClear)(GLbitfield) = nullptr;
    void (REPSP_GL_APIENTRY *glDrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    void (REPSP_GL_APIENTRY *glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (REPSP_GL_APIENTRY *glPixelStorei)(GLenum, GLint) = nullptr;
    void (REPSP_GL_APIENTRY *glFinish)() = nullptr;

    void (REPSP_GL_APIENTRY *glGenFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (REPSP_GL_APIENTRY *glDeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_GL_APIENTRY *glBindFramebuffer)(GLenum, GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    void (REPSP_GL_APIENTRY *glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
    GLenum (REPSP_GL_APIENTRY *glCheckFramebufferStatus)(GLenum) = nullptr;
    void (REPSP_GL_APIENTRY *glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) = nullptr;

    void (REPSP_GL_APIENTRY *glGenRenderbuffers)(GLsizei, GLuint*) = nullptr;
    void (REPSP_GL_APIENTRY *glDeleteRenderbuffers)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_GL_APIENTRY *glBindRenderbuffer)(GLenum, GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;

    void (REPSP_GL_APIENTRY *glGenTextures)(GLsizei, GLuint*) = nullptr;
    void (REPSP_GL_APIENTRY *glDeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_GL_APIENTRY *glActiveTexture)(GLenum) = nullptr;
    void (REPSP_GL_APIENTRY *glBindTexture)(GLenum, GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (REPSP_GL_APIENTRY *glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
    void (REPSP_GL_APIENTRY *glTexParameteri)(GLenum, GLenum, GLint) = nullptr;

    void (REPSP_GL_APIENTRY *glGenBuffers)(GLsizei, GLuint*) = nullptr;
    void (REPSP_GL_APIENTRY *glDeleteBuffers)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_GL_APIENTRY *glBindBuffer)(GLenum, GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glBufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;

    void (REPSP_GL_APIENTRY *glGenVertexArrays)(GLsizei, GLuint*) = nullptr;
    void (REPSP_GL_APIENTRY *glDeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_GL_APIENTRY *glBindVertexArray)(GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glEnableVertexAttribArray)(GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;

    GLuint (REPSP_GL_APIENTRY *glCreateShader)(GLenum) = nullptr;
    void (REPSP_GL_APIENTRY *glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (REPSP_GL_APIENTRY *glCompileShader)(GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glGetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (REPSP_GL_APIENTRY *glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (REPSP_GL_APIENTRY *glDeleteShader)(GLuint) = nullptr;

    GLuint (REPSP_GL_APIENTRY *glCreateProgram)() = nullptr;
    void (REPSP_GL_APIENTRY *glAttachShader)(GLuint, GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glLinkProgram)(GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glGetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (REPSP_GL_APIENTRY *glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (REPSP_GL_APIENTRY *glDeleteProgram)(GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glUseProgram)(GLuint) = nullptr;

    GLint (REPSP_GL_APIENTRY *glGetUniformLocation)(GLuint, const GLchar*) = nullptr;
    void (REPSP_GL_APIENTRY *glUniform1i)(GLint, GLint) = nullptr;
    void (REPSP_GL_APIENTRY *glUniform2i)(GLint, GLint, GLint) = nullptr;
    void (REPSP_GL_APIENTRY *glUniform3ui)(GLint, GLuint, GLuint, GLuint) = nullptr;
    void (REPSP_GL_APIENTRY *glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;

    template <typename Fn>
    static bool Load(const platform::DisplayBackend& display, Fn& fn, const char* name)
    {
        void* sym = display.GetGlProcAddress(name);
        if (sym == nullptr)
        {
            return false;
        }
        static_assert(sizeof(fn) == sizeof(sym));
        std::memcpy(&fn, &sym, sizeof(fn));
        return true;
    }

    bool LoadAll(const platform::DisplayBackend& display)
    {
        return Load(display, glViewport, "glViewport") &&
               Load(display, glScissor, "glScissor") &&
               Load(display, glEnable, "glEnable") &&
               Load(display, glDisable, "glDisable") &&
               Load(display, glColorMask, "glColorMask") &&
               Load(display, glDepthMask, "glDepthMask") &&
               Load(display, glDepthFunc, "glDepthFunc") &&
               Load(display, glBlendFuncSeparate, "glBlendFuncSeparate") &&
               Load(display, glBlendEquationSeparate, "glBlendEquationSeparate") &&
               Load(display, glBlendColor, "glBlendColor") &&
               Load(display, glClearColor, "glClearColor") &&
               Load(display, glClear, "glClear") &&
               Load(display, glDrawArrays, "glDrawArrays") &&
               Load(display, glReadPixels, "glReadPixels") &&
               Load(display, glPixelStorei, "glPixelStorei") &&
               Load(display, glFinish, "glFinish") &&
               Load(display, glGenFramebuffers, "glGenFramebuffers") &&
               Load(display, glDeleteFramebuffers, "glDeleteFramebuffers") &&
               Load(display, glBindFramebuffer, "glBindFramebuffer") &&
               Load(display, glFramebufferTexture2D, "glFramebufferTexture2D") &&
               Load(display, glFramebufferRenderbuffer, "glFramebufferRenderbuffer") &&
               Load(display, glCheckFramebufferStatus, "glCheckFramebufferStatus") &&
               Load(display, glBlitFramebuffer, "glBlitFramebuffer") &&
               Load(display, glGenRenderbuffers, "glGenRenderbuffers") &&
               Load(display, glDeleteRenderbuffers, "glDeleteRenderbuffers") &&
               Load(display, glBindRenderbuffer, "glBindRenderbuffer") &&
               Load(display, glRenderbufferStorage, "glRenderbufferStorage") &&
               Load(display, glGenTextures, "glGenTextures") &&
               Load(display, glDeleteTextures, "glDeleteTextures") &&
               Load(display, glActiveTexture, "glActiveTexture") &&
               Load(display, glBindTexture, "glBindTexture") &&
               Load(display, glTexImage2D, "glTexImage2D") &&
               Load(display, glTexSubImage2D, "glTexSubImage2D") &&
               Load(display, glTexParameteri, "glTexParameteri") &&
               Load(display, glGenBuffers, "glGenBuffers") &&
               Load(display, glDeleteBuffers, "glDeleteBuffers") &&
               Load(display, glBindBuffer, "glBindBuffer") &&
               Load(display, glBufferData, "glBufferData") &&
               Load(display, glGenVertexArrays, "glGenVertexArrays") &&
               Load(display, glDeleteVertexArrays, "glDeleteVertexArrays") &&
               Load(display, glBindVertexArray, "glBindVertexArray") &&
               Load(display, glEnableVertexAttribArray, "glEnableVertexAttribArray") &&
               Load(display, glVertexAttribPointer, "glVertexAttribPointer") &&
               Load(display, glCreateShader, "glCreateShader") &&
               Load(display, glShaderSource, "glShaderSource") &&
               Load(display, glCompileShader, "glCompileShader") &&
               Load(display, glGetShaderiv, "glGetShaderiv") &&
               Load(display, glGetShaderInfoLog, "glGetShaderInfoLog") &&
               Load(display, glDeleteShader, "glDeleteShader") &&
               Load(display, glCreateProgram, "glCreateProgram") &&
               Load(display, glAttachShader, "glAttachShader") &&
               Load(display, glLinkProgram, "glLinkProgram") &&
               Load(display, glGetProgramiv, "glGetProgramiv") &&
               Load(display, glGetProgramInfoLog, "glGetProgramInfoLog") &&
               Load(display, glDeleteProgram, "glDeleteProgram") &&
               Load(display, glUseProgram, "glUseProgram") &&
               Load(display, glGetUniformLocation, "glGetUniformLocation") &&
               Load(display, glUniform1i, "glUniform1i") &&
               Load(display, glUniform2i, "glUniform2i") &&
               Load(display, glUniform3ui, "glUniform3ui") &&
               Load(display, glUniform4f, "glUniform4f");
    }
};

} // namespace repsp::gpu
