#include "../../include/NavKit/model/ReasoningGrid.h"
#include <algorithm>
#include <from_current.hpp>
#include <set>
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/util/ErrorHandler.h"

const void Vec4::writeJson(std::ostream &f) {
    f << "{";

    f << "\"x\"" << ":";
    f << x;
    f << ",";

    f << "\"y\"" << ":";
    f << y;
    f << ",";

    f << "\"z\"" << ":";
    f << z;
    f << ",";

    f << "\"w\"" << ":";
    f << w;

    f << "}";
}

void Vec4::readJson(simdjson::ondemand::object p_Json) {
    x = double(p_Json["x"]);
    y = double(p_Json["y"]);
    z = double(p_Json["z"]);
    w = double(p_Json["w"]);
}

const void Properties::writeJson(std::ostream &f) {
    f << "{";

    f << "\"vMin\"" << ":";
    vMin.writeJson(f);
    f << ",";

    f << "\"vMax\"" << ":";
    vMax.writeJson(f);
    f << ",";

    f << "\"nGridWidth\"" << ":";
    f << nGridWidth;
    f << ",";

    f << "\"fGridSpacing\"" << ":";
    f << fGridSpacing;
    f << ",";

    f << "\"nVisibilityRange\"" << ":";
    f << nVisibilityRange;
    f << "}";
}

void Properties::readJson(simdjson::ondemand::object p_Json) {
    simdjson::ondemand::object vMinJson = p_Json["vMin"];
    vMin.readJson(vMinJson);

    simdjson::ondemand::object vMaxJson = p_Json["vMax"];
    vMax.readJson(vMaxJson);

    nGridWidth = uint64_t(p_Json["nGridWidth"]);

    fGridSpacing = double(p_Json["fGridSpacing"]);

    nVisibilityRange = uint64_t(p_Json["nVisibilityRange"]);
}

const void SizedArray::writeJson(std::ostream &f) {
    f << "{";
    f << "\"m_aBytes\"" << ":";
    f << "[";
    for (size_t i = 0; i < m_aBytes.size(); i++) {
        f << (uint64_t) m_aBytes[i];
        if (i < m_aBytes.size() - 1) {
            f << ",";
        }
    }

    f << "]" << ",";

    f << "\"m_nSize\"" << ":";
    f << m_nSize;

    f << "}";
}

void SizedArray::readJson(simdjson::ondemand::object p_Json) {
    simdjson::ondemand::array m_aBytesJson = p_Json["m_aBytes"];
    for (uint64_t byteJson: m_aBytesJson) {
        m_aBytes.push_back(byteJson);
    }
    m_nSize = uint64_t(p_Json["m_nSize"]);
}

const void Waypoint::writeJson(std::ostream &f) {
    f << "{";
    f << "\"nNeighbor0\"" << ":";
    f << nNeighbors[0];
    f << ",";
    f << "\"nNeighbor1\"" << ":";
    f << nNeighbors[1];
    f << ",";
    f << "\"nNeighbor2\"" << ":";
    f << nNeighbors[2];
    f << ",";
    f << "\"nNeighbor3\"" << ":";
    f << nNeighbors[3];
    f << ",";
    f << "\"nNeighbor4\"" << ":";
    f << nNeighbors[4];
    f << ",";
    f << "\"nNeighbor5\"" << ":";
    f << nNeighbors[5];
    f << ",";
    f << "\"nNeighbor6\"" << ":";
    f << nNeighbors[6];
    f << ",";
    f << "\"nNeighbor7\"" << ":";
    f << nNeighbors[7];
    f << ",";
    f << "\"vPos\"" << ":";
    vPos.writeJson(f);
    f << ",";
    f << "\"nVisionDataOffset\"" << ":";
    f << nVisionDataOffset;
    f << ",";
    f << "\"nLayerIndex\"" << ":";
    f << nLayerIndex;
    f << "}";
}

void Waypoint::readJson(simdjson::ondemand::object p_Json) {
    nNeighbors.clear();
    nNeighbors.push_back((uint16_t) uint64_t(p_Json["nNeighbor0"]));
    nNeighbors.push_back((uint16_t) uint64_t(p_Json["nNeighbor1"]));
    nNeighbors.push_back((uint16_t) uint64_t(p_Json["nNeighbor2"]));
    nNeighbors.push_back((uint16_t) uint64_t(p_Json["nNeighbor3"]));
    nNeighbors.push_back((uint16_t) uint64_t(p_Json["nNeighbor4"]));
    nNeighbors.push_back((uint16_t) uint64_t(p_Json["nNeighbor5"]));
    nNeighbors.push_back((uint16_t) uint64_t(p_Json["nNeighbor6"]));
    nNeighbors.push_back((uint16_t) uint64_t(p_Json["nNeighbor7"]));

    simdjson::ondemand::object vPosJson = p_Json["vPos"];
    vPos.readJson(vPosJson);

    nVisionDataOffset = uint64_t(p_Json["nVisionDataOffset"]);
    nLayerIndex = uint64_t(p_Json["nLayerIndex"]);
}

const void ReasoningGrid::writeJson(std::ostream &f) {
    f << "{";

    f << "\"m_WaypointList\"" << ":";
    f << "[";
    for (size_t i = 0; i < m_WaypointList.size(); i++) {
        m_WaypointList[i].writeJson(f);
        if (i < m_WaypointList.size() - 1) {
            f << ",";
        }
    }

    f << "]";
    f << ",";

    f << "\"m_LowVisibilityBits\"" << ":";
    m_LowVisibilityBits.writeJson(f);
    f << ",";

    f << "\"m_HighVisibilityBits\"" << ":";
    m_HighVisibilityBits.writeJson(f);
    f << ",";

    f << "\"m_Properties\"" << ":";
    m_Properties.writeJson(f);
    f << ",";

    f << "\"m_nNodeCount\"" << ":";
    f << m_nNodeCount;
    f << ",";

    f << "\"m_pVisibilityData\"" << ":";
    f << "[";
    for (size_t i = 0; i < m_pVisibilityData.size(); i++) {
        f << (uint64_t) m_pVisibilityData[i];
        if (i < m_pVisibilityData.size() - 1) {
            f << ",";
        }
    }

    f << "]";
    f << ",";

    f << "\"m_deadEndData\"" << ":";
    m_deadEndData.writeJson(f);

    f << "}";
}

void ReasoningGrid::readJson(const char *p_AirgPath) {
    simdjson::ondemand::parser p_Parser;
    simdjson::padded_string p_Json = simdjson::padded_string::load(p_AirgPath);
    simdjson::ondemand::document p_AirgDocument = p_Parser.iterate(p_Json);

    simdjson::ondemand::object m_PropertiesJson = p_AirgDocument["m_Properties"];
    m_Properties.readJson(m_PropertiesJson);
    simdjson::ondemand::object m_HighVisibilityBitsJson = p_AirgDocument["m_HighVisibilityBits"];
    m_HighVisibilityBits.readJson(m_HighVisibilityBitsJson);
    simdjson::ondemand::object m_LowVisibilityBitsJson = p_AirgDocument["m_LowVisibilityBits"];
    m_LowVisibilityBits.readJson(m_LowVisibilityBitsJson);
    simdjson::ondemand::object m_deadEndDataJson = p_AirgDocument["m_deadEndData"];
    m_deadEndData.readJson(m_deadEndDataJson);
    m_nNodeCount = int64_t(p_AirgDocument["m_nNodeCount"]);
    simdjson::ondemand::array m_WaypointListJson = p_AirgDocument["m_WaypointList"];
    for (simdjson::ondemand::value waypointJson: m_WaypointListJson) {
        Waypoint waypoint;
        waypoint.readJson(waypointJson);
        m_WaypointList.push_back(waypoint);
    }
    simdjson::ondemand::array m_pVisibilityDataJson = p_AirgDocument["m_pVisibilityData"];
    for (uint64_t visibilityDatum: m_pVisibilityDataJson) {
        m_pVisibilityData.push_back(visibilityDatum);
    }
}

std::vector<uint8_t> ReasoningGrid::getWaypointVisionData(int waypointIndex) {
    Waypoint &waypoint = m_WaypointList[waypointIndex];
    int nextWaypointOffset = (waypointIndex + 1) < m_WaypointList.size()
                                 ? m_WaypointList[waypointIndex + 1].nVisionDataOffset
                                 : m_pVisibilityData.size();
    int visibilityDataSize = nextWaypointOffset - waypoint.nVisionDataOffset;

    std::vector<uint8_t>::const_iterator first = m_pVisibilityData.begin() + waypoint.nVisionDataOffset;
    std::vector<uint8_t>::const_iterator last = (waypointIndex + 1) < m_WaypointList.size()
                                                    ? m_pVisibilityData.begin() + m_WaypointList[waypointIndex + 1].
                                                      nVisionDataOffset
                                                    : m_pVisibilityData.end();
    std::vector<uint8_t> waypointVisibilityData(first, last);
    return waypointVisibilityData;
}

int orientation(Vec3 p, Vec3 q, Vec3 r) {
    int val = (q.Y - p.Y) * (r.X - q.X) - (q.X - p.X) * (r.Y - q.Y);
    if (val == 0) return 0; // Collinear
    return (val > 0) ? 1 : 2; // Clockwise or counterclockwise
}

Vec3 reflect(Vec3 p, float x1, float y1, float x2, float y2) {
    float dx, dy, a, b;
    float x, y;

    dx = x2 - x1;
    dy = y2 - y1;

    a = (dx * dx - dy * dy) / (dx * dx + dy * dy);
    b = 2 * dx * dy / (dx * dx + dy * dy);

    x = a * (p.X - x1) + b * (p.Y - y1) + x1;
    y = b * (p.X - x1) - a * (p.Y - y1) + y1;
    return {x, y, 0};
}

float distanceFromEdge(Vec3 p, Vec3 v, Vec3 w) {
    const float l2 = v.DistanceSquaredTo(w);
    if (l2 == 0.0) return p.DistanceTo(v);
    const float t = std::max(0.0f, std::min(1.0f, (p - v).Dot(w - v) / l2));
    const Vec3 projection = v + (w - v) * t;
    return p.DistanceTo(projection);
}

// From https://wrfranklin.org/Research/Short_Notes/pnpoly.html
int closestEdge(int nvert, float *vertx, float *verty, float testx, float testy, float originx, float originy,
                float tolerance) {
    int cur, prev;
    bool c = false;
    int closestEdge = -2;
    float minDist = 10000;
    Vec3 testVert{testx, testy, 0};
    for (cur = 0, prev = nvert - 1; cur < nvert; prev = cur++) {
        float curToleranceX = 0;
        float curToleranceY = 0;
        float prevToleranceX = 0;
        float prevToleranceY = 0;
        float curX = vertx[cur] + curToleranceX;
        float curY = verty[cur] + curToleranceY;
        float prevX = vertx[prev] + prevToleranceX;
        float prevY = verty[prev] + prevToleranceY;
        Vec3 v{prevX, prevY, 0};
        Vec3 w{curX, curY, 0};

        float curDist = distanceFromEdge(testVert, v, w);
        if (minDist > curDist) {
            minDist = curDist;
            closestEdge = prev;
        }
        if (((curY > testy) != (prevY > testy)) &&
            (testx < (prevX - curX) * (testy - curY) / (prevY - curY) + curX)) {
            c = !c;
        }
    }
    if (c) {
        return -1;
    }
    if (minDist > tolerance) {
        return -2;
    }


    return closestEdge;
}

bool onSegment(Vec3 p, Vec3 q, Vec3 r) {
    if (q.X <= std::max(p.X, r.X) && q.X >= std::min(p.X, r.X) &&
        q.Y <= std::max(p.Y, r.Y) && q.Y >= std::min(p.Y, r.Y))
        return true;

    return false;
}

bool doIntersect(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2) {
    int o1 = orientation(p1, q1, p2);
    int o2 = orientation(p1, q1, q2);
    int o3 = orientation(p2, q2, p1);
    int o4 = orientation(p2, q2, q1);

    if (o1 != o2 && o3 != o4)
        return true;

    if (o1 == 0 && onSegment(p1, p2, q1)) return true;
    if (o2 == 0 && onSegment(p1, q2, q1)) return true;
    if (o3 == 0 && onSegment(p2, p1, q2)) return true;
    if (o4 == 0 && onSegment(p2, q1, q2)) return true;

    return false;
}

float calcZ(Vec3 p1, Vec3 p2, Vec3 p3, float x, float y) {
    float dx1 = x - p1.X;
    float dy1 = y - p1.Y;
    float dx2 = p2.X - p1.X;
    float dy2 = p2.Y - p1.Y;
    float dx3 = p3.X - p1.X;
    float dy3 = p3.Y - p1.Y;
    return p1.Z + ((dy1 * dx3 - dx1 * dy3) * (p2.Z - p1.Z) + (dx1 * dy2 - dy1 * dx2) * (p3.Z - p1.Z)) / (
               dx3 * dy2 - dx2 * dy3);
}

void initializeGrid(ReasoningGridBuilderHelper &h) {
    Logger::log(NK_INFO, "Initializing Reasoning Grid");

    for (int zi = 0; zi < h.gridZSize; zi++) {
        std::vector<std::vector<int> *> *yRow = new std::vector<std::vector<int> *>();
        h.grid->push_back(yRow);
        for (int yi = 0; yi < h.gridYSize; yi++) {
            std::vector<int> *xRow = new std::vector<int>();
            yRow->push_back(xRow);
            for (int xi = 0; xi < h.gridXSize; xi++) {
                xRow->push_back(65535);
            }
        }
    }
}

NavPower::BBox calculateBBox(NavPower::Area *area) {
    float s_minFloat = -300000000000;
    float s_maxFloat = 300000000000;
    NavPower::BBox bbox;
    bbox.m_min.X = s_maxFloat;
    bbox.m_min.Y = s_maxFloat;
    bbox.m_min.Z = s_maxFloat;
    bbox.m_max.X = s_minFloat;
    bbox.m_max.Y = s_minFloat;
    bbox.m_max.Z = s_minFloat;
    for (auto &edge: area->m_edges) {
        bbox.m_max.X = std::max(bbox.m_max.X, edge->m_pos.X);
        bbox.m_max.Y = std::max(bbox.m_max.Y, edge->m_pos.Y);
        bbox.m_max.Z = std::max(bbox.m_max.Z, edge->m_pos.Z);
        bbox.m_min.X = std::min(bbox.m_min.X, edge->m_pos.X);
        bbox.m_min.Y = std::min(bbox.m_min.Y, edge->m_pos.Y);
        bbox.m_min.Z = std::min(bbox.m_min.Z, edge->m_pos.Z);
    }
    return bbox;
}

void addWaypointsForGrid(ReasoningGrid *airg, NavPower::NavMesh *navMesh,
                         ReasoningGridBuilderHelper &h) {
    int wayPointIndex = airg->m_WaypointList.size();

    NavPower::Area *area;
    for (int areaIndex = 0; areaIndex < navMesh->m_areas.size(); areaIndex++) {
        area = &navMesh->m_areas[areaIndex];
        const int areaPointCount = area->m_edges.size();
        NavPower::BBox bbox = calculateBBox(area);

        for (int yi = 0; yi < h.gridYSize; yi++) {
            float y = h.min->Y + yi * h.spacing;
            if (y < bbox.m_min.Y - h.tolerance) {
                continue;
            }
            if (y > bbox.m_max.Y + h.tolerance) {
                break;
            }
            for (int xi = 0; xi < h.gridXSize; xi++) {
                float x = h.min->X + xi * h.spacing;
                if (x < bbox.m_min.X - h.tolerance) {
                    continue;
                }
                if (x > bbox.m_max.X + h.tolerance) {
                    break;
                }
                float z = calcZ(area->m_edges[0]->m_pos, area->m_edges[1]->m_pos, area->m_edges[2]->m_pos, x, y);
                int zi = -1;
                for (zi = 0; zi < h.gridZSize; zi++) {
                    float cz = h.min->Z + zi * h.spacing;
                    if (cz < bbox.m_min.Z) {
                        continue;
                    }
                    break;
                }
                if (h.grid->at(zi)->at(yi)->size() > xi && h.grid->at(zi)->at(yi)->at(xi) != 65535) {
                    continue;
                }
                bool pointInArea = false;
                bool neededTolerance = false;
                Vec3 reflected;
                //if (z > (minZ - h.zSpacing * h.zTolerance) && z < (maxZ + h.zSpacing * h.zTolerance)) {
                float areaXCoords[20];
                float areaYCoords[20];
                for (int i = 0; i < areaPointCount; i++) {
                    areaXCoords[i] = area->m_edges[i]->m_pos.X;
                    areaYCoords[i] = area->m_edges[i]->m_pos.Y;
                }
                int edge = closestEdge(areaPointCount, areaXCoords, areaYCoords, x, y, area->m_area->m_pos.X,
                                       area->m_area->m_pos.Y, h.tolerance);
                if (edge == -1) {
                    // In polygon with no adjustment needed
                    pointInArea = true;
                } else if (edge > -1) {
                    // Outside polygon but within tolerance, reflect across closest edge
                    //Logger::log(RC_LOG_PROGRESS, ("|--->Area: " + std::to_string(areaIndex) + " edge: " + std::to_string(edge) + " XI:" + std::to_string(xi) + " YI: " + std::to_string(yi) + " ZI: " + std::to_string(zi)).c_str());
                    neededTolerance = true;
                    pointInArea = true;
                    NavPower::Binary::Edge *edge0 = area->m_edges[edge];
                    NavPower::Binary::Edge *edge1 = area->m_edges[(edge + 1) % area->m_edges.size()];
                    float y1 = edge0->m_pos.Y;
                    float y2 = edge1->m_pos.Y;
                    float x1 = edge0->m_pos.X;
                    float x2 = edge1->m_pos.X;
                    Vec3 p{x, y, z};
                    reflected = reflect(p, x1, y1, x2, y2);
                    x = reflected.X;
                    y = reflected.Y;
                }
                if (pointInArea) {
                    h.waypointAreas.push_back(area);
                }
                if ((*(*h.grid)[zi])[yi]->size() == xi) {
                    (*(*h.grid)[zi])[yi]->push_back(pointInArea ? wayPointIndex++ : 65535);
                } else {
                    if ((*(*(*h.grid)[zi])[yi])[xi] == 65535 && pointInArea) {
                        (*(*(*h.grid)[zi])[yi])[xi] = wayPointIndex++;
                    }
                }
                if (wayPointIndex >= 65535) {
                    Logger::log(NK_ERROR, "Error building airg: Reached the maximum number of waypoints (65535)");
                    h.result = -1;
                    return;
                }
                if (pointInArea) {
                    Waypoint waypoint;
                    waypoint.vPos.x = x;
                    waypoint.vPos.y = y;
                    waypoint.vPos.z = z + 0.001;
                    waypoint.vPos.w = 1.0;
                    waypoint.nVisionDataOffset = 556 * airg->m_WaypointList.size();
                    waypoint.nLayerIndex = 0;
                    waypoint.xi = xi;
                    waypoint.yi = yi;
                    waypoint.zi = zi;
                    airg->m_WaypointList.push_back(waypoint);
                    if (neededTolerance) {
                        //Logger::log(RC_LOG_PROGRESS, ("|--->Waypoint index: " + std::to_string(airg->m_WaypointList.size() - 1) + " X:" + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z)).c_str());
                    }

                    h.waypointZLevels->push_back(zi);
                    if (airg->m_WaypointList.size() % 1000 == 0) {
                        Logger::log(NK_INFO,
                                    ("Processing... Current waypoint count: " + std::to_string(
                                         airg->m_WaypointList.size())).c_str());
                    }
                }
            }
            //}

            //int cxi = std::round((x - h.min->X) / h.spacing);
            //int cyi = std::round((y - h.min->Y) / h.spacing);
            //int czi = zi;
            //if (((*(*h.grid)[czi]).size() > cyi && (*(*h.grid)[czi])[cyi]->size() > cxi) &&
            //	((*h.grid).size() > czi && ((*(*(*h.grid)[czi])[cyi])[cxi] != 65535))) {//||
            //	//((*h.grid).size() > czi - 1 && czi > 0 && (*(*(*h.grid)[czi - 1])[cyi])[cxi] != 65535) ||
            //	//((*h.grid).size() > czi - 2 && czi > 1 && (*(*(*h.grid)[czi - 2])[cyi])[cxi] != 65535)) {
            //	Logger::log(RC_LOG_PROGRESS, ("Skipping duplicate waypoint : " + std::to_string(wayPointIndex) + " XI: " + std::to_string(xi) + " YI: " + std::to_string(yi) + " ZI: " + std::to_string(zi) + " CXI: " + std::to_string(cxi) + " CYI: " + std::to_string(cyi) + " CZI: " + std::to_string(czi)).c_str());

            //	pointInArea = false;
            //}
            //x = h.min->X + xi * h.spacing;  // REMOVE
            //y = h.min->Y + yi * h.spacing; // REMOVE
            //z = h.min->Z + zi * h.zSpacing; // REMOVE
        }
    }
}

bool waypointsAreConnected(ReasoningGridBuilderHelper &h, ReasoningGrid *airg, Vec3 waypointPos,
                           NavPower::Area *area, int waypoint2Index) {
    Navp& navp = Navp::getInstance();
    NavPower::Area *curArea = area;
    NavPower::Area *waypoint2Area = h.waypointAreas[waypoint2Index];
    if (area == waypoint2Area) {
        return true;
    }
    Waypoint *waypoint2 = &airg->m_WaypointList[waypoint2Index];
    Vec3 waypoint2Pos{waypoint2->vPos.x, waypoint2->vPos.y, waypoint2->vPos.z};
    std::set<NavPower::Area *> visitedAreas{curArea};
    while (curArea != 0) {
        visitedAreas.insert(curArea);
        NavPower::Binary::Edge *curEdge = curArea->m_edges[0];
        NavPower::Binary::Edge *prevEdge = curArea->m_edges[curArea->m_edges.size() - 1];

        if (doIntersect(waypointPos, waypoint2Pos, prevEdge->m_pos, curEdge->m_pos)) {
            if (prevEdge->m_pAdjArea == waypoint2Area->m_area) {
                return true;
            }
            if (prevEdge->m_pAdjArea == 0) {
                return false;
            }
            if (!visitedAreas.contains(navp.binaryAreaToAreaMap.at(prevEdge->m_pAdjArea))) {
                curArea = navp.binaryAreaToAreaMap.at(prevEdge->m_pAdjArea);
                continue;
            }
        }
        bool hasNextArea = false;

        for (int curEdgeIndex = 1, prevEdgeIndex = 0; curEdgeIndex < curArea->m_edges.size();
             prevEdgeIndex = curEdgeIndex++) {
            curEdge = curArea->m_edges[curEdgeIndex];
            prevEdge = curArea->m_edges[prevEdgeIndex];
            if (doIntersect(waypointPos, waypoint2Pos, prevEdge->m_pos, curEdge->m_pos)) {
                if (prevEdge->m_pAdjArea == waypoint2Area->m_area) {
                    return true;
                }
                if (prevEdge->m_pAdjArea == 0) {
                    return false;
                }
                if (!visitedAreas.contains(navp.binaryAreaToAreaMap.at(prevEdge->m_pAdjArea))) {
                    curArea = navp.binaryAreaToAreaMap.at(prevEdge->m_pAdjArea);
                    hasNextArea = true;
                    break;
                }
                //Logger::log(RC_LOG_PROGRESS, (": " + std::to_string(airg->m_WaypointList.size())).c_str());
                //Logger::log(RC_LOG_PROGRESS, ("Waypoint 1 -> 2 line goes through final edge, connects to area: " + std::to_string(prevEdge->m_pAdjArea)).c_str());
            }
        }
        if (hasNextArea) {
            continue;
        }
        return false;
    }
    return false;
}

void ReasoningGrid::build(ReasoningGrid *reasoningGrid, NavPower::NavMesh *navMesh, float spacing,
                          float zSpacing, float tolerance, float zTolerance) {
    Airg& airg = Airg::getInstance();
    CPPTRACE_TRY
        {
            // Initialize airg properties
            std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::string msg = "Started building Airg at ";
            msg += std::ctime(&start_time);
            Logger::log(NK_INFO, msg.data());
            auto start = std::chrono::high_resolution_clock::now();

            airg.airgLoadState.push_back(true);
            // grid is set up in this order: Z[Y[X[]]]
            std::vector<std::vector<std::vector<int> *> *> grid;
            Vec3 min = navMesh->m_graphHdr->m_bbox.m_min;
            min.X += spacing / 2;
            min.Y += spacing / 2;
            Vec3 max = navMesh->m_graphHdr->m_bbox.m_max;
            int gridXSize = std::ceil((max.X - min.X) / spacing);
            int gridYSize = std::ceil((max.Y - min.Y) / spacing);
            int gridZSize = std::ceil((max.Z - min.Z) / zSpacing);
            gridZSize = gridZSize > 0 ? gridZSize : 1;
            reasoningGrid->m_Properties.fGridSpacing = spacing;
            reasoningGrid->m_Properties.nGridWidth = gridYSize;
            reasoningGrid->m_Properties.vMin.x = min.X;
            reasoningGrid->m_Properties.vMin.y = min.Y;
            reasoningGrid->m_Properties.vMin.z = min.Z;
            reasoningGrid->m_Properties.vMin.w = 1;
            reasoningGrid->m_Properties.vMax.x = max.X;
            reasoningGrid->m_Properties.vMax.y = max.Y;
            reasoningGrid->m_Properties.vMax.z = max.Z;
            reasoningGrid->m_Properties.vMax.w = 1;
            reasoningGrid->m_Properties.nVisibilityRange = 23;


            std::vector<double> areaZMins;
            std::vector<double> areaZMaxes;
            std::vector<int> waypointZLevels;
            std::vector<std::pair<float, float> > waypointPositionAdjustments;

            // Get Z-level for each area
            for (auto area: navMesh->m_areas) {
                const int areaPointCount = area.m_edges.size();
                double areaMinZ = 1000;
                double areaMaxZ = -1000;
                double pointZ = 0;
                for (int i = 0; i < areaPointCount; i++) {
                    pointZ = area.m_edges[i]->m_pos.Z;
                    areaMinZ = areaMinZ < pointZ ? areaMinZ : pointZ;
                    areaMaxZ = areaMaxZ > pointZ ? areaMaxZ : pointZ;
                }
                areaZMins.push_back(areaMinZ);
                areaZMaxes.push_back(areaMaxZ);
            }

            // Build map of Z-level to area list
            std::vector<std::vector<int> > areasByZLevel;
            for (int zi = 0; zi < gridZSize; zi++) {
                std::vector<int> areasForZLevel;
                for (int areaIndex = 0; areaIndex < navMesh->m_areas.size(); areaIndex++) {
                    double minZ = min.Z + zi * zSpacing;
                    double maxZ = min.Z + (zi + 1) * zSpacing;
                    if ((areaZMins[areaIndex] >= minZ && areaZMins[areaIndex] <= maxZ) ||
                        (areaZMaxes[areaIndex] >= minZ && areaZMaxes[areaIndex] <= maxZ) ||
                        (areaZMins[areaIndex] <= minZ && areaZMaxes[areaIndex] >= maxZ)) {
                        areasForZLevel.push_back(areaIndex);
                    }
                }
                areasByZLevel.push_back(areasForZLevel);
            }

            // Fill in waypoints for grid points
            ReasoningGridBuilderHelper helper;
            helper.areasByZLevel = &areasByZLevel;
            helper.grid = &grid;
            helper.gridXSize = gridXSize;
            helper.gridYSize = gridYSize;
            helper.gridZSize = gridZSize;
            helper.min = &min;
            helper.spacing = spacing;
            helper.tolerance = 0;
            helper.waypointZLevels = &waypointZLevels;
            helper.zSpacing = zSpacing;
            helper.zTolerance = zTolerance;
            initializeGrid(helper);
            Logger::log(NK_INFO, "Building waypoints within areas...");

            addWaypointsForGrid(reasoningGrid, navMesh, helper);
            if (helper.result == -1) {
               airg.airgLoadState.push_back(true);
                return;
            }
            helper.tolerance = tolerance;
            int waypointsWithinAreas = reasoningGrid->m_WaypointList.size();
            Logger::log(NK_INFO,
                        ("Built " + std::to_string(waypointsWithinAreas) + " waypoints within areas.").c_str());
            Logger::log(NK_INFO, "Building waypoints within tolerance around areas...");
            addWaypointsForGrid(reasoningGrid, navMesh, helper);
            if (helper.result == -1) {
                airg.airgLoadState.push_back(true);
                return;
            }

            reasoningGrid->m_nNodeCount = reasoningGrid->m_WaypointList.size();

            // Build neighbor differences helper: South is 0, increases CCW
            std::pair<int, int> gridIndexDiff[8]{
                std::pair(0, -1),
                std::pair(1, -1),
                std::pair(1, 0),
                std::pair(1, 1),
                std::pair(0, 1),
                std::pair(-1, 1),
                std::pair(-1, 0),
                std::pair(-1, -1)
            };

            // Fill in neighbor values from grid points with waypoints
            for (int zi = 0; zi < gridZSize; zi++) {
                for (int yi = 0; yi < gridYSize; yi++) {
                    for (int xi = 0; xi < gridXSize; xi++) {
                        int waypointIndex = (*(*grid[zi])[yi])[xi];
                        if (waypointIndex != 65535) {
                            Waypoint *waypoint = &reasoningGrid->m_WaypointList[waypointIndex];
                            Vec3 waypointPos{waypoint->vPos.x, waypoint->vPos.y, waypoint->vPos.z};
                            NavPower::Area *area = helper.waypointAreas[waypointIndex];
                            for (int neighborNum = 0; neighborNum < 8; neighborNum++) {
                                int neighborWaypointIndex = 65535;
                                int nxi = xi + gridIndexDiff[neighborNum].first;
                                int nyi = yi + gridIndexDiff[neighborNum].second;
                                if (nxi >= 0 && nxi < gridXSize &&
                                    nyi >= 0 && nyi < gridYSize &&
                                    (*(*grid[zi])[nyi])[nxi] != 65535 &&
                                    (abs(waypointZLevels[waypointIndex] - waypointZLevels[(*(*grid[zi])[nyi])[nxi]]) <=
                                     1)) {
                                    int potentialNeighborWaypointIndex = (*(*grid[zi])[nyi])[nxi];
                                    //Logger::log(RC_LOG_PROGRESS, ("Checking if waypoints are connected: Waypoint Index: " + std::to_string(waypointIndex) + " PotentialNeighbor Index: " + std::to_string(potentialNeighborWaypointIndex)).c_str());

                                    if (waypointsAreConnected(helper, reasoningGrid, waypointPos, area,
                                                              potentialNeighborWaypointIndex)) {
                                        neighborWaypointIndex = potentialNeighborWaypointIndex;
                                    }
                                }
                                waypoint->nNeighbors.push_back(neighborWaypointIndex);
                            }
                        }
                    }
                }
            }

            // Add visibility data
            std::vector<uint8_t> newVisibilityData(reasoningGrid->m_nNodeCount * 556, 255);
            reasoningGrid->m_pVisibilityData = newVisibilityData;
            reasoningGrid->m_HighVisibilityBits.m_nSize = 0;
            reasoningGrid->m_LowVisibilityBits.m_nSize = 0;
            reasoningGrid->m_deadEndData.m_nSize = reasoningGrid->m_nNodeCount;
            std::vector<uint8_t> deadEndData;
            reasoningGrid->m_deadEndData.m_aBytes = deadEndData;
            int waypointsWithinTolerance = reasoningGrid->m_WaypointList.size() - waypointsWithinAreas;
            Logger::log(NK_INFO,
                        ("Built " + std::to_string(waypointsWithinTolerance) +
                         " waypoints within tolerance around areas.").c_str());
            Logger::log(NK_INFO,
                        ("Built " + std::to_string(reasoningGrid->m_WaypointList.size()) + " waypoints total.").c_str());

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
            msg = "Finished building Airg in ";
            msg += std::to_string(duration.count());
            msg += " seconds";
            Logger::log(NK_INFO, msg.data());
            airg.airgLoadState.push_back(true);
            airg.airgLoaded = true;
        }
    CPPTRACE_CATCH(const std::exception& e) {
        ErrorHandler::openErrorDialog(
            "An unexpected error occurred: " + std::string(e.what()) + "\n\nStack Trace:\n" +
            cpptrace::from_current_exception().to_string());
    } catch (...) {
        ErrorHandler::openErrorDialog("An unexpected error occurred:\n\nStack Trace:\n" +
                                               cpptrace::from_current_exception().to_string());
    }
}
