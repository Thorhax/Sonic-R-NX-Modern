/**
 * r_gles2_backend.c — OpenGL ES 2.0 implementation of the immediate-mode render API.
 *
 * Implements r_state.h, r_draw.h, r_texture.h, and frame lifecycle using
 * programmable shaders and vertex attributes on Nintendo Switch (Mesa GLES2).
 */

#include <GLES2/gl2.h>
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "r_texture.h"
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Forward declarations for frame functions in render_gles2.c */
extern void BeginFrame(void);
extern void EndFrame(void);
extern void FlipD3D(void);
extern void ProcessTpageStates(void);

/* Forward declarations for GL_* texture functions in render_gles2.c */
extern GLuint s_glTextures[52];
extern int s_glTextureDirty[52];
extern void GL_UploadTpage(int tpage);
extern void GL_UploadTpageRGBA(int tpage, unsigned char *rgba, int w, int h);
extern void GL_UploadTpageSubRect(int tpage, int destX, int destY, int width, int height);
extern void GL_MarkTpageDirty(int tpage);
extern void GL_ClearTpageDirty(int tpage);
extern void GL_FreezeTpage(int tpage);
extern void GL_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h);
extern void GL_SetNoColorKey(int tpage);
extern void GL_ClearNoColorKey(int tpage);
extern void GL_SetTpageGreen6(int tpage, int on);
extern void GL_InitTextures(void);
extern void GL_SetTpageRGBA8(int tpage, unsigned char *rgba);

/* Shader Program and Uniforms */
static GLuint s_program = 0;
static GLint s_locMVP = -1;
static GLint s_locTex = -1;
static GLint s_locUseTex = -1;
static GLint s_locTexEnv = -1;
static GLint s_locAlphaTest = -1;
static GLint s_locAlphaRef = -1;

/* Texture filter tracking */
static unsigned char s_tpageFilter[52];
static unsigned char s_glTextureFilter[52];

/* Internal state snapshot */
typedef struct {
    int          textureId;    /* tpage index, or -1 for untextured */
    R_BlendMode  blendMode;
    int          depthTest;
    R_DepthFunc  depthFunc;
    int          depthWrite;
    R_TexEnvMode texEnv;
    R_FilterMode filter;
    R_CullMode   cullMode;
    int          alphaTest;
    float        alphaRef;
    int          scissorEnabled;
    int          scissorX, scissorY, scissorW, scissorH;
} R_StateSnapshot;

static R_StateSnapshot s_desired;
static R_StateSnapshot s_current;

#define R_STATE_STACK_DEPTH 4
static R_StateSnapshot s_stateStack[R_STATE_STACK_DEPTH];
static int s_stackDepth = 0;

static const R_StateSnapshot s_defaults = {
    .textureId      = -1,
    .blendMode      = R_BLEND_ALPHA,
    .depthTest      = 1,
    .depthFunc      = R_DEPTH_LEQUAL,
    .depthWrite     = 1,
    .texEnv         = R_TEXENV_MODULATE,
    .filter         = R_FILTER_NEAREST,
    .cullMode       = R_CULL_NONE,
    .alphaTest      = 1,
    .alphaRef       = 0.01f,
    .scissorEnabled = 0,
    .scissorX       = 0,
    .scissorY       = 0,
    .scissorW       = 640,
    .scissorH       = 480,
};

static void GL_ApplyTpageFilter(int tp)
{
    unsigned char mode = s_tpageFilter[tp];
    if (mode == 0) {
        mode = (unsigned char)(s_desired.filter + 1);
    }
    if (mode != s_glTextureFilter[tp]) {
        GLint minMag = (mode == (R_FILTER_LINEAR + 1)) ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minMag);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, minMag);
        s_glTextureFilter[tp] = mode;
    }
}

/* =====================================================================
 * Shaders
 * ===================================================================== */

static const char *s_vsSrc =
    "attribute vec4 a_pos;\n"
    "attribute vec4 a_color;\n"
    "attribute vec2 a_texcoord;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    v_color = a_color;\n"
    "    v_texcoord = a_texcoord;\n"
    "    gl_Position = u_mvp * a_pos;\n"
    "}\n";

static const char *s_fsSrc =
    "precision mediump float;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform int u_use_texture;\n"
    "uniform int u_tex_env;\n"
    "uniform int u_alpha_test;\n"
    "uniform float u_alpha_ref;\n"
    "void main() {\n"
    "    vec4 c = v_color;\n"
    "    if (u_use_texture != 0) {\n"
    "        vec4 tex = texture2D(u_texture, v_texcoord);\n"
    "        if (u_tex_env == 1) {\n"
    "            c.rgb = tex.rgb + c.rgb - 0.5;\n"
    "            c.a = tex.a * c.a;\n"
    "        } else {\n"
    "            c = tex * c;\n"
    "        }\n"
    "    }\n"
    "    if (u_alpha_test != 0) {\n"
    "        if (c.a <= u_alpha_ref) {\n"
    "            discard;\n"
    "        }\n"
    "    }\n"
    "    gl_FragColor = c;\n"
    "}\n";

static GLuint CompileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(s, sizeof(buf), NULL, buf);
        fprintf(stderr, "Shader compile error: %s\n", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

void R_GLES2_InitShaders(void)
{
    if (s_program) {
        return;
    }
    GLuint vs = CompileShader(GL_VERTEX_SHADER, s_vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, s_fsSrc);
    s_program = glCreateProgram();
    glAttachShader(s_program, vs);
    glAttachShader(s_program, fs);
    glBindAttribLocation(s_program, 0, "a_pos");
    glBindAttribLocation(s_program, 1, "a_color");
    glBindAttribLocation(s_program, 2, "a_texcoord");
    glLinkProgram(s_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glUseProgram(s_program);
    s_locMVP = glGetUniformLocation(s_program, "u_mvp");
    s_locTex = glGetUniformLocation(s_program, "u_texture");
    s_locUseTex = glGetUniformLocation(s_program, "u_use_texture");
    s_locTexEnv = glGetUniformLocation(s_program, "u_tex_env");
    s_locAlphaTest = glGetUniformLocation(s_program, "u_alpha_test");
    s_locAlphaRef = glGetUniformLocation(s_program, "u_alpha_ref");
    glUniform1i(s_locTex, 0);
}

void R_GLES2_UpdateMVP(int screenW, int screenH)
{
    if (!s_program) {
        R_GLES2_InitShaders();
    }
    float m[16];
    memset(m, 0, sizeof(m));
    m[0]  =  2.0f / (float)screenW;
    m[5]  = -2.0f / (float)screenH;
    m[10] =  2.0f;
    m[12] = -1.0f;
    m[13] =  1.0f;
    m[14] = -1.0f;
    m[15] =  1.0f;

    glUseProgram(s_program);
    glUniformMatrix4fv(s_locMVP, 1, GL_FALSE, m);
}

/* =====================================================================
 * Flush pending state
 * ===================================================================== */

void R_FlushState(void)
{
    if (!s_program) {
        R_GLES2_InitShaders();
    }
    glUseProgram(s_program);

    /* Texture binding */
    int hasTexture = 0;
    if (s_desired.textureId >= 0 && s_desired.textureId < 52) {
        int tp = s_desired.textureId;
        if (g_tpagePixelBuf[tp] != NULL) {
            if (s_glTextureDirty[tp]) {
                GL_UploadTpage(tp);
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_glTextures[tp]);
            GL_ApplyTpageFilter(tp);
            hasTexture = 1;
        }
    }
    glUniform1i(s_locUseTex, hasTexture);
    s_current.textureId = s_desired.textureId;

    /* Depth test */
    if (s_desired.depthTest != s_current.depthTest) {
        if (s_desired.depthTest) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        s_current.depthTest = s_desired.depthTest;
    }

    /* Depth function */
    if (s_desired.depthFunc != s_current.depthFunc) {
        switch (s_desired.depthFunc) {
            case R_DEPTH_LEQUAL: glDepthFunc(GL_LEQUAL); break;
            case R_DEPTH_LESS:   glDepthFunc(GL_LESS); break;
            case R_DEPTH_ALWAYS: glDepthFunc(GL_ALWAYS); break;
        }
        s_current.depthFunc = s_desired.depthFunc;
    }

    /* Depth write */
    if (s_desired.depthWrite != s_current.depthWrite) {
        glDepthMask(s_desired.depthWrite ? GL_TRUE : GL_FALSE);
        s_current.depthWrite = s_desired.depthWrite;
    }

    /* Texture environment (MODULATE vs ADD_SIGNED) */
    glUniform1i(s_locTexEnv, (s_desired.texEnv == R_TEXENV_ADD_SIGNED) ? 1 : 0);
    s_current.texEnv = s_desired.texEnv;

    /* Blend mode */
    if (s_desired.blendMode != s_current.blendMode) {
        switch (s_desired.blendMode) {
            case R_BLEND_NONE:
                glDisable(GL_BLEND);
                break;
            case R_BLEND_ALPHA:
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case R_BLEND_ADDITIVE:
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                break;
        }
        s_current.blendMode = s_desired.blendMode;
    }

    s_current.filter = s_desired.filter;
    glDisable(GL_CULL_FACE);

    /* Alpha test */
    glUniform1i(s_locAlphaTest, s_desired.alphaTest ? 1 : 0);
    glUniform1f(s_locAlphaRef, s_desired.alphaRef);
    s_current.alphaTest = s_desired.alphaTest;
    s_current.alphaRef = s_desired.alphaRef;

    /* Scissor enable/disable */
    if (s_desired.scissorEnabled != s_current.scissorEnabled) {
        if (s_desired.scissorEnabled) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        s_current.scissorEnabled = s_desired.scissorEnabled;
    }

    /* Scissor rect */
    if (s_desired.scissorEnabled &&
        (s_desired.scissorX != s_current.scissorX ||
         s_desired.scissorY != s_current.scissorY ||
         s_desired.scissorW != s_current.scissorW ||
         s_desired.scissorH != s_current.scissorH))
    {
        glScissor(s_desired.scissorX, s_desired.scissorY,
                  s_desired.scissorW, s_desired.scissorH);
        s_current.scissorX = s_desired.scissorX;
        s_current.scissorY = s_desired.scissorY;
        s_current.scissorW = s_desired.scissorW;
        s_current.scissorH = s_desired.scissorH;
    }
}

/* =====================================================================
 * State API implementation
 * ===================================================================== */

void R_SetTexture(int tpageIndex)   { s_desired.textureId = tpageIndex; }
void R_SetBlendMode(R_BlendMode m)  { s_desired.blendMode = m; }
void R_SetDepthTest(int enable)     { s_desired.depthTest = enable; }
void R_SetDepthFunc(R_DepthFunc f)  { s_desired.depthFunc = f; }
void R_SetDepthWrite(int enable)    { s_desired.depthWrite = enable; }
void R_SetTexEnv(R_TexEnvMode mode) { s_desired.texEnv = mode; }
void R_SetFilter(R_FilterMode mode) { s_desired.filter = mode; }

void R_SetTpageFilter(int tpage, R_FilterMode mode)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageFilter[tpage] = (unsigned char)(mode + 1);
    }
}

void R_ClearTpageFilter(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageFilter[tpage] = 0;
    }
}

void R_SetTpageSatBoost(int tpage, int k256)
{
    (void)tpage;
    (void)k256;
}

void R_SetCullMode(R_CullMode mode) { s_desired.cullMode = mode; }
void R_SetAlphaTest(int enable)     { s_desired.alphaTest = enable; }
void R_SetAlphaRef(float ref)       { s_desired.alphaRef = ref; }

void R_SetScissor(int x, int y, int w, int h)
{
    s_desired.scissorEnabled = 1;
    s_desired.scissorX = x;
    s_desired.scissorY = y;
    s_desired.scissorW = w;
    s_desired.scissorH = h;
}

void R_DisableScissor(void)
{
    s_desired.scissorEnabled = 0;
}

void R_PushState(void)
{
    if (s_stackDepth < R_STATE_STACK_DEPTH) {
        s_stateStack[s_stackDepth++] = s_desired;
    }
}

void R_PopState(void)
{
    if (s_stackDepth > 0) {
        s_desired = s_stateStack[--s_stackDepth];
    }
}

void R_DebugGetState(int *depthTest, int *depthWrite, int *depthFunc,
                     int *blendMode, int *alphaTest, float *alphaRef)
{
    if (depthTest)  *depthTest  = s_desired.depthTest;
    if (depthWrite) *depthWrite = s_desired.depthWrite;
    if (depthFunc)  *depthFunc  = (int)s_desired.depthFunc;
    if (blendMode)  *blendMode  = (int)s_desired.blendMode;
    if (alphaTest)  *alphaTest  = s_desired.alphaTest;
    if (alphaRef)   *alphaRef   = s_desired.alphaRef;
}

void R_ResetState(void)
{
    s_desired = s_defaults;
    memset(&s_current, 0xFF, sizeof(s_current));
    s_current.textureId = -99;
    s_current.alphaRef = -1.0f;
    memset(s_glTextureFilter, 0, sizeof(s_glTextureFilter));
    s_stackDepth = 0;
}

/* =====================================================================
 * Geometry submission
 * ===================================================================== */

typedef struct {
    float pos[4];
    float col[4];
    float uv[2];
} GLES2_Vertex;

void R_DrawTriFan(const RenderVertex *v, int count)
{
    if (count < 3) {
        return;
    }

    R_FlushState();

    GLES2_Vertex buf[64];
    if (count > 64) {
        count = 64;
    }

    for (int i = 0; i < count; i++) {
        float w = (v[i].rhw > 0.0f) ? (1.0f / v[i].rhw) : 1.0f;
        buf[i].pos[0] = v[i].sx * w;
        buf[i].pos[1] = v[i].sy * w;
        buf[i].pos[2] = v[i].sz * w;
        buf[i].pos[3] = w;

        uint32_t argb = v[i].color;
        buf[i].col[0] = (float)((argb >> 16) & 0xFF) / 255.0f;
        buf[i].col[1] = (float)((argb >>  8) & 0xFF) / 255.0f;
        buf[i].col[2] = (float)((argb      ) & 0xFF) / 255.0f;
        buf[i].col[3] = (float)((argb >> 24) & 0xFF) / 255.0f;

        buf[i].uv[0] = v[i].u;
        buf[i].uv[1] = v[i].v;
    }

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(GLES2_Vertex), buf[0].pos);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GLES2_Vertex), buf[0].col);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GLES2_Vertex), buf[0].uv);

    glDrawArrays(GL_TRIANGLE_FAN, 0, count);
}

void R_DrawTri(const RenderVertex v[3])
{
    R_DrawTriFan(v, 3);
}

void R_DrawQuad(const RenderVertex v[4])
{
    R_DrawTriFan(v, 4);
}

void R_DrawQuad2D(float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1,
                  float z, uint32_t color)
{
    float rhw = (z > 0.0f) ? (1.0f / z) : 1.0f;
    float farSafe = (g_farClipFloat > 0.0f) ? g_farClipFloat : 1.0f;
    float normZ = z / farSafe;

    RenderVertex v[4];
    v[0] = (RenderVertex){ x0, y0, normZ, rhw, color, 0, u0, v0 };
    v[1] = (RenderVertex){ x1, y0, normZ, rhw, color, 0, u1, v0 };
    v[2] = (RenderVertex){ x1, y1, normZ, rhw, color, 0, u1, v1 };
    v[3] = (RenderVertex){ x0, y1, normZ, rhw, color, 0, u0, v1 };
    R_DrawQuad(v);
}

void R_DrawQuad2DSolid(float x0, float y0, float x1, float y1,
                       float z, uint32_t color)
{
    int savedTex = s_desired.textureId;
    s_desired.textureId = -1;

    float rhw = (z > 0.0f) ? (1.0f / z) : 1.0f;
    float farSafe = (g_farClipFloat > 0.0f) ? g_farClipFloat : 1.0f;
    float normZ = z / farSafe;

    RenderVertex v[4];
    v[0] = (RenderVertex){ x0, y0, normZ, rhw, color, 0, 0.0f, 0.0f };
    v[1] = (RenderVertex){ x1, y0, normZ, rhw, color, 0, 0.0f, 0.0f };
    v[2] = (RenderVertex){ x1, y1, normZ, rhw, color, 0, 0.0f, 0.0f };
    v[3] = (RenderVertex){ x0, y1, normZ, rhw, color, 0, 0.0f, 0.0f };
    R_DrawQuad(v);

    s_desired.textureId = savedTex;
}

/* =====================================================================
 * Texture API forwarding
 * ===================================================================== */

void R_InitTextures(void) { GL_InitTextures(); }
void R_UploadTexture(int tpage) { GL_UploadTpage(tpage); }
void R_MarkTextureDirty(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_glTextureFilter[tpage] = 0;
    }
    GL_MarkTpageDirty(tpage);
}
void R_ClearTextureDirty(int tpage) { GL_ClearTpageDirty(tpage); }
void R_FreezeTexture(int tpage) { GL_FreezeTpage(tpage); }
void R_ThawTexture(int tpage) { (void)tpage; }
void R_SetNoColorKey(int tpage) { GL_SetNoColorKey(tpage); }
void R_ClearNoColorKey(int tpage) { GL_ClearNoColorKey(tpage); }
void R_SetTpageGreen6(int tpage, int on) { GL_SetTpageGreen6(tpage, on); }
void R_SetTpageRGBA8(int tpage, unsigned char *rgba) { GL_SetTpageRGBA8(tpage, rgba); }
void R_UploadTextureRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    GL_UploadTpageRGBA(tpage, rgba, w, h);
}
void R_UploadTextureSubRect(int tpage, int x, int y, int w, int h)
{
    GL_UploadTpageSubRect(tpage, x, y, w, h);
}
void R_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    GL_SetPendingRGBA(tpage, rgba, w, h);
}

void R_EndFrame(void)
{
    EndFrame();
}

void R_Flip(void)
{
    FlipD3D();
}

void R_ClearAndReset(void)
{
    ProcessTpageStates();
}

void R_ClearDepth(void)
{
    R_FlushState();
    glClear(GL_DEPTH_BUFFER_BIT);
}
