#include "stdafx.h"
#include "Renderer.h"
#include "Shared.h"

static const int FONT_W = 8;
static const int FONT_H = 16;
static const int FONT_COLS = 16;
static const int FONT_ROWS = 8;

// 8x16 bitmap font data from FSD build/utils/font8x16.inc
#include "font8x16.inc"

struct FontVert {
    float x, y, z, w;
    D3DCOLOR color;
    float u, v;
};
static const DWORD FontFVF = D3DFVF_XYZW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct BgVert {
    float x, y, z, w;
    D3DCOLOR color;
};
static const DWORD BgFVF = D3DFVF_XYZW | D3DFVF_DIFFUSE;

void CRenderer::BuildFontTexture() {
    if (!m_dev) return;
    m_dev->CreateTexture(128, 128, 1, 0, D3DFMT_A8R8G8B8, 0, &m_fontTex, NULL);
    if (!m_fontTex) return;
    D3DLOCKED_RECT lr;
    m_fontTex->LockRect(0, &lr, NULL, 0);
    DWORD *pixels = (DWORD*)lr.pBits;

    for (int row = 0; row < FONT_ROWS; row++) {
        for (int col = 0; col < FONT_COLS; col++) {
            int ch = row * FONT_COLS + col;
            for (int y = 0; y < FONT_H; y++) {
                int byteY = ch * FONT_H + y;
                if (byteY >= sizeof(fontdata)) break;
                unsigned char b = fontdata[byteY];
                for (int x = 0; x < FONT_W; x++) {
                    int px = col * FONT_W + x;
                    int py = row * FONT_H + y;
                    if (b & (1 << (7 - x))) {
                        pixels[py * (lr.Pitch / 4) + px] = 0xFFFFFFFF;
                    } else {
                        pixels[py * (lr.Pitch / 4) + px] = 0x00000000;
                    }
                }
            }
        }
    }
    m_fontTex->UnlockRect(0);
}

bool CRenderer::Init(D3DDevice *dev, int screenW, int screenH) {
    m_dev = dev;
    m_screenW = screenW;
    m_screenH = screenH;
    if (!m_dev) return false;

    BuildFontTexture();

    // BG vertex buffer (full-screen quad)
    m_dev->CreateVertexBuffer(4 * sizeof(BgVert), 0, BgFVF, D3DPOOL_DEFAULT, &m_bgVb, NULL);
    BgVert *v = NULL;
    m_bgVb->Lock(0, 0, (void**)&v, 0);
    v[0].x = -1.0f; v[0].y = -1.0f; v[0].z = 0.5f; v[0].w = 1.0f; v[0].color = 0x80000000;
    v[1].x =  1.0f; v[1].y = -1.0f; v[1].z = 0.5f; v[1].w = 1.0f; v[1].color = 0x80000000;
    v[2].x = -1.0f; v[2].y =  1.0f; v[2].z = 0.5f; v[2].w = 1.0f; v[2].color = 0x80000000;
    v[3].x =  1.0f; v[3].y =  1.0f; v[3].z = 0.5f; v[3].w = 1.0f; v[3].color = 0x80000000;
    m_bgVb->Unlock();

    // Font vertex buffer (enough for 80 chars * 6 verts)
    m_dev->CreateVertexBuffer(80 * 6 * sizeof(FontVert), 0, FontFVF, D3DPOOL_DEFAULT, &m_fontVb, NULL);

    m_init = true;
    return true;
}

void CRenderer::DrawChar(int x, int y, char c) {
    if (!m_fontVb || !m_fontTex) return;
    int ch = (unsigned char)c;
    int texu = (ch % FONT_COLS) * FONT_W;
    int texv = (ch / FONT_COLS) * FONT_H;

    float sx = (float)x / (float)m_screenW * 2.0f - 1.0f;
    float sy = 1.0f - (float)y / (float)m_screenH * 2.0f;
    float sw = (float)FONT_W / (float)m_screenW * 2.0f;
    float sh = (float)FONT_H / (float)m_screenH * 2.0f;

    FontVert *pv = NULL;
    m_fontVb->Lock(0, 6 * sizeof(FontVert), (void**)&pv, 0);
    float u0 = (float)texu / 128.0f;
    float v0 = (float)texv / 128.0f;
    float u1 = (float)(texu + FONT_W) / 128.0f;
    float v1 = (float)(texv + FONT_H) / 128.0f;

    // Tri 1
    pv[0].x = sx;    pv[0].y = sy;     pv[0].z = 0; pv[0].w = 1; pv[0].color = 0xFFFFFFFF; pv[0].u = u0; pv[0].v = v0;
    pv[1].x = sx+sw; pv[1].y = sy;     pv[1].z = 0; pv[1].w = 1; pv[1].color = 0xFFFFFFFF; pv[1].u = u1; pv[1].v = v0;
    pv[2].x = sx;    pv[2].y = sy-sh;  pv[2].z = 0; pv[2].w = 1; pv[2].color = 0xFFFFFFFF; pv[2].u = u0; pv[2].v = v1;
    // Tri 2
    pv[3].x = sx+sw; pv[3].y = sy;     pv[3].z = 0; pv[3].w = 1; pv[3].color = 0xFFFFFFFF; pv[3].u = u1; pv[3].v = v0;
    pv[4].x = sx+sw; pv[4].y = sy-sh;  pv[4].z = 0; pv[4].w = 1; pv[4].color = 0xFFFFFFFF; pv[4].u = u1; pv[4].v = v1;
    pv[5].x = sx;    pv[5].y = sy-sh;  pv[5].z = 0; pv[5].w = 1; pv[5].color = 0xFFFFFFFF; pv[5].u = u0; pv[5].v = v1;
    m_fontVb->Unlock();

    m_dev->SetStreamSource(0, m_fontVb, 0, sizeof(FontVert));
    m_dev->SetFVF(FontFVF);
    m_dev->SetTexture(0, m_fontTex);
    m_dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    m_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    m_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
}

void CRenderer::DrawString(int x, int y, const char *s) {
    int cx = x;
    int py = y;
    while (*s) {
        if (*s == '\n') { cx = x; py += FONT_H; }
        else { DrawChar(cx, py, *s); cx += FONT_W; }
        s++;
    }
}

void CRenderer::Render() {
    if (!m_init) return;

    m_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    m_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    m_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    // Draw BG quad
    m_dev->SetStreamSource(0, m_bgVb, 0, sizeof(BgVert));
    m_dev->SetFVF(BgFVF);
    m_dev->SetTexture(0, NULL);
    m_dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

    // Draw text
    char buf[256];
    sprintf_s(buf, "XeFTP  v1.0");
    DrawString(8, 8, buf);

    sprintf_s(buf, "IP: %s : %d", g_ip.c_str(), g_port);
    DrawString(8, 28, buf);

    sprintf_s(buf, "Connections: %d", g_connCount);
    DrawString(8, 44, buf);

    sprintf_s(buf, "Write Protect: %s", g_writeProtect ? "ON" : "OFF");
    DrawString(8, 60, buf);

    int lc = LogCount();
    int ly = 80;
    for (int i = 0; i < lc; i++) {
        DrawString(8, ly, LogGet(i));
        ly += FONT_H;
        if (ly > m_screenH - 20) break;
    }

    sprintf_s(buf, "[Back] exit  [X] toggle write protect");
    DrawString(8, m_screenH - 20, buf);
}

void CRenderer::Shutdown() {
    if (m_fontVb) { m_fontVb->Release(); m_fontVb = NULL; }
    if (m_bgVb)   { m_bgVb->Release(); m_bgVb = NULL; }
    if (m_fontTex) { m_fontTex->Release(); m_fontTex = NULL; }
    m_dev = NULL;
    m_init = false;
}
