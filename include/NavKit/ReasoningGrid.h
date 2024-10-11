#pragma once

#include <string>
#include <vector>
#include <set>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdlib.h>
#include "..\NavWeakness\NavPower.h"
#include "..\RecastDemo\SampleInterfaces.h"
#include "..\..\extern\simdjson\simdjson.h"
#include "NavKit.h"

class NavKit;
class Vec4 {
public:
	float x;
	float y;
	float z;
	float w;

	const void writeJson(std::ostream& f);
	void readJson(simdjson::ondemand::object p_Json);
};

class Properties {
public:
	Vec4 vMin;
	Vec4 vMax;
	uint32_t nGridWidth;
	float fGridSpacing;
	uint32_t nVisibilityRange;

	const void writeJson(std::ostream& f);
	void readJson(simdjson::ondemand::object p_Json);
};

class SizedArray {
public:
	std::vector<uint8_t> m_aBytes;
	uint32_t m_nSize;

	const void writeJson(std::ostream& f);
	void readJson(simdjson::ondemand::object p_Json);
};

class Waypoint {
public:
	std::vector<int> nNeighbors;
	Vec4 vPos;
	uint32_t nVisionDataOffset;
	uint32_t nLayerIndex;

	const void writeJson(std::ostream& f);
	void readJson(simdjson::ondemand::object p_Json);
};

class ReasoningGridBuilderHelper {
public:
	float spacing;
	float zSpacing;
	Vec3* min;
	float gridXSize;
	float gridYSize;
	float gridZSize;
	float tolerance;
	float zTolerance;
	std::vector<std::vector<int>>* areasByZLevel;
	std::vector<int>* waypointZLevels;
	std::vector<std::vector<std::vector<int>*>*>* grid;
};

class ReasoningGrid {
public:
	Properties m_Properties;
	SizedArray m_HighVisibilityBits;
	SizedArray m_LowVisibilityBits;
	SizedArray m_deadEndData;
	uint32_t m_nNodeCount;
	std::vector<Waypoint> m_WaypointList;
	std::vector<uint8_t> m_pVisibilityData;

	const void writeJson(std::ostream& f);
	void readJson(const char* p_AirgPath);
	bool HasVisibility(unsigned int nFromNode, unsigned int nToNode, uint8_t nLow);
	static void build(ReasoningGrid* airg, NavPower::NavMesh* navMesh, NavKit* ctx, float spacing, float zSpacing, float tolerance, float zTolerance);
};
