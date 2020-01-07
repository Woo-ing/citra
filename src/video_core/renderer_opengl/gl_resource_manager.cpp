// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <list>
#include <utility>
#include <vector>
#include <glad/glad.h>
#include "common/common_types.h"
#include "common/microprofile.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_util.h"
#include "video_core/renderer_opengl/gl_state.h"

MICROPROFILE_DEFINE(OpenGL_ResourceCreation, "OpenGL", "Resource Creation", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_ResourceDeletion, "OpenGL", "Resource Deletion", MP_RGB(128, 128, 192));

namespace OpenGL {

void OGLRenderbuffer::Create() {
    if (handle != 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    glGenRenderbuffers(1, &handle);
}

void OGLRenderbuffer::Release() {
    if (handle == 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceDeletion);
    glDeleteRenderbuffers(1, &handle);
    OpenGLState::GetCurState().ResetRenderbuffer(handle).Apply();
    handle = 0;
}

template <typename T, typename Creator, typename Deletor>
class HandlePool {
public:
    HandlePool(size_t new_size = 0) : m_size(0) {}
    ~HandlePool() {
        Resize(0);
    }

    bool IsEmpty() {
        return m_size == 0;
    }

    void Resize(size_t new_size) {
        if (new_size < m_size) {
            std::vector<T> arr(m_size - new_size);
            for (size_t i = 0; i < arr.size(); ++i) {
                arr[i] = m_pool.back();
                m_pool.pop_back();
            }
            Deletor deletor;
            deletor(arr.size(), &arr[0]);
        } else if (new_size > m_size) {
            std::vector<T> arr(new_size - m_size);
            Creator creator;
            creator(arr.size(), &arr[0]);
            for (size_t i = 0; i < arr.size(); ++i) {
                m_pool.push_front(arr[i]);
            }
        }
        m_size = new_size;
    }

    T Create() {
        if (!m_pool.empty()) {
            T h = m_pool.back();
            m_pool.pop_back();
            --m_size;
            return h;
        } else {
            T h;
            Creator creator;
            creator(1, &h);
            return h;
        }
    }

    void Release(T h) {
        m_pool.push_front(h);
        ++m_size;
    }

    size_t Size() {
        return m_pool.size();
    }

protected:
    std::list<T> m_pool;
    size_t m_size;
};

struct TextureCreator {
    constexpr void operator()(size_t n, GLuint* p) const {
        glGenTextures((GLsizei)n, p);
    }
};

struct TextureDeletor {
    constexpr void operator()(size_t n, GLuint* p) const {
        glDeleteTextures((GLsizei)n, p);
    }
};

HandlePool<GLuint, TextureCreator, TextureDeletor> TexturePool(128);
HandlePool<GLuint, TextureCreator, TextureDeletor> TextureRecycel(0);

void OGLTexture::Create() {
    if (handle != 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    //glGenTextures(1, &handle);
    if (TexturePool.IsEmpty()) {
        TexturePool.Resize(128);
    }
    handle = TexturePool.Create();
}

void OGLTexture::Release() {
    if (handle == 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceDeletion);
    // glDeleteTextures(1, &handle);
    TextureRecycel.Release(handle);
    if (TextureRecycel.Size() > 16)
        TextureRecycel.Resize(0);
    OpenGLState::GetCurState().ResetTexture(handle).Apply();
    handle = 0;
}

void OGLSampler::Create() {
    if (handle != 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    glGenSamplers(1, &handle);
}

void OGLSampler::Release() {
    if (handle == 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceDeletion);
    glDeleteSamplers(1, &handle);
    OpenGLState::GetCurState().ResetSampler(handle).Apply();
    handle = 0;
}

void OGLShader::Create(const char* source, GLenum type) {
    if (handle != 0)
        return;
    if (source == nullptr)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    handle = LoadShader(source, type);
}

void OGLShader::Release() {
    if (handle == 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceDeletion);
    glDeleteShader(handle);
    handle = 0;
}

void OGLProgram::Create(bool separable_program, const std::vector<GLuint>& shaders) {
    if (handle != 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    handle = LoadProgram(separable_program, shaders);
}

void OGLProgram::Create(const char* vert_shader, const char* frag_shader) {
    OGLShader vert, frag;
    vert.Create(vert_shader, GL_VERTEX_SHADER);
    frag.Create(frag_shader, GL_FRAGMENT_SHADER);

    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    Create(false, {vert.handle, frag.handle});
}

void OGLProgram::Create(const std::vector<GLubyte>& bin) {
    if (handle != 0)
        return;
    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    handle = LoadProgramBin(bin);
}

void OGLProgram::GetBin(std::vector<GLubyte>& bin) {
    if (handle == 0)
        return;
    GetProgramBin(handle, bin);
}

void OGLProgram::Release() {
    if (handle == 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceDeletion);
    glDeleteProgram(handle);
    OpenGLState::GetCurState().ResetProgram(handle).Apply();
    handle = 0;
}

void OGLPipeline::Create() {
    if (handle != 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    glGenProgramPipelines(1, &handle);
}

void OGLPipeline::Release() {
    if (handle == 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceDeletion);
    glDeleteProgramPipelines(1, &handle);
    OpenGLState::GetCurState().ResetPipeline(handle).Apply();
    handle = 0;
}

void OGLBuffer::Create() {
    if (handle != 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    glGenBuffers(1, &handle);
}

void OGLBuffer::Release() {
    if (handle == 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceDeletion);
    glDeleteBuffers(1, &handle);
    OpenGLState::GetCurState().ResetBuffer(handle).Apply();
    handle = 0;
}

void OGLVertexArray::Create() {
    if (handle != 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    glGenVertexArrays(1, &handle);
}

void OGLVertexArray::Release() {
    if (handle == 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceDeletion);
    glDeleteVertexArrays(1, &handle);
    OpenGLState::GetCurState().ResetVertexArray(handle).Apply();
    handle = 0;
}

void OGLFramebuffer::Create() {
    if (handle != 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceCreation);
    glGenFramebuffers(1, &handle);
}

void OGLFramebuffer::Release() {
    if (handle == 0)
        return;

    MICROPROFILE_SCOPE(OpenGL_ResourceDeletion);
    glDeleteFramebuffers(1, &handle);
    OpenGLState::GetCurState().ResetFramebuffer(handle).Apply();
    handle = 0;
}

} // namespace OpenGL
