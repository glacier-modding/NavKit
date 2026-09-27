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

#ifndef IMGUI_RENDER_GL_H
#define IMGUI_RENDER_GL_H

#include <glm/vec3.hpp>
#include <string>
#include <vector>

struct WorldTextLabel {
    std::string text;
    glm::vec3 position;
    glm::vec3 color;
    float size;
};

bool imguiRenderGLInit(const char* fontpath);
void imguiRenderGLDestroy();
void imguiRenderGLDraw();
bool imguiRenderGLIsWorldTextInRange(const glm::vec3& position);
void imguiRenderGLDrawWorldText(const char* text, float x, float y, float z, float r, float g, float b, float size);
void imguiRenderGLDrawWorldTextBatch(const std::vector<WorldTextLabel>& labels);

#endif // IMGUI_RENDER_GL_H
