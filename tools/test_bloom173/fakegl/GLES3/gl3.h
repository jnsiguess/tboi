/* A desk stand-in for <GLES3/gl3.h>: only what bloom.c and gpu.c use, served
 * by the fake GL state machine in fakegl.c (tools/test_bloom173). */
#ifndef FAKE_GL3_H
#define FAKE_GL3_H
#include <stddef.h>
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat;
typedef void GLvoid;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NONE 0
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_TEXTURE 0x1702
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_FRAMEBUFFER 0x8D40
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_ARRAY_BUFFER 0x8892
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_STATIC_DRAW 0x88E4
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VIEWPORT 0x0BA2
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_CULL_FACE 0x0B44
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_EXTENSIONS 0x1F03
#define GL_RENDERER 0x1F01

GLuint glCreateShader(GLenum);
void glShaderSource(GLuint, GLsizei, const char *const *, const GLint *);
void glCompileShader(GLuint);
void glGetShaderiv(GLuint, GLenum, GLint *);
void glDeleteShader(GLuint);
GLboolean glIsProgram(GLuint);
GLboolean glIsTexture(GLuint);
GLboolean glIsFramebuffer(GLuint);
GLboolean glIsVertexArray(GLuint);
void glGetIntegerv(GLenum, GLint *);
void glGetFloatv(GLenum, GLfloat *);
void glActiveTexture(GLenum);
GLuint glCreateProgram(void);
void glAttachShader(GLuint, GLuint);
void glLinkProgram(GLuint);
void glGetProgramiv(GLuint, GLenum, GLint *);
void glDeleteProgram(GLuint);
void glUseProgram(GLuint);
void glUniform1i(GLint, GLint);
GLint glGetUniformLocation(GLuint, const char *);
void glGenVertexArrays(GLsizei, GLuint *);
void glBindVertexArray(GLuint);
void glGenBuffers(GLsizei, GLuint *);
void glBindBuffer(GLenum, GLuint);
void glBufferData(GLenum, GLsizeiptr, const void *, GLenum);
void glEnableVertexAttribArray(GLuint);
void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
void glGenTextures(GLsizei, GLuint *);
void glBindTexture(GLenum, GLuint);
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
void glTexParameteri(GLenum, GLenum, GLint);
void glGenFramebuffers(GLsizei, GLuint *);
void glBindFramebuffer(GLenum, GLuint);
void glFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint);
GLenum glCheckFramebufferStatus(GLenum);
void glGetFramebufferAttachmentParameteriv(GLenum, GLenum, GLenum, GLint *);
GLboolean glIsEnabled(GLenum);
void glEnable(GLenum);
void glDisable(GLenum);
void glViewport(GLint, GLint, GLsizei, GLsizei);
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat);
void glClear(GLbitfield);
void glDrawArrays(GLenum, GLint, GLsizei);
const GLubyte *glGetString(GLenum);
#endif
