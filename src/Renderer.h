#pragma once
#include "stdafx.h"

class CRenderer {
private:
    CRenderer() : m_dev(NULL), m_fontTex(NULL), m_fontVb(NULL), m_bgVb(NULL),
        m_screenW(640), m_screenH(480) {}
    ~CRenderer() {}
    CRenderer(const CRenderer&);
    CRenderer& operator=(const CRenderer&);

    D3DDevice *m_dev;
    D3DTexture *m_fontTex;
    D3DVertexBuffer *m_fontVb;
    D3DVertexBuffer *m_bgVb;
    int m_screenW, m_screenH;
    bool m_init;

    void BuildFontTexture();
    void DrawChar(int x, int y, char c);
    void DrawString(int x, int y, const char *s);

public:
    static CRenderer& Instance() {
        static CRenderer r;
        return r;
    }

    bool Init(D3DDevice *dev, int screenW, int screenH);
    void Render();
    void Shutdown();
};
