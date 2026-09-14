/**
 * render_gles2.c — OpenGL ES 2.0 rendering backend (Nintendo Switch)
 *
 * Replaces Direct3D / OpenGL 1.x rendering calls with GLES2 shader pipeline.
 */

#include <GLES2/gl2.h>
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "platform.h"
#include "net_transport.h"
#include "endian_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern void R_GLES2_InitShaders(void);
extern void R_GLES2_UpdateMVP(int screenW, int screenH);

/* Viewport dimensions */
int g_glBackingWidth = 640;
int g_glBackingHeight = 480;
int g_glViewportOffsetX = 0;
int g_glViewportOffsetY = 0;

static uint16_t __attribute__((aligned(32))) s_rgba4Storage[1024 * 256];

GLuint s_glTextures[52];
static int s_glTexturesInited = 0;
int s_glTextureDirty[52];
static int s_tpageKeepPixels[52];
static int s_tpageNoColorKey[52];
static int s_tpageGreen6[52];
static unsigned char *s_pendingRGBA[52];
static int s_pendingRGBAWidth[52];
static int s_pendingRGBAHeight[52];
static unsigned char *s_tpageRGBA8Buf[52];

void GL_InitTextures(void)
{
    if (s_glTexturesInited) {
        return;
    }
    glGenTextures(52, s_glTextures);
    for (int i = 0; i < 52; i++) {
        s_glTextureDirty[i] = 1;
    }
    s_glTexturesInited = 1;
}

void GL_UploadTpage(int tpage)
{
    if (!s_glTexturesInited) {
        GL_InitTextures();
    }

    unsigned short *pixels = (unsigned short *)g_tpagePixelBuf[tpage];
    if (pixels == NULL) {
        return;
    }

    int w = g_tpageWidth[tpage];
    int h = g_tpageHeight[tpage];
    if (w == 0) w = 256;
    if (h == 0) h = 256;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_glTextures[tpage]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (s_tpageRGBA8Buf[tpage]) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, s_tpageRGBA8Buf[tpage]);
        s_glTextureDirty[tpage] = 0;
        return;
    }

    if (s_pendingRGBA[tpage]) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     s_pendingRGBAWidth[tpage], s_pendingRGBAHeight[tpage], 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, s_pendingRGBA[tpage]);
        free(s_pendingRGBA[tpage]);
        s_pendingRGBA[tpage] = NULL;
        s_glTextureDirty[tpage] = 0;
        return;
    }

    int count = w * h;
    uint16_t *out = s_rgba4Storage;
    if (s_tpageNoColorKey[tpage]) {
        int green6 = s_tpageGreen6[tpage];
        for (int i = 0; i < count; i++) {
            unsigned short p = pixels[i];
            unsigned char r5 = (p >> 11) & 0x1F;
            unsigned char b5 = p & 0x1F;
            unsigned char g6;
            if (green6) {
                g6 = (p >> 5) & 0x3F;
            } else {
                unsigned char g5 = (p >> 6) & 0x1F;
                g6 = (g5 << 1) | (g5 >> 4);
            }
            out[i] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                     GL_RGB, GL_UNSIGNED_SHORT_5_6_5, out);
    } else {
        for (int i = 0; i < count; i++) {
            unsigned short p = pixels[i];
            unsigned char r5 = (p >> 11) & 0x1F;
            unsigned char g5 = (p >> 6) & 0x1F;
            unsigned char b5 = p & 0x1F;
            unsigned char a1 = IS_COLOR_KEY_RGB5(r5, g5, b5) ? 0 : 1;
            out[i] = (uint16_t)((r5 << 11) | (g5 << 6) | (b5 << 1) | a1);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, out);
    }

    s_glTextureDirty[tpage] = 0;
}

void GL_UploadTpageRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    if (!s_glTexturesInited) {
        GL_InitTextures();
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_glTextures[tpage]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    s_glTextureDirty[tpage] = 0;
}

void GL_SetTpageRGBA8(int tpage, unsigned char *rgba)
{
    if (tpage < 0 || tpage >= 52) {
        return;
    }
    if (s_tpageRGBA8Buf[tpage] && s_tpageRGBA8Buf[tpage] != rgba) {
        free(s_tpageRGBA8Buf[tpage]);
    }
    s_tpageRGBA8Buf[tpage] = rgba;
    s_glTextureDirty[tpage] = 1;
}

void GL_MarkTpageDirty(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_glTextureDirty[tpage] = 1;
    }
}

void GL_FreezeTpage(int tpage)
{
    if (!s_glTexturesInited) {
        GL_InitTextures();
    }
    if (tpage >= 0 && tpage < 52 && g_tpagePixelBuf[tpage] != NULL) {
        GL_UploadTpage(tpage);
    }
}

void GL_ClearTpageDirty(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_glTextureDirty[tpage] = 0;
    }
}

void GL_UploadTpageSubRect(int tpage, int destX, int destY, int width, int height)
{
    if (!s_glTexturesInited) {
        GL_InitTextures();
    }
    if (tpage < 0 || tpage >= 52) {
        return;
    }

    unsigned short *pixels = (unsigned short *)g_tpagePixelBuf[tpage];
    if (pixels == NULL) {
        return;
    }

    int tpageW = g_tpageWidth[tpage];
    if (tpageW <= 0) {
        tpageW = 256;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_glTextures[tpage]);
    uint16_t *out = s_rgba4Storage;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            unsigned short p = pixels[(destY + row) * tpageW + (destX + col)];
            unsigned char r5 = (p >> 11) & 0x1F;
            unsigned char g5 = (p >> 6) & 0x1F;
            unsigned char b5 = p & 0x1F;
            unsigned char a1 = IS_COLOR_KEY_RGB5(r5, g5, b5) ? 0 : 1;
            out[row * width + col] = (uint16_t)((r5 << 11) | (g5 << 6) | (b5 << 1) | a1);
        }
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, destX, destY, width, height,
                    GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, out);
}

void GL_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    if (tpage < 0 || tpage >= 52) {
        free(rgba);
        return;
    }
    if (s_pendingRGBA[tpage]) {
        free(s_pendingRGBA[tpage]);
    }
    s_pendingRGBA[tpage] = rgba;
    s_pendingRGBAWidth[tpage] = w;
    s_pendingRGBAHeight[tpage] = h;
    s_glTextureDirty[tpage] = 1;
}

void BeginFrame(void)
{
    if (g_vertexArrayBase) {
        SrcVertex *v = g_vertexArrayBase;
        for (int i = 0; i < 32768; i++) {
            v->depth = 0;
            v++;
        }
    }

    if (!s_glTexturesInited) {
        GL_InitTextures();
    }

    R_GLES2_InitShaders();

    int fullW, fullH;
    platform_get_drawable_size(&fullW, &fullH);

    int vpW, vpH;
    if (fullW * 3 > fullH * 4) {
        vpH = fullH;
        vpW = (fullH * 4) / 3;
    } else {
        vpW = fullW;
        vpH = (fullW * 3) / 4;
    }
    int offsetX = (fullW - vpW) / 2;
    int offsetY = (fullH - vpH) / 2;

    g_glViewportOffsetX = offsetX;
    g_glViewportOffsetY = offsetY;
    g_glBackingWidth = vpW;
    g_glBackingHeight = vpH;
    glViewport(offsetX, offsetY, vpW, vpH);

    R_GLES2_UpdateMVP(g_screenWidth, g_screenHeight);

    R_ResetState();
}

void EndFrame(void)
{
    glFlush();
}

void FlipD3D(void)
{
    platform_gl_swap();
}

void SetViewportFromConfig(int *config)
{
    if (config == NULL) {
        return;
    }

    g_clipLeft = config[0];
    g_clipLeftDouble = g_clipLeft * 2;
    g_clipTop = config[1];
    g_clipRight = config[2];
    g_clipBottom = config[3];
    g_projScaleX = config[4];
    g_projScaleXCurrent = config[5];
    g_projScaleY = config[6];
    g_screenCenterX = config[7];
    g_screenCenterY = config[8];
    g_screenWidthFull = config[9];
    g_screenHeightFull = config[0xa];
    g_vpClipLeft10 = config[0xb];
    g_vpClipRight10 = config[0xc];
    g_vpClipLeft16 = config[0xd];
    g_vpClipRight16 = config[0xe];
    g_vpParam0F = config[0xf];
    g_vpParam10 = config[0x10];
}

void ProcessTpageStates(void)
{
    int changed;

    R_DisableScissor();
    R_SetDepthWrite(1);
    R_FlushState();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    R_SetScissor(g_glViewportOffsetX, g_glViewportOffsetY,
                 g_glBackingWidth, g_glBackingHeight);
    R_FlushState();

    do {
        changed = 0;
        for (int i = 0; i < 0x34; i++) {
            unsigned char state = (unsigned char)g_tpageStateArray[i];
            if (state > 7) {
                continue;
            }

            switch (state) {
                case 6:
                case 7:
                    if (state == 6) {
                        if (g_tpagePixelBuf[i] != NULL) {
                            free(g_tpagePixelBuf[i]);
                            g_tpagePixelBuf[i] = NULL;
                        }
                        s_glTextureDirty[i] = 1;
                        if (s_pendingRGBA[i] != NULL) {
                            free(s_pendingRGBA[i]);
                            s_pendingRGBA[i] = NULL;
                        }
                    }
                    g_tpageStateArray[i] = 0;
                    break;

                case 1:
                case 2:
                    g_tpageStateArray[i] = 3;
                    changed = 1;
                    break;

                case 3:
                    g_tpageStateArray[i] = 4;
                    changed = 1;
                    break;

                case 5:
                    g_tpageStateArray[i] = 4;
                    break;

                case 0:
                case 4:
                default:
                    break;
            }
        }
    } while (changed);
}

void CleanupD3DTPages(void)
{
    for (int i = 0; i < 0x34; i++) {
        char state = g_tpageStateArray[i];
        if (state == 0 || state == 5 || i == TPAGE_PLATFORM_ICONS) {
            continue;
        }
        g_tpageStateArray[i] = 6;
    }
    ProcessTpageStates();
}

void RenderBackground(void)
{
    R_SetScissor(g_glViewportOffsetX, g_glViewportOffsetY,
                 g_glBackingWidth, g_glBackingHeight);
    R_FlushState();

    if (g_introCountdown <= 0xD2 && g_introCountdown >= 0) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        R_DisableScissor();
        R_FlushState();
        return;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    R_DisableScissor();
    R_FlushState();
}

void RenderWavingMenuBackground(void)
{
    int tpage = g_uiTexPage;
    if (tpage < 0 || tpage >= 52 || g_tpageStateArray[tpage] != 4 || g_tpagePixelBuf[tpage] == NULL) {
        return;
    }

    if (!s_glTexturesInited) {
        GL_InitTextures();
    }
    if (s_glTextureDirty[tpage]) {
        GL_UploadTpage(tpage);
    }

    struct IntVert { int sx, sy, uvU, uvV; };
    struct IntVert buf[8][8];

    int sinAngle = (g_totalFrames & 0x7F) << 5;
    int yWorld   = 0x483;
    int uvV      = 0x8000;

    for (int row = 0; row < 8; row++) {
        int vertAngle = sinAngle;
        int xWorld    = -0x604;
        int uvU       = 0x8000;

        for (int col = 0; col < 8; col++) {
            int depth = 0x8CA - (g_sinTable[vertAngle] >> 7);
            buf[row][col].sx = g_screenCenterX + (g_projScaleXCurrent * xWorld) / depth;
            buf[row][col].sy = g_screenCenterY - (g_projScaleY * yWorld) / depth;
            buf[row][col].uvU = uvU;
            buf[row][col].uvV = uvV;

            vertAngle = (vertAngle - 0x14D) & 0xFFF;
            xWorld += 0x1B8;
            uvU += 0x246DB6;
        }

        sinAngle = (sinAngle - 0xDE) & 0xFFF;
        yWorld -= 0x14A;
        uvV += 0x246DB6;
    }

    R_PushState();
    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);
    R_SetBlendMode(R_BLEND_ALPHA);
    R_FlushState();

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 7; col++) {
            const struct IntVert *src[4] = {
                &buf[row][col],
                &buf[row][col + 1],
                &buf[row + 1][col + 1],
                &buf[row + 1][col]
            };

            RenderVertex rv[4];
            for (int k = 0; k < 4; k++) {
                rv[k].sx = (float)src[k]->sx;
                rv[k].sy = (float)src[k]->sy;
                rv[k].sz = 0.5f;
                rv[k].rhw = 1.0f;
                rv[k].color = VERTEX_WHITE;
                rv[k].specular = 0;
                rv[k].u = g_uvLUT256[src[k]->uvU >> 16];
                rv[k].v = g_uvLUT256[src[k]->uvV >> 16];
            }
            R_DrawQuad(rv);
        }
    }

    R_PopState();
}

void GL_SetNoColorKey(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageNoColorKey[tpage] = 1;
        s_glTextureDirty[tpage] = 1;
    }
}

void GL_ClearNoColorKey(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageNoColorKey[tpage] = 0;
        s_glTextureDirty[tpage] = 1;
    }
}

void GL_SetTpageGreen6(int tpage, int on)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageGreen6[tpage] = on ? 1 : 0;
    }
}

void GL_KeepPixels(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageKeepPixels[tpage] = 1;
    }
}

void FinalizeMenuTexturesD3D(void)
{
    for (int i = 0; i < 52; i++) {
        if (g_tpagePixelBuf[i] != NULL) {
            s_glTextureDirty[i] = 1;
        }
    }
}
