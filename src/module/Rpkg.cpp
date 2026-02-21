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
HashList* Rpkg::hashList = nullptr;
std::map<std::string, HashListEntry> Rpkg::hashToHashListEntryMap{};
std::map<std::string, HashListEntry> Rpkg::ioiStringToHashListEntryMap{};
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
    Logger::log(NK_INFO, "Reading filtered hash list.");
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
    Logger::log(NK_INFO, "Done reading filtered hash list.");
    Logger::log(NK_INFO, "Getting full hash list.");
    getHashList();
    Logger::log(NK_INFO, "Loading hash list into memory.");

    RustStringList* hashListRustStringList = hash_list_get_all_hashes(hashList);
    Logger::log(NK_INFO, "Total hash list entries: %d, adding entries...", hashListRustStringList->length);
    for (int i = 0; i < hashListRustStringList->length; i++) {
        std::string hash = hashListRustStringList->entries[i];
        const char* pathCStr = hash_list_get_path_by_hash(hashList, hash.c_str());
        std::string path = pathCStr ? pathCStr : "";
        const char* hintCStr = hash_list_get_hint_by_hash(hashList, hash.c_str());
        std::string ioiString = hintCStr ? hintCStr : "";
        auto typeInt = hash_list_get_resource_type_by_hash(hashList, hash.c_str());
        char typeChars[4];
        std::memcpy(typeChars, &typeInt, sizeof(typeInt));
        std::string type(typeChars, sizeof(typeChars));

        hashToHashListEntryMap.insert({hash, {hash, ioiString, type}});
        ioiStringToHashListEntryMap.insert({ioiString, {hash, ioiString, type}});
    }
    Logger::log(NK_INFO, "Done getting full hash list.");
    Menu::updateMenuState();
}

bool Rpkg::canExtract() {
    return extractionDataInitComplete;
}

int Rpkg::extractResourcesFromRpkgs(const std::vector<std::string>& hashes, const ResourceType type) {
    CPPTRACE_TRY
    {
            const NavKitSettings & navKitSettings = NavKitSettings::getInstance();
            const std::string runtimeFolder = navKitSettings.hitmanFolder + "\\Runtime";
            const std::string resourceFolder = navKitSettings.outputFolder + "\\" + (type == NAVP
            ? "navp"
            : type == AIRG
            ? "airg"
            : "tga");
            char**hashesNeeded = new char*[hashes.size()];

            for (size_t i = 0; i < hashes.size(); ++i) {
            hashesNeeded[i] = new char[hashes[i].size() + 1];
            std::strcpy(hashesNeeded[i], hashes[i].c_str());
            
        }
        extract_resources_from_rpkg(
            runtimeFolder.c_str(),
            hashesNeeded,
            hashes.size(),
            partitionManager,
            resourceFolder.c_str(),
            type == NAVP ? "NAVP" : type == AIRG ? "AIRG" : "TEXT",
            Logger::rustLogCallback);
        
    }
    CPPTRACE_CATCH(const std::exception & e)
    {
        std::string msg = "Error extracting Resources.";
        msg += e.what();
        msg += " Stack trace: ";
        msg += cpptrace::from_current_exception().to_string();
        Logger::log(NK_ERROR, msg.c_str());
        return -1;
    }
    catch
    (...)
    {
        Logger::log(NK_ERROR, "Error extracting Resource.");
        return -1;
    }
    return 0;
}

int Rpkg::getHashList() {
    const auto ret = get_hash_list_from_file_or_repo(NavKitSettings::getInstance().outputFolder.c_str(),
                                                     Logger::rustLogCallback);
    if (ret == nullptr) {
        return -1;
    }
    hashList = ret;
    return 0;
}