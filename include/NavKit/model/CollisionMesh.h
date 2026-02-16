#pragma once

#include "../../ResourceLib_HM3/Generated/HM3/ZHMGen.h"

class STriangle {
public:
    int v0;
    int v1;
    int v2;
};

enum ECollisionSource : __int32 {
    CS_BoxPrimitive = 0x0,
    CS_TriMesh = 0x1,
};

class ZResourceID;

class CollisionMesh {
public:
    std::vector<float4> m_Vertices;
    std::vector<float4> m_FaceNormals;
    std::vector<STriangle> m_Faces;
    SMatrix m_mTransform;
    float4 m_vMin;
    float4 m_vMax;
    ZResourceID* m_ResourceID;
    bool m_bMeshLoaded;
    ECollisionSource m_Source;
};
