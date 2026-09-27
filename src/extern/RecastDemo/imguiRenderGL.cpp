//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>
#include "../../include/RecastDemo/imgui.h"
#include "../../include/RecastDemo/imguiRenderGL.h"
#include "SDL.h"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/render/CoreProfileDraw.h"

// Some math headers don't have PI defined.
static const float PI = 3.14159265f;

void imguifree(void* ptr, void* userptr);
void* imguimalloc(size_t size, void* userptr);

#define STBTT_malloc(x, y) imguimalloc(x, y)
#define STBTT_free(x, y) imguifree(x, y)
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../extern/stb_truetype.h"

void imguifree(void* ptr, void* /*userptr*/) {
    free(ptr);
}

void* imguimalloc(size_t size, void* /*userptr*/) {
    return malloc(size);
}

static const unsigned TEMP_COORD_COUNT = 100;
static float g_tempCoords[TEMP_COORD_COUNT * 2];
static float g_tempNormals[TEMP_COORD_COUNT * 2];

static const int CIRCLE_VERTS = 8 * 4;
static float g_circleVerts[CIRCLE_VERTS * 2];

static stbtt_bakedchar g_cdata[96]; // ASCII 32..126 is 95 glyphs
static GLuint g_ftex = 0;
static stbtt_bakedchar g_worldCdata[96];
static GLuint g_worldFtex = 0;
static const int WORLD_FONT_TEXTURE_SIZE = 1024;
static constexpr float MAX_WORLD_TEXT_DISTANCE_SQUARED = 100.0f * 100.0f;
static std::vector<CoreProfileVertex> g_vertices;

static glm::mat4 uiProjection() {
    const Renderer& renderer = Renderer::getInstance();
    return glm::ortho(0.0f, static_cast<float>(renderer.width), 0.0f, static_cast<float>(renderer.height));
}

static glm::vec4 unpackColor(unsigned int color) {
    constexpr float invByte = 1.0f / 255.0f;
    return {static_cast<float>(color & 0xff) * invByte, static_cast<float>((color >> 8) & 0xff) * invByte,
        static_cast<float>((color >> 16) & 0xff) * invByte, static_cast<float>((color >> 24) & 0xff) * invByte};
}

static CoreProfileVertex makeUiVertex(float x, float y, const glm::vec4& color, float u = 0.0f, float v = 0.0f) {
    return {{x, y, 0.0f}, {0.0f, 0.0f, 1.0f}, color, {u, v}};
}

static CoreProfileVertex makeWorldTextVertex(float x, float y, float z, const glm::vec4& color, float u, float v) {
    return {{x, y, z}, {0.0f, 0.0f, 1.0f}, color, {u, v}};
}

static void drawPending() {
    if (g_vertices.empty())
        return;
    const glm::mat4 identity(1.0f);
    drawCoreProfileVertices(g_vertices, GL_TRIANGLES, identity, identity, uiProjection(), 0, false, false);
    g_vertices.clear();
}

static void drawPolygon(const float* coords, unsigned numCoords, float r, unsigned int col) {
    if (numCoords > TEMP_COORD_COUNT)
        numCoords = TEMP_COORD_COUNT;

    for (unsigned i = 0, j = numCoords - 1; i < numCoords; j = i++) {
        const float* v0 = &coords[j * 2];
        const float* v1 = &coords[i * 2];
        float dx = v1[0] - v0[0];
        float dy = v1[1] - v0[1];
        float d = sqrtf(dx * dx + dy * dy);
        if (d > 0) {
            d = 1.0f / d;
            dx *= d;
            dy *= d;
        }
        g_tempNormals[j * 2 + 0] = dy;
        g_tempNormals[j * 2 + 1] = -dx;
    }

    for (unsigned i = 0, j = numCoords - 1; i < numCoords; j = i++) {
        float dlx0 = g_tempNormals[j * 2 + 0];
        float dly0 = g_tempNormals[j * 2 + 1];
        float dlx1 = g_tempNormals[i * 2 + 0];
        float dly1 = g_tempNormals[i * 2 + 1];
        float dmx = (dlx0 + dlx1) * 0.5f;
        float dmy = (dly0 + dly1) * 0.5f;
        float dmr2 = dmx * dmx + dmy * dmy;
        if (dmr2 > 0.000001f) {
            float scale = 1.0f / dmr2;
            if (scale > 10.0f)
                scale = 10.0f;
            dmx *= scale;
            dmy *= scale;
        }
        g_tempCoords[i * 2 + 0] = coords[i * 2 + 0] + dmx * r;
        g_tempCoords[i * 2 + 1] = coords[i * 2 + 1] + dmy * r;
    }

    const glm::vec4 fillColor = unpackColor(col);
    const glm::vec4 transparentColor(fillColor.r, fillColor.g, fillColor.b, 0.0f);

    for (unsigned i = 0, j = numCoords - 1; i < numCoords; j = i++) {
        g_vertices.push_back(makeUiVertex(coords[i * 2], coords[i * 2 + 1], fillColor));
        g_vertices.push_back(makeUiVertex(coords[j * 2], coords[j * 2 + 1], fillColor));
        g_vertices.push_back(makeUiVertex(g_tempCoords[j * 2], g_tempCoords[j * 2 + 1], transparentColor));

        g_vertices.push_back(makeUiVertex(g_tempCoords[j * 2], g_tempCoords[j * 2 + 1], transparentColor));
        g_vertices.push_back(makeUiVertex(g_tempCoords[i * 2], g_tempCoords[i * 2 + 1], transparentColor));

        g_vertices.push_back(makeUiVertex(coords[i * 2], coords[i * 2 + 1], fillColor));
    }

    for (unsigned i = 2; i < numCoords; ++i) {
        g_vertices.push_back(makeUiVertex(coords[0], coords[1], fillColor));
        g_vertices.push_back(makeUiVertex(coords[(i - 1) * 2], coords[(i - 1) * 2 + 1], fillColor));
        g_vertices.push_back(makeUiVertex(coords[i * 2], coords[i * 2 + 1], fillColor));
    }
}

static void drawRect(float x, float y, float w, float h, float fth, unsigned int col) {
    float verts[4 * 2] = {
        x + 0.5f,
        y + 0.5f,
        x + w - 0.5f,
        y + 0.5f,
        x + w - 0.5f,
        y + h - 0.5f,
        x + 0.5f,
        y + h - 0.5f,
    };
    drawPolygon(verts, 4, fth, col);
}

/*
static void drawEllipse(float x, float y, float w, float h, float fth, unsigned int col)
{
    float verts[CIRCLE_VERTS*2];
    const float* cverts = g_circleVerts;
    float* v = verts;

    for (int i = 0; i < CIRCLE_VERTS; ++i)
    {
        *v++ = x + cverts[i*2]*w;
        *v++ = y + cverts[i*2+1]*h;
    }

    drawPolygon(verts, CIRCLE_VERTS, fth, col);
}
*/

static void drawRoundedRect(float x, float y, float w, float h, float r, float fth, unsigned int col) {
    const unsigned n = CIRCLE_VERTS / 4;
    float verts[(n + 1) * 4 * 2];
    const float* cverts = g_circleVerts;
    float* v = verts;

    for (unsigned i = 0; i <= n; ++i) {
        *v++ = x + w - r + cverts[i * 2] * r;
        *v++ = y + h - r + cverts[i * 2 + 1] * r;
    }

    for (unsigned i = n; i <= n * 2; ++i) {
        *v++ = x + r + cverts[i * 2] * r;
        *v++ = y + h - r + cverts[i * 2 + 1] * r;
    }

    for (unsigned i = n * 2; i <= n * 3; ++i) {
        *v++ = x + r + cverts[i * 2] * r;
        *v++ = y + r + cverts[i * 2 + 1] * r;
    }

    for (unsigned i = n * 3; i < n * 4; ++i) {
        *v++ = x + w - r + cverts[i * 2] * r;
        *v++ = y + r + cverts[i * 2 + 1] * r;
    }
    *v++ = x + w - r + cverts[0] * r;
    *v++ = y + r + cverts[1] * r;

    drawPolygon(verts, (n + 1) * 4, fth, col);
}

static void drawLine(float x0, float y0, float x1, float y1, float r, float fth, unsigned int col) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float d = sqrtf(dx * dx + dy * dy);
    if (d > 0.0001f) {
        d = 1.0f / d;
        dx *= d;
        dy *= d;
    }
    float nx = dy;
    float ny = -dx;
    float verts[4 * 2];
    r -= fth;
    r *= 0.5f;
    if (r < 0.01f)
        r = 0.01f;
    dx *= r;
    dy *= r;
    nx *= r;
    ny *= r;

    verts[0] = x0 - dx - nx;
    verts[1] = y0 - dy - ny;

    verts[2] = x0 - dx + nx;
    verts[3] = y0 - dy + ny;

    verts[4] = x1 + dx + nx;
    verts[5] = y1 + dy + ny;

    verts[6] = x1 + dx - nx;
    verts[7] = y1 + dy - ny;

    drawPolygon(verts, 4, fth, col);
}

bool imguiRenderGLInit(const char* fontpath) {
    for (int i = 0; i < CIRCLE_VERTS; ++i) {
        float a = (float)i / (float)CIRCLE_VERTS * PI * 2;
        g_circleVerts[i * 2 + 0] = cosf(a);
        g_circleVerts[i * 2 + 1] = sinf(a);
    }

    // Load font.
    FILE* fp = fopen(fontpath, "rb");
    if (!fp)
        return false;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return false;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    unsigned char* ttfBuffer = (unsigned char*)malloc(size);
    if (!ttfBuffer) {
        fclose(fp);
        return false;
    }

    size_t readLen = fread(ttfBuffer, 1, size, fp);
    fclose(fp);
    if (readLen != static_cast<size_t>(size)) {
        free(ttfBuffer);
        return false;
    }

    fp = 0;

    unsigned char* bmap = (unsigned char*)malloc(512 * 512);
    if (!bmap) {
        free(ttfBuffer);
        return false;
    }

    stbtt_BakeFontBitmap(ttfBuffer, 0, 15.0f, bmap, 512, 512, 32, 96, g_cdata);

    // can free ttf_buffer at this point
    glGenTextures(1, &g_ftex);
    glBindTexture(GL_TEXTURE_2D, g_ftex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 512, 512, 0, GL_RED, GL_UNSIGNED_BYTE, bmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    unsigned char* worldBitmap = (unsigned char*)malloc(WORLD_FONT_TEXTURE_SIZE * WORLD_FONT_TEXTURE_SIZE);
    if (!worldBitmap) {
        free(ttfBuffer);
        free(bmap);
        glDeleteTextures(1, &g_ftex);
        g_ftex = 0;
        return false;
    }
    if (stbtt_BakeFontBitmap(ttfBuffer, 0, 72.0f, worldBitmap, WORLD_FONT_TEXTURE_SIZE, WORLD_FONT_TEXTURE_SIZE, 32, 96,
            g_worldCdata) <= 0) {
        free(ttfBuffer);
        free(bmap);
        free(worldBitmap);
        glDeleteTextures(1, &g_ftex);
        g_ftex = 0;
        return false;
    }
    glGenTextures(1, &g_worldFtex);
    glBindTexture(GL_TEXTURE_2D, g_worldFtex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, WORLD_FONT_TEXTURE_SIZE, WORLD_FONT_TEXTURE_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE,
        worldBitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    free(ttfBuffer);
    free(bmap);
    free(worldBitmap);

    return true;
}

void imguiRenderGLDestroy() {
    if (g_ftex) {
        glDeleteTextures(1, &g_ftex);
        g_ftex = 0;
    }
    if (g_worldFtex) {
        glDeleteTextures(1, &g_worldFtex);
        g_worldFtex = 0;
    }
}

static void getBakedQuad(
    stbtt_bakedchar* chardata, int pw, int ph, int char_index, float* xpos, float* ypos, stbtt_aligned_quad* q) {
    stbtt_bakedchar* b = chardata + char_index;
    int round_x = STBTT_ifloor(*xpos + b->xoff);
    int round_y = STBTT_ifloor(*ypos - b->yoff);

    q->x0 = (float)round_x;
    q->y0 = (float)round_y;
    q->x1 = (float)round_x + b->x1 - b->x0;
    q->y1 = (float)round_y - b->y1 + b->y0;

    q->s0 = b->x0 / (float)pw;
    q->t0 = b->y0 / (float)pw;
    q->s1 = b->x1 / (float)ph;
    q->t1 = b->y1 / (float)ph;

    *xpos += b->xadvance;
}

static const float g_tabStops[4] = {150, 210, 270, 330};

static float getTextLength(stbtt_bakedchar* chardata, const char* text) {
    float xpos = 0;
    float len = 0;
    while (*text) {
        int c = (unsigned char)*text;
        if (c == '\t') {
            for (int i = 0; i < 4; ++i) {
                if (xpos < g_tabStops[i]) {
                    xpos = g_tabStops[i];
                    break;
                }
            }
        } else if (c >= 32 && c < 128) {
            stbtt_bakedchar* b = chardata + c - 32;
            int round_x = STBTT_ifloor((xpos + b->xoff) + 0.5);
            len = round_x + b->x1 - b->x0 + 0.5f;
            xpos += b->xadvance;
        }
        ++text;
    }
    return len;
}

static void drawText(float x, float y, const char* text, int align, unsigned int col) {
    if (!g_ftex)
        return;
    if (!text)
        return;

    if (align == IMGUI_ALIGN_CENTER)
        x -= getTextLength(g_cdata, text) / 2;
    else if (align == IMGUI_ALIGN_RIGHT)
        x -= getTextLength(g_cdata, text);

    drawPending();
    std::vector<CoreProfileVertex> textVertices;
    const glm::vec4 color = unpackColor(col);

    const float ox = x;

    while (*text) {
        int c = (unsigned char)*text;
        if (c == '\t') {
            for (int i = 0; i < 4; ++i) {
                if (x < g_tabStops[i] + ox) {
                    x = g_tabStops[i] + ox;
                    break;
                }
            }
        } else if (c >= 32 && c < 128) {
            stbtt_aligned_quad q;
            getBakedQuad(g_cdata, 512, 512, c - 32, &x, &y, &q);

            textVertices.push_back(makeUiVertex(q.x0, q.y0, color, q.s0, q.t1));
            textVertices.push_back(makeUiVertex(q.x1, q.y1, color, q.s1, q.t0));
            textVertices.push_back(makeUiVertex(q.x1, q.y0, color, q.s1, q.t1));

            textVertices.push_back(makeUiVertex(q.x0, q.y0, color, q.s0, q.t1));
            textVertices.push_back(makeUiVertex(q.x0, q.y1, color, q.s0, q.t0));
            textVertices.push_back(makeUiVertex(q.x1, q.y1, color, q.s1, q.t0));
        }
        ++text;
    }

    const glm::mat4 identity(1.0f);
    drawCoreProfileVertices(textVertices, GL_TRIANGLES, identity, identity, uiProjection(), g_ftex, true, false);
}

void imguiRenderGLDraw() {
    const imguiGfxCmd* q = imguiGetRenderQueue();
    int nq = imguiGetRenderQueueSize();

    const float s = 1.0f / 8.0f;

    g_vertices.clear();
    glDisable(GL_SCISSOR_TEST);
    for (int i = 0; i < nq; ++i) {
        const imguiGfxCmd& cmd = q[i];
        if (cmd.type == IMGUI_GFXCMD_RECT) {
            if (cmd.rect.r == 0) {
                drawRect((float)cmd.rect.x * s + 0.5f, (float)cmd.rect.y * s + 0.5f, (float)cmd.rect.w * s - 1,
                    (float)cmd.rect.h * s - 1, 1.0f, cmd.col);
            } else {
                drawRoundedRect((float)cmd.rect.x * s + 0.5f, (float)cmd.rect.y * s + 0.5f, (float)cmd.rect.w * s - 1,
                    (float)cmd.rect.h * s - 1, (float)cmd.rect.r * s, 1.0f, cmd.col);
            }
        } else if (cmd.type == IMGUI_GFXCMD_LINE) {
            drawLine(cmd.line.x0 * s, cmd.line.y0 * s, cmd.line.x1 * s, cmd.line.y1 * s, cmd.line.r * s, 1.0f, cmd.col);
        } else if (cmd.type == IMGUI_GFXCMD_TRIANGLE) {
            if (cmd.flags == 1) {
                const float verts[3 * 2] = {
                    (float)cmd.rect.x * s + 0.5f,
                    (float)cmd.rect.y * s + 0.5f,
                    (float)cmd.rect.x * s + 0.5f + (float)cmd.rect.w * s - 1,
                    (float)cmd.rect.y * s + 0.5f + (float)cmd.rect.h * s / 2 - 0.5f,
                    (float)cmd.rect.x * s + 0.5f,
                    (float)cmd.rect.y * s + 0.5f + (float)cmd.rect.h * s - 1,
                };
                drawPolygon(verts, 3, 1.0f, cmd.col);
            }
            if (cmd.flags == 2) {
                const float verts[3 * 2] = {
                    (float)cmd.rect.x * s + 0.5f,
                    (float)cmd.rect.y * s + 0.5f + (float)cmd.rect.h * s - 1,
                    (float)cmd.rect.x * s + 0.5f + (float)cmd.rect.w * s / 2 - 0.5f,
                    (float)cmd.rect.y * s + 0.5f,
                    (float)cmd.rect.x * s + 0.5f + (float)cmd.rect.w * s - 1,
                    (float)cmd.rect.y * s + 0.5f + (float)cmd.rect.h * s - 1,
                };
                drawPolygon(verts, 3, 1.0f, cmd.col);
            }
        } else if (cmd.type == IMGUI_GFXCMD_TEXT) {
            drawText(cmd.text.x, cmd.text.y, cmd.text.text, cmd.text.align, cmd.col);
        } else if (cmd.type == IMGUI_GFXCMD_SCISSOR) {
            drawPending();
            if (cmd.flags) {
                glEnable(GL_SCISSOR_TEST);
                glScissor(cmd.rect.x, cmd.rect.y, cmd.rect.w, cmd.rect.h);
            } else {
                glDisable(GL_SCISSOR_TEST);
            }
        }
    }
    drawPending();
    glDisable(GL_SCISSOR_TEST);
}

bool imguiRenderGLIsWorldTextInRange(const glm::vec3& position) {
    const Renderer& renderer = Renderer::getInstance();
    const glm::vec3 camera(renderer.cameraPos[0], renderer.cameraPos[1], renderer.cameraPos[2]);
    const glm::vec3 cameraDelta = position - camera;
    return glm::dot(cameraDelta, cameraDelta) <= MAX_WORLD_TEXT_DISTANCE_SQUARED;
}

void imguiRenderGLDrawWorldTextBatch(const std::vector<WorldTextLabel>& labels) {
    if (!g_worldFtex || labels.empty())
        return;

    const Renderer& renderer = Renderer::getInstance();
    std::vector<CoreProfileVertex> vertices;
    for (const WorldTextLabel& label : labels) {
        if (!imguiRenderGLIsWorldTextInRange(label.position))
            continue;

        const glm::vec4 cameraPosition = renderer.view * glm::vec4(label.position, 1.0f);
        const glm::vec4 color(label.color, 1.0f);
        const float scale = label.size * 0.0002f;
        float xpos = 0.0f;
        float ypos = 0.0f;
        const size_t vertexStart = vertices.size();
        for (const unsigned char* text = reinterpret_cast<const unsigned char*>(label.text.c_str()); *text; ++text) {
            const int c = *text;
            if (c == '\t') {
                for (const float tabStop : g_tabStops) {
                    if (xpos < tabStop) {
                        xpos = tabStop;
                        break;
                    }
                }
            } else if (c >= 32 && c < 128) {
                stbtt_aligned_quad q;
                getBakedQuad(g_worldCdata, WORLD_FONT_TEXTURE_SIZE, WORLD_FONT_TEXTURE_SIZE, c - 32, &xpos, &ypos, &q);
                vertices.push_back(makeWorldTextVertex(cameraPosition.x + q.x0 * scale, cameraPosition.y - q.y0 * scale,
                    cameraPosition.z, color, q.s0, q.t1));
                vertices.push_back(makeWorldTextVertex(cameraPosition.x + q.x1 * scale, cameraPosition.y - q.y1 * scale,
                    cameraPosition.z, color, q.s1, q.t0));
                vertices.push_back(makeWorldTextVertex(cameraPosition.x + q.x1 * scale, cameraPosition.y - q.y0 * scale,
                    cameraPosition.z, color, q.s1, q.t1));
                vertices.push_back(makeWorldTextVertex(cameraPosition.x + q.x0 * scale, cameraPosition.y - q.y0 * scale,
                    cameraPosition.z, color, q.s0, q.t1));
                vertices.push_back(makeWorldTextVertex(cameraPosition.x + q.x0 * scale, cameraPosition.y - q.y1 * scale,
                    cameraPosition.z, color, q.s0, q.t0));
                vertices.push_back(makeWorldTextVertex(cameraPosition.x + q.x1 * scale, cameraPosition.y - q.y1 * scale,
                    cameraPosition.z, color, q.s1, q.t0));
            }
        }
        if (vertices.size() > vertexStart) {
            float lowestY = vertices[vertexStart].position.y;
            for (size_t i = vertexStart; i < vertices.size(); ++i) {
                lowestY = std::min(lowestY, vertices[i].position.y);
            }
            const float verticalOffset = cameraPosition.y - lowestY;
            for (size_t i = vertexStart; i < vertices.size(); ++i) {
                vertices[i].position.y += verticalOffset;
            }
        }
    }
    if (vertices.empty())
        return;

    const glm::mat4 identity(1.0f);
    drawCoreProfileVertices(vertices, GL_TRIANGLES, identity, identity, renderer.projection, g_worldFtex, true, true);
}

void imguiRenderGLDrawWorldText(const char* text, const float x, const float y, const float z, const float r,
    const float g, const float b, const float size) {
    if (!text)
        return;
    imguiRenderGLDrawWorldTextBatch({WorldTextLabel{text, {x, y, z}, {r, g, b}, size}});
}
