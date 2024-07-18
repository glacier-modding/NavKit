#include "..\include\NavKit\Airg.h"
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

	nGridWidth = uint64_t(p_Json["nGridWidth"]);
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

const void Airg::writeJson(std::ostream& f) {
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

void Airg::readJson(const char* p_AirgPath) {
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

// From https://wrfranklin.org/Research/Short_Notes/pnpoly.html
int pnpoly(int nvert, float* vertx, float* verty, float testx, float testy)
{
	int i, j, c = 0;
	for (i = 0, j = nvert - 1; i < nvert; j = i++) {
		if (((verty[i] > testy) != (verty[j] > testy)) &&
			(testx < (vertx[j] - vertx[i]) * (testy - verty[i]) / (verty[j] - verty[i]) + vertx[i]))
			c = !c;
	}
	return c;
}

void Airg::build(NavPower::NavMesh* navMesh, BuildContext* ctx) {
	ctx->log(RC_LOG_PROGRESS, "Started building Airg.");
	double spacing = 1;
	// Grid = Z[Y[X[]]]
	std::vector<std::vector<std::vector<int>>> grid;
	Vec3 min = navMesh->m_graphHdr->m_bbox.m_min;
	min.X += spacing / 2;
	min.Y += spacing / 2;
	Vec3 max = navMesh->m_graphHdr->m_bbox.m_max;
	int gridXSize = std::ceil((max.X - min.X) / spacing);
	int gridYSize = std::ceil((max.Y - min.Y) / spacing);
	int gridZSize = 1;
	int wayPointIndex = 0;
	for (int zi = 0; zi < gridZSize; zi++) {
		std::vector<std::vector<int>> yRow;
		for (int yi = 0; yi < gridYSize; yi++) {
			std::vector<int> xRow;
			for (int xi = 0; xi < gridXSize; xi++) {
				bool pointInArea = false;
				double x = min.X + xi * spacing;
				double y = min.Y + yi * spacing;
				double z = min.Z + zi * spacing;
				for (auto area : navMesh->m_areas) {
					const int areaPointCount = area.m_edges.size();
					float areaXCoords[10];
					float areaYCoords[10];
					for (int i = 0; i < areaPointCount; i++) {
						areaXCoords[i] = area.m_edges[i]->m_pos.X;
						areaYCoords[i] = area.m_edges[i]->m_pos.Y;
					}
					pointInArea = pnpoly(areaPointCount, areaXCoords, areaYCoords, x, y);
					if (pointInArea) {
						break;
					}
				}
				xRow.push_back(pointInArea ? wayPointIndex++ : 65535);
				if (pointInArea) {
					Waypoint waypoint;
					waypoint.vPos.x = x;
					waypoint.vPos.y = y;
					waypoint.vPos.z = z;
					m_WaypointList.push_back(waypoint);
				}
			}
			yRow.push_back(xRow);
		}
		grid.push_back(yRow);
	}
	
	// Neighbors: South is 0, increases CCW
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

	for (int zi = 0; zi < gridZSize; zi++) {
		for (int yi = 0; yi < gridYSize; yi++) {
			for (int xi = 0; xi < gridXSize; xi++) {
				int waypointIndex = grid[zi][yi][xi];
				if (waypointIndex != 65535) {
					for (int neighborNum = 0; neighborNum < 8; neighborNum++) {
						int neighborWaypointIndex = 65535;
						int nxi = xi + gridIndexDiff[neighborNum].first;
						int nyi = yi + gridIndexDiff[neighborNum].second;
						if (nxi >= 0 && nxi < gridXSize &&
							nyi >= 0 && nyi < gridYSize) {
							neighborWaypointIndex = grid[zi][nyi][nxi];
						}
						m_WaypointList[waypointIndex].nNeighbors.push_back(neighborWaypointIndex);
					}
				}
			}
		}
	}
}