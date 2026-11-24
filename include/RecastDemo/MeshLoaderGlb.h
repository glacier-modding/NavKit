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

#ifndef MESHLOADER_GLB
#define MESHLOADER_GLB

#include <string>
#include <vector>

class rcMeshLoaderGlb {
public:
    rcMeshLoaderGlb();
    ~rcMeshLoaderGlb();

    bool load(const std::string& fileName);

    const float* getVerts() const {
        return m_verts;
    }
    const float* getNormals() const {
        return m_normals;
    }
    const int* getTris() const {
        return m_tris;
    }
    int getVertCount() const {
        return m_vertCount;
    }
    int getTriCount() const {
        return m_triCount;
    }
    // One flag per triangle: whether it is a water surface (its material is WATER_MATERIAL_NAME). Null when the
    // mesh has no water.
    const unsigned char* getWaterTris() const {
        return m_waterTriCount ? m_waterTris.data() : nullptr;
    }
    int getWaterTriCount() const {
        return m_waterTriCount;
    }
    const std::string& getFileName() const {
        return m_filename;
    }

    // Explicitly disabled copy constructor and copy assignment operator.
    rcMeshLoaderGlb(const rcMeshLoaderGlb&);
    rcMeshLoaderGlb& operator=(const rcMeshLoaderGlb&);

    void addVertex(float x, float y, float z, int& cap);
    void addTriangle(int a, int b, int c, int& cap);

    std::string m_filename;
    float m_scale;
    float* m_verts;
    int* m_tris;
    float* m_normals;
    int m_vertCount;
    int m_triCount;
    std::vector<unsigned char> m_waterTris;
    int m_waterTriCount;
};

#endif // MESHLOADER_GLB
