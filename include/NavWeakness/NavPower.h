/*
NavPower.h - v3.0.0
A header file for use with NavPower's binary navmesh files. Adapted for use
in NavKit.

Licensed under the MIT License
SPDX-License-Identifier: MIT
Copyright (c) 2022+ Anthony Fuller et al.

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>
#include <stdlib.h>
#include <limits>

#include "../../extern/simdjson/simdjson.h"

#include "Vec3.h"

uint32_t RangeCheck(uint32_t val, uint32_t min, uint32_t max);

inline uint32_t c_byteswap_ulong(uint32_t p_Value) {
#if _MSC_VER
    return _byteswap_ulong(p_Value);
#else
    return ((p_Value >> 24) & 0x000000FF) | ((p_Value >> 8) & 0x0000FF00) | ((p_Value << 8) & 0x00FF0000) |
        ((p_Value << 24) & 0xFF000000);
#endif
}

namespace NavPower {
    class BBox {
    public:
        Vec3 m_min;
        Vec3 m_max;

        void writeBinary(std::ostream& f);

        void copy(BBox o);

        bool Contains(const Vec3& p) const {
            return p.X >= m_min.X && p.X <= m_max.X && p.Y >= m_min.Y && p.Y <= m_max.Y && p.Z >= m_min.Z &&
                p.Z <= m_max.Z;
        }
    };

    enum Axis { X, Y, Z, UNDEF };

    std::string AxisToString(Axis axis);

    Axis AxisStringToEnumValue(auto p_Axis);

    enum class AreaUsageFlags : uint32_t {
        AREA_FLAT = 1,
        AREA_STEPS = 8,
    };

    std::string AreaUsageFlagToString(AreaUsageFlags p_AreaUsageFlag);

    AreaUsageFlags AreaUsageFlagStringToEnumValue(auto p_AreaUsageFlag);

    enum EdgeType { EDGE_NORMAL, EDGE_PORTAL };

    std::string EdgeTypeToString(EdgeType p_EdgeType);

    EdgeType EdgeTypeStringToEnumValue(auto p_EdgeType);

    class Area;

    // The binary formats of NavPower data structures
    namespace Binary {
        // POD Struct exactly matching the file layout (320 bytes)
        struct NavGraphHeaderLayout {
            uint32_t m_version;
            uint32_t m_layer;
            uint32_t m_areaBytes;
            uint32_t m_kdTreeBytes;
            uint32_t m_linkRecordBytes;
            uint32_t m_totalBytes;
            float m_buildScale;
            float m_voxSize;
            float m_radius;
            float m_stepHeight;
            float m_height;
            struct {
                float min[3];
                float max[3];
            } m_bbox;
            uint32_t m_buildUpAxis;
            uint8_t m_pad[252];
        };

        class Header {
        public:
            uint32_t m_endianFlag = 0;
            uint32_t m_version = 2;
            uint32_t m_imageSize; // Size of the file excluding this header
            uint32_t m_checksum; // Checksum of data excluding this header
            uint32_t m_runtimeFlags = 0;
            uint32_t m_constantFlags = 0;

            void writeBinary(std::ostream& f);
        };

        class SectionHeader {
        public:
            uint32_t m_id = 0x10000;
            uint32_t m_size; // Size of this section excluding this header
            uint32_t m_pointerSize = 1; // Size of pointers inside the section

            void writeBinary(std::ostream& f);
        };

        class NavSetHeader {
        public:
            uint32_t m_endianFlag = 0;
            uint32_t m_version = 0x28; // The version of the nav graph, this case it is 40
            uint32_t m_numGraphs = 1; // Number of NavGraphs in this image

            void writeBinary(std::ostream& f);
        };

        class NavGraphHeaderBase {
        public:
            uint32_t m_version{};
            uint32_t m_layer = 0;
            uint32_t m_areaBytes{};
            uint32_t m_kdTreeBytes{};
            uint32_t m_linkRecordBytes = 0;
            uint32_t m_totalBytes{};
            float m_buildScale = 2.0f;
            float m_voxSize = 0.1f;
            float m_radius = 0.2f;
            float m_stepHeight = 0.3f;
            float m_height = 1.8f; // Human Height
            BBox m_bbox;
            Axis m_buildUpAxis = Axis::Z;
            // In NAVPs from Hitman WoA the padding isn't just 0x00
            // It is however identical in all files, changing it to all 0x00 makes NPCs disappear completely
            uint8_t m_pad[252] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 191, 20, 214, 126, 45, 220,
                142, 102, 131, 239, 87, 73, 97, 255, 105, 143, 97, 205, 209, 30, 157, 156, 22, 114, 114, 230, 29, 240,
                132, 79, 74, 119, 2, 215, 232, 57, 44, 83, 203, 201, 18, 30, 51, 116, 158, 12, 244, 213, 212, 159, 212,
                164, 89, 126, 53, 207, 50, 34, 244, 204, 207, 211, 144, 45, 72, 211, 143, 117, 230, 217, 29, 42, 229,
                192, 247, 43, 120, 129, 135, 68, 14, 95, 80, 0, 212, 97, 141, 190, 123, 5, 21, 7, 59, 51, 130, 31, 24,
                112, 146, 218, 100, 84, 206, 177, 133, 62, 105, 21, 248, 70, 106, 4, 150, 115, 14, 217, 22, 47, 103,
                104, 212, 247, 74, 74, 208, 87, 104, 118};

            virtual void writeBinary(std::ostream& f) = 0;
            virtual void readBinary(std::istream& f) = 0;
            [[nodiscard]] virtual size_t getSize() const {
                return sizeof(NavGraphHeaderLayout);
            }
        };

        class NavGraphHeaderHm : public NavGraphHeaderBase {
        public:
            NavGraphHeaderHm() {
                m_version = 0x28;
            }
            void writeBinary(std::ostream& f) override;
            void readBinary(std::istream& f) override;
        };

        class NavGraphHeaderKnt : public NavGraphHeaderBase {
        public:
            uint32_t m_unknown0 = 0;
            uint32_t m_unknown1 = 0;
            uint32_t m_unknown2 = 56;
            uint32_t m_unknown3 = 0;

            NavGraphHeaderKnt() {
                m_version = 0x30;
            }
            void writeBinary(std::ostream& f) override;
            void readBinary(std::istream& f) override;
            [[nodiscard]] size_t getSize() const override {
                return sizeof(NavGraphHeaderLayout) + 16;
            }
        };

        class AreaFlags {
        public:
            // flag 1
            uint32_t GetNumEdges() const {
                return m_flags1 & 0x7F;
            }
            void SetNumEdges(uint32_t p_numEdges) {
                m_flags1 = (m_flags1 & ~0x7F) | (p_numEdges & 0x7F);
            }
            uint32_t GetIslandNum() const {
                return (m_flags1 >> 7) & 0x3FFFF;
            }
            void SetIslandNum(uint32_t p_islandNum) {
                m_flags1 = (m_flags1 & ~0x3FFFF) | ((p_islandNum << 7) & 0x3FFFF);
            }

            // flag 2
            uint32_t GetAreaUsageCount() const {
                return m_flags2 & 0x3FF;
            }
            void SetAreaUsageCount(uint32_t p_usageCount) {
                m_flags2 = (m_flags2 & ~0x3FF) | (p_usageCount & 0x3FF);
            }
            uint32_t GetObCostMult() const {
                return (m_flags2 & 0x000F0000) >> 16;
            }
            void SetObCostMult(uint32_t p_obCostMult) {
                m_flags2 = (m_flags2 & ~0x000F0000) | ((RangeCheck(p_obCostMult, 1, 15) << 16) & 0x000F0000);
            }
            uint32_t GetStaticCostMult() const {
                return (m_flags2 & 0x00F00000) >> 20;
            }
            void SetStaticCostMult(uint32_t p_staticCostMult) {
                m_flags2 = (m_flags2 & ~0x00F00000) | ((RangeCheck(p_staticCostMult, 1, 15) << 20) & 0x00F00000);
            }
            uint32_t GetBasisVert() const {
                return (m_flags2 & 0x7F000000) >> 24;
            }
            void SetBasisVert(uint32_t basisVert) {
                m_flags2 = (m_flags2 & ~0x7F000000) | ((basisVert << 24) & 0x7F000000);
            }

            // flag 3
            // there's a few functions here but the data is always zero

            // flag 4
            // there's one function here which returns the flag, always zero

            uint32_t m_flags1;
            uint32_t m_flags2;
            uint32_t m_flags3;
            uint32_t m_flags4;

            void writeBinary(std::ostream& f);
            bool operator==(AreaFlags const& other) const;
        };

        class Edge;

        class Area {
        public:
            uint64_t m_pProxy = 0;
            uint64_t m_dynAreaData = 0;
            uint64_t m_pFirstLink = 0;
            uint64_t m_pSearchParent = 0;
            Vec3 m_pos;
            float m_radius;
            uint32_t m_searchCost = 0xFFFFFFFF;
            AreaUsageFlags m_usageFlags;
            AreaFlags m_flags;

            [[nodiscard]] Edge* GetFirstEdge() {
                return reinterpret_cast<Edge*>(reinterpret_cast<uintptr_t>(this) + sizeof(Area));
            }

            void writeJson(std::ostream& f, uint64_t s_areaIndex);
            void readJson(auto p_Json);
            void writeBinary(std::ostream& f);
            bool operator==(Area const& other) const;
        };

        class Edge {
        public:
            Area* m_pAdjArea;
            Vec3 m_pos;
            uint32_t m_flags1;
            uint32_t m_flags2;
            char m_pad[4] = "\0\0\0";

            // flags 1
            // Obstacles
            uint32_t GetObID() const {
                return m_flags1 & 0x7FFF;
            }
            void SetObID(uint32_t p_Value) {
                m_flags1 |= p_Value & 0x7FFF;
            }

            // Partition logic
            bool GetPartition() const {
                return (m_flags1 & 0x1000) != 0;
            }
            void SetPartition(bool partition) {
                if (partition)
                    m_flags1 |= 0x1000;
                else
                    m_flags1 &= ~0x1000;
            }

            // Normal or Portal
            EdgeType GetType() const {
                return (EdgeType)((m_flags1 & 0x8000) >> 15);
            }
            void SetType(EdgeType p_EdgeType) {
                m_flags1 |= (p_EdgeType) << 15;
            }

            // flags 2
            // Distance between area centers for adjacent edges
            // Calculate the scaled fixed point distance between the centers of two areas.
            static uint32_t CalcScaledDistBetweenAreaCenters(const Vec3& centerPos1, const Vec3& centerPos2) {
                constexpr float scale = 2;
                constexpr float FIXED_POINT_EDGE_COST_MULT = 1000;
                const float distBetweenAreaCenters = (centerPos1 - centerPos2).GetMagnitude();
                const auto cost =
                    static_cast<uint32_t>(distBetweenAreaCenters * (FIXED_POINT_EDGE_COST_MULT / scale) + 1);
                return cost;
            }

            void writeJson(std::ostream& f, std::map<Binary::Area*, uint32_t>* p_AreaPointerToIndexMap);
            void readJson(simdjson::ondemand::object p_Json);
            void writeBinary(std::ostream& f, std::map<Binary::Area*, Binary::Area*>* s_AreaPointerToOffsetPointerMap);
            void updateAdjacentDistances(const Area* m_pParentArea);
            bool operator==(Edge const& other) const;
        };

        class KDTreeData {
        public:
            BBox m_bbox;
            uint32_t m_size;

            void writeBinary(std::ostream& f);
        };

        class KDLeaf {
        public:
            uint32_t m_data = 0x80000000;

            uint32_t GetPrimOffset() const {
                return m_data & 0x7FFFFFFF;
            }
            void SetPrimOffset(uint32_t primOffset) {
                m_data |= primOffset & 0x7FFFFFFF;
            }
            void SetIsLeaf(int p_isLeaf) {
                if (p_isLeaf)
                    m_data |= 0x80000000;
                else
                    m_data &= ~0x80000000;
            }

            std::pair<uint32_t, NavPower::Area> GetArea(std::map<uint32_t, uint32_t> s_AreaNavGraphOffsetToIndexMap,
                std::vector<NavPower::Area>& s_NavMeshAreas);
        };

        class KDNode {
        public:
            uint32_t m_data;
            float m_dLeft;
            float m_dRight;

            bool IsLeaf() {
                return m_data & 0x80000000 ? true : false;
            }
            void SetIsLeaf(int p_isLeaf) {
                m_data = (m_data & ~0x80000000) | (p_isLeaf & 0x80000000);
            }
            Axis GetSplitAxis() {
                return (Axis)((m_data >> 28) & 7);
            }
            void SetSplitAxis(Axis p_axis) {
                m_data = (m_data & ~0x70000000) | ((static_cast<uint32_t>(p_axis) << 28) & 0x70000000);
            }
            uint32_t GetRightOffset() {
                return m_data & 0xFFFFFFF;
            }
            void SetRightOffset(uint32_t p_rightOffset) {
                m_data = (m_data & ~0xFFFFFFF) | (p_rightOffset & 0xFFFFFFF);
            }
            KDNode* GetLeft() {
                return this + 1;
            }
            KDNode* GetRight() {
                return (KDNode*)((char*)this + GetRightOffset());
            }

            std::vector<std::pair<uint32_t, NavPower::Area>> GetAreas(
                std::map<uint32_t, uint32_t> s_AreaNavGraphOffsetToIndexMap,
                std::vector<NavPower::Area>& s_NavMeshAreas);
            void writeBinary(std::ostream& f, uintptr_t p_KdTreeEnd);
        };
    }; // namespace Binary

    class Area {
    public:
        Binary::Area* m_area;
        std::vector<Binary::Edge*> m_edges;

        void writeJson(std::ostream& f, std::map<Binary::Area*, uint32_t>* p_AreaPointerToIndexMap);
        void readJson(auto p_Json);
        void writeBinary(std::ostream& f, std::map<Binary::Area*, Binary::Area*>* s_AreaPointerToOffsetPointerMap);
        bool operator==(Area const& other) const;
        bool operator<(Area const& other) const;

        // This research and function was created by github.com/OrfeasZ aka NoFaTe
        // This marked vertex is calculated by drawing a line between the first and second vertex in the list
        // and then going through each of the other vertices, finding the point perpendicular to that line for each,
        // and picking the one with the biggest distance between the perpendicular point and the vertex.
        //
        // Example:
        //            _ (c)
        //        _ /    | \
        //     (d)       |  \
        //    / |        |   \
        //   /  |        |    \
        // (a)-(y)------(x)---(b)
        //
        // Assuming this is a surface comprised of vertices a, b, c, d we draw a line between a and b and then
        // find the perpendicular points on that line for vertices c and d (in this case x and y accordingly).
        // In this example we see that the distance between x and c is greater than the distance between y and d
        // so the vertex we need to mark is c.
        size_t CalculateBasisVert() {
            size_t s_FoundVertex = 0;
            float s_MaxDistance = std::numeric_limits<float>::min();

            for (size_t i = 2; i < m_edges.size(); ++i) {
                // Find the perpendicular point from this vertex to the line formed by the first two vertices.
                const Vec3 s_PerpendicularPoint =
                    m_edges[i]->m_pos.PerpendicularPointTo(m_edges[0]->m_pos, m_edges[1]->m_pos);

                // Get the distance between this vertex and the perpendicular point.
                const float s_Distance = s_PerpendicularPoint.DistanceTo(m_edges[i]->m_pos);

                if (s_Distance > s_MaxDistance) {
                    s_FoundVertex = i;
                    s_MaxDistance = s_Distance;
                }
            }

            return (s_FoundVertex >= 2 ? s_FoundVertex : 2);
        }

        Vec3 CalculateNormal() {
            Vec3 v0 = m_edges.at(0)->m_pos;
            Vec3 v1 = m_edges.at(1)->m_pos;
            Vec3 basis = m_edges.at(2)->m_pos;

            Vec3 vec1 = basis - v0;
            Vec3 vec2 = v1 - v0;
            Vec3 cross = vec1.Cross(vec2);

            return cross.GetUnitVec();
        }

        BBox CalculateBBox();
        Vec3 CalculateCentroid();
        void updateAdjacentDistances() const;
    };

    // Helps with outputting the k-d tree as Bounding Boxes
    struct KDTreeHelper {
        Binary::KDNode* m_node;
        BBox m_bbox;
        uint32_t m_depth;
        uint32_t m_splitAxis;
    };

    BBox generateBbox(const std::vector<Area>& s_areas);
    bool compareX(Area& a1, Area& a2);
    bool compareY(Area& a1, Area& a2);
    bool compareZ(Area& a1, Area& a2);

    struct KDTreeResult {
        Axis m_axis;
        BBox m_bbox;
        Binary::Area* m_pArea;
        bool m_isLeaf;
        Binary::KDNode* node;
    };

    class NavGraph {
    public:
        Binary::NavGraphHeaderBase* m_hdr;
        std::vector<Area> m_areas;
        Binary::KDTreeData* m_kdTreeData;
        Binary::KDNode* m_rootKDNode;
        bool m_isKnt;

        NavGraph() {}
        NavGraph(auto s_NavGraphJson);

        // Build m_areas pointer to index map so the pointers can be replaced with indices (+1) in the JSON file
        std::map<Binary::Area*, uint32_t> AreaPointerToIndexMap();

        // Build m_areas index to pointer map so the indices (+1) in the JSON file can be replaced with pointers
        std::map<uint64_t, Binary::Area*> AreaIndexToPointerMap();

        // Build m_areas area pointer to NavGraph offset map so the KD Tree can set the primoffset
        std::map<Binary::Area*, uint32_t> AreaPointerToNavGraphOffsetMap();

        // Spatially traverses the KD-tree to find the leaf bounding box for a given position.
        // This is the most reliable way to find which KD-leaf an area belongs to without relying on pointers.
        BBox FindLeafBBoxForPosition(Vec3 pos);

        // Build m_areas NavGraph offset to index map so the KD Tree can get the area index from the primoffset
        std::map<uint32_t, uint32_t> AreaNavGraphOffsetToIndexMap();

        class KdTreeGenerationHelper {
        public:
            Axis splitAxis;
            std::vector<Area> left;
            std::vector<Area> right;
            float s_LeftSplit;
            float s_RightSplit;
        };

        KdTreeGenerationHelper splitAreas(std::vector<Area> s_originalAreas);
        uint32_t generateKdTree(uintptr_t s_nodePtr, std::vector<Area>& s_areas,
            std::map<Binary::Area*, uint32_t>& p_AreaPointerToNavGraphOffsetMap);

        void writeJson(std::ostream& f);
        void readJson(auto s_NavGraphJson, bool s_IsKnt);
        void writeBinary(std::ostream& f);

        void read(uintptr_t& p_data, bool& p_isKnt);

        // This parses the k-d tree and outputs it as a vector of bounding boxes
        std::map<uint32_t, std::vector<std::pair<uint32_t, BBox>>> ParseKDTree();

        // This parses the k-d tree and outputs it as a vector of bounding boxes
        std::map<uint32_t, std::vector<KDTreeResult>> ParseKDTreeToKDTreeResult();

        // Requires a pointer to the NavGraph, fixes Area pointers in the NavGraph
        // The reason we do this is so it actually points to an area in memory and
        // not a pointer to a location in the file relative to the start of the NavGraph
        void FixAreaPointers(uintptr_t navGraphStart, size_t areaBytes);
    };

    class Section {
    public:
        Binary::SectionHeader* m_hdr;
        Binary::NavSetHeader* m_setHdr;
        std::vector<NavGraph> m_aNavGraphs;

        Section() {}

        void read(uintptr_t& p_data, bool& p_isKnt);

        void readJson(auto p_SectionJson, bool s_IsKnt);

        void writeJson(std::ofstream& f);

        void writeBinary(std::ostream& f);
    };

    class NavMesh {
    public:
        Binary::Header* m_hdr;
        std::vector<Section> m_aSections;
        bool m_isKnt = false;

        NavMesh() {}
        NavMesh(const char* p_NavGraphJsonPath);
        NavMesh(uintptr_t p_data, uint32_t p_filesize) {
            read(p_data, p_filesize);
        };

        void read(uintptr_t p_data, uint32_t p_filesize);

        void readJson(const char* p_NavGraphJsonPath);

        void writeJson(std::ofstream& f);

        void writeBinary(std::ostream& f);
    };

    // This function was made by github.com/OrfeasZ aka NoFaTe
    uint32_t CalculateChecksum(void* p_Data, uint32_t p_Size);
} // namespace NavPower
