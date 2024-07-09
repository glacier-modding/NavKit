#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdlib.h>
#if _WIN32
#define SIMD_PATH "..\\..\\extern\\simdjson\\simdjson.h"
#else
#define SIMD_PATH "../../extern/simdjson/simdjson.h"
#endif
#include SIMD_PATH


class Vec4 {
public:
	float x;
	float y;
	float z;
	float w;

	void writeJson(std::ostream& f);
	void readJson(auto p_Json);
};

class Properties {
public:
	Vec4 vMin;
	Vec4 vMax;
	uint32_t nGridWidth;
	float fGridSpacing;
	uint32_t nVisibilityRange;

	void writeJson(std::ostream& f);
	void readJson(auto p_Json);
};

class SizedArray {
public:
	std::vector<uint32_t> m_aBytes;
	uint32_t m_nSize;

	void writeJson(std::ostream& f);
	void readJson(auto p_Json);
};

class Waypoint {
public:
	short nNeighbor0;
	short nNeighbor1;
	short nNeighbor2;
	short nNeighbor3;
	short nNeighbor4;
	short nNeighbor5;
	short nNeighbor6;
	short nNeighbor7;
	Vec4 vPos;
	uint32_t nVisionDataOffset;
	uint32_t nLayerIndex;

	void writeJson(std::ostream& f);
	void readJson(auto p_Json);
};

class Airg {
public:
	Properties m_Properties;
	SizedArray m_HighVisibilityBits;
	SizedArray m_LowVisibilityBits;
	SizedArray m_deadEndData;
	uint32_t m_nNodeCount;
	std::vector<Waypoint> m_WaypointList;
	std::vector<uint32_t> m_pVisibilityData;

	void writeJson(std::ostream& f);
	void readJson(const char* p_AirgPath);
};
