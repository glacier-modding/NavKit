#include "../../include/NavWeakness/NavWeakness.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <vector>
#include <limits>

#include "../../include/NavWeakness/NavPower.h"
namespace NavWeakness {
    NavPower::NavMesh LoadNavMeshFromBinary(const char* p_NavMeshPath)
    {
        if (!std::filesystem::is_regular_file(p_NavMeshPath))
            throw std::runtime_error("Input path is not a regular file.");

        // Read the entire file to memory.
        const long s_FileSize = std::filesystem::file_size(p_NavMeshPath);

        if (s_FileSize < sizeof(NavPower::NavGraph))
            throw std::runtime_error("Invalid NavMesh File.");

        std::ifstream s_FileStream(p_NavMeshPath, std::ios::in | std::ios::binary);

        if (!s_FileStream)
            throw std::runtime_error("Error creating input file stream.");

        void* s_FileData = malloc(s_FileSize);
        s_FileStream.read(static_cast<char*>(s_FileData), s_FileSize);

        s_FileStream.close();

        const auto s_FileStartPtr = reinterpret_cast<uintptr_t>(s_FileData);

        // We calculate the checksum now as we alter the data when loading the NavMesh below
        // In future there will be a way to calculate it from the modified data, this will do for now
        const uint32_t s_Checksum = NavPower::CalculateChecksum(reinterpret_cast<void*>(s_FileStartPtr + sizeof(NavPower::Binary::Header)), (s_FileSize - sizeof(NavPower::Binary::Header)));

        NavPower::NavMesh s_NavMesh((uintptr_t)s_FileData, s_FileSize);
        if (s_NavMesh.m_hdr->m_checksum != s_Checksum)
        {
            printf("===== NavPower Header ====\n");
            printf("Hdr_endianFlag: %x\n", s_NavMesh.m_hdr->m_endianFlag);
            printf("Hdr_version: %x\n", s_NavMesh.m_hdr->m_version);
            printf("Hdr_imageSize: %x\n", s_NavMesh.m_hdr->m_imageSize);
            printf("Hdr_checksum: %x\n", s_NavMesh.m_hdr->m_checksum);
            printf("Hdr_runtimeFlags: %x\n", s_NavMesh.m_hdr->m_runtimeFlags);
            printf("Hdr_constantFlags: %x\n", s_NavMesh.m_hdr->m_constantFlags);
            printf("Checksums didn't match.");
            // Commenting this out for now because it doesn't seem to work for First Light
            // throw std::runtime_error("Checksums didn't match.");
        }

        return s_NavMesh;
    }

    NavPower::NavMesh LoadNavMeshFromJson(const char* p_NavMeshPath)
    {
        // Read the entire file to memory.
        if (!std::filesystem::is_regular_file(p_NavMeshPath))
            throw std::runtime_error("Input path is not a regular file.");

        return NavPower::NavMesh(p_NavMeshPath);
    }

    void OutputNavMesh_NAVP_Write(NavPower::NavMesh * p_NavMesh, const char* p_NavMeshOutputPath)
    {
        // Get output filename and delete file if it exists
        const std::string s_OutputFileName = std::filesystem::path(p_NavMeshOutputPath).string();
        std::filesystem::remove(s_OutputFileName);

        // Write the Navmesh to NAVP binary file
        std::ofstream fileOutputStream(s_OutputFileName, std::ios::out | std::ios::binary | std::ios::app);
        p_NavMesh->writeBinary(fileOutputStream);
        fileOutputStream.close();
    }

    // Outputs the navmesh to binary format for use by Hitman WoA
    void OutputNavMesh_NAVP(const char *p_NavMeshPath, const char *p_NavMeshOutputPath, bool b_SourceIsJson = false)
    {
        if (b_SourceIsJson)
        {
            NavPower::NavMesh s_NavMesh = LoadNavMeshFromJson(p_NavMeshPath);
            OutputNavMesh_NAVP_Write(&s_NavMesh, p_NavMeshOutputPath);
        }
        else {
            NavPower::NavMesh s_NavMesh = LoadNavMeshFromBinary(p_NavMeshPath);
            OutputNavMesh_NAVP_Write(&s_NavMesh, p_NavMeshOutputPath);
        }
    }

    void OutputNavMesh_JSON_Write(NavPower::NavMesh* p_NavMesh, const char* p_NavMeshOutputPath)
    {
        // Get output filename and delete file if it exists
        const std::string s_OutputFileName = std::filesystem::path(p_NavMeshOutputPath).string();
        std::filesystem::remove(s_OutputFileName);

        // Write the navp to JSON file
        std::ofstream fileOutputStream(s_OutputFileName);
        p_NavMesh->writeJson(fileOutputStream);
        fileOutputStream.close();
    }

    // Outputs the navmesh to json format
    void OutputNavMesh_JSON(const char* p_NavMeshPath, const char* p_NavMeshOutputPath, bool b_SourceIsJson = false)
    {
        if (b_SourceIsJson)
        {
            NavPower::NavMesh s_NavMesh = LoadNavMeshFromJson(p_NavMeshPath);
            OutputNavMesh_JSON_Write(&s_NavMesh, p_NavMeshOutputPath);
        }
        else {
            NavPower::NavMesh s_NavMesh = LoadNavMeshFromBinary(p_NavMeshPath);
            OutputNavMesh_JSON_Write(&s_NavMesh, p_NavMeshOutputPath);
        }
    }
}
