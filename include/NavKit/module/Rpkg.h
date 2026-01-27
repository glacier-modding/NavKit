#pragma once
#include <optional>
#include <string>
#include <thread>

struct PartitionManager;
struct HashList;

enum ResourceType {
    NAVP,
    AIRG
};

class Rpkg {
public:
    static void initExtractionData();

    static bool canExtract();

    static int extractResourceFromRpkgs(const std::string& hash, ResourceType type);

    static int getHashList();

    static bool extractionDataInitComplete;
    static std::string gameVersion;
    static PartitionManager* partitionManager;
    static HashList* hashList;

    static std::optional<std::jthread> backgroundWorker;
};
