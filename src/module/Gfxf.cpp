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
#include <glm/gtc/quaternion.hpp>
#include <stb_image.h>
#include <wx/app.h>
#include <wx/arrstr.h>
#include <wx/button.h>
#include <wx/hyperlink.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/process.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include <navkit-rpkg-lib.h>
#include "../../include/NavKit/UiIds.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/Project.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/WxApplication.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/util/GfxMapAlignment.h"
#include "../../include/NavKit/util/GfxMapLayerSwf.h"
#include "../../include/NavKit/render/Mesh.h"

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

    // Whether an RGBA pixel of an exported map layer is its backdrop: transparent, or the canvas and water colour
    // the official maps fill their background with. Floors and walls never use it, so it can be cleared to show the
    // scene through. (A map's corner pixel is no guide: where land reaches the corner it is floor-coloured.)
    bool isMapBackdrop(const unsigned char* rgba) {
        constexpr std::array<int, 3> backdrop = {0x1c, 0x1e, 0x20};
        constexpr int tolerance = 8;
        return rgba[3] == 0 ||
            (std::abs(rgba[0] - backdrop[0]) <= tolerance && std::abs(rgba[1] - backdrop[1]) <= tolerance &&
                std::abs(rgba[2] - backdrop[2]) <= tolerance);
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

    bool isGfxfContainer(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::array<char, 4> signature{};
        input.read(signature.data(), static_cast<std::streamsize>(signature.size()));
        return input.gcount() == static_cast<std::streamsize>(signature.size()) &&
            std::string_view(signature.data(), signature.size()) == "BIN1";
    }

    bool isRawGfx(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::array<char, 3> signature{};
        input.read(signature.data(), static_cast<std::streamsize>(signature.size()));
        return input.gcount() == static_cast<std::streamsize>(signature.size()) &&
            std::string_view(signature.data(), signature.size()) == "GFX";
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

    // How to start FFDec's CLI: the program and the arguments that go before FFDec's own. Empty when FFDec is missing.
    struct FfdecCommand {
        std::vector<wxString> prefix;
    };

    // Thrown when FFDec could not be started because no suitable Java could be found or started.
    struct JavaMissingError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    constexpr int minimumJavaVersion = 8;
    constexpr auto javaDownloadUrl = "https://adoptium.net/temurin/releases/?os=windows&arch=x64&package=jre";

    wxString pathToWx(const std::filesystem::path& path) {
        return wxString::FromUTF8(pathToUtf8(path));
    }

    // Whether an FFDec run that ended with `exitCode` means Java is missing: the process did not start, or the
    // bundled Windows launcher found no suitable Java (it exits with -1 then).
    bool isJavaMissingExit(const long exitCode) {
        return exitCode == -1;
    }

    // The Java major version named by the release file of a Java home, or 0 when it has none.
    int javaHomeMajorVersion(const std::filesystem::path& javaHome) {
        std::ifstream input(javaHome / "release");
        std::string line;
        constexpr std::string_view key = "JAVA_VERSION=";
        while (std::getline(input, line)) {
            if (!line.starts_with(key)) {
                continue;
            }
            std::string value = line.substr(key.size());
            std::erase(value, '"');
            if (value.starts_with("1.")) {
                value.erase(0, 2); // 1.8.0_392 is Java 8
            }
            int major = 0;
            for (const char character : value) {
                if (!std::isdigit(static_cast<unsigned char>(character)) || major > 1000) {
                    break;
                }
                major = major * 10 + (character - '0');
            }
            return major;
        }
        return 0;
    }

    // Java from JAVA_HOME or the PATH that is new enough for FFDec, or an empty path.
    std::filesystem::path findJavaExecutable() {
#ifdef _WIN32
        const wxString javaName = "java.exe";
#else
        const wxString javaName = "java";
#endif
        std::vector<std::filesystem::path> candidates;
        wxString javaHome;
        if (wxGetEnv("JAVA_HOME", &javaHome) && !javaHome.empty()) {
            const auto homeBuffer = javaHome.ToUTF8();
            const auto nameBuffer = javaName.ToUTF8();
            if (homeBuffer && nameBuffer) {
                candidates.push_back(
                    std::filesystem::u8path(homeBuffer.data()) / "bin" / std::filesystem::u8path(nameBuffer.data()));
            }
        }
        candidates.push_back(findExecutableInPath(javaName));

        for (const auto& candidate : candidates) {
            if (candidate.empty() || !isRegularFile(candidate)) {
                continue;
            }
            // Resolve links, such as those in Oracle's javapath folder, to the Java home they belong to.
            std::error_code error;
            std::filesystem::path resolved = std::filesystem::canonical(candidate, error);
            if (error) {
                resolved = candidate;
            }
            const int version = javaHomeMajorVersion(resolved.parent_path().parent_path());
            // A java with no release file beside it is a shim or an unusual layout; trust it to be new enough.
            if (version == 0 || version >= minimumJavaVersion) {
                return candidate;
            }
            Logger::log(NK_WARN, "Ignoring Java %d at %s: FFDec needs Java %d or newer.", version,
                pathToUtf8(candidate).c_str(), minimumJavaVersion);
        }
        return {};
    }

    FfdecCommand findFfdecCommand() {
        wxString configuredPath;
        if (wxGetEnv("FFDEC_PATH", &configuredPath) && !configuredPath.empty()) {
            const auto buffer = configuredPath.ToUTF8();
            if (buffer) {
                const std::filesystem::path path = std::filesystem::u8path(buffer.data());
                if (isRegularFile(path)) {
                    return {{pathToWx(path)}};
                }
            }
        }

        const std::filesystem::path bundledDirectory = FileUtil::getApplicationResourcePath("ffdec");
#ifdef _WIN32
        // The bundled ffdec-cli.exe launcher finds Java only through the registry entries Java installers write, so
        // it misses Java from a zip, a package manager or an IDE. Run FFDec's jar with Java from JAVA_HOME or the
        // PATH when there is one, and leave the registry search to the launcher otherwise.
        const std::filesystem::path bundledJar = bundledDirectory / "ffdec-cli.jar";
        if (isRegularFile(bundledJar)) {
            const std::filesystem::path java = findJavaExecutable();
            if (!java.empty()) {
                return {{pathToWx(java), "-Djava.net.preferIPv4Stack=true", "-Djna.nosys=true",
                    "-Djava.util.Arrays.useLegacyMergeSort=true", "-jar", pathToWx(bundledJar)}};
            }
        }
#endif
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
                    return {{pathToWx(bundledCandidate)}};
                }
            }
            const std::filesystem::path pathCandidate = findExecutableInPath(executableName);
            if (!pathCandidate.empty()) {
                return {{pathToWx(pathCandidate)}};
            }
        }

#ifdef _WIN32
        const std::array<std::filesystem::path, 2> commonInstallPaths = {
            std::filesystem::path("C:/Program Files (x86)/FFDec/ffdec-cli.exe"),
            std::filesystem::path("C:/Program Files/FFDec/ffdec-cli.exe")};
        for (const auto& path : commonInstallPaths) {
            if (isRegularFile(path)) {
                return {{pathToWx(path)}};
            }
        }
#endif
        return {};
    }

    // Tells the user to install Java, with a link to a recommended download. Call on the main thread.
    void showJavaRequiredDialog(const wxString& title) {
        Logger::log(
            NK_ERROR, "FFDec could not run because no Java %d or newer could be found or started.", minimumJavaVersion);
        wxDialog dialog(getMainFrame(), wxID_ANY, title);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const wxString message = wxString::Format(
            "NavKit uses FFDec to open and build GFX map files, and FFDec needs Java %d or newer. No suitable Java "
            "installation could be found or started.\n\n"
            "Install Java, then try again. A recommended version is Eclipse Adoptium Temurin: on its download page, "
            "choose the "
            "Windows x64 JRE and download the .msi installer.",
            minimumJavaVersion);
        auto* text = new wxStaticText(&dialog, wxID_ANY, message);
        text->Wrap(dialog.FromDIP(440));
        sizer->Add(text, 0, wxALL, dialog.FromDIP(12));
        sizer->Add(new wxHyperlinkCtrl(&dialog, wxID_ANY, "Download Adoptium Temurin", javaDownloadUrl), 0,
            wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));
        sizer->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, dialog.FromDIP(8));
        dialog.SetSizerAndFit(sizer);
        dialog.CentreOnParent();
        dialog.ShowModal();
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
        return GfxMapLayerSwf::spriteLayerName(pathToUtf8(path.parent_path().filename()));
    }

    // FFDec renders map layers at this many pixels per canvas unit (one unit is one metre in the game's maps).
    constexpr float ffdecZoom = 2.0f;
    // Walls are matched where they cross this far above a layer's floor, like the GFX builder draws them.
    constexpr float sectionHeight = 1.2f;
    // A geometry placement is used only when it is this much better than a random one.
    constexpr float minimumAlignmentConfidence = 1.5f;

    struct Bounds {
        glm::vec3 minimum{std::numeric_limits<float>::max()};
        glm::vec3 maximum{std::numeric_limits<float>::lowest()};

        void add(const Bounds& other) {
            minimum = glm::min(minimum, other.minimum);
            maximum = glm::max(maximum, other.maximum);
        }

        [[nodiscard]] bool valid() const {
            return minimum.x <= maximum.x;
        }
    };

    // The world bounds of a map space config's volume boxes. Some exports drop the leading digit of entity IDs in
    // references, so an unambiguous suffix match is accepted, like the GFX builder does.
    Bounds mapSpaceBounds(const Json::MapSpaceConfig& config, const std::vector<Json::Entity>& volumeBoxes) {
        Bounds bounds;
        for (const std::string& id : config.inclusions.data) {
            if (id.empty()) {
                continue;
            }
            const Json::Entity* volume = nullptr;
            int matches = 0;
            for (const Json::Entity& candidate : volumeBoxes) {
                if (candidate.id == id) {
                    volume = &candidate;
                    matches = 1;
                    break;
                }
                if (candidate.id.size() > id.size() && candidate.id.ends_with(id)) {
                    volume = &candidate;
                    ++matches;
                }
            }
            if (volume == nullptr || matches != 1) {
                continue;
            }
            const glm::quat rotation(volume->rotation.w, volume->rotation.x, volume->rotation.y, volume->rotation.z);
            const glm::vec3 half =
                glm::abs(glm::vec3(volume->scale.data.x, volume->scale.data.y, volume->scale.data.z)) * 0.5f;
            const glm::vec3 centre(volume->position.x, volume->position.y, volume->position.z);
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 local((corner & 1 ? 1.0f : -1.0f) * half.x, (corner & 2 ? 1.0f : -1.0f) * half.y,
                    (corner & 4 ? 1.0f : -1.0f) * half.z);
                const glm::vec3 point = centre + rotation * local;
                bounds.minimum = glm::min(bounds.minimum, point);
                bounds.maximum = glm::max(bounds.maximum, point);
            }
        }
        return bounds;
    }

    // The bounds of each map layer's volumes, in the order of the GFX builder's slices, and the canvas: the
    // background volumes, or else all of the layers' volumes.
    std::pair<std::vector<Bounds>, Bounds> sceneMapSpaces() {
        const Scene& scene = Scene::getInstance();
        std::vector<Bounds> slices;
        Bounds background;
        Bounds all;
        for (const Json::MapSpaceConfig& config : scene.mapSpaceConfigs) {
            const Bounds bounds = mapSpaceBounds(config, scene.volumeBoxes);
            if (GfxMapLayerSwf::isBackgroundMapSpaceName(config.name)) {
                if (bounds.valid()) {
                    background.add(bounds);
                }
                continue;
            }
            slices.push_back(bounds);
            if (bounds.valid()) {
                all.add(bounds);
            }
        }
        return {slices, background.valid() ? background : all};
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

void Gfxf::handleOpenGfxClicked() {
    const nfdu8filteritem_t filter = {"GFX files", "gfx"};
    if (char* fileName = FileUtil::openNfdLoadDialog(&filter, 1)) {
        const std::string path = fileName;
        NFD_FreePathU8(fileName);
        loadGfxfFromFile(path);
    }
}

bool Gfxf::loadGfxfFromFile(const std::string& fileName) {
    if (!isRegularFile(std::filesystem::u8path(fileName))) {
        showGfxfError("The selected GFXF/GFX/SWF file does not exist or is not a regular file.");
        return false;
    }
    if (!startLoad(fileName)) {
        return false;
    }
    source = FileSource::disk(fileName);
    return true;
}

bool Gfxf::extractGfxfFromRpkgs(const std::string& hash) {
    const std::filesystem::path outputFile =
        std::filesystem::u8path(NavKitSettings::getInstance().outputFolder) / "gfxf" / (hash + ".GFX");
    std::error_code removeError;
    std::filesystem::remove(outputFile, removeError);
    if (removeError) {
        showGfxfError("Could not remove the previous GFXF extraction: " + removeError.message());
        return false;
    }
    if (Rpkg::extractResourcesFromRpkgs({hash}, GFXF) != 0 || !isRegularFile(outputFile)) {
        showGfxfError("The RPKG library did not produce the expected GFX payload: " + pathToUtf8(outputFile));
        return false;
    }
    Gfxf& gfxf = getInstance();
    if (!gfxf.loadGfxfFromFile(pathToUtf8(outputFile))) {
        return false;
    }
    gfxf.source = FileSource::rpkg(hash);
    return true;
}

bool Gfxf::canSave() const {
    return gfxBuilt && !saving;
}

char* Gfxf::openSaveGfxfFileDialog() {
    const nfdu8filteritem_t filter = {"GFXF files", "GFXF"};
    return FileUtil::openNfdSaveDialog(&filter, 1, "output.GFXF");
}

std::vector<unsigned char> Gfxf::extractGfxFromGfxf(const std::vector<unsigned char>& gfxfBytes) {
    // GFXF resources are BIN1 containers: a 16-byte header with a big-endian data size, then a data section whose
    // first two 64-bit fields are the GFX payload's offset (relative to the data section) and its size.
    // The game's own GFXF files put a 32-bit element count directly before the payload (offset 0x44), but the
    // glacier_bin1 serializer that navkit-rpkg-lib uses leaves it out (offset 0x40); the game reads both, so the
    // payload is located through its offset rather than at a fixed position.
    constexpr size_t dataSectionOffset = 16;
    const auto readLittleEndian = [&gfxfBytes](const size_t offset) {
        std::uint64_t value = 0;
        for (size_t i = 0; i < 8; ++i) {
            value |= static_cast<std::uint64_t>(gfxfBytes[offset + i]) << (8 * i);
        }
        return value;
    };
    if (gfxfBytes.size() < dataSectionOffset + 16 || !std::equal(gfxfBytes.begin(), gfxfBytes.begin() + 4, "BIN1")) {
        throw std::runtime_error("The GFXF file is missing its BIN1 header.");
    }
    const std::uint64_t dataSize = static_cast<std::uint64_t>(gfxfBytes[8]) << 24 |
        static_cast<std::uint64_t>(gfxfBytes[9]) << 16 | static_cast<std::uint64_t>(gfxfBytes[10]) << 8 |
        static_cast<std::uint64_t>(gfxfBytes[11]);
    if (dataSize > gfxfBytes.size() - dataSectionOffset) {
        throw std::runtime_error("The GFXF data section extends beyond the end of the file.");
    }
    const std::uint64_t gfxOffset = readLittleEndian(dataSectionOffset);
    const std::uint64_t gfxSize = readLittleEndian(dataSectionOffset + 8);
    if (gfxOffset > dataSize || gfxSize > dataSize - gfxOffset) {
        throw std::runtime_error("The GFXF payload extends beyond its data section.");
    }
    const auto gfxBegin = gfxfBytes.begin() + static_cast<std::ptrdiff_t>(dataSectionOffset + gfxOffset);
    std::vector<unsigned char> gfxBytes(gfxBegin, gfxBegin + static_cast<std::ptrdiff_t>(gfxSize));
    if (gfxBytes.size() < 8 || !std::equal(gfxBytes.begin(), gfxBytes.begin() + 3, "GFX")) {
        throw std::runtime_error("The GFXF payload is not a GFX file.");
    }
    const std::uint32_t declaredLength = static_cast<std::uint32_t>(gfxBytes[4]) |
        static_cast<std::uint32_t>(gfxBytes[5]) << 8 | static_cast<std::uint32_t>(gfxBytes[6]) << 16 |
        static_cast<std::uint32_t>(gfxBytes[7]) << 24;
    if (declaredLength != gfxBytes.size()) {
        throw std::runtime_error("The GFXF payload's GFX header does not match its size.");
    }
    return gfxBytes;
}

std::vector<unsigned char> Gfxf::loadGfxFromFile(const std::string& fileName) {
    std::vector<unsigned char> bytes = readFileBytes(std::filesystem::u8path(fileName));
    if (bytes.empty()) {
        throw std::runtime_error("Could not read GFX/GFXF file: " + fileName);
    }
    if (bytes.size() >= 4 && std::equal(bytes.begin(), bytes.begin() + 4, "BIN1")) {
        return extractGfxFromGfxf(bytes);
    }
    if (bytes.size() < 8 || !std::equal(bytes.begin(), bytes.begin() + 3, "GFX")) {
        throw std::runtime_error("The file is neither a GFX nor a GFXF file: " + fileName);
    }
    return bytes;
}

bool Gfxf::packGfxToGfxfFile(const std::vector<unsigned char>& gfxBytes, const std::string& fileName) {
    return !gfxBytes.empty() &&
        pack_gfx_to_gfxf(gfxBytes.data(), gfxBytes.size(), fileName.c_str(), Logger::rustLogCallback) == 0;
}

void Gfxf::setGfxData(std::vector<unsigned char> bytes) {
    {
        std::lock_guard lock(gfxDataMutex);
        gfxData = std::move(bytes);
        gfxBuilt = !gfxData.empty();
    }
    Menu::updateMenuState();
}

bool Gfxf::exportGfxfToGfxfFile(const std::string& fileName) {
    saving = true;
    bool saved = false;
    Menu::updateMenuState();
    const auto start = std::chrono::high_resolution_clock::now();
    Logger::log(NK_INFO, "Saving Gfxf to file: '%s'...", fileName.c_str());

    {
        std::lock_guard lock(gfxDataMutex);
        if (gfxData.empty()) {
            Logger::log(NK_ERROR, "No built GFX is available to save as Gfxf.");
        } else if (!packGfxToGfxfFile(gfxData, fileName)) {
            Logger::log(NK_ERROR, "Error saving Gfxf file '%s'.", fileName.c_str());
        } else {
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            Logger::log(NK_INFO, "Finished saving Gfxf in %lld ms.", duration.count());
            saved = true;
        }
    }

    saving = false;
    Menu::updateMenuState();
    return saved;
}

void Gfxf::handleSaveGfxfClicked() {
    if (char* fileName = openSaveGfxfFileDialog()) {
        const std::string path = fileName;
        NFD_FreePathU8(fileName);
        saveWorker.emplace([this, path] {
            if (exportGfxfToGfxfFile(path)) {
                Project::fileSaved([this, path] { source = FileSource::disk(path); });
            }
        });
    }
}

bool Gfxf::exportGfxToGfxFile(const std::string& fileName) {
    saving = true;
    bool saved = false;
    Menu::updateMenuState();
    Logger::log(NK_INFO, "Saving Gfx to file: '%s'...", fileName.c_str());

    {
        std::lock_guard lock(gfxDataMutex);
        if (gfxData.empty()) {
            Logger::log(NK_ERROR, "No GFX is available to save.");
        } else {
            std::ofstream output(std::filesystem::u8path(fileName), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(gfxData.data()), static_cast<std::streamsize>(gfxData.size()));
            output.close();
            if (output) {
                Logger::log(NK_INFO, "Finished saving Gfx.");
                saved = true;
            } else {
                Logger::log(NK_ERROR, "Error saving Gfx file '%s'.", fileName.c_str());
            }
        }
    }

    saving = false;
    Menu::updateMenuState();
    return saved;
}

void Gfxf::handleSaveGfxClicked() {
    const nfdu8filteritem_t filter = {"GFX files", "gfx"};
    if (char* fileName = FileUtil::openNfdSaveDialog(&filter, 1, "output.gfx")) {
        const std::string path = fileName;
        NFD_FreePathU8(fileName);
        saveWorker.emplace([this, path] {
            if (exportGfxToGfxFile(path)) {
                Project::fileSaved([this, path] { source = FileSource::disk(path); });
            }
        });
    }
}

void Gfxf::buildGfx() {
    const std::time_t startTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    Logger::log(NK_INFO, "Starting GFX build at %s", std::ctime(&startTime));
    const auto start = std::chrono::high_resolution_clock::now();
    const auto& mapSpaceConfigs = Scene::getInstance().mapSpaceConfigs;
    std::string error;
    std::filesystem::path temporaryDirectory;
    std::filesystem::path gfxfPath;
    bool javaMissing = false;
    try {
        // Blender writes one output_NN.svg per map-space config that is not the background, which it writes as
        // output_background.svg instead.
        const size_t layerCount = static_cast<size_t>(std::ranges::count_if(mapSpaceConfigs,
            [](const auto& config) { return !GfxMapLayerSwf::isBackgroundMapSpaceName(config.name); }));
        if (layerCount == 0) {
            throw std::runtime_error("The loaded scene has no map-space configurations for map layers.");
        }

        const FfdecCommand ffdecCommand = findFfdecCommand();
        if (ffdecCommand.prefix.empty()) {
            throw std::runtime_error(
                "The bundled FFDec CLI is missing. Repair the NavKit installation or set FFDEC_PATH to an "
                "FFDec CLI executable.");
        }

        const std::filesystem::path templatePath = FileUtil::getApplicationResourcePath("map_template.gfx");
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
        const auto runFfdec = [&ffdecCommand](std::vector<wxString> arguments, const std::string& action) {
            arguments.insert(arguments.begin(), ffdecCommand.prefix.begin(), ffdecCommand.prefix.end());
            const long exitCode = executeFfdec(arguments);
            if (isJavaMissingExit(exitCode)) {
                throw JavaMissingError("FFDec could not start while " + action + ": Java was not found.");
            }
            if (exitCode != 0) {
                throw std::runtime_error(
                    "FFDec failed while " + action + " (exit code " + std::to_string(exitCode) + ").");
            }
        };
        const auto asWxPath = [](const std::filesystem::path& path) { return wxString::FromUTF8(pathToUtf8(path)); };
        const auto readText = [](const std::filesystem::path& path, const std::string& description) {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                throw std::runtime_error("FFDec did not write the " + description + ".");
            }
            return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        };
        runFfdec({"-swf2xml", asWxPath(templatePath), asWxPath(templateXml)}, "opening the GFX template");

        // Every map SVG shares the background's canvas, at one unit per metre.
        const std::filesystem::path backgroundPath = outputDirectory / "output_background.svg";
        if (!isRegularFile(backgroundPath)) {
            throw std::runtime_error("The Blender map background SVG is missing: " + pathToUtf8(backgroundPath));
        }
        const auto canvasSize = GfxMapLayerSwf::svgCanvasSize(readText(backgroundPath, "map background SVG"));
        if (!canvasSize) {
            throw std::runtime_error("The map background SVG has no canvas size: " + pathToUtf8(backgroundPath));
        }
        // The scene's map-layer ZUIControlEntity class names ("<package>.SLICE_NN") give the package that replaces
        // the template's Generated_Map, so the GFX defines the classes the scene's map layers name.
        std::string mapPackage;
        for (const Json::UIControl& uiControl : Scene::getInstance().uiControls) {
            const std::string package = GfxMapLayerSwf::mapLayerPackage(uiControl.className);
            if (package.empty()) {
                continue;
            }
            if (mapPackage.empty()) {
                mapPackage = package;
            } else if (package != mapPackage) {
                Logger::log(NK_WARN, "Map layer class '%s' is not in package '%s'; using '%s' for the GFX.",
                    uiControl.className.c_str(), mapPackage.c_str(), mapPackage.c_str());
            }
        }
        if (mapPackage.empty()) {
            Logger::log(NK_WARN,
                "The loaded scene has no map-layer UI control class names; the GFX keeps the template's package.");
        } else {
            Logger::log(NK_INFO, "Using map class package '%s' for the GFX.", mapPackage.c_str());
        }
        const std::string generatedXml = GfxMapLayerSwf::createMapLayerSwfXml(
            readText(templateXml, "GFX template XML"), layerCount, canvasSize->first, canvasSize->second, mapPackage);
        const std::filesystem::path generatedXmlPath = temporaryDirectory / "generated.xml";
        std::ofstream generatedXmlOutput(generatedXmlPath, std::ios::binary | std::ios::trunc);
        generatedXmlOutput.write(generatedXml.data(), static_cast<std::streamsize>(generatedXml.size()));
        generatedXmlOutput.close();
        if (!generatedXmlOutput) {
            throw std::runtime_error("Could not write the generated GFX shell.");
        }

        const std::filesystem::path shapeDirectory = temporaryDirectory / "shapes";
        std::filesystem::create_directories(shapeDirectory, filesystemError);
        if (filesystemError) {
            throw std::runtime_error("Could not create the temporary GFX shape folder: " + filesystemError.message());
        }
        const auto stageShape = [&](const std::filesystem::path& svgPath, const unsigned int shapeId) {
            const std::filesystem::path shapePath = shapeDirectory / (std::to_string(shapeId) + ".svg");
            std::filesystem::copy_file(
                svgPath, shapePath, std::filesystem::copy_options::overwrite_existing, filesystemError);
            if (filesystemError) {
                throw std::runtime_error(
                    "Could not stage map-layer SVG " + pathToUtf8(svgPath) + ": " + filesystemError.message());
            }
        };
        for (size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
            const std::string layerNumber =
                layerIndex < 10 ? "0" + std::to_string(layerIndex) : std::to_string(layerIndex);
            const std::filesystem::path svgPath = outputDirectory / ("output_" + layerNumber + ".svg");
            if (!isRegularFile(svgPath)) {
                throw std::runtime_error("The Blender map-layer SVG is missing: " + pathToUtf8(svgPath));
            }
            if (const auto layerSize = GfxMapLayerSwf::svgCanvasSize(readText(svgPath, "map-layer SVG")); !layerSize ||
                std::abs(layerSize->first - canvasSize->first) > 0.01 ||
                std::abs(layerSize->second - canvasSize->second) > 0.01) {
                throw std::runtime_error("The map-layer SVG " + pathToUtf8(svgPath) +
                    " is not the size of the map background; rebuild the map from the scene.");
            }
            stageShape(backgroundPath, GfxMapLayerSwf::backgroundShapeId(layerIndex));
            stageShape(svgPath, GfxMapLayerSwf::layerShapeId(layerIndex));
        }

        const std::filesystem::path baseGfx = temporaryDirectory / "base.gfx";
        runFfdec({"-xml2swf", asWxPath(generatedXmlPath), asWxPath(baseGfx)}, "creating map-layer sprites");
        const std::filesystem::path importedGfx = temporaryDirectory / "imported.gfx";
        runFfdec({"-importShapes", asWxPath(baseGfx), asWxPath(importedGfx), asWxPath(shapeDirectory)},
            "importing map-layer SVGs");

        const std::filesystem::path importedXml = temporaryDirectory / "imported.xml";
        runFfdec({"-swf2xml", asWxPath(importedGfx), asWxPath(importedXml)}, "validating map-layer sprites");
        if (const std::string validationError =
                GfxMapLayerSwf::validateMapLayerSwfXml(readText(importedXml, "generated GFX XML"), layerCount);
            !validationError.empty()) {
            throw std::runtime_error("FFDec produced an invalid map GFX: " + validationError);
        }

        std::vector<unsigned char> builtGfx = readFileBytes(importedGfx);
        if (builtGfx.size() < 21 || !std::equal(builtGfx.begin(), builtGfx.begin() + 3, "GFX")) {
            throw std::runtime_error("FFDec did not produce a valid output.gfx file.");
        }
        const std::filesystem::path outputPath = outputDirectory / "output.gfx";
        std::filesystem::copy_file(
            importedGfx, outputPath, std::filesystem::copy_options::overwrite_existing, filesystemError);
        if (filesystemError) {
            throw std::runtime_error("Could not save output.gfx: " + filesystemError.message());
        }
        Logger::log(NK_INFO, "Built GFX with %zu map layers at %s", layerCount, pathToUtf8(outputPath).c_str());

        // Pack the GFX into output.gfxf and make sure it reads back to the same GFX before loading it.
        const std::filesystem::path outputGfxfPath = outputDirectory / "output.gfxf";
        if (!packGfxToGfxfFile(builtGfx, pathToUtf8(outputGfxfPath))) {
            throw std::runtime_error("Could not save output.gfxf.");
        }
        if (loadGfxFromFile(pathToUtf8(outputGfxfPath)) != builtGfx) {
            throw std::runtime_error("output.gfxf does not read back to the built GFX.");
        }
        Logger::log(NK_INFO, "Saved Gfxf to %s", pathToUtf8(outputGfxfPath).c_str());
        setGfxData(std::move(builtGfx));
        gfxfPath = outputGfxfPath;
    } catch (const JavaMissingError& exception) {
        javaMissing = true;
        error = "Failed to build output.gfxf: " + std::string(exception.what());
    } catch (const std::exception& exception) {
        error = "Failed to build output.gfxf: " + std::string(exception.what());
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
        if (wxTheApp != nullptr && javaMissing) {
            wxTheApp->CallAfter([] { showJavaRequiredDialog("GFX build failed"); });
        } else if (wxTheApp != nullptr) {
            wxTheApp->CallAfter([error] {
                wxMessageBox(wxString::FromUTF8(error), "GFX build failed", wxOK | wxICON_ERROR, getMainFrame());
            });
        }
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    Logger::log(NK_INFO, "Finished GFX build in %s seconds", std::to_string(duration.count()).c_str());
    if (!gfxfPath.empty() && wxTheApp != nullptr) {
        wxTheApp->CallAfter([gfxfPath] {
            if (Gfxf& gfxf = getInstance(); gfxf.loadGfxfFromFile(pathToUtf8(gfxfPath))) {
                gfxf.source = FileSource::generated();
            }
        });
    }
    building = false;
}

bool Gfxf::startLoad(const std::string& fileName) {
    if (!canLoad()) {
        return false;
    }
    closeGfxfSettingsDialog();

    const FfdecCommand ffdecCommand = findFfdecCommand();
    if (ffdecCommand.prefix.empty()) {
        showGfxfError("The bundled FFDec CLI is missing. Repair the NavKit installation or set FFDEC_PATH to an "
                      "FFDec CLI executable.");
        return false;
    }

    ++loadGeneration;
    const std::uint64_t generation = loadGeneration;
    model.cleanup();
    gfxfLoaded = false;
    loadedFile.clear();
    layerNames.clear();
    layerImages.clear();
    layerPlacements.clear();
    selectedLayerIndex = 0;
    loading = true;
    // Saving must only ever write the map on screen, so the previous GFX goes until this one has loaded.
    setGfxData({});

    static std::atomic_uint64_t outputCounter = 0;
    const std::string inputFile = fileName;
    backgroundWorker.emplace([this, generation, ffdecCommand, inputFile](std::stop_token) {
        ExportResult result{generation, inputFile, {}, {}};
        std::filesystem::path outputDirectory;
        try {
            outputDirectory = std::filesystem::temp_directory_path() / "NavKit" / "gfxf" /
                (std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                    std::to_string(++outputCounter));
            std::filesystem::create_directories(outputDirectory);

            // FFDec cannot read the BIN1 container of a GFXF, so hand it the GFX payload instead.
            std::filesystem::path gfxInput = std::filesystem::u8path(inputFile);
            if (isGfxfContainer(gfxInput)) {
                result.gfx = loadGfxFromFile(inputFile);
                gfxInput = outputDirectory / "gfxf-payload.gfx";
                std::ofstream payload(gfxInput, std::ios::binary | std::ios::trunc);
                payload.write(
                    reinterpret_cast<const char*>(result.gfx.data()), static_cast<std::streamsize>(result.gfx.size()));
                payload.close();
                if (!payload) {
                    throw std::runtime_error("Could not write the GFXF's GFX payload for FFDec.");
                }
            } else if (isRawGfx(gfxInput)) {
                result.gfx = loadGfxFromFile(inputFile);
            }

            std::filesystem::path ffdecInput;
            if (prepareFfdecInput(gfxInput, outputDirectory, ffdecInput, result.error)) {
                std::vector<wxString> arguments = ffdecCommand.prefix;
                arguments.insert(arguments.end(),
                    {"-zoom", "2", "-format", "frame:png,sprite:png", "-export", "frame,sprite",
                        pathToWx(outputDirectory), pathToWx(ffdecInput)});
                const long exitCode = executeFfdec(arguments);
                if (isJavaMissingExit(exitCode)) {
                    result.javaMissing = true;
                    result.error = "FFDec CLI could not start: Java was not found (exit code -1).";
                } else if (exitCode != 0) {
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
    return true;
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
        items.push_back(Rpkg::displayName(hash, ioiString));
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
            return Rpkg::displayName(candidate.first, candidate.second) == selected;
        });
        if (entry != entries.end()) {
            extractGfxfFromRpkgs(entry->first);
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
    content->Add(new wxStaticText(dialog, wxID_ANY, "Project Settings:"), 0, wxLEFT | wxRIGHT | wxTOP, 10);

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
    auto* align = new wxButton(dialog, wxID_ANY, "Align to Scene");
    align->SetToolTip("Place the map on the loaded GLB's geometry (or, without one, on the scene's map space "
                      "volumes). The offsets above move it from there.");
    align->Enable(gfxfLoaded && !aligning);
    auto* reset = new wxButton(dialog, IDC_BUTTON_RESET_DEFAULTS, "Reset Defaults");
    auto* buttonRow = new wxBoxSizer(wxHORIZONTAL);
    buttonRow->Add(align, 0, wxRIGHT, 8);
    buttonRow->Add(reset, 0);
    dialog->SetSizer(content);
    content->Add(buttonRow, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    align->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { getInstance().alignToScene(); });

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
    finalizeAlignment();
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
    if (result->javaMissing) {
        showJavaRequiredDialog("GFXF load failed");
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
    layerImages.clear();
    layerPlacements.clear();
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
            layerImages.clear();
            layerPlacements.clear();
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
        LayerPlacement placement;
        placement.slice = GfxMapLayerSwf::layerSliceNumber(layerName);
        const bool mapLayer = placement.slice >= 0;
        texture.width = width;
        texture.height = height;
        if (mapLayer) {
            int minX = width;
            int minY = height;
            int maxX = -1;
            int maxY = -1;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
                    if (!isMapBackdrop(pixels + pixel)) {
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
            placement.cropOffset = glm::vec2(static_cast<float>(minX), static_cast<float>(minY)) / ffdecZoom;
            texture.data.resize(static_cast<size_t>(texture.width) * texture.height * 4);
            for (int y = 0; y < texture.height; ++y) {
                const size_t sourceOffset = (static_cast<size_t>(minY + y) * width + minX) * 4;
                const size_t destinationOffset = static_cast<size_t>(y) * texture.width * 4;
                std::copy_n(pixels + sourceOffset, static_cast<size_t>(texture.width) * 4,
                    texture.data.data() + destinationOffset);
            }
            for (size_t pixel = 0; pixel < texture.data.size(); pixel += 4) {
                if (isMapBackdrop(texture.data.data() + pixel)) {
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
        placement.size = glm::vec2(static_cast<float>(texture.width), static_cast<float>(texture.height)) / ffdecZoom;
        layerPlacements.push_back(placement);
        // The pixels go to the GPU below; keep a copy for placing the map on the scene's geometry.
        layerImages.push_back({texture.width, texture.height, texture.data, ffdecZoom});
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
    setGfxData(std::move(result->gfx));
    Logger::log(NK_INFO, "GFXF load complete: %s (%zu map layers)", loadedFile.c_str(), layerNames.size());
    alignToScene();
}

void Gfxf::placeFromScene() {
    // A map built by NavKit has its canvas on the background (or else the layers') volumes, at one unit per
    // metre, and each layer on its slice's floor.
    const auto [slices, canvas] = sceneMapSpaces();
    canvasOrigin = canvas.valid() ? glm::vec2(canvas.minimum.x, -canvas.maximum.y) : glm::vec2(0.0f);
    for (LayerPlacement& placement : layerPlacements) {
        const bool known = placement.slice >= 0 && static_cast<size_t>(placement.slice) < slices.size() &&
            slices[placement.slice].valid();
        placement.elevation = known ? slices[placement.slice].minimum.z : 0.0f;
    }
}

void Gfxf::alignToScene() {
    if (!gfxfLoaded || aligning || layerImages.empty()) {
        return;
    }
    placeFromScene();
    const SceneMesh& sceneMesh = SceneMesh::getInstance();
    if (!sceneMesh.glbLoaded || sceneMesh.model.meshes.empty()) {
        Logger::log(NK_INFO, "GFXF placed from the scene's map space volumes (no GLB is loaded to align it to).");
        return;
    }

    // The GLB is y-up: the game's (x, y, z) is the GLB's (x, -z, y).
    std::vector<glm::vec3> triangles;
    for (const Mesh& mesh : sceneMesh.model.meshes) {
        for (const unsigned int index : mesh.indices) {
            const glm::vec3& position = mesh.vertices[index].position;
            triangles.emplace_back(position.x, -position.z, position.y);
        }
    }
    const auto [slices, canvas] = sceneMapSpaces();
    std::vector<std::optional<std::pair<float, float>>> sliceHeights;
    for (const LayerPlacement& placement : layerPlacements) {
        if (placement.slice >= 0 && static_cast<size_t>(placement.slice) < slices.size() &&
            slices[placement.slice].valid()) {
            sliceHeights.emplace_back(std::pair{slices[placement.slice].minimum.z, slices[placement.slice].maximum.z});
        } else {
            sliceHeights.emplace_back(std::nullopt);
        }
    }
    aligning = true;
    alignWorker.reset();
    alignWorker.emplace([this, generation = loadGeneration, images = layerImages, placements = layerPlacements,
                            triangles = std::move(triangles), sliceHeights = std::move(sliceHeights)](std::stop_token) {
        AlignmentResult result{generation, std::nullopt, 0.0f, {}};
        // Each layer's floor: the main floor level inside its slice, or else the slice-th level of the scene.
        const std::vector<float> sceneLevels = GfxMapAlignment::floorLevels(
            triangles, std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(), 2.5f);
        for (size_t layer = 0; layer < images.size(); ++layer) {
            float elevation = 0.0f;
            if (sliceHeights[layer]) {
                const auto [bottom, top] = *sliceHeights[layer];
                const auto levels = GfxMapAlignment::floorLevels(triangles, bottom - 0.5f, top, 2.5f);
                elevation = levels.empty() ? bottom : levels.front();
            } else if (!sceneLevels.empty()) {
                const size_t level = placements[layer].slice >= 0 ? static_cast<size_t>(placements[layer].slice) : 0;
                elevation = sceneLevels[std::min(level, sceneLevels.size() - 1)];
            }
            result.elevations.push_back(elevation);
            const auto placement = GfxMapAlignment::placeLayer(
                images[layer], GfxMapAlignment::sectionSegments(triangles, elevation + sectionHeight));
            if (placement && placement->confidence > result.confidence) {
                result.confidence = placement->confidence;
                result.canvasOrigin = placement->topLeft - placements[layer].cropOffset;
            }
        }
        if (result.confidence < minimumAlignmentConfidence) {
            result.canvasOrigin.reset();
        }
        std::lock_guard lock(resultMutex);
        completedAlignment = std::move(result);
    });
    Logger::log(NK_INFO, "Aligning the GFXF map with the loaded GLB...");
}

void Gfxf::finalizeAlignment() {
    std::optional<AlignmentResult> result;
    {
        std::lock_guard lock(resultMutex);
        result.swap(completedAlignment);
    }
    if (!result) {
        return;
    }
    alignWorker.reset();
    aligning = false;
    if (result->generation != loadGeneration || result->elevations.size() != layerPlacements.size()) {
        return;
    }
    for (size_t layer = 0; layer < layerPlacements.size(); ++layer) {
        layerPlacements[layer].elevation = result->elevations[layer];
    }
    if (result->canvasOrigin) {
        canvasOrigin = *result->canvasOrigin;
        Logger::log(NK_INFO, "Aligned the GFXF map with the GLB geometry (canvas origin %.1f, %.1f; match %.1fx).",
            canvasOrigin.x, -canvasOrigin.y, result->confidence);
    } else {
        Logger::log(NK_WARN,
            "Could not align the GFXF map with the GLB geometry; it stays where the scene's map "
            "space volumes put it.");
    }
}

void Gfxf::render(const Shader& shader, const glm::mat4& view, const glm::mat4& projection) const {
    if (!gfxfLoaded || !showGfxf || model.meshes.empty() || selectedLayerIndex >= model.meshes.size()) {
        return;
    }

    const Mesh& mesh = model.meshes[selectedLayerIndex];
    if (mesh.textures.empty() || mesh.textures.front().height <= 0) {
        return;
    }
    // The layer lies on the map plane at one metre per canvas unit: u is the GLB's x and v its z (the game's -y),
    // with the image's top row at the smallest v. The quad spans 10 units, its first texel at (-5, -5) once turned.
    const LayerPlacement placement =
        selectedLayerIndex < layerPlacements.size() ? layerPlacements[selectedLayerIndex] : LayerPlacement{};
    const glm::vec2 centre = canvasOrigin + placement.cropOffset + placement.size * 0.5f;
    const glm::vec3 offset{static_cast<float>(offsetX), static_cast<float>(offsetZ), static_cast<float>(offsetY)};
    // Just above the floor so that the map shows over it.
    const glm::vec3 position = glm::vec3(centre.x, placement.elevation + 0.05f, centre.y) + offset;
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), position);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::scale(
        modelMatrix, glm::vec3(placement.size.x / 10.0f * renderScale, 1.0f, placement.size.y / 10.0f * renderScale));
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
    source = {};
    layerNames.clear();
    layerImages.clear();
    layerPlacements.clear();
    selectedLayerIndex = 0;
    model.cleanup();
    setGfxData({});
    if (backgroundWorker) {
        loading = true;
    } else {
        loading = false;
        std::lock_guard lock(resultMutex);
        completedLoad.reset();
    }
}

void Gfxf::startGfxBuild() {
    if (building) {
        return;
    }
    // The previous build, if any, has finished; join its thread before starting the next one.
    buildWorker.reset();
    building = true;
    buildWorker.emplace(&Gfxf::buildGfx, this);
}
