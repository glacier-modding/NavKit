#include <GL/glew.h>

#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/model/ReasoningGrid.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

Grid::Grid() {
    spacing = 2.25;
    xMin = -22.5;
    yMin = -22.5;
    xMax = 22.5;
    yMax = 22.5;
    xOffset = 0;
    yOffset = 0;
    gridWidth = std::ceil((xMax - xMin) / spacing);
    showGrid = true;
}

void Grid::loadBoundsFromAirg() {
    const Airg& airg = Airg::getInstance();
    const ReasoningGrid* reasoningGrid = airg.reasoningGrid;
    xMin = reasoningGrid->m_Properties.vMin.x;
    yMin = reasoningGrid->m_Properties.vMin.y;
    xMax = reasoningGrid->m_Properties.vMax.x;
    yMax = reasoningGrid->m_Properties.vMax.y;
    spacing = reasoningGrid->m_Properties.fGridSpacing;
    gridWidth = std::ceil((xMax - xMin) / spacing);
}

void Grid::saveSpacing(const float newSpacing) {
    PersistedSettings::getInstance().setValue("Airg", "spacing", std::to_string(newSpacing));
    spacing = newSpacing;
}

void Grid::renderGrid() const {
    glEnable(GL_DEPTH_TEST);

    static GLuint gridVAO = 0;
    static GLuint gridVBO = 0;
    static GLuint whiteTextureID = 0;
    static int gridVertexCount = 0;

    static float lastXMin = 0, lastXMax = 0, lastYMin = 0, lastYMax = 0, lastSpacing = 0;
    static float lastXOffset = 0, lastYOffset = 0;

    float zOffset = 0;
    const Airg& airg = Airg::getInstance();
    const ReasoningGrid* reasoningGrid = airg.reasoningGrid;
    if (airg.selectedWaypointIndex != -1) {
        if (airg.selectedWaypointIndex < reasoningGrid->m_WaypointList.size()) {
            const Waypoint& selectedWaypoint = reasoningGrid->m_WaypointList[airg.selectedWaypointIndex];
            zOffset = -selectedWaypoint.vPos.z;
        }
    } else {
        zOffset = reasoningGrid->m_Properties.vMin.z;
    }

    const bool dirty = (gridVAO == 0) ||
        (xMin != lastXMin) || (xMax != lastXMax) ||
        (yMin != lastYMin) || (yMax != lastYMax) ||
        (spacing != lastSpacing) ||
        (xOffset != lastXOffset) || (yOffset != lastYOffset);

    if (dirty) {
        lastXMin = xMin;
        lastXMax = xMax;
        lastYMin = yMin;
        lastYMax = yMax;
        lastSpacing = spacing;
        lastXOffset = xOffset;
        lastYOffset = yOffset;

        struct Vertex {
            glm::vec3 pos;
            glm::vec3 normal;
        };
        std::vector<Vertex> vertices;

        const float startX = xMin + xOffset;
        const float startY = yMin + yOffset;
        const int numX = static_cast<int>(std::ceil((xMax - startX) / spacing));
        const int numY = static_cast<int>(std::ceil((yMax - startY) / spacing));

        for (int i = 0; i <= numX; ++i) {
            float x = startX + i * spacing;
            if (x > xMax) {
                break;
            }
            float zStart = -startY;
            float zEnd = -(startY + numY * spacing);
            if (-(startY + numY * spacing) < -yMax) {
                zEnd = -yMax;
            }

            vertices.push_back({{x, 0.0f, zStart}, {0.0f, 1.0f, 0.0f}});
            vertices.push_back({{x, 0.0f, zEnd}, {0.0f, 1.0f, 0.0f}});
        }

        for (int i = 0; i <= numY; ++i) {
            const float y = startY + i * spacing;
            if (y > yMax) {
                break;
            }
            float z = -y;
            float xStart = startX;
            float xEnd = startX + numX * spacing;
            if (xEnd > xMax) {
                xEnd = xMax;
            }

            vertices.push_back({{xStart, 0.0f, z}, {0.0f, 1.0f, 0.0f}});
            vertices.push_back({{xEnd, 0.0f, z}, {0.0f, 1.0f, 0.0f}});
        }

        gridVertexCount = vertices.size();

        if (gridVAO == 0) {
            glGenVertexArrays(1, &gridVAO);
        }
        if (gridVBO == 0) {
            glGenBuffers(1, &gridVBO);
        }

        glBindVertexArray(gridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), static_cast<void*>(nullptr));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    }

    if (whiteTextureID == 0) {
        glGenTextures(1, &whiteTextureID);
        glBindTexture(GL_TEXTURE_2D, whiteTextureID);
        const unsigned char white[] = {255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    const Renderer& renderer = Renderer::getInstance();
    renderer.shader.use();

    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.02f - zOffset, 0.0f));

    renderer.shader.setMat4("model", model);
    renderer.shader.setMat4("view", renderer.view);
    renderer.shader.setMat4("projection", renderer.projection);
    renderer.shader.setMat3("normalMatrix", glm::mat3(1.0f)); // Identity
    renderer.shader.setVec4("flatColor", glm::vec4(0.6f, 0.6f, 0.6f, 0.6f));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTextureID);
    renderer.shader.setInt("tileTexture", 0);

    glBindVertexArray(gridVAO);
    glDrawArrays(GL_LINES, 0, gridVertexCount);
    glBindVertexArray(0);
    glUseProgram(0);
}

void Grid::renderGridText() const {
    const Vec3 color = {0.6, 0.6, 0.6};
    float zOffset = 0;
    const Airg& airg = Airg::getInstance();
    const ReasoningGrid* reasoningGrid = airg.reasoningGrid;
    if (airg.selectedWaypointIndex != -1) {
        if (airg.selectedWaypointIndex < reasoningGrid->m_WaypointList.size()) {
            const Waypoint& selectedWaypoint = reasoningGrid->m_WaypointList[airg.selectedWaypointIndex];
            zOffset = -selectedWaypoint.vPos.z;
        }
    } else {
        zOffset = reasoningGrid->m_Properties.vMin.z;
    }
    const float minX = xMin + xOffset;
    const float minY = yMin + yOffset;
    const float gridSpacing = spacing;
    const float z = 0.02 - zOffset;
    int xi = -1;
    glColor4f(color.X, color.Y, color.Z, 0.6);
    Renderer& renderer = Renderer::getInstance();
    const Vec3 camPos{renderer.cameraPos[0], -renderer.cameraPos[2], renderer.cameraPos[1]};
    for (float x = minX; x < xMax; x += gridSpacing) {
        xi++;
        int yi = -1;
        for (float y = minY; y < yMax; y += gridSpacing) {
            yi++;
            Vec3 pos{x, y, z};
            const float distance = camPos.DistanceTo(pos);
            if (distance > 100) {
                continue;
            }
            const Vec3 textCoords{x + gridSpacing / 2.0f, z, -(y + gridSpacing / 2.0f)};
            // int cellIndex = xi + yi * gridWidth;
            std::string cellText = std::to_string(xi) + ", " + std::to_string(yi);
            // + " = " + std::to_string(cellIndex);
            renderer.drawText(cellText, textCoords, color, 20);
        }
    }
}