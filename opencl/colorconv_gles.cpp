#include <stdexcept>

#include <assert.h>
#include <EGL/egl.h>
#include <GLES3/gl31.h>

class GLESError: public std::runtime_error {
public:
    int eglError;
    int glesError;

    GLESError(const char* msg): runtime_error(msg) {
        eglError = eglGetError();
        glesError = glGetError();
    }
};

class GLESCtx {
public:
    EGLDisplay disp_ = EGL_NO_DISPLAY;
    EGLContext ctx_ = EGL_NO_CONTEXT;
    GLint computeShader_ = GL_NONE;
    GLint prg_ = GL_NONE;

    void CleanUp() {
        eglMakeCurrent(disp_, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx_);

        if (prg_ != GL_NONE) {
            glDeleteProgram(prg_);
            prg_ = GL_NONE;
        }
        if (computeShader_ != GL_NONE) {
            glDeleteShader(computeShader_);
            computeShader_ = GL_NONE;
        }

        eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (ctx_ != EGL_NO_CONTEXT) {
            eglDestroyContext(disp_, ctx_);
            ctx_ = EGL_NO_CONTEXT;
        }
    }

    ~GLESCtx() {
        CleanUp();
    }

    void IntiCtx() {
        disp_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        assert(disp_ != EGL_NO_DISPLAY);
        EGLint eglver[] = { 1, 5 };
        if (eglInitialize(disp_, &eglver[0], &eglver[1]) != EGL_TRUE)
            throw GLESError("fail to init egl environment");
        EGLint cfg_attrs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_NONE
        };
        
        EGLConfig selected_cfg[1] = { 0 };
        EGLint selected_cfg_size = 1;
        if (eglChooseConfig(disp_, cfg_attrs, selected_cfg, 1, &selected_cfg_size) != EGL_TRUE)
            throw GLESError("fail to select egl config");
        assert(selected_cfg[0] != 0);
        EGLint ctx_attrs[] = {
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 1,
            EGL_NONE
        };
        ctx_ = eglCreateContext(disp_, selected_cfg[0], EGL_NO_CONTEXT, ctx_attrs);
        if (ctx_ == EGL_NO_CONTEXT)
            throw GLESError("fail to create opengl es 3.1 context");

        eglMakeCurrent(disp_, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx_);
    }

    void BuildProgram() {
        computeShader_ = glCreateShader(GL_COMPUTE_SHADER);

        const char* shaderSrc = R"_SHADER_(#version 310 es

void main() {}
)_SHADER_";
        const char* shaderSrcList[] = { shaderSrc };
        int shaderLenList[] = { -1 };
        glShaderSource(computeShader_, 1, shaderSrcList, shaderLenList);
        glCompileShader(computeShader_);
        GLint compileStatus;
        glGetShaderiv(computeShader_, GL_COMPILE_STATUS, &compileStatus);
        if (compileStatus != GL_TRUE) {
            GLint infoLen = 0;
            glGetShaderiv(computeShader_, GL_INFO_LOG_LENGTH, &infoLen);
            std::string msg;
            msg.resize(infoLen + 1);
            glGetShaderInfoLog(computeShader_, msg.size(), &infoLen, msg.data());
            throw GLESError(msg.data());
        }

        prg_ = glCreateProgram();
        glAttachShader(prg_, computeShader_);
        glLinkProgram(prg_);
        GLint linkStatus;
        glGetProgramiv(prg_, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint infoLen = 0;
            glGetProgramiv(prg_, GL_INFO_LOG_LENGTH, &infoLen);
            std::string msg;
            msg.resize(infoLen + 1);
            glGetProgramInfoLog(prg_, msg.size(), &infoLen, msg.data());
                throw GLESError(msg.data());
        }
    }
};

#include <iostream>
int main() {
    const char* driver = getenv("GALLIUM_DRIVER");
    GLESCtx ctx;
    try {
        ctx.IntiCtx();
        ctx.BuildProgram();
        return 0;
    } catch(std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}