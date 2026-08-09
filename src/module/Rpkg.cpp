#include "../../include/NavKit/module/Rpkg.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma comment(lib, "Version.lib")
#include <mutex>
#include <fstream>

#include <cpptrace/from_current.hpp>

#include "../../include/QuickDigest/quickdigest5.hpp"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/navkit-rpkg-lib/navkit-rpkg-lib.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/util/ErrorHandler.h"
class NavKitSettings;

std::string Rpkg::gameVersion = "HM3";
bool Rpkg::extractionDataInitComplete = false;
bool Rpkg::unknownGameVersion = false;
PartitionManager* Rpkg::partitionManager = nullptr;
HashList* Rpkg::hashList = nullptr;
std::map<std::string, HashListEntry> Rpkg::hashToHashListEntryMap{};
std::map<std::string, HashListEntry> Rpkg::ioiStringToHashListEntryMap{};
std::mutex Rpkg::hashMapsMutex;
std::optional<std::jthread> Rpkg::backgroundWorker{};

std::string Rpkg::getExeVersion(const std::string& filePath) {
    DWORD handle = 0;
    std::wstring widePath = std::filesystem::path(filePath).wstring();

    DWORD size = GetFileVersionInfoSizeW(widePath.c_str(), &handle);

    if (size == 0) {
        Logger::log(NK_ERROR, "Failed to get version size.");

        return "Failed to get version size.";
    }

    std::vector<BYTE> buffer(size);
    if (!GetFileVersionInfoW(widePath.c_str(), 0, size, buffer.data())) {
        Logger::log(NK_ERROR, "Failed to retrieve version info.");

        return "Failed to retrieve version info.";
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&fileInfo), &len) || len == 0) {
        Logger::log(NK_ERROR, "Failed to query root version value.");
        return "Failed to query root version value.";
    }

    std::string major = std::to_string(HIWORD(fileInfo->dwFileVersionMS));
    std::string minor = std::to_string(LOWORD(fileInfo->dwFileVersionMS));
    std::string build = std::to_string(HIWORD(fileInfo->dwFileVersionLS));

    return major + "." + minor + "." + build;
}

void Rpkg::initExtractionData() {
    CPPTRACE_TRY
    {
        const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
        const std::string retailFolder = navKitSettings.hitmanFolder + "\\Retail";
        Logger::log(NK_INFO, "Checking Hitman Platform.");
        checkHitmanVersion();
        if (unknownGameVersion) {
            return;
        }
        Logger::log(NK_INFO, "Scanning resource packages.");

        std::jthread filteredHashListThread([]() {
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
                            const std::string navpHash =
                                dotPos != std::string::npos ? hashPart.substr(0, dotPos) : hashPart;
                            std::lock_guard lock(Navp::navpHashIoiStringMapMutex);
                            Navp::navpHashIoiStringMap[navpHash] = ioiString;
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
                            const std::string airgHash =
                                dotPos != std::string::npos ? hashPart.substr(0, dotPos) : hashPart;
                            std::lock_guard lock(Airg::airgHashIoiStringMapMutex);
                            Airg::airgHashIoiStringMap[airgHash] = ioiString;
                        }
                    }
                }
            }
            Logger::log(NK_INFO, "Done reading filtered hash list.");
        });

        partitionManager = scan_packages(retailFolder.c_str(), gameVersion.c_str(), Logger::rustLogCallback);
        extractionDataInitComplete = true;
        Logger::log(NK_INFO, "Done scanning resource packages.");

        std::jthread navpThread([]() {
            const auto navpFilesInRpkgsRustStringList = get_all_resources_hashes_by_type_from_rpkg_files(
                partitionManager, "NAVP", Logger::rustLogCallback);
            std::lock_guard lock(Navp::navpHashIoiStringMapMutex);
            std::set<std::string> navpFilesInRpkgs;

            for (int i = 0; i < navpFilesInRpkgsRustStringList->length; i++) {
                navpFilesInRpkgs.insert(std::string(get_string_from_list(navpFilesInRpkgsRustStringList, i)));
            }
            // Insert Navp files from RPKG into map
            for (const auto& navpHash : navpFilesInRpkgs) {
                if (!Navp::navpHashIoiStringMap.contains(navpHash)) {
                    Navp::navpHashIoiStringMap[navpHash] = navpHash;
                }
            }
            std::set<std::string> toErase;
            // Remove Navp files not in RPKG from map
            for (const auto& navpHash : Navp::navpHashIoiStringMap | std::views::keys) {
                if (!navpFilesInRpkgs.contains(navpHash)) {
                    toErase.insert(navpHash);
                }
            }
            for (const auto& navpHash : toErase) {
                Navp::navpHashIoiStringMap.erase(navpHash);
            }
        });

        std::jthread airgThread([]() {
            const auto airgFilesInRpkgsRustStringList = get_all_resources_hashes_by_type_from_rpkg_files(
                partitionManager, "AIRG", Logger::rustLogCallback);
            std::lock_guard lock(Airg::airgHashIoiStringMapMutex);
            std::set<std::string> airgFilesInRpkgs;

            for (int i = 0; i < airgFilesInRpkgsRustStringList->length; i++) {
                airgFilesInRpkgs.insert(std::string(get_string_from_list(airgFilesInRpkgsRustStringList, i)));
            }
            // Insert Airg files from RPKG into map
            for (const auto& airgHash : airgFilesInRpkgs) {
                if (!Airg::airgHashIoiStringMap.contains(airgHash)) {
                    Airg::airgHashIoiStringMap[airgHash] = airgHash;
                }
            }
            std::set<std::string> toErase;
            // Remove Airg files not in RPKG from map
            for (const auto& airgHash : Airg::airgHashIoiStringMap | std::views::keys) {
                if (!airgFilesInRpkgs.contains(airgHash)) {
                    toErase.insert(airgHash);
                }
            }
            for (const auto& airgHash : toErase) {
                Airg::airgHashIoiStringMap.erase(airgHash);
            }
        });

        // Disabling for now since it's not actually used yet
        // std::jthread hashListThread([]() {
        //     Logger::log(NK_INFO, "Getting full hash list.");
        //     getHashList();
        // });

        navpThread.join();
        airgThread.join();
        filteredHashListThread.join();
        // hashListThread.join();

        // Logger::log(NK_INFO, "Loading hash list into memory.");
        // RustStringList* hashListRustStringList = hash_list_get_all_hashes(hashList);
        // Logger::log(NK_INFO, "Total hash list entries: %d, adding entries...", hashListRustStringList->length);
        //
        // const int numThreads = std::thread::hardware_concurrency();
        // const int entriesPerThread = (hashListRustStringList->length + numThreads - 1) / numThreads;
        // std::vector<std::jthread> workers;
        //
        // for (int t = 0; t < numThreads; ++t) {
        //     workers.emplace_back([t, entriesPerThread, hashListRustStringList]() {
        //         const int start = t * entriesPerThread;
        //         const int end = std::min(start + entriesPerThread, static_cast<int>(hashListRustStringList->length));
        //         for (int i = start; i < end; i++) {
        //             std::string hash = hashListRustStringList->entries[i];
        //             const char* pathCStr = hash_list_get_path_by_hash(hashList, hash.c_str());
        //             std::string path = pathCStr ? pathCStr : "";
        //             const char* hintCStr = hash_list_get_hint_by_hash(hashList, hash.c_str());
        //             std::string ioiString = hintCStr ? hintCStr : "";
        //             auto typeInt = hash_list_get_resource_type_by_hash(hashList, hash.c_str());
        //             char typeChars[4];
        //             std::memcpy(typeChars, &typeInt, sizeof(typeInt));
        //             std::string type(typeChars, sizeof(typeChars));
        //
        //             std::lock_guard lock(hashMapsMutex);
        //             hashToHashListEntryMap.insert({hash, {hash, ioiString, type}});
        //             ioiStringToHashListEntryMap.insert({ioiString, {hash, ioiString, type}});
        //         }
        //     });
        // }
        // workers.clear();
        // Logger::log(NK_INFO, "Done getting full hash list.");
        Menu::updateMenuState();
        Logger::log(NK_INFO, "Done initializing.");
    }
    CPPTRACE_CATCH(const std::exception& e) {
        ErrorHandler::openErrorDialog("Could not update NavKit settings. Ensure the Hitman Directory setting points to a valid Hitman World of Assassination installation directory.\n\nError message: " + std::string(e.what()) + "\n\nStack Trace:\n" +
            cpptrace::from_current_exception().to_string());
    } catch (...) {
        ErrorHandler::openErrorDialog("Could not update NavKit settings. Ensure the Hitman Directory setting points to a valid Hitman World of Assassination installation directory.\n\nStack Trace: \n" +
            cpptrace::from_current_exception().to_string());
    }
}

void Rpkg::checkHitmanVersion() {
    const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
    const std::string hitmanFolder = navKitSettings.hitmanFolder;
    static constexpr const char* GAME_VERSION = "3.270.1";
    static std::map<std::string, std::string> gameHashes({
        std::pair("b894cfa2f11b6db52db587a21de688b2", "epic"), // base game
        std::pair("6ce4ebfdd9e22e179206281d818850f5", "epic"), // ansel unlock
        std::pair("4f1b7753a40359bde5d4aa013257c5f1", "steam"), // base game
        std::pair("406865e7486cbc3b77a5f22fd73fbe00", "steam"), // ansel unlock
        std::pair("cfdf300263b03d625099226882eafe84", "microsoft")
    });
    const std::string exePath = hitmanFolder + "\\Retail\\HITMAN3.exe";
    const std::string exeVersion = getExeVersion(exePath);

    if (!(std::filesystem::exists(hitmanFolder + R"(\Retail\Runtime\chunk0.rpkg)") || std::filesystem::exists(exePath))) {
        Logger::log(NK_ERROR, "HITMAN3.exe couldn't be located.");
    }

    if (std::filesystem::exists(hitmanFolder + R"(\Retail\Runtime\chunk0.rpkg)") && !std::filesystem::exists(
        hitmanFolder + "\\MicrosoftGame.Config")) {
        Logger::log(NK_ERROR, "The game config couldn't be located.");
    }
    std::string platform;
    if (std::filesystem::exists(hitmanFolder + R"(\Retail\Runtime\chunk0.rpkg)")) {
        const std::string hash = QuickDigest5::fileToHash(hitmanFolder + "\\MicrosoftGame.Config");
        platform = gameHashes.contains(hash) ? gameHashes[hash] : "undefined";
    } else {
        const std::string hash = QuickDigest5::fileToHash(hitmanFolder + "\\Retail\\HITMAN3.exe");
        platform = gameHashes.contains(hash) ? gameHashes[hash] : "undefined";
    }
    if (platform == "undefined") {
        Logger::log(
            NK_ERROR,
            "Unknown game version. If the game has recently updated, wait for a NavKit update to be released; the developers are already aware. If you're using a cracked version of the game, that's the problem."
        );
        unknownGameVersion = true;
    } else {
        Logger::log(NK_INFO, "Detected game platform: %s version %s. Currently supported version: %s", platform.c_str(), exeVersion.c_str(), GAME_VERSION);
    }
}

bool Rpkg::canExtract() {
    return extractionDataInitComplete && !unknownGameVersion;
}

int Rpkg::extractResourcesFromRpkgs(const std::vector<std::string>& hashes, const ResourceType type) {
    CPPTRACE_TRY
        {
            const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
            const std::string runtimeFolder = navKitSettings.hitmanFolder + "\\Runtime";
            const std::string resourceFolder = navKitSettings.outputFolder + "\\" + (type == NAVP
                ? "navp"
                : type == AIRG
                ? "airg"
                : "tga");
            std::vector<const char*> hashPtrs(hashes.size());
            for (size_t i = 0; i < hashes.size(); ++i) {
                hashPtrs[i] = hashes[i].c_str();
            }
            extract_resources_from_rpkg(
                runtimeFolder.c_str(),
                hashPtrs.data(),
                hashes.size(),
                partitionManager,
                resourceFolder.c_str(),
                type == NAVP ? "NAVP" : type == AIRG ? "AIRG" : "TEXT",
                Logger::rustLogCallback);
        }
    CPPTRACE_CATCH(const std::exception & e) {
        std::string msg = "Error extracting Resources.";
        msg += e.what();
        msg += " Stack trace: ";
        msg += cpptrace::from_current_exception().to_string();
        Logger::log(NK_ERROR, msg.c_str());
        return -1;
    } catch
    (...) {
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
