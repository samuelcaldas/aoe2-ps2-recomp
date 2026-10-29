#include <aoe2/renderer/gles_shader.hpp>

#if defined(__SWITCH__)
#include <GLES2/gl2.h>
#elif defined(AOE2_HAS_GLES)
#include <GLES3/gl3.h>
#endif

namespace aoe2::renderer {

namespace {

#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
const char* kVertSrc300 =
    "#version 300 es\n"
    "precision mediump float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "uniform mat4 u_proj;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

const char* kFragSrc300 =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    o_color = texture(u_tex, v_uv) * v_color;\n"
    "}\n";

const char* kVertSrc100 =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_color;\n"
    "uniform mat4 u_proj;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

const char* kFragSrc100 =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "uniform sampler2D u_tex;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_tex, v_uv) * v_color;\n"
    "}\n";
#endif

} // namespace

GlesShader::~GlesShader() {
    Teardown();
}

uint32_t GlesShader::Compile(uint32_t type, const char* src) {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    GLuint shd = glCreateShader(type);
    if (!shd) return 0;
    glShaderSource(shd, 1, &src, nullptr);
    glCompileShader(shd);
    GLint ok = 0;
    glGetShaderiv(shd, GL_COMPILE_STATUS, &ok);
    if (ok) return shd;
    glDeleteShader(shd);
#endif
    return 0;
}

bool GlesShader::Link(uint32_t vert, uint32_t frag) {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }
    GLuint prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_uv");
    glBindAttribLocation(prog, 2, "a_color");
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    glDeleteShader(vert);
    glDeleteShader(frag);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(prog);
        return false;
    }
    program_ = prog;
    proj_loc_ = glGetUniformLocation(prog, "u_proj");
    tex_loc_ = glGetUniformLocation(prog, "u_tex");
    return true;
#endif
    return false;
}

bool GlesShader::Initialize() {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    uint32_t v = Compile(GL_VERTEX_SHADER, kVertSrc300);
    uint32_t f = Compile(GL_FRAGMENT_SHADER, kFragSrc300);
    if (Link(v, f)) return true;
    v = Compile(GL_VERTEX_SHADER, kVertSrc100);
    f = Compile(GL_FRAGMENT_SHADER, kFragSrc100);
    return Link(v, f);
#endif
    return false;
}

void GlesShader::Bind() {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (!program_) return;
    glUseProgram(program_);
    if (tex_loc_ >= 0) glUniform1i(tex_loc_, 0);
#endif
}

void GlesShader::UpdateProjection(float width, float height) {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (!program_ || proj_loc_ < 0 || width <= 0.0f || height <= 0.0f) return;
    const float mat[16] = {
        2.0f / width, 0.0f,          0.0f, 0.0f,
        0.0f,        -2.0f / height, 0.0f, 0.0f,
        0.0f,         0.0f,         -1.0f, 0.0f,
       -1.0f,         1.0f,          0.0f, 1.0f
    };
    glUseProgram(program_);
    glUniformMatrix4fv(proj_loc_, 1, GL_FALSE, mat);
#endif
}

void GlesShader::Teardown() {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (program_) glDeleteProgram(program_);
#endif
    program_ = 0; proj_loc_ = -1; tex_loc_ = -1;
}

} // namespace aoe2::renderer
