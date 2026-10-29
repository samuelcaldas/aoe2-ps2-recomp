#pragma once

#include <cstdint>

namespace aoe2::renderer {

class GlesShader {
public:
    GlesShader() = default;
    ~GlesShader();

    bool Initialize();
    void Bind();
    void UpdateProjection(float width, float height);
    void Teardown();

    bool IsValid() const { return program_ != 0; }
    uint32_t GetProgram() const { return program_; }

private:
    uint32_t Compile(uint32_t type, const char* src);
    bool Link(uint32_t vert, uint32_t frag);

    uint32_t program_{0};
    int32_t proj_loc_{-1};
    int32_t tex_loc_{-1};
};

} // namespace aoe2::renderer
