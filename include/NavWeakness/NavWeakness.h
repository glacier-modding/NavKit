#pragma once
#include "NavPower.h"
namespace NavWeakness {
    void OutputNavMesh_NAVP(const char* p_NavMeshPath, const char* p_NavMeshOutputPath, bool p_SourceIsJson);
    void OutputNavMesh_JSON(const char* p_NavMeshPath, const char* p_NavMeshOutputPath, bool p_SourceIsJson);
    void OutputNavMesh_JSON_Write(NavPower::NavMesh* p_NavMesh, const char* p_NavMeshOutputPath);
    void OutputNavMesh_NAVP_Write(NavPower::NavMesh* p_NavMesh, const char* p_NavMeshOutputPath);
    NavPower::NavMesh LoadNavMeshFromJson(const char* p_NavMeshPath);
    NavPower::NavMesh LoadNavMeshFromBinary(const char* p_NavMeshPath);
} // namespace NavWeakness
