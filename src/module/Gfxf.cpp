#include "../../include/NavKit/module/Gfxf.h"
#include "../../include/NavKit/util/FilteredListPicker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <wx/app.h>
#include <wx/arrstr.h>
#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/process.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include "../../include/NavKit/UiIds.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/WxApplication.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/render/Mesh.h"

#include <pugixml.hpp>

std::map<std::string, std::string> Gfxf::gfxfHashIoiStringMap{};
std::mutex Gfxf::gfxfHashIoiStringMapMutex;
wxDialog* Gfxf::hGfxfDialog = nullptr;
wxDialog* Gfxf::hGfxfSettingsDialog = nullptr;

namespace {
    std::vector<unsigned char> readFileBytes(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return {};
        }
        return std::vector<unsigned char>(bytes.begin(), bytes.end());
    }

    bool prepareFfdecInput(const std::filesystem::path& inputPath, const std::filesystem::path& outputDirectory,
        std::filesystem::path& ffdecInput, std::string& error) {
        ffdecInput = inputPath;
        std::ifstream input(inputPath, std::ios::binary);
        std::array<unsigned char, 4> signature{};
        input.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
        // GFXF containers use 'F' here; only patch raw GFX files with a numeric format version.
        if (input.gcount() != static_cast<std::streamsize>(signature.size()) || signature[0] != 'G' ||
            signature[1] != 'F' || signature[2] != 'X' || signature[3] >= 0x20) {
            return true;
        }

        constexpr size_t gfxTagStreamOffset = 21;
        std::vector<unsigned char> bytes = readFileBytes(inputPath);
        if (bytes.size() < gfxTagStreamOffset) {
            error = "Could not read the GFX file's tag stream.";
            return false;
        }

        bool patched = false;
        bool foundEndTag = false;
        size_t position = gfxTagStreamOffset;
        while (position < bytes.size()) {
            if (bytes.size() - position < 2) {
                error = "The GFX file contains a truncated tag header.";
                return false;
            }
            const size_t tagHeader =
                static_cast<size_t>(bytes[position]) | (static_cast<size_t>(bytes[position + 1]) << 8);
            const size_t tagCode = tagHeader >> 6;
            size_t tagLength = tagHeader & 0x3f;
            position += 2;
            if (tagLength == 0x3f) {
                if (bytes.size() - position < 4) {
                    error = "The GFX file contains a truncated long tag header.";
                    return false;
                }
                tagLength = static_cast<size_t>(bytes[position]) | (static_cast<size_t>(bytes[position + 1]) << 8) |
                    (static_cast<size_t>(bytes[position + 2]) << 16) | (static_cast<size_t>(bytes[position + 3]) << 24);
                position += 4;
            }
            if (tagLength > bytes.size() - position) {
                error = "The GFX file contains a tag that extends beyond the end of the file.";
                return false;
            }
            if (tagCode == 39) {
                if (tagLength < 4) {
                    error = "The GFX file contains a malformed sprite definition.";
                    return false;
                }
                if (bytes[position + 2] == 0 && bytes[position + 3] == 0) {
                    bytes[position + 2] = 1;
                    patched = true;
                }
            }
            position += tagLength;
            if (tagCode == 0) {
                foundEndTag = true;
                break;
            }
        }
        if (!foundEndTag) {
            error = "The GFX file does not contain a complete tag stream.";
            return false;
        }
        if (!patched) {
            return true;
        }

        ffdecInput = outputDirectory / "gfxf-with-renderable-sprites.gfx";
        std::ofstream output(ffdecInput, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            error = "Could not prepare the GFX file for map layer export.";
            return false;
        }
        return true;
    }

    bool isRegularFile(const std::filesystem::path& path) {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) && !error;
    }

    std::string pathToUtf8(const std::filesystem::path& path) {
        const auto value = path.u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    std::filesystem::path findExecutableInPath(const wxString& executableName) {
        wxString searchPath;
        if (!wxGetEnv("PATH", &searchPath)) {
            return {};
        }

#ifdef _WIN32
        constexpr wxChar pathSeparator = ';';
#else
        constexpr wxChar pathSeparator = ':';
#endif

        const wxScopedCharBuffer nameBuffer = executableName.ToUTF8();
        if (!nameBuffer) {
            return {};
        }
        size_t start = 0;
        while (start < searchPath.length()) {
            const size_t end = searchPath.find(pathSeparator, start);
            const wxString directory =
                end == wxString::npos ? searchPath.Mid(start) : searchPath.Mid(start, end - start);
            const wxScopedCharBuffer directoryBuffer = directory.ToUTF8();
            if (directoryBuffer) {
                const std::filesystem::path candidate =
                    std::filesystem::u8path(directoryBuffer.data()) / std::filesystem::u8path(nameBuffer.data());
                if (isRegularFile(candidate)) {
                    return candidate;
                }
            }
            if (end == wxString::npos) {
                break;
            }
            start = end + 1;
        }
        return {};
    }

    long executeFfdec(const std::vector<wxString>& arguments) {
        if (wxTheApp == nullptr) {
            return -1;
        }

        struct ProcessState {
            std::mutex mutex;
            std::condition_variable terminatedCondition;
            bool terminated = false;
            long exitCode = -1;
        };
        auto processState = std::make_shared<ProcessState>();
        auto launchResult = std::make_shared<std::promise<std::pair<long, wxProcess*>>>();
        std::future<std::pair<long, wxProcess*>> launchFuture = launchResult->get_future();

        wxTheApp->CallAfter([arguments, processState, launchResult] {
            auto* process = new wxProcess(wxPROCESS_DEFAULT);
            process->Bind(wxEVT_END_PROCESS, [processState](wxProcessEvent& event) {
                {
                    std::lock_guard lock(processState->mutex);
                    processState->exitCode = event.GetExitCode();
                    processState->terminated = true;
                }
                processState->terminatedCondition.notify_one();
            });

            std::vector<const wxChar*> argv;
            argv.reserve(arguments.size() + 1);
            for (const wxString& argument : arguments) {
                argv.push_back(argument.c_str());
            }
            argv.push_back(nullptr);
            const long processId = wxExecute(argv.data(), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, process);
            if (processId <= 0) {
                delete process;
                launchResult->set_value({0, nullptr});
                return;
            }
            launchResult->set_value({processId, process});
        });

        const auto [processId, process] = launchFuture.get();
        if (processId <= 0 || process == nullptr) {
            return -1;
        }
        {
            std::unique_lock lock(processState->mutex);
            processState->terminatedCondition.wait(lock, [&processState] { return processState->terminated; });
        }

        auto cleanupResult = std::make_shared<std::promise<void>>();
        std::future<void> cleanupFuture = cleanupResult->get_future();
        wxTheApp->CallAfter([process, cleanupResult] {
            delete process;
            cleanupResult->set_value();
        });
        cleanupFuture.wait();
        return processState->exitCode;
    }

    std::string findFfdecExecutable() {
        wxString configuredPath;
        if (wxGetEnv("FFDEC_PATH", &configuredPath) && !configuredPath.empty()) {
            const auto buffer = configuredPath.ToUTF8();
            if (buffer) {
                const std::filesystem::path path = std::filesystem::u8path(buffer.data());
                if (isRegularFile(path)) {
                    return pathToUtf8(path);
                }
            }
        }

        const std::filesystem::path bundledDirectory = FileUtil::getApplicationResourcePath("ffdec");
        const std::array<wxString, 3> executableNames = {
#ifdef _WIN32
            "ffdec-cli.exe", "ffdec.exe", "ffdec-cli"
#else
            "ffdec", "ffdec.sh", "ffdec-cli"
#endif
        };
        for (const wxString& executableName : executableNames) {
            const wxScopedCharBuffer nameBuffer = executableName.ToUTF8();
            if (nameBuffer) {
                const std::filesystem::path bundledCandidate =
                    bundledDirectory / std::filesystem::u8path(nameBuffer.data());
                if (isRegularFile(bundledCandidate)) {
                    return pathToUtf8(bundledCandidate);
                }
            }
            const std::filesystem::path pathCandidate = findExecutableInPath(executableName);
            if (!pathCandidate.empty()) {
                return pathToUtf8(pathCandidate);
            }
        }

#ifdef _WIN32
        const std::array<std::filesystem::path, 2> commonInstallPaths = {
            std::filesystem::path("C:/Program Files (x86)/FFDec/ffdec-cli.exe"),
            std::filesystem::path("C:/Program Files/FFDec/ffdec-cli.exe")};
        for (const auto& path : commonInstallPaths) {
            if (isRegularFile(path)) {
                return pathToUtf8(path);
            }
        }
#endif
        return {};
    }

    void showGfxfError(const std::string& message) {
        Logger::log(NK_ERROR, "%s", message.c_str());
        wxMessageBox(wxString::FromUTF8(message), "GFXF load failed", wxOK | wxICON_ERROR, getMainFrame());
    }

    void closeGfxfSettingsDialog() {
        if (Gfxf::hGfxfSettingsDialog != nullptr) {
            wxDialog* dialog = Gfxf::hGfxfSettingsDialog;
            Gfxf::hGfxfSettingsDialog = nullptr;
            dialog->Destroy();
        }
    }

    std::string spriteLayerName(const std::filesystem::path& path) {
        const std::string folderName = pathToUtf8(path.parent_path().filename());
        const size_t slicePosition = folderName.find("SLICE_");
        const size_t floorPosition = folderName.find(".F");
        size_t numberStart = std::string::npos;
        if (slicePosition != std::string::npos) {
            numberStart = slicePosition + 6;
        } else if (floorPosition != std::string::npos) {
            numberStart = floorPosition + 2;
        } else {
            return {};
        }
        size_t numberEnd = numberStart;
        while (numberEnd < folderName.size() && std::isdigit(static_cast<unsigned char>(folderName[numberEnd]))) {
            ++numberEnd;
        }
        if (numberEnd == numberStart || folderName.find("ZMapView", numberEnd) == std::string::npos) {
            return {};
        }
        return "Level " + folderName.substr(numberStart, numberEnd - numberStart);
    }

} // namespace

bool Gfxf::canLoad() const {
    return !loading;
}

void Gfxf::handleOpenGfxfClicked() {
    const nfdu8filteritem_t filter = {"GFXF, GFX, and SWF files", "gfxf,gfx,swf"};
    if (char* fileName = FileUtil::openNfdLoadDialog(&filter, 1)) {
        const std::string path = fileName;
        NFD_FreePathU8(fileName);
        loadGfxfFromFile(path);
    }
}

void Gfxf::loadGfxfFromFile(const std::string& fileName) {
    if (!isRegularFile(std::filesystem::u8path(fileName))) {
        showGfxfError("The selected GFXF/GFX/SWF file does not exist or is not a regular file.");
        return;
    }
    startLoad(fileName);
}

void Gfxf::buildGfx() {
    const std::time_t startTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    Logger::log(NK_INFO, "Starting GFX build at %s", std::ctime(&startTime));
    const auto start = std::chrono::high_resolution_clock::now();
    const auto& mapSpaceConfigs = Scene::getInstance().mapSpaceConfigs;
    std::string error;
    std::filesystem::path temporaryDirectory;
    try {
        if (mapSpaceConfigs.empty()) {
            throw std::runtime_error("The loaded scene has no map-space configurations.");
        }

        const std::string ffdecExecutable = findFfdecExecutable();
        if (ffdecExecutable.empty()) {
            throw std::runtime_error(
                "The bundled FFDec CLI is missing. Repair the NavKit installation or set FFDEC_PATH to an "
                "FFDec CLI executable.");
        }

        const std::filesystem::path templatePath = FileUtil::getApplicationResourcePath("polarbear005.gfx");
        if (!isRegularFile(templatePath)) {
            throw std::runtime_error("The GFX template is missing: " + pathToUtf8(templatePath));
        }

        const std::filesystem::path outputDirectory =
            std::filesystem::u8path(NavKitSettings::getInstance().outputFolder);
        std::error_code filesystemError;
        std::filesystem::create_directories(outputDirectory, filesystemError);
        if (filesystemError) {
            throw std::runtime_error("Could not create the GFX output folder: " + filesystemError.message());
        }

        static std::atomic_uint64_t outputCounter = 0;
        temporaryDirectory = std::filesystem::temp_directory_path() / "NavKit" / "gfx-build" /
            (std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(++outputCounter));
        std::filesystem::create_directories(temporaryDirectory, filesystemError);
        if (filesystemError) {
            throw std::runtime_error("Could not create the temporary GFX build folder: " + filesystemError.message());
        }

        const std::filesystem::path templateXml = temporaryDirectory / "template.xml";
        const auto runFfdec = [&ffdecExecutable](std::vector<wxString> arguments, const std::string& action) {
            arguments.insert(arguments.begin(), wxString::FromUTF8(ffdecExecutable));
            const long exitCode = executeFfdec(arguments);
            if (exitCode != 0) {
                throw std::runtime_error(
                    "FFDec failed while " + action + " (exit code " + std::to_string(exitCode) + ").");
            }
        };
        const auto asWxPath = [](const std::filesystem::path& path) { return wxString::FromUTF8(pathToUtf8(path)); };
        runFfdec({"-swf2xml", asWxPath(templatePath), asWxPath(templateXml)}, "opening the GFX template");

        const auto loadXml = [](const std::filesystem::path& path, pugi::xml_document& document) {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                return pugi::xml_parse_result{};
            }
            const std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            return document.load_buffer(contents.data(), contents.size());
        };
        pugi::xml_document templateDocument;
        if (const pugi::xml_parse_result parseResult = loadXml(templateXml, templateDocument); !parseResult) {
            throw std::runtime_error(
                "Could not parse FFDec's GFX template XML: " + std::string(parseResult.description()));
        }

        struct LayerShape {
            unsigned int spriteId;
            unsigned int shapeId;
            std::filesystem::path svgPath;
        };
        std::vector<LayerShape> layers;
        layers.reserve(mapSpaceConfigs.size());
        const pugi::xml_node rootTags = templateDocument.child("swf").child("tags");
        const pugi::xml_node exportAssets = [&rootTags] {
            for (pugi::xml_node item : rootTags.children("item")) {
                if (std::string_view(item.attribute("type").as_string()) == "ExportAssetsTag") {
                    return item;
                }
            }
            return pugi::xml_node{};
        }();
        if (!exportAssets) {
            throw std::runtime_error("The GFX template does not export its map-layer sprites.");
        }

        const pugi::xml_node spriteIds = exportAssets.child("tags");
        const pugi::xml_node spriteNames = exportAssets.child("names");
        size_t sliceSpriteCount = 0;
        for (pugi::xml_node name : spriteNames.children("item")) {
            const std::string exportedName = name.text().as_string();
            if (exportedName.find("SLICE_") != std::string::npos && exportedName.ends_with("_ZMapView")) {
                ++sliceSpriteCount;
            }
        }
        if (sliceSpriteCount != mapSpaceConfigs.size()) {
            throw std::runtime_error("The GFX template has " + std::to_string(sliceSpriteCount) +
                " map-layer sprites, but the loaded scene has " + std::to_string(mapSpaceConfigs.size()) +
                " map-space configurations.");
        }
        for (size_t layerIndex = 0; layerIndex < mapSpaceConfigs.size(); ++layerIndex) {
            const std::string layerNumber =
                layerIndex < 10 ? "0" + std::to_string(layerIndex) : std::to_string(layerIndex);
            const std::string expectedSpriteName = "SLICE_" + layerNumber + "_ZMapView";
            unsigned int spriteId = 0;
            for (auto currentId = spriteIds.child("item"), currentName = spriteNames.child("item");
                currentId && currentName;
                currentId = currentId.next_sibling("item"), currentName = currentName.next_sibling("item")) {
                const std::string exportedName = currentName.text().as_string();
                if (exportedName.size() >= expectedSpriteName.size() &&
                    exportedName.compare(exportedName.size() - expectedSpriteName.size(), expectedSpriteName.size(),
                        expectedSpriteName) == 0) {
                    spriteId = currentId.text().as_uint();
                    break;
                }
            }
            if (spriteId == 0) {
                throw std::runtime_error("The GFX template has no sprite for map layer " + layerNumber + ".");
            }

            pugi::xml_node sprite;
            for (pugi::xml_node item : rootTags.children("item")) {
                if (std::string_view(item.attribute("type").as_string()) == "DefineSpriteTag" &&
                    item.attribute("spriteId").as_uint() == spriteId) {
                    sprite = item;
                    break;
                }
            }
            const pugi::xml_node placeObject = sprite.child("subTags").child("item");
            const unsigned int shapeId = placeObject.attribute("characterId").as_uint();
            if (!placeObject || shapeId == 0) {
                throw std::runtime_error(
                    "The GFX template map-layer sprite " + layerNumber + " does not contain a replaceable shape.");
            }

            std::filesystem::path svgPath = outputDirectory / ("output_" + layerNumber + ".svg");
            if (!isRegularFile(svgPath)) {
                throw std::runtime_error("The Blender map-layer SVG is missing: " + pathToUtf8(svgPath));
            }
            layers.push_back({spriteId, shapeId, std::move(svgPath)});
        }

        const std::filesystem::path shapeDirectory = temporaryDirectory / "shapes";
        std::filesystem::create_directories(shapeDirectory, filesystemError);
        if (filesystemError) {
            throw std::runtime_error("Could not create the temporary GFX shape folder: " + filesystemError.message());
        }
        for (const LayerShape& layer : layers) {
            const std::filesystem::path shapePath = shapeDirectory / (std::to_string(layer.shapeId) + ".svg");
            std::filesystem::copy_file(
                layer.svgPath, shapePath, std::filesystem::copy_options::overwrite_existing, filesystemError);
            if (filesystemError) {
                throw std::runtime_error(
                    "Could not stage map-layer SVG " + pathToUtf8(layer.svgPath) + ": " + filesystemError.message());
            }
        }

        const std::filesystem::path importedGfx = temporaryDirectory / "imported.gfx";
        runFfdec({"-importShapes", asWxPath(templatePath), asWxPath(importedGfx), asWxPath(shapeDirectory)},
            "importing map-layer SVGs");

        const std::filesystem::path importedXml = temporaryDirectory / "imported.xml";
        runFfdec({"-swf2xml", asWxPath(importedGfx), asWxPath(importedXml)}, "preparing map-layer sprites");
        pugi::xml_document importedDocument;
        if (const pugi::xml_parse_result parseResult = loadXml(importedXml, importedDocument); !parseResult) {
            throw std::runtime_error(
                "Could not parse FFDec's imported GFX XML: " + std::string(parseResult.description()));
        }

        const pugi::xml_node importedTags = importedDocument.child("swf").child("tags");
        pugi::xml_node showFrame;
        for (pugi::xml_node item : importedTags.children("item")) {
            if (std::string_view(item.attribute("type").as_string()) == "ShowFrameTag") {
                showFrame = item;
                break;
            }
        }
        if (!showFrame) {
            throw std::runtime_error("The GFX template has no ShowFrame tag.");
        }
        for (const LayerShape& layer : layers) {
            pugi::xml_node sprite;
            for (pugi::xml_node item : importedTags.children("item")) {
                if (std::string_view(item.attribute("type").as_string()) == "DefineSpriteTag" &&
                    item.attribute("spriteId").as_uint() == layer.spriteId) {
                    sprite = item;
                    break;
                }
            }
            pugi::xml_node subTags = sprite.child("subTags");
            pugi::xml_node placeObject = subTags.child("item");
            if (!placeObject) {
                throw std::runtime_error("FFDec removed the map-layer sprite's shape placement.");
            }
            pugi::xml_document placeObjectCopy;
            placeObjectCopy.append_copy(placeObject);
            subTags.remove_children();
            placeObject = subTags.append_copy(placeObjectCopy.first_child());
            placeObject.attribute("depth").set_value(1);
            pugi::xml_node matrix = placeObject.child("matrix");
            matrix.attribute("scaleX").set_value(0.5);
            matrix.attribute("scaleY").set_value(0.5);
            matrix.attribute("translateX").set_value(0);
            matrix.attribute("translateY").set_value(0);
            subTags.append_copy(showFrame);
            sprite.attribute("frameCount").set_value(1);
        }

        const std::filesystem::path finalXml = temporaryDirectory / "output.xml";
        std::ofstream xmlOutput(finalXml, std::ios::binary | std::ios::trunc);
        if (!xmlOutput) {
            throw std::runtime_error("Could not write the final GFX XML.");
        }
        importedDocument.save(xmlOutput, "  ", pugi::format_default, pugi::encoding_utf8);
        if (!xmlOutput) {
            throw std::runtime_error("Could not finish writing the final GFX XML.");
        }

        const std::filesystem::path temporaryOutput = temporaryDirectory / "output.gfx";
        runFfdec({"-xml2swf", asWxPath(finalXml), asWxPath(temporaryOutput)}, "saving output.gfx");
        filesystemError.clear();
        if (!isRegularFile(temporaryOutput) || std::filesystem::file_size(temporaryOutput, filesystemError) < 21 ||
            filesystemError) {
            throw std::runtime_error("FFDec did not produce a valid output.gfx file.");
        }
        const std::filesystem::path outputPath = outputDirectory / "output.gfx";
        std::filesystem::copy_file(
            temporaryOutput, outputPath, std::filesystem::copy_options::overwrite_existing, filesystemError);
        if (filesystemError) {
            throw std::runtime_error("Could not save output.gfx: " + filesystemError.message());
        }
        Logger::log(NK_INFO, "Built GFX with %zu map layers at %s", layers.size(), pathToUtf8(outputPath).c_str());
    } catch (const std::exception& exception) {
        error = "Failed to build output.gfx: " + std::string(exception.what());
    }

    if (!temporaryDirectory.empty()) {
        std::error_code cleanupError;
        std::filesystem::remove_all(temporaryDirectory, cleanupError);
        if (cleanupError) {
            Logger::log(NK_WARN, "Could not remove temporary GFX build files: %s", cleanupError.message().c_str());
        }
    }
    if (!error.empty()) {
        Logger::log(NK_ERROR, "%s", error.c_str());
        if (wxTheApp != nullptr) {
            wxTheApp->CallAfter([error] {
                wxMessageBox(wxString::FromUTF8(error), "GFX build failed", wxOK | wxICON_ERROR, getMainFrame());
            });
        }
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    Logger::log(NK_INFO, "Finished GFX build in %s seconds", std::to_string(duration.count()).c_str());
}

void Gfxf::startLoad(const std::string& fileName) {
    if (!canLoad()) {
        return;
    }
    closeGfxfSettingsDialog();

    const std::string ffdecExecutable = findFfdecExecutable();
    if (ffdecExecutable.empty()) {
        showGfxfError("The bundled FFDec CLI is missing. Repair the NavKit installation or set FFDEC_PATH to an "
                      "FFDec CLI executable.");
        return;
    }

    ++loadGeneration;
    const std::uint64_t generation = loadGeneration;
    model.cleanup();
    gfxfLoaded = false;
    loadedFile.clear();
    layerNames.clear();
    selectedLayerIndex = 0;
    loading = true;
    Menu::updateMenuState();

    static std::atomic_uint64_t outputCounter = 0;
    const std::string inputFile = fileName;
    backgroundWorker.emplace([this, generation, ffdecExecutable, inputFile](std::stop_token) {
        ExportResult result{generation, inputFile, {}, {}};
        std::filesystem::path outputDirectory;
        try {
            outputDirectory = std::filesystem::temp_directory_path() / "NavKit" / "gfxf" /
                (std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                    std::to_string(++outputCounter));
            std::filesystem::create_directories(outputDirectory);

            std::filesystem::path ffdecInput;
            if (prepareFfdecInput(std::filesystem::u8path(inputFile), outputDirectory, ffdecInput, result.error)) {
                const std::vector<wxString> arguments = {wxString::FromUTF8(ffdecExecutable), "-zoom", "2", "-format",
                    "frame:png,sprite:png", "-export", "frame,sprite", wxString::FromUTF8(pathToUtf8(outputDirectory)),
                    wxString::FromUTF8(pathToUtf8(ffdecInput))};
                const long exitCode = executeFfdec(arguments);
                if (exitCode != 0) {
                    result.error =
                        "FFDec CLI failed to export SWF map layers (exit code " + std::to_string(exitCode) + ").";
                } else {
                    std::vector<std::pair<std::string, std::filesystem::path>> spriteFiles;
                    const std::filesystem::path spritesDirectory = outputDirectory / "sprites";
                    if (std::filesystem::exists(spritesDirectory)) {
                        for (const auto& entry : std::filesystem::recursive_directory_iterator(spritesDirectory)) {
                            if (!entry.is_regular_file() || entry.path().extension() != ".png") {
                                continue;
                            }
                            const std::string layerName = spriteLayerName(entry.path());
                            if (!layerName.empty()) {
                                spriteFiles.emplace_back(layerName, entry.path());
                            }
                        }
                    }
                    std::ranges::sort(spriteFiles, [](const auto& left, const auto& right) {
                        return left.first != right.first ? left.first < right.first : left.second < right.second;
                    });
                    for (const auto& [layerName, spriteFile] : spriteFiles) {
                        std::vector<unsigned char> bytes = readFileBytes(spriteFile);
                        if (bytes.empty()) {
                            result.error = "FFDec produced an empty or oversized image for " + layerName + ".";
                            break;
                        }
                        result.layers.emplace_back(layerName, std::move(bytes));
                    }

                    if (result.layers.empty() && result.error.empty()) {
                        std::filesystem::path frameFile = outputDirectory / "frames" / "1.png";
                        if (!isRegularFile(frameFile)) {
                            frameFile = outputDirectory / "1.png";
                        }
                        std::vector<unsigned char> bytes = readFileBytes(frameFile);
                        if (bytes.empty()) {
                            result.error = "FFDec completed without producing a usable map layer or first SWF frame.";
                        } else {
                            result.layers.emplace_back("Frame 1", std::move(bytes));
                        }
                    }
                }
            }
        } catch (const std::exception& exception) {
            result.error = "Failed to export SWF map layers: " + std::string(exception.what());
        }

        if (!outputDirectory.empty()) {
            std::error_code error;
            std::filesystem::remove_all(outputDirectory, error);
            if (error && result.error.empty()) {
                result.error = "Failed to remove temporary FFDec output: " + error.message();
            }
        }

        std::lock_guard lock(resultMutex);
        completedLoad = std::move(result);
    });
}

void Gfxf::showExtractGfxfDialog() {
    if (hGfxfDialog) {
        hGfxfDialog->Raise();
        return;
    }

    std::vector<std::pair<std::string, std::string>> entries;
    {
        std::lock_guard lock(gfxfHashIoiStringMapMutex);
        entries.assign(gfxfHashIoiStringMap.begin(), gfxfHashIoiStringMap.end());
    }
    std::ranges::sort(entries, [](const auto& left, const auto& right) {
        return left.second != right.second ? left.second < right.second : left.first < right.first;
    });
    if (entries.empty()) {
        wxMessageBox("No GFXF resources were found in the loaded RPKG files.", "No GFXF resources",
            wxOK | wxICON_INFORMATION, getMainFrame());
        return;
    }

    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "Load GFXF from resource package", wxDefaultPosition,
        wxSize(750, 450), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    hGfxfDialog = dialog;
    auto* picker = new FilteredListPicker(dialog, IDC_COMBOBOX_GFXF);
    std::vector<std::string> items;
    items.reserve(entries.size());
    for (const auto& [hash, ioiString] : entries) {
        items.push_back(ioiString.empty() ? hash : ioiString);
    }
    picker->setItems(std::move(items));

    auto* buttons = new wxStdDialogButtonSizer();
    auto* open = new wxButton(dialog, wxID_OK, "Load GFXF");
    auto* cancel = new wxButton(dialog, wxID_CANCEL, "Cancel");
    buttons->AddButton(open);
    buttons->AddButton(cancel);
    buttons->Realize();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(
        new wxStaticText(dialog, wxID_ANY, "GFXF file in resource packages (RPKG) to extract and load"), 0, wxALL, 10);
    sizer->Add(picker, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    sizer->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 10);
    dialog->SetSizer(sizer);

    open->Bind(wxEVT_BUTTON, [dialog, picker, entries = std::move(entries)](wxCommandEvent&) {
        const std::string selected = picker->getSelection();
        const auto entry = std::ranges::find_if(entries, [&selected](const auto& candidate) {
            return (candidate.second.empty() ? candidate.first : candidate.second) == selected;
        });
        if (entry != entries.end()) {
            const std::string& hash = entry->first;
            const std::filesystem::path outputFile =
                std::filesystem::u8path(NavKitSettings::getInstance().outputFolder) / "gfxf" / (hash + ".gfx");
            std::error_code removeError;
            std::filesystem::remove(outputFile, removeError);
            if (removeError) {
                showGfxfError("Could not remove the previous GFXF extraction: " + removeError.message());
            } else if (Rpkg::extractResourcesFromRpkgs({hash}, GFXF) != 0 || !isRegularFile(outputFile)) {
                showGfxfError("The RPKG library did not produce the expected GFX payload: " + pathToUtf8(outputFile));
            } else {
                Gfxf::getInstance().loadGfxfFromFile(pathToUtf8(outputFile));
            }
        }
        dialog->Close();
    });
    cancel->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) { dialog->Close(); });
    picker->onActivate([open] {
        wxCommandEvent clickEvent(wxEVT_BUTTON, open->GetId());
        clickEvent.SetEventObject(open);
        open->ProcessWindowEvent(clickEvent);
    });
    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent&) {
        if (hGfxfDialog == dialog) {
            hGfxfDialog = nullptr;
        }
        dialog->Destroy();
    });
    dialog->Layout();
    dialog->CentreOnParent();
    dialog->Show();
}

void Gfxf::updateDialogControls(wxDialog* dialog) {
    auto* offsetXSlider = static_cast<wxSlider*>(dialog->FindWindow(IDC_SLIDER_GFXF_OFFSET_X));
    auto* offsetYSlider = static_cast<wxSlider*>(dialog->FindWindow(IDC_SLIDER_GFXF_OFFSET_Y));
    auto* offsetZSlider = static_cast<wxSlider*>(dialog->FindWindow(IDC_SLIDER_GFXF_OFFSET_Z));
    auto* scaleSlider = static_cast<wxSlider*>(dialog->FindWindow(IDC_SLIDER_GFXF_SCALE));
    auto* opacitySlider = static_cast<wxSlider*>(dialog->FindWindow(IDC_SLIDER_GFXF_OPACITY));
    float renderScale = 1.0f;
    float renderOpacity = 1.0f;
    offsetXSlider->SetValue(offsetX);
    offsetYSlider->SetValue(offsetY);
    offsetZSlider->SetValue(offsetZ);
    scaleSlider->SetValue(static_cast<int>(renderScale * 100.0f + 0.5f));
    opacitySlider->SetValue(static_cast<int>(renderOpacity * 100.0f + 0.5f));
    dialog->FindWindow(IDC_STATIC_GFXF_OFFSET_X_VALUE)->SetLabel(std::to_string(offsetX));
    dialog->FindWindow(IDC_STATIC_GFXF_OFFSET_Y_VALUE)->SetLabel(std::to_string(offsetY));
    dialog->FindWindow(IDC_STATIC_GFXF_OFFSET_Z_VALUE)->SetLabel(std::to_string(offsetZ));
    dialog->FindWindow(IDC_STATIC_GFXF_SCALE_VALUE)
        ->SetLabel(wxString::Format("%d%%", static_cast<int>(renderScale * 100.0f + 0.5f)));
    dialog->FindWindow(IDC_STATIC_GFXF_OPACITY_VALUE)
        ->SetLabel(wxString::Format("%d%%", static_cast<int>(renderOpacity * 100.0f + 0.5f)));
}

void Gfxf::showGfxfSettingsDialog() {
    if (hGfxfSettingsDialog != nullptr) {
        hGfxfSettingsDialog->Raise();
        return;
    }
    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "GFXF Settings", wxDefaultPosition, wxSize(460, 360));
    hGfxfSettingsDialog = dialog;
    auto* content = new wxBoxSizer(wxVERTICAL);
    if (gfxfLoaded && !layerNames.empty()) {
        content->Add(new wxStaticText(dialog, wxID_ANY, "Map Layer:"), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        wxArrayString choices;
        for (const std::string& layerName : layerNames) {
            choices.Add(wxString::FromUTF8(layerName));
        }
        auto* list = new wxListBox(dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
        list->SetSelection(static_cast<int>(std::min(selectedLayerIndex, layerNames.size() - 1)));
        list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) {
            const int selection = event.GetSelection();
            if (selection < 0 || static_cast<size_t>(selection) >= layerNames.size()) {
                return;
            }
            selectedLayerIndex = static_cast<size_t>(selection);
            Logger::log(NK_INFO, "Selected GFXF layer: %s", layerNames[selectedLayerIndex].c_str());
        });
        content->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);
    } else {
        content->Add(new wxStaticText(dialog, wxID_ANY, "No GFXF map is currently loaded."), 0, wxALL, 10);
    }

    offsetX = std::clamp(offsetX, -100, 100);
    offsetY = std::clamp(offsetY, -100, 100);
    offsetZ = std::clamp(offsetZ, -100, 100);
    const int scalePercent = std::clamp(static_cast<int>(renderScale * 100.0f + 0.5f), 25, 150);
    const int opacityPercent = std::clamp(static_cast<int>(renderOpacity * 100.0f + 0.5f), 0, 100);
    auto* offsetXValue = new wxStaticText(dialog, IDC_STATIC_GFXF_OFFSET_X_VALUE, wxString::Format("%d", offsetX));
    auto* offsetYValue = new wxStaticText(dialog, IDC_STATIC_GFXF_OFFSET_Y_VALUE, wxString::Format("%d", offsetY));
    auto* offsetZValue = new wxStaticText(dialog, IDC_STATIC_GFXF_OFFSET_Z_VALUE, wxString::Format("%d", offsetZ));
    auto* scaleValue = new wxStaticText(dialog, IDC_STATIC_GFXF_SCALE_VALUE, wxString::Format("%d%%", scalePercent));
    auto* opacityValue =
        new wxStaticText(dialog, IDC_STATIC_GFXF_OPACITY_VALUE, wxString::Format("%d%%", opacityPercent));
    auto* controls = new wxFlexGridSizer(5, 3, 8, 8);
    controls->Add(new wxStaticText(dialog, wxID_ANY, "Scale:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* scaleSlider = new wxSlider(dialog, IDC_SLIDER_GFXF_SCALE, scalePercent, 25, 150);
    controls->Add(scaleSlider, 1, wxEXPAND);
    controls->Add(scaleValue, 0, wxALIGN_CENTER_VERTICAL);
    controls->Add(new wxStaticText(dialog, wxID_ANY, "Opacity:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* opacitySlider = new wxSlider(dialog, IDC_SLIDER_GFXF_OPACITY, opacityPercent, 0, 100);
    controls->Add(opacitySlider, 1, wxEXPAND);
    controls->Add(opacityValue, 0, wxALIGN_CENTER_VERTICAL);
    controls->Add(new wxStaticText(dialog, wxID_ANY, "X Offset:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* offsetXSlider = new wxSlider(dialog, IDC_SLIDER_GFXF_OFFSET_X, offsetX, -100, 100);
    controls->Add(offsetXSlider, 1, wxEXPAND);
    controls->Add(offsetXValue, 0, wxALIGN_CENTER_VERTICAL);
    controls->Add(new wxStaticText(dialog, wxID_ANY, "Y Offset:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* offsetYSlider = new wxSlider(dialog, IDC_SLIDER_GFXF_OFFSET_Y, offsetY, -100, 100);
    controls->Add(offsetYSlider, 1, wxEXPAND);
    controls->Add(offsetYValue, 0, wxALIGN_CENTER_VERTICAL);
    controls->Add(new wxStaticText(dialog, wxID_ANY, "Z Offset:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* offsetZSlider = new wxSlider(dialog, IDC_SLIDER_GFXF_OFFSET_Z, offsetZ, -100, 100);
    controls->Add(offsetZSlider, 1, wxEXPAND);
    controls->Add(offsetZValue, 0, wxALIGN_CENTER_VERTICAL);
    controls->AddGrowableCol(1, 1);
    content->Add(controls, 0, wxEXPAND | wxALL, 10);
    auto* reset = new wxButton(dialog, IDC_BUTTON_RESET_DEFAULTS, "Reset Defaults");
    dialog->SetSizer(content);
    content->Add(reset, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    scaleSlider->Bind(wxEVT_SLIDER, [this, scaleValue](wxCommandEvent& event) {
        renderScale = static_cast<float>(event.GetInt()) / 100.0f;
        scaleValue->SetLabel(wxString::Format("%d%%", event.GetInt()));
    });
    opacitySlider->Bind(wxEVT_SLIDER, [this, opacityValue](wxCommandEvent& event) {
        renderOpacity = static_cast<float>(event.GetInt()) / 100.0f;
        opacityValue->SetLabel(wxString::Format("%d%%", event.GetInt()));
    });
    offsetXSlider->Bind(wxEVT_SLIDER, [this, offsetXValue](wxCommandEvent& event) {
        offsetX = event.GetInt();
        offsetXValue->SetLabel(wxString::Format("%d", offsetX));
    });
    offsetYSlider->Bind(wxEVT_SLIDER, [this, offsetYValue](wxCommandEvent& event) {
        offsetY = event.GetInt();
        offsetYValue->SetLabel(wxString::Format("%d", offsetY));
    });
    offsetZSlider->Bind(wxEVT_SLIDER, [this, offsetZValue](wxCommandEvent& event) {
        offsetZ = event.GetInt();
        offsetZValue->SetLabel(wxString::Format("%d", offsetZ));
    });
    reset->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) {
        getInstance().resetDefaults();
        getInstance().updateDialogControls(dialog);
    });

    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent&) {
        if (hGfxfSettingsDialog == dialog) {
            hGfxfSettingsDialog = nullptr;
        }
        dialog->Destroy();
    });
    dialog->SetMinSize(wxSize(420, 360));
    dialog->Layout();
    dialog->CentreOnParent();
    dialog->Show();
}

void Gfxf::resetDefaults() {
    renderScale = 1.0f;
    renderOpacity = 1.0f;
    offsetX = 0;
    offsetY = 0;
    offsetZ = 0;
}

void Gfxf::finalizeLoad() {
    std::optional<ExportResult> result;
    {
        std::lock_guard lock(resultMutex);
        if (completedLoad) {
            result = std::move(completedLoad);
            completedLoad.reset();
        }
    }
    if (!result) {
        return;
    }

    backgroundWorker.reset();
    loading = false;
    if (result->generation != loadGeneration) {
        Menu::updateMenuState();
        return;
    }
    if (!result->error.empty()) {
        showGfxfError(result->error);
        Menu::updateMenuState();
        return;
    }

    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    for (size_t layerIndex = 0; layerIndex < result->layers.size(); ++layerIndex) {
        const auto& [layerName, pngBytes] = result->layers[layerIndex];
        int width = 0;
        int height = 0;
        unsigned char* pixels = stbi_load_from_memory(
            pngBytes.data(), static_cast<int>(pngBytes.size()), &width, &height, nullptr, STBI_rgb_alpha);
        if (pixels == nullptr || width <= 0 || height <= 0 || width > maxTextureSize || height > maxTextureSize) {
            if (pixels != nullptr) {
                stbi_image_free(pixels);
            }
            model.cleanup();
            layerNames.clear();
            selectedLayerIndex = 0;
            showGfxfError("Could not decode an FFDec PNG layer or its dimensions exceed the OpenGL texture limit.");
            Menu::updateMenuState();
            return;
        }

        Texture texture{};
        texture.type = "texture_diffuse";
        const std::string textureName = "gfxf-layer-" + std::to_string(layerIndex) + ".png";
        texture.path.Set(textureName.c_str());
        texture.bpp = 32;
        texture.internalFormat = GL_RGBA8;
        texture.uploadFormat = GL_RGBA;
        texture.loaded = true;
        const bool mapLayer = layerName.starts_with("Level ");
        texture.width = width;
        texture.height = height;
        if (mapLayer) {
            int minX = width;
            int minY = height;
            int maxX = -1;
            int maxY = -1;
            const unsigned char* background = pixels;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
                    const int colorDifference =
                        std::max({std::abs(pixels[pixel] - background[0]), std::abs(pixels[pixel + 1] - background[1]),
                            std::abs(pixels[pixel + 2] - background[2]), std::abs(pixels[pixel + 3] - background[3])});
                    if (colorDifference > 8) {
                        minX = std::min(minX, x);
                        minY = std::min(minY, y);
                        maxX = std::max(maxX, x);
                        maxY = std::max(maxY, y);
                    }
                }
            }
            if (maxX >= minX && maxY >= minY) {
                const int padding = std::max(2, std::max(width, height) / 100);
                minX = std::max(0, minX - padding);
                minY = std::max(0, minY - padding);
                maxX = std::min(width - 1, maxX + padding);
                maxY = std::min(height - 1, maxY + padding);
            } else {
                minX = 0;
                minY = 0;
                maxX = width - 1;
                maxY = height - 1;
            }
            texture.width = maxX - minX + 1;
            texture.height = maxY - minY + 1;
            texture.data.resize(static_cast<size_t>(texture.width) * texture.height * 4);
            for (int y = 0; y < texture.height; ++y) {
                const size_t sourceOffset = (static_cast<size_t>(minY + y) * width + minX) * 4;
                const size_t destinationOffset = static_cast<size_t>(y) * texture.width * 4;
                std::copy_n(pixels + sourceOffset, static_cast<size_t>(texture.width) * 4,
                    texture.data.data() + destinationOffset);
            }
            for (size_t pixel = 0; pixel < texture.data.size(); pixel += 4) {
                const int colorDifference = std::max(
                    {std::abs(texture.data[pixel] - background[0]), std::abs(texture.data[pixel + 1] - background[1]),
                        std::abs(texture.data[pixel + 2] - background[2])});
                if (colorDifference <= 8) {
                    texture.data[pixel + 3] = 0;
                }
            }
        } else {
            const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            texture.data.assign(pixels, pixels + pixelCount * 4);
        }
        for (size_t alphaIndex = 3; alphaIndex < texture.data.size(); alphaIndex += 4) {
            if (texture.data[alphaIndex] > 25 && texture.data[alphaIndex] < 255) {
                texture.hasBlendedAlpha = true;
                break;
            }
        }
        stbi_image_free(pixels);
        const float scale = 5.0f;
        const std::vector<Vertex> vertices = {
            {{-scale, 0.0f, scale}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
            {{scale, 0.0f, scale}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
            {{scale, 0.0f, -scale}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
            {{-scale, 0.0f, -scale}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        };
        const std::vector<unsigned int> indices = {0, 1, 2, 0, 2, 3};
        model.texturesLoaded.push_back(texture);
        model.meshes.emplace_back(vertices, indices, std::vector<Texture>{texture});
        layerNames.push_back(layerName);
    }
    model.initGl();
    selectedLayerIndex = 0;
    loadedFile = std::move(result->sourceFile);
    gfxfLoaded = true;
    Menu::updateMenuState();
    Logger::log(NK_INFO, "GFXF load complete: %s (%zu map layers)", loadedFile.c_str(), layerNames.size());
}

void Gfxf::render(const Shader& shader, const glm::mat4& view, const glm::mat4& projection) const {
    if (!gfxfLoaded || !showGfxf || model.meshes.empty() || selectedLayerIndex >= model.meshes.size()) {
        return;
    }

    const Mesh& mesh = model.meshes[selectedLayerIndex];
    if (mesh.textures.empty() || mesh.textures.front().height <= 0) {
        return;
    }
    const float imageAspect =
        static_cast<float>(mesh.textures.front().width) / static_cast<float>(mesh.textures.front().height);
    const float width = 88.0f * renderScale;
    const float height = width / imageAspect;
    const glm::vec3 center{0.0f, 0.0f, 0.0f};
    const glm::vec3 offset{static_cast<float>(offsetX), static_cast<float>(-offsetZ), static_cast<float>(offsetY)};
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), center + offset);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::scale(modelMatrix, glm::vec3(width, 1.0f, height));
    shader.use();
    shader.setBool("useFlatColor", true);
    shader.setBool("useVertexColor", true);
    shader.setBool("useTexture", false);
    shader.setBool("useUiTexture", true);
    shader.setBool("useAlphaTexture", false);
    shader.setBool("useFog", false);
    shader.setVec4("flatColor", {1.0f, 1.0f, 1.0f, renderOpacity});
    shader.setInt("uiTexture", 0);
    shader.setMat4("projection", projection);
    shader.setMat4("view", view);
    shader.setMat4("model", modelMatrix);
    shader.setMat3("normalMatrix", glm::mat3(1.0f));

    const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean polygonOffsetEnabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    GLboolean depthWriteEnabled = GL_TRUE;
    GLint depthFunction = GL_LESS;
    GLfloat polygonOffsetFactor = 0.0f;
    GLfloat polygonOffsetUnits = 0.0f;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteEnabled);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &polygonOffsetFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &polygonOffsetUnits);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mesh.textures.front().id);
    glBindVertexArray(mesh.VAO);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glPolygonOffset(polygonOffsetFactor, polygonOffsetUnits);
    if (!polygonOffsetEnabled) {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glDepthFunc(depthFunction);
    glDepthMask(depthWriteEnabled);
    if (cullEnabled) {
        glEnable(GL_CULL_FACE);
    }
    if (!depthTestEnabled) {
        glDisable(GL_DEPTH_TEST);
    }
}

void Gfxf::clear() {
    ++loadGeneration;
    closeGfxfSettingsDialog();
    gfxfLoaded = false;
    loadedFile.clear();
    layerNames.clear();
    selectedLayerIndex = 0;
    model.cleanup();
    if (backgroundWorker) {
        loading = true;
    } else {
        loading = false;
        std::lock_guard lock(resultMutex);
        completedLoad.reset();
    }
}

void Gfxf::startGfxBuild() {
    if (backgroundWorker) {
        return;
    }
    backgroundWorker.emplace(&Gfxf::buildGfx, this);
}
