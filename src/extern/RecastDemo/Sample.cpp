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
// This source has been modified from the original by Daniel Bierek for use
// in NavKit.

#include <math.h>
#include <stdio.h>
#include "..\..\include\RecastDemo\Sample.h"
#include "..\..\include\RecastDemo\InputGeom.h"
#include "Recast.h"
#include "RecastDebugDraw.h"
#include "DetourDebugDraw.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "..\..\include\RecastDemo\imgui.h"
#include "SDL.h"
#include "SDL_opengl.h"
#include <fstream>
#include <vector>

#ifdef WIN32
#	define snprintf _snprintf
#endif

SampleTool::~SampleTool()
{
	// Defined out of line to fix the weak v-tables warning
}

SampleToolState::~SampleToolState()
{
	// Defined out of line to fix the weak v-tables warning
}

unsigned int SampleDebugDraw::areaToCol(unsigned int area)
{
	switch(area)
	{
	// Ground (0) : light blue
	case SAMPLE_POLYAREA_GROUND: return duRGBA(0, 192, 255, 255);
	// Water : blue
	case SAMPLE_POLYAREA_WATER: return duRGBA(0, 0, 255, 255);
	// Road : brown
	case SAMPLE_POLYAREA_ROAD: return duRGBA(50, 20, 12, 255);
	// Door : cyan
	case SAMPLE_POLYAREA_DOOR: return duRGBA(0, 255, 255, 255);
	// Grass : green
	case SAMPLE_POLYAREA_GRASS: return duRGBA(0, 255, 0, 255);
	// Jump : yellow
	case SAMPLE_POLYAREA_JUMP: return duRGBA(255, 255, 0, 255);
	// Unexpected : red
	default: return duRGBA(255, 0, 0, 255);
	}
}

Sample::Sample() :
	m_geom(0),
	m_navMesh(0),
	m_navQuery(0),
	m_crowd(0),
	m_navMeshDrawFlags(DU_DRAWNAVMESH_OFFMESHCONS|DU_DRAWNAVMESH_CLOSEDLIST),
	m_filterLowHangingObstacles(true),
	m_filterLedgeSpans(true),
	m_filterWalkableLowHeightSpans(true),
	m_tool(0),
	m_ctx(0)
{
	resetCommonSettings();
	m_navQuery = dtAllocNavMeshQuery();

	for (int i = 0; i < MAX_TOOLS; i++)
		m_toolStates[i] = 0;
}

Sample::~Sample()
{
	dtFreeNavMeshQuery(m_navQuery);
	dtFreeNavMesh(m_navMesh);
	delete m_tool;
	for (int i = 0; i < MAX_TOOLS; i++)
		delete m_toolStates[i];
}

void Sample::setTool(SampleTool* tool)
{
	delete m_tool;
	m_tool = tool;
	if (tool)
		m_tool->init(this);
}

void Sample::handleSettings()
{
}

void Sample::handleTools()
{
}

void Sample::handleDebugMode()
{
}

void Sample::handleRender()
{
	if (!m_geom)
		return;

	// Draw mesh
	duDebugDrawTriMesh(&m_dd, m_geom->getMesh()->getVerts(), m_geom->getMesh()->getVertCount(),
					   m_geom->getMesh()->getTris(), m_geom->getMesh()->getNormals(), m_geom->getMesh()->getTriCount(), 0, 1.0f);
	// Draw bounds
	const float* bmin = m_geom->getMeshBoundsMin();
	const float* bmax = m_geom->getMeshBoundsMax();


	duDebugDrawBoxWire(&m_dd, bmin[0],bmin[1],bmin[2], bmax[0],bmax[1],bmax[2], duRGBA(255,255,255,128), 1.0f);
}

void Sample::handleRenderOverlay(double* /*proj*/, double* /*model*/, int* /*view*/)
{
}

void Sample::handleMeshChanged(InputGeom* geom)
{
	m_geom = geom;

	const BuildSettings* buildSettings = geom->getBuildSettings();
	if (buildSettings)
	{
		m_cellSize = buildSettings->cellSize;
		m_cellHeight = buildSettings->cellHeight;
		m_agentHeight = buildSettings->agentHeight;
		m_agentRadius = buildSettings->agentRadius;
		m_agentMaxClimb = buildSettings->agentMaxClimb;
		m_agentMaxSlope = buildSettings->agentMaxSlope;
		m_regionMinSize = buildSettings->regionMinSize;
		m_regionMergeSize = buildSettings->regionMergeSize;
		m_edgeMaxLen = buildSettings->edgeMaxLen;
		m_edgeMaxError = buildSettings->edgeMaxError;
		m_vertsPerPoly = buildSettings->vertsPerPoly;
		m_detailSampleDist = buildSettings->detailSampleDist;
		m_detailSampleMaxError = buildSettings->detailSampleMaxError;
		m_partitionType = buildSettings->partitionType;
	}
}

void Sample::collectSettings(BuildSettings& settings)
{
	settings.cellSize = m_cellSize;
	settings.cellHeight = m_cellHeight;
	settings.agentHeight = m_agentHeight;
	settings.agentRadius = m_agentRadius;
	settings.agentMaxClimb = m_agentMaxClimb;
	settings.agentMaxSlope = m_agentMaxSlope;
	settings.regionMinSize = m_regionMinSize;
	settings.regionMergeSize = m_regionMergeSize;
	settings.edgeMaxLen = m_edgeMaxLen;
	settings.edgeMaxError = m_edgeMaxError;
	settings.vertsPerPoly = m_vertsPerPoly;
	settings.detailSampleDist = m_detailSampleDist;
	settings.detailSampleMaxError = m_detailSampleMaxError;
	settings.partitionType = m_partitionType;
}


void Sample::resetCommonSettings()
{
	m_cellSize = 0.1f;
	m_cellHeight = 0.1f;
	m_agentHeight = 1.8f;
	m_agentRadius = 0.2f;
	m_agentMaxClimb = 0.41f;
	m_agentMaxSlope = 45.0f;
	m_regionMinSize = 25;
	m_regionMergeSize = 30;
	m_edgeMaxLen = 5.0f;
	m_edgeMaxError = 1.4f;
	m_vertsPerPoly = 3.0f;
	m_detailSampleDist = 1.5f;
	m_detailSampleMaxError = 1.4f;
	m_partitionType = SAMPLE_PARTITION_WATERSHED;
}

void Sample::handleCommonSettings()
{
	imguiLabel("Rasterization");
	imguiSlider("Cell Size", &m_cellSize, 0.01f, 0.4f, 0.01f);
	imguiSlider("Cell Height", &m_cellHeight, 0.01f, 0.4f, 0.01f);

	imguiSeparator();
	imguiLabel("Agent");
	imguiSlider("Height", &m_agentHeight, 0.01f, 3.0f, 0.01f);
	imguiSlider("Radius", &m_agentRadius, 0.00f, 1.0f, 0.01f);
	imguiSlider("Max Climb", &m_agentMaxClimb, 0.01f, 1.0f, 0.01f);
	imguiSlider("Max Slope", &m_agentMaxSlope, 0.0f, 90.0f, 1.0f);

	imguiSeparator();
	imguiLabel("Region");
	imguiSlider("Min Region Size", &m_regionMinSize, 0.0f, 50.0f, 1.0f);
	imguiSlider("Merged Region Size", &m_regionMergeSize, 0.0f, 550.0f, 1.0f);

	imguiSeparator();
	imguiLabel("Partitioning");
	if (imguiCheck("Watershed", m_partitionType == SAMPLE_PARTITION_WATERSHED))
		m_partitionType = SAMPLE_PARTITION_WATERSHED;
	if (imguiCheck("Monotone", m_partitionType == SAMPLE_PARTITION_MONOTONE))
		m_partitionType = SAMPLE_PARTITION_MONOTONE;
	if (imguiCheck("Layers", m_partitionType == SAMPLE_PARTITION_LAYERS))
		m_partitionType = SAMPLE_PARTITION_LAYERS;

	imguiSeparator();
	imguiLabel("Filtering");
	if (imguiCheck("Low Hanging Obstacles", m_filterLowHangingObstacles))
		m_filterLowHangingObstacles = !m_filterLowHangingObstacles;
	if (imguiCheck("Ledge Spans", m_filterLedgeSpans))
		m_filterLedgeSpans= !m_filterLedgeSpans;
	if (imguiCheck("Walkable Low Height Spans", m_filterWalkableLowHeightSpans))
		m_filterWalkableLowHeightSpans = !m_filterWalkableLowHeightSpans;

	imguiSeparator();
	imguiLabel("Polygonization");
	imguiSlider("Max Edge Length", &m_edgeMaxLen, 0.1f, 50.0f, 0.1f);
	imguiSlider("Max Edge Error", &m_edgeMaxError, 0.01f, 3.0f, 0.01f);
	imguiSlider("Verts Per Poly", &m_vertsPerPoly, 3.0f, 12.0f, 1.0f);

	imguiSeparator();
	imguiLabel("Detail Mesh");
	imguiSlider("Sample Distance", &m_detailSampleDist, 0.9f, 16.0f, 0.1f);
	imguiSlider("Max Sample Error", &m_detailSampleMaxError, 0.01f, 16.0f, 0.01f);

	imguiSeparator();
}

void Sample::handleClick(const float* s, const float* p, bool shift)
{
	if (m_tool)
		m_tool->handleClick(s, p, shift);
}

void Sample::handleToggle()
{
	if (m_tool)
		m_tool->handleToggle();
}

void Sample::handleStep()
{
	if (m_tool)
		m_tool->handleStep();
}

bool Sample::handleBuild()
{
	return true;
}

void Sample::handleUpdate(const float dt)
{
	if (m_tool)
		m_tool->handleUpdate(dt);
	updateToolStates(dt);
}


void Sample::updateToolStates(const float dt)
{
	for (int i = 0; i < MAX_TOOLS; i++)
	{
		if (m_toolStates[i])
			m_toolStates[i]->handleUpdate(dt);
	}
}

void Sample::initToolStates(Sample* sample)
{
	for (int i = 0; i < MAX_TOOLS; i++)
	{
		if (m_toolStates[i])
			m_toolStates[i]->init(sample);
	}
}

void Sample::resetToolStates()
{
	for (int i = 0; i < MAX_TOOLS; i++)
	{
		if (m_toolStates[i])
			m_toolStates[i]->reset();
	}
}

void Sample::renderToolStates()
{
	for (int i = 0; i < MAX_TOOLS; i++)
	{
		if (m_toolStates[i])
			m_toolStates[i]->handleRender();
	}
}

void Sample::renderOverlayToolStates(double* proj, double* model, int* view)
{
	for (int i = 0; i < MAX_TOOLS; i++)
	{
		if (m_toolStates[i])
			m_toolStates[i]->handleRenderOverlay(proj, model, view);
	}
}

static const int NAVMESHSET_MAGIC = 'M'<<24 | 'S'<<16 | 'E'<<8 | 'T'; //'MSET';
static const int NAVMESHSET_VERSION = 1;

struct NavMeshSetHeader
{
	int magic;
	int version;
	int numTiles;
	dtNavMeshParams params;
};

struct NavMeshTileHeader
{
	dtTileRef tileRef;
	int dataSize;
};

dtNavMesh* Sample::loadAll(const char* path)
{
	FILE* fp = fopen(path, "rb");
	if (!fp) return 0;

	// Read header.
	NavMeshSetHeader header;
	size_t readLen = fread(&header, sizeof(NavMeshSetHeader), 1, fp);
	if (readLen != 1)
	{
		fclose(fp);
		return 0;
	}
	if (header.magic != NAVMESHSET_MAGIC)
	{
		fclose(fp);
		return 0;
	}
	if (header.version != NAVMESHSET_VERSION)
	{
		fclose(fp);
		return 0;
	}

	dtNavMesh* mesh = dtAllocNavMesh();
	if (!mesh)
	{
		fclose(fp);
		return 0;
	}
	dtStatus status = mesh->init(&header.params);
	if (dtStatusFailed(status))
	{
		fclose(fp);
		return 0;
	}

	// Read tiles.
	for (int i = 0; i < header.numTiles; ++i)
	{
		NavMeshTileHeader tileHeader;
		readLen = fread(&tileHeader, sizeof(tileHeader), 1, fp);
		if (readLen != 1)
		{
			fclose(fp);
			return 0;
		}

		if (!tileHeader.tileRef || !tileHeader.dataSize)
			break;

		unsigned char* data = (unsigned char*)dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM);
		if (!data) break;
		memset(data, 0, tileHeader.dataSize);
		readLen = fread(data, tileHeader.dataSize, 1, fp);
		if (readLen != 1)
		{
			dtFree(data);
			fclose(fp);
			return 0;
		}

		mesh->addTile(data, tileHeader.dataSize, DT_TILE_FREE_DATA, tileHeader.tileRef, 0);
	}

	fclose(fp);

	return mesh;
}

#define DT_NULL_LINK 0xffffffff
#define DT_EX_LINK 0x8000
#define DT_EX_LINK_SHIFT 15
#define DT_TILE_MASK 0xf
#define DT_POLY_MASK 0x3ff
#define DT_NEXT_MASK 0xff
int decodeNeiToTotalIndex(uint16_t polyIndex, int tileIndex, const std::vector<int>& tilePolyCounts) {
    if (polyIndex == DT_NULL_LINK) {
        return -1;
    }

    if (polyIndex & DT_EX_LINK) {
        int targetTileIndex = (polyIndex >> DT_EX_LINK_SHIFT) & DT_TILE_MASK;
        int targetPolyIndex = polyIndex & DT_POLY_MASK;

        int totalIndex = 0;
        for (int i = 0; i < targetTileIndex; ++i) {
            totalIndex += tilePolyCounts[i];
        }
        totalIndex += targetPolyIndex;
        return totalIndex;
    }
    int totalIndex = 0;
    for (int i = 0; i < tileIndex; ++i) {
        totalIndex += tilePolyCounts[i];
    }
    totalIndex += polyIndex;
    return totalIndex - 1;
}

int getTotalIndex(const int tileIndex, const int polyIndex, const std::vector<int>& tilePolyCounts) {
	int totalIndex = 0;
	for (int i = 0; i < tileIndex; ++i) {
		totalIndex += tilePolyCounts[i];
	}
	totalIndex += polyIndex;
	return totalIndex;
}

struct PolyInfo {
	int tileIndex;
	int tilePolyIndex;
	int totalPolyIndex;
};

void Sample::saveAll(const char* s_OutputFileName)
{
	const dtNavMesh* mesh = m_navMesh;
	if (!mesh) return;

	remove(s_OutputFileName);

	std::ofstream f(s_OutputFileName);
	f << std::fixed << std::boolalpha;
	f << "{\"Areas\":[";
	std::vector<int> tilePolyCounts;
	for (int tileIndex = 0; tileIndex < mesh->getMaxTiles(); ++tileIndex) {
		const dtMeshTile* tile = mesh->getTile(tileIndex);
		if (!tile || !tile->header || !tile->dataSize) {
			tilePolyCounts.push_back(0);
			continue;
		}
		tilePolyCounts.push_back(tile->header->polyCount);
	}
	int totalAreaCount = 0;
	for (int count : tilePolyCounts) {
		totalAreaCount += count;
	}
	std::vector<unsigned short> allPolyflags;
	int curPoly = 0;
	std::vector<PolyInfo> indexToPolyInfo(totalAreaCount, {-1, -1});

	for (int tileIndex = 0; tileIndex < mesh->getMaxTiles(); ++tileIndex) {
		const dtMeshTile *tile = mesh->getTile(tileIndex);
		if (!tile || !tile->header || !tile->dataSize) {
			continue;
		}
		for (int polyIndex = 0; polyIndex < tile->header->polyCount; polyIndex++) {
			allPolyflags.push_back(tile->polys[polyIndex].flags);
			if (tile->polys[polyIndex].flags != SAMPLE_POLYFLAGS_DISABLED) {
				const int totalIndex = getTotalIndex(tileIndex, polyIndex, tilePolyCounts);
				indexToPolyInfo[totalIndex] = {tileIndex, polyIndex, curPoly++};
			}
		}
	}
	curPoly = 0;
	bool validAreaFound = false;
	// Store tiles.
	for (int tileIndex = 0; tileIndex < mesh->getMaxTiles(); ++tileIndex)
	{
		const dtMeshTile* tile = mesh->getTile(tileIndex);
		if (!tile || !tile->header || !tile->dataSize) continue;
		int numPolys = tile->header->polyCount;
		for (int polyIndex = 0; polyIndex < numPolys; polyIndex++) {
			const dtPoly& poly = tile->polys[polyIndex];

			const int totalIndex = getTotalIndex(tileIndex, polyIndex, tilePolyCounts);
			if (poly.flags == SAMPLE_POLYFLAGS_DISABLED) {
				continue;
			}
			if (validAreaFound) {
				f << ",";
			}
			if (!validAreaFound) {
				validAreaFound = true;
			}
			f << "{";
			f << R"("Area":{"Index":)" << indexToPolyInfo[totalIndex].totalPolyIndex;
			f << "},";
			f << "\"Edges\":[";

			int numVerts = poly.vertCount;
			for (int vi = 0; vi < numVerts; vi++) {
				unsigned short vertIndex = poly.verts[vi];
				float X = tile->verts[vertIndex * 3];
				float Y = -tile->verts[vertIndex * 3 + 2];
				float Z = tile->verts[vertIndex * 3 + 1];
				f << "{";
				for (unsigned int linkIndex = poly.firstLink; linkIndex != DT_NULL_LINK; linkIndex = tile->links[linkIndex].next)
				{
					const dtLink& link = tile->links[linkIndex];
					if (link.edge != vi) {
						continue;
					}
					const dtMeshTile* targetTile = nullptr;
					const dtPoly* targetPoly = nullptr;
					mesh->getTileAndPolyByRef(link.ref, &targetTile, &targetPoly);
					if (targetTile && targetPoly)
					{
						int targetTileIndex = -1;
						for (int i = 0; i < mesh->getMaxTiles(); ++i) {
							if (mesh->getTile(i) == targetTile) {
								targetTileIndex = i;
								break;
							}
						}
						if (targetTileIndex != -1) {
							int targetPolyIndex = -1;
							for (int i = 0; i < targetTile->header->polyCount; ++i) {
								if (&targetTile->polys[i] == targetPoly) {
									targetPolyIndex = i;
									break;
								}
							}
							if (targetPolyIndex != -1) {
								int neiTotalIndex = getTotalIndex(targetTileIndex, targetPolyIndex, tilePolyCounts);
								if (neiTotalIndex < totalAreaCount && neiTotalIndex != -1) {
									if (allPolyflags[neiTotalIndex] != SAMPLE_POLYFLAGS_DISABLED) {
										f << "\"Adjacent Area\":";
										f << indexToPolyInfo[neiTotalIndex].totalPolyIndex + 1 << ",";
									}
								}
							}
						}
					}
				}
				f << "\"Position\":";
				f << "{";
				f << "\"X\":" << X << ",";
				f << "\"Y\":" << Y << ",";
				f << "\"Z\":" << Z;
				f << "}";
				f << "}";
				if (vi < numVerts - 1)
				{
					f << ",";
				}
			}

			f << "]}";
		}
	}
	f << "],";
	f << R"("NavpJsonVersion": "0.1")";
	f << "}";
	f.close();
}
