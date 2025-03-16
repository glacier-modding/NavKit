#include <GL/glew.h>

#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/model/ReasoningGrid.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Settings.h"

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

void Grid::loadBoundsFromNavp() {
    const Navp &navp = Navp::getInstance();
    xMin = navp.navMesh->m_graphHdr->m_bbox.m_min.X;
    yMin = navp.navMesh->m_graphHdr->m_bbox.m_min.Y;
    xMax = navp.navMesh->m_graphHdr->m_bbox.m_max.X;
    yMax = navp.navMesh->m_graphHdr->m_bbox.m_max.Y;
    gridWidth = std::ceil((xMax - xMin) / spacing);
}

void Grid::loadBoundsFromAirg() {
    Airg &airg = Airg::getInstance();
    ReasoningGrid *reasoningGrid = airg.reasoningGrid;
    xMin = reasoningGrid->m_Properties.vMin.x;
    yMin = reasoningGrid->m_Properties.vMin.y;
    xMax = reasoningGrid->m_Properties.vMax.x;
    yMax = reasoningGrid->m_Properties.vMax.y;
    spacing = reasoningGrid->m_Properties.fGridSpacing;
    gridWidth = std::ceil((xMax - xMin) / spacing);
}

void Grid::saveSpacing(float newSpacing) {
    Settings::setValue("Airg", "spacing", std::to_string(newSpacing).c_str());
    spacing = newSpacing;
}

void Grid::renderGrid() const {
    const Vec3 color = {0.6, 0.6, 0.6};
    float zOffset = 0;
    Airg &airg = Airg::getInstance();
    ReasoningGrid *reasoningGrid = airg.reasoningGrid;
    if (airg.selectedWaypointIndex != -1) {
        if (airg.selectedWaypointIndex < reasoningGrid->m_WaypointList.size()) {
            const Waypoint &selectedWaypoint = reasoningGrid->m_WaypointList[airg.selectedWaypointIndex];
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
    Renderer &renderer = Renderer::getInstance();
    Vec3 camPos{renderer.cameraPos[0], -renderer.cameraPos[2], renderer.cameraPos[1]};
    for (float x = minX; x < xMax; x += gridSpacing) {
        xi++;
        int yi = -1;
        for (float y = minY; y < yMax; y += gridSpacing) {
            yi++;
            Vec3 pos{x, y, z};
            float distance = camPos.DistanceTo(pos);
            if (distance > 100) {
                continue;
            }
            glBegin(GL_LINE_LOOP);
            glVertex3f(x + gridSpacing, z, -y);
            glVertex3f(x + gridSpacing, z, -(y + gridSpacing));
            glVertex3f(x, z, -(y + gridSpacing));
            glVertex3f(x, z, -y);
            glEnd();
            Vec3 textCoords{x + gridSpacing / 2.0f, z, -(y + gridSpacing / 2.0f)};
            int cellIndex = xi + yi * gridWidth;
            std::string cellText = std::to_string(xi) + ", " + std::to_string(yi) + " = " + std::to_string(cellIndex);
            renderer.drawText(cellText, textCoords, color);
        }
    }
}
