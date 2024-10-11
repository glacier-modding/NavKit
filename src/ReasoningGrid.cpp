#include "..\include\NavKit\ReasoningGrid.h"
const void Vec4::writeJson(std::ostream& f) {
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

const void Properties::writeJson(std::ostream& f) {
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

const void SizedArray::writeJson(std::ostream& f) {
	f << "{";
	f << "\"m_aBytes\"" << ":";
	f << "[";
	for (size_t i = 0; i < m_aBytes.size(); i++)
	{
		f << m_aBytes[i];
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
	for (uint64_t byteJson : m_aBytesJson) {
		m_aBytes.push_back(byteJson);
	}
	m_nSize = uint64_t(p_Json["m_nSize"]);
}

const void Waypoint::writeJson(std::ostream& f) {
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
	nNeighbors.push_back(uint64_t(p_Json["nNeighbor0"]));
	nNeighbors.push_back(uint64_t(p_Json["nNeighbor1"]));
	nNeighbors.push_back(uint64_t(p_Json["nNeighbor2"]));
	nNeighbors.push_back(uint64_t(p_Json["nNeighbor3"]));
	nNeighbors.push_back(uint64_t(p_Json["nNeighbor4"]));
	nNeighbors.push_back(uint64_t(p_Json["nNeighbor5"]));
	nNeighbors.push_back(uint64_t(p_Json["nNeighbor6"]));
	nNeighbors.push_back(uint64_t(p_Json["nNeighbor7"]));

	simdjson::ondemand::object vPosJson = p_Json["vPos"];
	vPos.readJson(vPosJson);

	nVisionDataOffset = uint64_t(p_Json["nVisionDataOffset"]);
	nLayerIndex = uint64_t(p_Json["nLayerIndex"]);
}

const void ReasoningGrid::writeJson(std::ostream& f) {
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
	for (size_t i = 0; i < m_pVisibilityData.size(); i++)
	{
		f << m_pVisibilityData[i];
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

void ReasoningGrid::readJson(const char* p_AirgPath) {
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
	for (simdjson::ondemand::value waypointJson : m_WaypointListJson)
	{
		Waypoint waypoint;
		waypoint.readJson(waypointJson);
		m_WaypointList.push_back(waypoint);
	}
	simdjson::ondemand::array m_pVisibilityDataJson = p_AirgDocument["m_pVisibilityData"];
	for (uint64_t visibilityDatum : m_pVisibilityDataJson) {
		m_pVisibilityData.push_back(visibilityDatum);
	}
}

int orientation(Vec3 p, Vec3 q, Vec3 r) {
	int val = (q.Y - p.Y) * (r.X - q.X) - (q.X - p.X) * (r.Y - q.Y);
	if (val == 0) return 0; // Collinear
	return (val > 0) ? 1 : 2; // Clockwise or counterclockwise
}

Vec3 expandedPoint(Vec3 origin, Vec3 vert, float tolerance) {
	float toleranceX = vert.X > origin.X ? tolerance : -tolerance;
	float toleranceY = vert.Y > origin.Y ? tolerance : -tolerance;
	Vec3 expanded { vert.X + toleranceX, vert.Y + toleranceY, 0 };
	return expanded;
}

Vec3 reflect(Vec3 p, float x1, float y1, float x2, float y2) {
	float dx, dy, a, b;
	float x, y;

	dx = x2 - x1;
	dy = y2 - y2;

	a = (dx * dx - dy * dy) / (dx * dx + dy * dy);
	b = 2 * dx * dy / (dx * dx + dy * dy);

	x = a * (p.X - x1) + b * (p.Y - y1) + x1;
	y = b * (p.X - x1) - a * (p.Y - y1) + y1;
	return { x, y, 0 };
}

float distanceFromEdge(Vec3 p, Vec3 v, Vec3 w) {
	const float l2 = v.DistanceSquaredTo(w);  // i.e. |w-v|^2 -  avoid a sqrt
	if (l2 == 0.0) return p.DistanceTo(v);   // v == w case
	// Consider the line extending the segment, parameterized as v + t (w - v).
	// We find projection of point p onto the line. 
	// It falls where t = [(p-v) . (w-v)] / |w-v|^2
	// We clamp t from [0,1] to handle points outside the segment vw.
	const float t = std::max(0.0f, std::min(1.0f, (p - v).Dot(w - v) / l2));
	const Vec3 projection = v + (w - v) * t;  // Projection falls on the segment
	return p.DistanceTo(projection);
}
// From https://wrfranklin.org/Research/Short_Notes/pnpoly.html
int closestEdge(int nvert, float* vertx, float* verty, float testx, float testy, float originx, float originy, float tolerance, NavKit* navKit) {
	
	///////////// Ray method
	int cur, prev;
	bool c = false;
	int closestEdge = -2;
	float minDist = 10000;
	Vec3 testVert{ testx, testy, 0 };
	for (cur = 0, prev = nvert - 1; cur < nvert; prev = cur++) {
		float curToleranceX = 0; //vertx[i] > originx ? tolerance : -tolerance;
		float curToleranceY = 0; //verty[i] > originy ? tolerance : -tolerance;
		float prevToleranceX = 0; //vertx[j] > originx ? tolerance : -tolerance;
		float prevToleranceY = 0; //verty[j] > originy ? tolerance : -tolerance;
		float curX = vertx[cur] + curToleranceX;
		float curY = verty[cur] + curToleranceY;
		float prevX = vertx[prev] + prevToleranceX;
		float prevY = verty[prev] + prevToleranceY;
		Vec3 v{ prevX, prevY, 0 };
		Vec3 w{ curX, curY, 0 };

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
	navKit->log(rcLogCategory::RC_LOG_PROGRESS, ("Within tolerance: " + std::to_string(minDist) + " at Test X: " + std::to_string(testx) + " Test Y: " + std::to_string(testy) + " with Origin X: " + std::to_string(originx) + " Origin Y: " + std::to_string(originy) + " Point 1 X: " + std::to_string(vertx[closestEdge]) + " Point 1 Y: " + std::to_string(verty[closestEdge]) + " Point 2 X: " + std::to_string(vertx[(closestEdge + 1) % nvert]) + " Point 2 Y: " + std::to_string(verty[(closestEdge + 1) % nvert])).c_str());

	return closestEdge;

	/////////////// Orientation method
	//int i, j;
	//Vec3 prevVert{ vertx[nvert - 1], verty[nvert - 1], 0 };
	//Vec3 curVert{ vertx[0], verty[0], 0 };
	//Vec3 origin{ originx, originy, 0 };
	//if (tolerance != 0) {
	//	//prevVert = expandedPoint(origin, prevVert, tolerance);
	//	//curVert = expandedPoint(origin, curVert, tolerance);
	//}
	//Vec3 testVert{ testx, testy, 0 };
	//int prev = orientation(prevVert, curVert, testVert);
	//int closestEdge = -2;
	//float minDist = 10000;
	//bool inside = true;
	//for (i = 1; i < nvert; i++) {
	//	prevVert.X = vertx[i - 1];
	//	prevVert.Y = verty[i - 1];
	//	curVert.X = vertx[i];
	//	curVert.Y = verty[i];
	//	if (tolerance != 0) {
	//		//prevVert = expandedPoint(origin, prevVert, tolerance);
	//		//curVert = expandedPoint(origin, curVert, tolerance);
	//	}
	//	float curDist = distanceFromEdge(testVert, prevVert.X, prevVert.Y, curVert.X, curVert.Y);
	//	if (minDist > curDist) {
	//		minDist = curDist;
	//		closestEdge = i - 1;
	//	}
	//	int cur = orientation(prevVert, curVert, testVert);
	//	if (cur != prev) {
	//		inside = false;
	//	}
	//}
	//if (inside) {
	//	return -1;
	//}
	//if (minDist > tolerance) {
	//	return -2;
	//}
	//return closestEdge;
}

float calcZ(Vec3 p1, Vec3 p2, Vec3 p3, float x, float y) {
	float dx1 = x - p1.X;
	float dy1 = y - p1.Y;
	float dx2 = p2.X - p1.X;
	float dy2 = p2.Y - p1.Y;
	float dx3 = p3.X - p1.X;
	float dy3 = p3.Y - p1.Y;
	return p1.Z + ((dy1 * dx3 - dx1 * dy3) * (p2.Z - p1.Z) + (dx1 * dy2 - dy1 * dx2) * (p3.Z - p1.Z)) / (dx3 * dy2 - dx2 * dy3);
}

void addWaypointsForGrid(ReasoningGrid* airg, NavPower::NavMesh* navMesh, NavKit* navKit, ReasoningGridBuilderHelper h) {
	int wayPointIndex = airg->m_WaypointList.size();
	for (int zi = 0; zi < h.gridZSize; zi++) {
		if ((*h.grid).size() == zi) {
			std::vector<std::vector<int>*>* yRow = new std::vector<std::vector<int>*>();
			h.grid->push_back(yRow);
		}
		//navKit->log(rcLogCategory::RC_LOG_PROGRESS, ("Adding waypoints for Z level: " + std::to_string(zi) + " at Z: " + std::to_string(h.min->Z + zi * h.zSpacing)).c_str());
		float minZ = h.min->Z + zi * h.zSpacing;
		float maxZ = h.min->Z + (zi + 1) * h.zSpacing;
		for (int yi = 0; yi < h.gridYSize; yi++) {
			
			if ((*h.grid)[zi]->size() == yi) {
				std::vector<int>* xRow = new std::vector<int>();
				(*h.grid)[zi]->push_back(xRow);
			}
			for (int xi = 0; xi < h.gridXSize; xi++) {
				if ((*(*h.grid)[zi])[yi]->size() > xi && (*(*(*h.grid)[zi])[yi])[xi] != 65535) {
					continue;
				}
				float x = h.min->X + xi * h.spacing;
				float y = h.min->Y + yi * h.spacing;
				float z = h.min->Z + zi * h.zSpacing;
				bool pointInArea = false;
				Vec3 reflected;

				for (int areaIndex : (*h.areasByZLevel)[zi]) {
					auto area = navMesh->m_areas[areaIndex];
					const int areaPointCount = area.m_edges.size();
					z = calcZ(area.m_edges[0]->m_pos, area.m_edges[1]->m_pos, area.m_edges[2]->m_pos, x, y);
					if (z > (minZ - h.zSpacing * h.zTolerance) && z < (maxZ + h.zSpacing * h.zTolerance)) {
						float areaXCoords[20];
						float areaYCoords[20];
						for (int i = 0; i < areaPointCount; i++) {
							areaXCoords[i] = area.m_edges[i]->m_pos.X;
							areaYCoords[i] = area.m_edges[i]->m_pos.Y;
						}
						int edge = closestEdge(areaPointCount, areaXCoords, areaYCoords, x, y, area.m_area->m_pos.X, area.m_area->m_pos.Y, h.tolerance, navKit);
						if (edge == -1) { // In polygon with no adjustment needed
							pointInArea = true;
							break;
						} else if (edge > -1) { // Outside polygon but within tolerance, reflect across closest edge
							navKit->log(rcLogCategory::RC_LOG_PROGRESS, ("|--->Area: " + std::to_string(areaIndex) + " edge: " + std::to_string(edge) + " XI:" + std::to_string(xi) + " YI: " + std::to_string(yi) + " ZI: " + std::to_string(zi)).c_str());

							pointInArea = true;
							NavPower::Binary::Edge* edge0 = area.m_edges[edge];
							NavPower::Binary::Edge* edge1 = area.m_edges[(edge + 1) % area.m_edges.size()];
							float y1 = edge0->m_pos.Y;
							float y2 = edge1->m_pos.Y;
							float x1 = edge0->m_pos.X;
							float x2 = edge1->m_pos.X;
							Vec3 p{ x, y, z };
							reflected = reflect(p, x1, y1, x2, y2);
							x = reflected.X;
							y = reflected.Y;
							break;
						}
					}
				}
				if ((*(*h.grid)[zi])[yi]->size() == xi) {
					(*(*h.grid)[zi])[yi]->push_back(pointInArea ? wayPointIndex++ : 65535);
				}
				else {
					if ((*(*(*h.grid)[zi])[yi])[xi] == 65535 && pointInArea) {
						(*(*(*h.grid)[zi])[yi])[xi] = wayPointIndex++;
					}
				}
				if (pointInArea) {
					Waypoint waypoint;
					waypoint.vPos.x = x;
					waypoint.vPos.y = y;
					waypoint.vPos.z = z + 0.001;
					waypoint.vPos.w = 1.0;
					waypoint.nVisionDataOffset = 556 * airg->m_WaypointList.size();
					waypoint.nLayerIndex = 0;
					airg->m_WaypointList.push_back(waypoint);
					h.waypointZLevels->push_back(zi);
				}
			}
		}
	}
}

void ReasoningGrid::build(ReasoningGrid* airg, NavPower::NavMesh* navMesh, NavKit* navKit, float spacing, float zSpacing, float tolerance, float zTolerance) {
	// Initialize airg properties
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::string msg = "Started building Airg at ";
	msg += std::ctime(&start_time);
	navKit->log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();

	navKit->airg->airgLoadState.push_back(true);
	// grid is set up in this order: Z[Y[X[]]]
	std::vector<std::vector<std::vector<int>*>*> grid;
	Vec3 min = navMesh->m_graphHdr->m_bbox.m_min;
	min.X += spacing / 2;
	min.Y += spacing / 2;
	Vec3 max = navMesh->m_graphHdr->m_bbox.m_max;
	int gridXSize = std::ceil((max.X - min.X) / spacing);
	int gridYSize = std::ceil((max.Y - min.Y) / spacing);
	int gridZSize = std::ceil((max.Z - min.Z) / zSpacing);
	gridZSize = gridZSize > 0 ? gridZSize : 1;
	airg->m_Properties.fGridSpacing = spacing;
	airg->m_Properties.nGridWidth = gridYSize;
	airg->m_Properties.vMin.x = min.X;
	airg->m_Properties.vMin.y = min.Y;
	airg->m_Properties.vMin.z = min.Z;
	airg->m_Properties.vMin.w = 1;
	airg->m_Properties.vMax.x = max.X;
	airg->m_Properties.vMax.y = max.Y;
	airg->m_Properties.vMax.z = max.Z;
	airg->m_Properties.vMax.w = 1;
	airg->m_Properties.nVisibilityRange = 23;


	std::vector<double> areaZMins;
	std::vector<double> areaZMaxes;
	std::vector<int> waypointZLevels;
	std::vector<std::pair<float, float>> waypointPositionAdjustments;

	// Get Z-level for each area
	for (auto area : navMesh->m_areas) {
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
	std::vector<std::vector<int>> areasByZLevel;
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
	addWaypointsForGrid(airg, navMesh, navKit, helper);
	helper.tolerance = tolerance;
	addWaypointsForGrid(airg, navMesh, navKit, helper);

	airg->m_nNodeCount = airg->m_WaypointList.size();
	airg->m_deadEndData.m_nSize = airg->m_nNodeCount;

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
					for (int neighborNum = 0; neighborNum < 8; neighborNum++) {
						int neighborWaypointIndex = 65535;
						int nxi = xi + gridIndexDiff[neighborNum].first;
						int nyi = yi + gridIndexDiff[neighborNum].second;
						if (nxi >= 0 && nxi < gridXSize &&
							nyi >= 0 && nyi < gridYSize &&
							((*(*grid[zi])[nyi])[nxi] == 65535 || abs(waypointZLevels[waypointIndex] - waypointZLevels[(*(*grid[zi])[nyi])[nxi]]) <= 1)) {
							neighborWaypointIndex = (*(*grid[zi])[nyi])[nxi];
						}
						airg->m_WaypointList[waypointIndex].nNeighbors.push_back(neighborWaypointIndex);
					}
				}
			}
		}
	}

	// Add visibility data
	std::vector<uint32_t> visibilityData(airg->m_nNodeCount * 556);
	for (int i = 0; i < airg->m_nNodeCount * 556; i++) {
		visibilityData[i] = 0;
	}
	airg->m_pVisibilityData = visibilityData;
	airg->m_HighVisibilityBits.m_nSize = 0;
	airg->m_LowVisibilityBits.m_nSize = 0;
	airg->m_deadEndData.m_nSize = airg->m_nNodeCount;
	airg->m_deadEndData.m_nSize = airg->m_nNodeCount;

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	msg = "Finished building Airg in ";
	msg += std::to_string(duration.count());
	msg += " seconds";
	navKit->log(RC_LOG_PROGRESS, msg.data());
	navKit->airg->airgLoadState.push_back(true);
	navKit->airg->airgLoaded = true;
}