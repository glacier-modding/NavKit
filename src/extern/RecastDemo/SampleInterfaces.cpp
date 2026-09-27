// This source has been modified from the original by Daniel Bierek for use
// in NavKit.
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include "../../include/RecastDemo/SampleInterfaces.h"
#include "Recast.h"
#include "RecastDebugDraw.h"
#include "DetourDebugDraw.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/render/CoreProfileDraw.h"
#include "../../include/RecastDemo/PerfTimer.h"
#include "SDL.h"
#include <GL/glew.h>
#ifdef WIN32
#define snprintf _snprintf
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////

BuildContext::BuildContext() : m_messageCount(0), m_textPoolSize(0) {
    memset(m_messages, 0, sizeof(char*) * MAX_MESSAGES);

    resetTimers();
}

// Virtual functions for custom implementations.
void BuildContext::doResetLog() {
    m_messageCount = 0;
    m_textPoolSize = 0;
    m_logBuffer.clear();
}

void BuildContext::doLog(const rcLogCategory category, const char* msg, const int len) {
    if (!len)
        return;

    std::lock_guard lock(m_log_mutex);
    if (m_logBuffer.size() >= MAX_MESSAGES - 1) {
        m_logBuffer.pop_front();
    } else {
        m_messageCount++;
    }
    m_logBuffer.push_back(msg);
    // if (m_messageCount >= MAX_MESSAGES) {
    //	//dumpLog("%s");
    //	resetLog();
    //	return;
    // }
    // char* dst = &m_textPool[m_textPoolSize];
    // int n = TEXT_POOL_SIZE - m_textPoolSize;
    // if (n < 2) {
    //	//dumpLog("%s");
    //	resetLog();
    //	dst = &m_textPool[m_textPoolSize];
    //	n = TEXT_POOL_SIZE - m_textPoolSize;
    // }

    // char* cat = dst;
    // char* text = dst+1;
    // const int maxtext = n-1;
    //// Store category
    //*cat = (char)category;
    //// Store message
    // const int count = rcMin(len+1, maxtext);
    // memcpy(text, msg, count);
    // text[count-1] = '\0';
    // m_textPoolSize += 1 + count;
    // m_messages[m_messageCount++] = dst;
}

void BuildContext::doResetTimers() {
    for (int i = 0; i < RC_MAX_TIMERS; ++i)
        m_accTime[i] = -1;
}

void BuildContext::doStartTimer(const rcTimerLabel label) {
    m_startTime[label] = getPerfTime();
}

void BuildContext::doStopTimer(const rcTimerLabel label) {
    const TimeVal endTime = getPerfTime();
    const TimeVal deltaTime = endTime - m_startTime[label];
    if (m_accTime[label] == -1)
        m_accTime[label] = deltaTime;
    else
        m_accTime[label] += deltaTime;
}

int BuildContext::doGetAccumulatedTime(const rcTimerLabel label) const {
    return getPerfTimeUsec(m_accTime[label]);
}

void BuildContext::dumpLog(const char* format, ...) {
    // Print header.
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
    printf("\n");

    // Print messages
    const int TAB_STOPS[4] = {28, 36, 44, 52};
    for (int i = 0; i < m_messageCount; ++i) {
        const char* msg = m_messages[i] + 1;
        int n = 0;
        while (*msg) {
            if (*msg == '\t') {
                int count = 1;
                for (int j = 0; j < 4; ++j) {
                    if (n < TAB_STOPS[j]) {
                        count = TAB_STOPS[j] - n;
                        break;
                    }
                }
                while (--count) {
                    putchar(' ');
                    n++;
                }
            } else {
                putchar(*msg);
                n++;
            }
            msg++;
        }
        putchar('\n');
    }
}

int BuildContext::getLogCount() const {
    return m_messageCount;
}

const char* BuildContext::getLogText(const int i) const {
    if (i < m_logBuffer.size()) {
        return m_logBuffer[i].c_str();
    } else {
        return "";
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

class GLCheckerTexture {
    unsigned int m_texId;

public:
    GLCheckerTexture() : m_texId(0) {}

    ~GLCheckerTexture() {
        if (m_texId != 0)
            glDeleteTextures(1, &m_texId);
    }
    void bind() {
        if (m_texId == 0) {
            // Create checker pattern.
            const unsigned int col0 = duRGBA(215, 215, 215, 255);
            const unsigned int col1 = duRGBA(255, 255, 255, 255);
            static const int TSIZE = 64;
            unsigned int data[TSIZE * TSIZE];

            glGenTextures(1, &m_texId);
            glBindTexture(GL_TEXTURE_2D, m_texId);

            int level = 0;
            int size = TSIZE;
            while (size > 0) {
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x)
                        data[x + y * size] = (x == 0 || y == 0) ? col0 : col1;
                glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                size /= 2;
                level++;
            }

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            glBindTexture(GL_TEXTURE_2D, m_texId);
        }
    }
    unsigned int id() {
        bind();
        return m_texId;
    }
};
static GLCheckerTexture g_tex;

void DebugDrawGL::depthMask(bool state) {
    glDepthMask(state ? GL_TRUE : GL_FALSE);
}

void DebugDrawGL::texture(bool state) {
    m_textureEnabled = state;
}

void DebugDrawGL::begin(duDebugDrawPrimitives prim, float size) {
    m_primitive = prim;
    m_size = size;
    m_vertices.clear();
}

void DebugDrawGL::vertex(const float* pos, unsigned int color) {
    vertex(pos[0], pos[1], pos[2], color, 0.0f, 0.0f);
}

void DebugDrawGL::vertex(const float x, const float y, const float z, unsigned int color) {
    vertex(x, y, z, color, 0.0f, 0.0f);
}

void DebugDrawGL::vertex(const float* pos, unsigned int color, const float* uv) {
    vertex(pos[0], pos[1], pos[2], color, uv[0], uv[1]);
}

void DebugDrawGL::vertex(
    const float x, const float y, const float z, unsigned int color, const float u, const float v) {
    constexpr float inverseByte = 1.0f / 255.0f;
    m_vertices.push_back({{x, y, z}, {0.0f, 1.0f, 0.0f},
        {static_cast<float>(color & 0xff) * inverseByte, static_cast<float>((color >> 8) & 0xff) * inverseByte,
            static_cast<float>((color >> 16) & 0xff) * inverseByte,
            static_cast<float>((color >> 24) & 0xff) * inverseByte},
        {u, v}});
}

void DebugDrawGL::end() {
    GLenum primitive = GL_POINTS;
    std::vector<CoreProfileVertex> vertices;
    switch (m_primitive) {
    case DU_DRAW_POINTS:
        primitive = GL_POINTS;
        vertices = m_vertices;
        break;
    case DU_DRAW_LINES:
        primitive = GL_LINES;
        vertices = m_vertices;
        break;
    case DU_DRAW_TRIS:
        primitive = GL_TRIANGLES;
        vertices = m_vertices;
        break;
    case DU_DRAW_QUADS:
        primitive = GL_TRIANGLES;
        vertices.reserve((m_vertices.size() / 4) * 6);
        for (size_t i = 0; i + 3 < m_vertices.size(); i += 4) {
            vertices.insert(vertices.end(),
                {m_vertices[i], m_vertices[i + 1], m_vertices[i + 2], m_vertices[i], m_vertices[i + 2],
                    m_vertices[i + 3]});
        }
        break;
    }
    if (m_primitive == DU_DRAW_LINES)
        glLineWidth(m_size);
    else if (m_primitive == DU_DRAW_POINTS)
        glPointSize(m_size);
    Renderer& renderer = Renderer::getInstance();
    drawCoreProfileVertices(
        vertices, primitive, glm::mat4(1.0f), renderer.view, renderer.projection, m_textureEnabled ? g_tex.id() : 0);
    glLineWidth(1.0f);
    glPointSize(1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FileIO::FileIO() : m_fp(0), m_mode(-1) {}

FileIO::~FileIO() {
    if (m_fp)
        fclose(m_fp);
}

bool FileIO::openForWrite(const char* path) {
    if (m_fp)
        return false;
    m_fp = fopen(path, "wb");
    if (!m_fp)
        return false;
    m_mode = 1;
    return true;
}

bool FileIO::openForRead(const char* path) {
    if (m_fp)
        return false;
    m_fp = fopen(path, "rb");
    if (!m_fp)
        return false;
    m_mode = 2;
    return true;
}

bool FileIO::isWriting() const {
    return m_mode == 1;
}

bool FileIO::isReading() const {
    return m_mode == 2;
}

bool FileIO::write(const void* ptr, const size_t size) {
    if (!m_fp || m_mode != 1)
        return false;
    fwrite(ptr, size, 1, m_fp);
    return true;
}

bool FileIO::read(void* ptr, const size_t size) {
    if (!m_fp || m_mode != 2)
        return false;
    size_t readLen = fread(ptr, size, 1, m_fp);
    return readLen == 1;
}
