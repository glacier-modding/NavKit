#pragma once
#include <optional>
#include <string>
#include <thread>

struct PartitionManager;

enum ResourceType {
    NAVP,
    AIRG
};

class Rpkg {
public:
    static void initExtractionData();

    static bool canExtract();

    static int extractResourceFromRpkgs(const std::string& hash, ResourceType type);

    static bool extractionDataInitComplete;
    static std::string gameVersion;
    static PartitionManager* partitionManager;

    static std::optional<std::jthread> backgroundWorker;
};
