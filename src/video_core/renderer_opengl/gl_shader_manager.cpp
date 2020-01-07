// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <unordered_map>
#include <boost/functional/hash.hpp>
#include <boost/variant.hpp>
#include "video_core/renderer_opengl/gl_shader_manager.h"

namespace OpenGL {

static void SetShaderUniformBlockBinding(GLuint shader, const char* name, UniformBindings binding,
                                         std::size_t expected_size) {
    const GLuint ub_index = glGetUniformBlockIndex(shader, name);
    if (ub_index == GL_INVALID_INDEX) {
        return;
    }
    GLint ub_size = 0;
    glGetActiveUniformBlockiv(shader, ub_index, GL_UNIFORM_BLOCK_DATA_SIZE, &ub_size);
    ASSERT_MSG(ub_size == expected_size, "Uniform block size did not match! Got {}, expected {}",
               static_cast<int>(ub_size), expected_size);
    glUniformBlockBinding(shader, ub_index, static_cast<GLuint>(binding));
}

static void SetShaderUniformBlockBindings(GLuint shader) {
    SetShaderUniformBlockBinding(shader, "shader_data", UniformBindings::Common,
                                 sizeof(UniformData));
    SetShaderUniformBlockBinding(shader, "vs_config", UniformBindings::VS, sizeof(VSUniformData));
}

static void SetShaderSamplerBinding(GLuint shader, const char* name,
                                    TextureUnits::TextureUnit binding) {
    GLint uniform_tex = glGetUniformLocation(shader, name);
    if (uniform_tex != -1) {
        glUniform1i(uniform_tex, binding.id);
    }
}

static void SetShaderImageBinding(GLuint shader, const char* name, GLuint binding) {
    GLint uniform_tex = glGetUniformLocation(shader, name);
    if (uniform_tex != -1) {
        glUniform1i(uniform_tex, static_cast<GLint>(binding));
    }
}

static void SetShaderSamplerBindings(GLuint shader) {
    OpenGLState cur_state = OpenGLState::GetCurState();
    GLuint old_program = std::exchange(cur_state.draw.shader_program, shader);
    cur_state.Apply();

    // Set the texture samplers to correspond to different texture units
    SetShaderSamplerBinding(shader, "tex0", TextureUnits::PicaTexture(0));
    SetShaderSamplerBinding(shader, "tex1", TextureUnits::PicaTexture(1));
    SetShaderSamplerBinding(shader, "tex2", TextureUnits::PicaTexture(2));
    SetShaderSamplerBinding(shader, "tex_cube", TextureUnits::TextureCube);

    // Set the texture samplers to correspond to different lookup table texture units
    SetShaderSamplerBinding(shader, "texture_buffer_lut_rg", TextureUnits::TextureBufferLUT_RG);
    SetShaderSamplerBinding(shader, "texture_buffer_lut_rgba", TextureUnits::TextureBufferLUT_RGBA);

    SetShaderImageBinding(shader, "shadow_buffer", ImageUnits::ShadowBuffer);
    SetShaderImageBinding(shader, "shadow_texture_px", ImageUnits::ShadowTexturePX);
    SetShaderImageBinding(shader, "shadow_texture_nx", ImageUnits::ShadowTextureNX);
    SetShaderImageBinding(shader, "shadow_texture_py", ImageUnits::ShadowTexturePY);
    SetShaderImageBinding(shader, "shadow_texture_ny", ImageUnits::ShadowTextureNY);
    SetShaderImageBinding(shader, "shadow_texture_pz", ImageUnits::ShadowTexturePZ);
    SetShaderImageBinding(shader, "shadow_texture_nz", ImageUnits::ShadowTextureNZ);

    cur_state.draw.shader_program = old_program;
    cur_state.Apply();
}

void PicaUniformsData::SetFromRegs(const Pica::ShaderRegs& regs,
                                   const Pica::Shader::ShaderSetup& setup) {
    std::transform(std::begin(setup.uniforms.b), std::end(setup.uniforms.b), std::begin(bools),
                   [](bool value) -> BoolAligned { return {value ? GL_TRUE : GL_FALSE}; });
    std::transform(std::begin(regs.int_uniforms), std::end(regs.int_uniforms), std::begin(i),
                   [](const auto& value) -> GLuvec4 {
                       return {value.x.Value(), value.y.Value(), value.z.Value(), value.w.Value()};
                   });
    std::transform(std::begin(setup.uniforms.f), std::end(setup.uniforms.f), std::begin(f),
                   [](const auto& value) -> GLvec4 {
                       return {value.x.ToFloat32(), value.y.ToFloat32(), value.z.ToFloat32(),
                               value.w.ToFloat32()};
                   });
}

/**
 * An object representing a shader program staging. It can be either a shader object or a program
 * object, depending on whether separable program is used.
 */
class OGLShaderStage {
public:
    explicit OGLShaderStage(bool separable) {
        if (separable) {
            shader_or_program = OGLProgram();
        } else {
            shader_or_program = OGLShader();
        }
    }

    void Create(const char* source, u32 uniform_flag, GLenum type) {
        if (shader_or_program.which() == 0) {
            boost::get<OGLShader>(shader_or_program).Create(source, type);
        } else {
            OGLShader shader;
            shader.Create(source, type);
            OGLProgram& program = boost::get<OGLProgram>(shader_or_program);
            program.Create(true, {shader.handle});

            if ((uniform_flag & UNIFORM_FLAG_SHADER_DATA) != 0)
                SetShaderUniformBlockBinding(program.handle, "shader_data", UniformBindings::Common,
                                             sizeof(UniformData));
            if ((uniform_flag & UNIFORM_FLAG_VS_CONFIG) != 0)
                SetShaderUniformBlockBinding(program.handle, "vs_config", UniformBindings::VS,
                                             sizeof(VSUniformData));

            // SetShaderUniformBlockBindings(program.handle);
            SetShaderSamplerBindings(program.handle);
        }
    }

    GLuint GetHandle() const {
        if (shader_or_program.which() == 0) {
            return boost::get<OGLShader>(shader_or_program).handle;
        } else {
            return boost::get<OGLProgram>(shader_or_program).handle;
        }
    }

    void GetBin(std::vector<GLubyte>& bin) {
        if (shader_or_program.which() == 0) {
        } else {
            OGLProgram& program = boost::get<OGLProgram>(shader_or_program);
            program.GetBin(bin);
        }
    }

    void SetBin(const std::vector<GLubyte>& bin) {

        if (shader_or_program.which() == 0) {
        } else {
            OGLProgram& program = boost::get<OGLProgram>(shader_or_program);
            program.Create(bin);

            SetShaderUniformBlockBinding(program.handle, "shader_data", UniformBindings::Common,
                                         sizeof(UniformData));
            SetShaderUniformBlockBinding(program.handle, "vs_config", UniformBindings::VS,
                                         sizeof(VSUniformData));

            // SetShaderUniformBlockBindings(program.handle);
            SetShaderSamplerBindings(program.handle);
        }
    }

private:
    boost::variant<OGLShader, OGLProgram> shader_or_program;
};

class TrivialVertexShader {
public:
    explicit TrivialVertexShader(bool separable, u32& uniform_flag) : program(separable) {
        std::string source;
        GenerateTrivialVertexShader(separable, source, uniform_flag);
        program.Create(source.c_str(), uniform_flag, GL_VERTEX_SHADER);
    }
    GLuint Get() const {
        return program.GetHandle();
    }

private:
    OGLShaderStage program;
};

template <typename KeyConfigType,
          void (*CodeGenerator)(const KeyConfigType&, bool, std::string&, u32&), GLenum ShaderType>
class ShaderCache {
public:
    explicit ShaderCache(bool separable) : separable(separable) {}
    ~ShaderCache() {
        SaveShaderBin();
    }
    GLuint Get(const KeyConfigType& config, u32& uniform_flag) {
        u64 hash = config.Hash();
        auto [iter, new_shader] = shaders.emplace(config, OGLShaderStage{separable});
        OGLShaderStage& cached_shader = iter->second;
        if (new_shader) {
            auto [iter_bin, new_shader_bin] = shader_bins.emplace(hash, std::vector<GLubyte>{});
            std::vector<GLubyte>& shader_bin = iter_bin->second;
            if (new_shader_bin) {
                std::string source;
                CodeGenerator(config, separable, source, uniform_flag);
                cached_shader.Create(source.c_str(), uniform_flag, ShaderType);
                cached_shader.GetBin(shader_bin);
            } else {
                cached_shader.SetBin(shader_bin);
            }
        }
        return cached_shader.GetHandle();
    }

    void SaveShaderBin() {
        FILE* bin_file = fopen(bin_file_name.c_str(), "wb");
        if (bin_file != NULL) {
            std::map<u64, std::vector<GLubyte>>::iterator iter = shader_bins.begin();
            for (; iter != shader_bins.end(); ++iter) {
                fwrite(&iter->first, sizeof(u64), 1, bin_file);
                std::vector<GLubyte>& bin = iter->second;
                u32 size = (u32)bin.size();
                fwrite(&size, sizeof(u32), 1, bin_file);
                fwrite(bin.data(), 1, size, bin_file);
            }
            fclose(bin_file);
        }
    }

    void LoadShaderBin(const char* file_name) {
        bin_file_name = file_name;
        FILE* bin_file = fopen(file_name, "rb");
        if (bin_file != NULL) {
            for (; !feof(bin_file);) {
                u64 hash = 0;
                if (fread(&hash, sizeof(u64), 1, bin_file) == 1) {
                    u32 size = 0;
                    fread(&size, sizeof(u32), 1, bin_file);

                    std::vector<GLubyte>& bin = shader_bins[hash];
                    bin.resize(size);
                    fread(bin.data(), 1, size, bin_file);
                }
            }
            fclose(bin_file);
        }
    }

private:
    bool separable;
    std::unordered_map<KeyConfigType, OGLShaderStage> shaders;
    std::map<u64, std::vector<GLubyte>> shader_bins;
    std::string bin_file_name;
};

// This is a cache designed for shaders translated from PICA shaders. The first cache matches the
// config structure like a normal cache does. On cache miss, the second cache matches the generated
// GLSL code. The configuration is like this because there might be leftover code in the PICA shader
// program buffer from the previous shader, which is hashed into the config, resulting several
// different config values from the same shader program.
template <typename KeyConfigType,
          bool (*CodeGenerator)(const Pica::Shader::ShaderSetup&, const KeyConfigType&, bool,
                                std::string&, u32&),
          GLenum ShaderType>
class ShaderDoubleCache {
public:
    explicit ShaderDoubleCache(bool separable) : separable(separable) {}
    GLuint Get(const KeyConfigType& key, const Pica::Shader::ShaderSetup& setup,
               u32& uniform_flag) {
        auto map_it = shader_map.find(key);
        if (map_it == shader_map.end()) {
            std::string program;
            bool ret = CodeGenerator(setup, key, separable, program, uniform_flag);
            if (!ret) {
                shader_map[key] = nullptr;
                return 0;
            }

            auto [iter, new_shader] = shader_cache.emplace(program, OGLShaderStage{separable});
            OGLShaderStage& cached_shader = iter->second;
            if (new_shader) {
                cached_shader.Create(program.c_str(), uniform_flag, ShaderType);
            }
            shader_map[key] = &cached_shader;
            return cached_shader.GetHandle();
        }

        if (map_it->second == nullptr) {
            return 0;
        }

        return map_it->second->GetHandle();
    }

private:
    bool separable;
    std::unordered_map<KeyConfigType, OGLShaderStage*> shader_map;
    std::unordered_map<std::string, OGLShaderStage> shader_cache;
};

using ProgrammableVertexShaders =
    ShaderDoubleCache<PicaVSConfig, &GenerateVertexShader, GL_VERTEX_SHADER>;

using FixedGeometryShaders =
    ShaderCache<PicaFixedGSConfig, &GenerateFixedGeometryShader, GL_GEOMETRY_SHADER>;

using FragmentShaders = ShaderCache<PicaFSConfig, &GenerateFragmentShader, GL_FRAGMENT_SHADER>;

class ShaderProgramManager::Impl {
public:
    explicit Impl(bool separable, bool is_amd)
        : is_amd(is_amd), separable(separable), uniform_flag(0),
          programmable_vertex_shaders(separable), trivial_vertex_shader(separable, uniform_flag),
          fixed_geometry_shaders(separable), fragment_shaders(separable) {
        if (separable)
            pipeline.Create();
        fixed_geometry_shaders.LoadShaderBin("gs.bin");
        fragment_shaders.LoadShaderBin("fs.bin");
    }

    struct ShaderTuple {
        GLuint vs = 0;
        GLuint gs = 0;
        GLuint fs = 0;

        bool operator==(const ShaderTuple& rhs) const {
            return std::tie(vs, gs, fs) == std::tie(rhs.vs, rhs.gs, rhs.fs);
        }

        bool operator!=(const ShaderTuple& rhs) const {
            return std::tie(vs, gs, fs) != std::tie(rhs.vs, rhs.gs, rhs.fs);
        }

        struct Hash {
            std::size_t operator()(const ShaderTuple& tuple) const {
                std::size_t hash = 0;
                boost::hash_combine(hash, tuple.vs);
                boost::hash_combine(hash, tuple.gs);
                boost::hash_combine(hash, tuple.fs);
                return hash;
            }
        };
    };

    bool is_amd;

    ShaderTuple current;

    ProgrammableVertexShaders programmable_vertex_shaders;
    TrivialVertexShader trivial_vertex_shader;

    FixedGeometryShaders fixed_geometry_shaders;

    FragmentShaders fragment_shaders;

    bool separable;
    u32 uniform_flag;
    std::unordered_map<ShaderTuple, OGLProgram, ShaderTuple::Hash> program_cache;
    OGLPipeline pipeline;
};

ShaderProgramManager::ShaderProgramManager(bool separable, bool is_amd)
    : impl(std::make_unique<Impl>(separable, is_amd)) {}

ShaderProgramManager::~ShaderProgramManager() = default;

bool ShaderProgramManager::UseProgrammableVertexShader(const PicaVSConfig& config,
                                                       const Pica::Shader::ShaderSetup& setup) {
    GLuint handle = impl->programmable_vertex_shaders.Get(config, setup, impl->uniform_flag);
    if (handle == 0)
        return false;
    impl->current.vs = handle;
    return true;
}

void ShaderProgramManager::UseTrivialVertexShader() {
    impl->current.vs = impl->trivial_vertex_shader.Get();
}

void ShaderProgramManager::UseFixedGeometryShader(const PicaFixedGSConfig& config) {
    impl->current.gs = impl->fixed_geometry_shaders.Get(config, impl->uniform_flag);
}

void ShaderProgramManager::UseTrivialGeometryShader() {
    impl->current.gs = 0;
}

void ShaderProgramManager::UseFragmentShader(const PicaFSConfig& config) {
    impl->current.fs = impl->fragment_shaders.Get(config, impl->uniform_flag);
}

void ShaderProgramManager::ApplyTo(OpenGLState& state) {
    if (impl->separable) {
        if (impl->is_amd) {
            // Without this reseting, AMD sometimes freezes when one stage is changed but not for
            // the others.
            // On the other hand, including this reset seems to introduce memory leak in Intel
            // Graphics.
            glUseProgramStages(
                impl->pipeline.handle,
                GL_VERTEX_SHADER_BIT | GL_GEOMETRY_SHADER_BIT | GL_FRAGMENT_SHADER_BIT, 0);
        }

        glUseProgramStages(impl->pipeline.handle, GL_VERTEX_SHADER_BIT, impl->current.vs);
        glUseProgramStages(impl->pipeline.handle, GL_GEOMETRY_SHADER_BIT, impl->current.gs);
        glUseProgramStages(impl->pipeline.handle, GL_FRAGMENT_SHADER_BIT, impl->current.fs);
        state.draw.shader_program = 0;
        state.draw.program_pipeline = impl->pipeline.handle;
    } else {
        OGLProgram& cached_program = impl->program_cache[impl->current];
        if (cached_program.handle == 0) {
            cached_program.Create(false, {impl->current.vs, impl->current.gs, impl->current.fs});

            if ((impl->uniform_flag & UNIFORM_FLAG_SHADER_DATA) != 0)
                SetShaderUniformBlockBinding(cached_program.handle, "shader_data",
                                             UniformBindings::Common, sizeof(UniformData));
            if ((impl->uniform_flag & UNIFORM_FLAG_VS_CONFIG) != 0)
                SetShaderUniformBlockBinding(cached_program.handle, "vs_config",
                                             UniformBindings::VS, sizeof(VSUniformData));
            // SetShaderUniformBlockBindings(cached_program.handle);
            SetShaderSamplerBindings(cached_program.handle);
        }
        state.draw.shader_program = cached_program.handle;
    }
}
} // namespace OpenGL
