#include "../../include/NavKit/module/Rpkg.h"
#include <fstream>

#include <cpptrace/from_current.hpp>

#include "../../include/NavKit/module/Airg.h"
#include "../../include/navkit-rpkg-lib/navkit-rpkg-lib.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
class NavKitSettings;

std::string Rpkg::gameVersion = "HM3";
bool Rpkg::extractionDataInitComplete = false;
PartitionManager* Rpkg::partitionManager = nullptr;
std::optional<std::jthread> Rpkg::backgroundWorker{};

void Rpkg::initExtractionData() {
    Logger::log(NK_INFO, "Scanning resource packages.");
    const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
    const std::string retailFolder = navKitSettings.hitmanFolder + "\\Retail";
    partitionManager = scan_packages(retailFolder.c_str(), gameVersion.c_str(), Logger::rustLogCallback);
    extractionDataInitComplete = true;
    Logger::log(NK_INFO, "Done scanning resource packages.");
    const auto navpFilesInRpkgs = get_all_resources_hashes_by_type_from_rpkg_files(
        partitionManager, "NAVP", Logger::rustLogCallback);
    for (int i = 0; i < navpFilesInRpkgs->length; i++) {
        if (std::string navpHash = get_string_from_list(navpFilesInRpkgs, i); !Navp::navpHashIoiStringMap.
            contains(navpHash)) {
            Navp::navpHashIoiStringMap[navpHash] = navpHash;
        }
    }
    const auto airgFilesInRpkgs = get_all_resources_hashes_by_type_from_rpkg_files(
        partitionManager, "AIRG", Logger::rustLogCallback);
    for (int i = 0; i < airgFilesInRpkgs->length; i++) {
        if (std::string airgHash = get_string_from_list(airgFilesInRpkgs, i); !Airg::airgHashIoiStringMap.
            contains(airgHash)) {
            Airg::airgHashIoiStringMap[airgHash] = airgHash;
        }
    }
    Logger::log(NK_INFO, "Reading hash list.");
    if (std::ifstream file("hash_list_filtered.txt"); file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find(".NAVP") != std::string::npos) {
                if (const size_t commaPos = line.find(','); commaPos != std::string::npos) {
                    std::string hashPart = line.substr(0, commaPos);
                    std::string ioiString = line.substr(commaPos + 1);
                    if (!ioiString.empty() && ioiString.back() == '\r') {
                        ioiString.pop_back();
                    }
                    const size_t dotPos = hashPart.find('.');
                    if (const std::string navpHash =
                        dotPos != std::string::npos
                            ? hashPart.substr(0, dotPos)
                            : hashPart; Navp::navpHashIoiStringMap.contains(navpHash)) {
                        Navp::navpHashIoiStringMap[navpHash] = ioiString;
                    }
                }
            }
            if (line.find(".AIRG") != std::string::npos) {
                if (const size_t commaPos = line.find(','); commaPos != std::string::npos) {
                    std::string hashPart = line.substr(0, commaPos);
                    std::string ioiString = line.substr(commaPos + 1);
                    if (!ioiString.empty() && ioiString.back() == '\r') {
                        ioiString.pop_back();
                    }
                    const size_t dotPos = hashPart.find('.');
                    if (const std::string airgHash =
                        dotPos != std::string::npos
                            ? hashPart.substr(0, dotPos)
                            : hashPart; Airg::airgHashIoiStringMap.contains(airgHash)) {
                        Airg::airgHashIoiStringMap[airgHash] = ioiString;
                    }
                }
            }
        }
    }
    Logger::log(NK_INFO, "Done reading hash list.");
    Menu::updateMenuState();
}

bool Rpkg::canExtract() {
    return extractionDataInitComplete;
}

int Rpkg::extractResourceFromRpkgs(const std::string& hash, const ResourceType type) {
    CPPTRACE_TRY
        {
            const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
            const std::string runtimeFolder = navKitSettings.hitmanFolder + "\\Runtime";
            const std::string navpFolder = navKitSettings.outputFolder + "\\" + (type == NAVP ? "navp" : "airg");
            const char* hashesNeeded[] = {hash.c_str()};
            extract_resources_from_rpkg(
                runtimeFolder.c_str(),
                hashesNeeded,
                1,
                partitionManager,
                navpFolder.c_str(),
                type == NAVP ? "NAVP" : "AIRG",
                Logger::rustLogCallback);
        }
    CPPTRACE_CATCH(const std::exception& e) {
        std::string msg = "Error extracting Resource: " + hash + " ";
        msg += e.what();
        msg += " Stack trace: ";
        msg += cpptrace::from_current_exception().to_string();
        Logger::log(NK_ERROR, msg.c_str());
        return -1;
    } catch (...) {
        Logger::log(NK_ERROR, ("Error extracting Resource: " + hash).c_str());
        return -1;
    }
    return 0;
}
