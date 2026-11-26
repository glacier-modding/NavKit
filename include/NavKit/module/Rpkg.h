#pragma once
#include <mutex>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct PartitionManager;
struct HashList;

enum ResourceType { NAVP, AIRG, TEXT, CRMD, GFXF, PRIM_RESOURCE };

class HashListEntry {
public:
    HashListEntry(const std::string& hash, const std::string& ioiString, const std::string& type) :
        hash(hash), ioiString(ioiString), type(type) {}

    std::string hash;
    std::string ioiString;
    std::string type;
};

class Rpkg {
public:
    static void initExtractionData();

    static void checkHitmanVersion();

    static bool canExtract();

    static int extractResourcesFromRpkgs(const std::vector<std::string>& hashes, ResourceType type);

    static int getHashList();

    static std::string displayName(const std::string& hash, const std::string& ioiString);

    static std::string displayNameForHash(
        const std::map<std::string, std::string>& hashToIoiString, std::mutex& mutex, const std::string& hash);

    static bool extractionDataInitComplete;
    static std::string gameVersion;
    static PartitionManager* partitionManager;
    static HashList* hashList;
    static std::map<std::string, HashListEntry> hashToHashListEntryMap;
    static std::map<std::string, HashListEntry> ioiStringToHashListEntryMap;
    static std::mutex hashMapsMutex;

    static std::optional<std::jthread> backgroundWorker;
    static bool unknownGameVersion;
};
