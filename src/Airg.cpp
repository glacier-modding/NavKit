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