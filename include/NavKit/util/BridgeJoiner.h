#pragma once

#include <string>
#include <vector>

namespace NavPower {
    class NavMesh;
}

namespace Json {
    class PfBox;
}

struct BridgeJoinResult {
    std::string boxName;
    bool joined = false;
    std::string detail;
};

// Adds a planner-excluded area between the closest pair of facing boundary edges inside each bridge box
std::vector<BridgeJoinResult> joinPlannerExcludedBridges(
    NavPower::NavMesh& navMesh, const std::vector<Json::PfBox>& bridgeBoxes);
