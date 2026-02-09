#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct PartitionManager;
struct HashList;

enum ResourceType {
    NAVP,
    AIRG,
    TEXT
};

class HashListEntry {
public:
    HashListEntry(const std::string& hash, const std::string& ioiString, const std::string& type) :
        hash(hash), ioiString(ioiString), type(type) {};

    std::string hash;
    std::string ioiString;
    std::string type;
};

class Rpkg {
public:
    static void initExtractionData();

    static bool canExtract();

    static int extractResourcesFromRpkgs(const std::vector<std::string>& hashes, ResourceType type);

    static int getHashList();

    static bool extractionDataInitComplete;
    static std::string gameVersion;
    static PartitionManager* partitionManager;
    static HashList* hashList;
    static std::map<std::string, HashListEntry> hashToHashListEntryMap;
    static std::map<std::string, HashListEntry> ioiStringToHashListEntryMap;

    static std::optional<std::jthread> backgroundWorker;
};
