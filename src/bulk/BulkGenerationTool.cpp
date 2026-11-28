// NavKit Bulk Generation Tool: generates NAVP, AIRG and CRMD files for a queue of vanilla missions and writes them in
// the Simple Mod Framework layout <game files>/<chunk>/<mission>/<hash>.<type>, and can keep each mission's source
// files in a separate folder. When the game files folder is an SMF mod's content folder, it deploys the mod and
// verifies the deployed files once the queue is finished.

#include "../../include/NavKit/bulk/BulkGenerator.h"
#include "../../include/NavKit/bulk/LivePipeline.h"
#include "../../include/NavKit/NavKitConfig.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Rpkg.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/frame.h>
#include <wx/graphics.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/radiobox.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
    using NavKit::Bulk::Mission;
    using NavKit::Bulk::MissionResult;
    using NavKit::Bulk::Phase;
    using NavKit::LivePipeline::LogLevel;

    constexpr const char* settingsSection = "BulkGeneration";
    constexpr const char* toolTitle = "NavKit Bulk Generation Tool";

    enum MenuId { ID_MENU_HELP = wxID_HIGHEST + 1, ID_MENU_ABOUT, ID_MENU_NAVKIT_SETTINGS, ID_MENU_OPEN_NAVKIT };

    constexpr const char* helpText =
        "The NavKit Bulk Generation Tool generates NAVP, AIRG and CRMD files for a queue of vanilla missions.\n"
        "\n"
        "Before you start\n"
        "  Set the HITMAN folder and Blender in Settings > NavKit Settings, and install ZHMModSDK with the Editor "
        "mod.\n"
        "\n"
        "Game files directory\n"
        "  Generated files are written as <directory>\\<chunk>\\<mission>\\<hash>.<type>. When this is a Simple Mod "
        "Framework mod's content folder, the mod is deployed and its files are checked once the queue is finished.\n"
        "\n"
        "Source files directory\n"
        "  With Keep .nav.json, .glb and .blend ticked, each mission's scene, geometry and Blender file are saved to "
        "<directory>\\<mission>. This adds a second Blender run per mission. It cannot be inside the game files "
        "directory, because Simple Mod Framework would pack these files.\n"
        "\n"
        "File types\n"
        "  Only the ticked types are written. GFXF is not available yet.\n"
        "\n"
        "Mission Selector and Queue\n"
        "  The tool scans the game's RPKG files for every mission that uses a NAVP or AIRG. Select missions (Ctrl and "
        "Shift select several), then press Add to Queue. Remove from Queue takes them out. Select All and Deselect All "
        "act on the list you last clicked.\n"
        "\n"
        "Start and Pause\n"
        "  Start works through the queue. Each mission leaves the queue when it starts. HITMAN is launched on the "
        "mission, the scene is extracted, the game is closed, and the files are built and saved. Pause stops after the "
        "mission in progress, and Start continues the queue.\n"
        "\n"
        "Variant missions\n"
        "  A variant such as an escalation also uses its base mission's NAVP, AIRG or crowd maps. Those files are left "
        "to the base mission, and only the variant's own files are written.\n"
        "\n"
        "Log level\n"
        "  Basic lists the operations for each mission and their results. Full adds everything NavKit normally logs. "
        "Debug adds NavKit's debug messages.\n"
        "\n"
        "Menus\n"
        "  Settings > NavKit Settings changes the paths NavKit and this tool share, and is available while the tool is "
        "idle. Tools > Open NavKit starts NavKit.";

    /// Whether @p path is @p directory or somewhere inside it.
    bool isInsideDirectory(const std::filesystem::path& path, const std::filesystem::path& directory) {
        std::error_code error;
        const std::filesystem::path fullPath = std::filesystem::weakly_canonical(path, error);
        const std::filesystem::path fullDirectory = std::filesystem::weakly_canonical(directory, error);
        const std::filesystem::path relative = fullPath.lexically_relative(fullDirectory);
        return !error && !relative.empty() && *relative.begin() != "..";
    }

    std::string utf8(const std::filesystem::path& path) {
        return wxString(path.wstring()).utf8_string();
    }

    std::string timestamp() {
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        std::ostringstream stream;
        stream << std::put_time(&local, "%H:%M:%S");
        return stream.str();
    }

    /// Command-line options for running the tool unattended, for example from a script.
    struct Automation {
        std::optional<std::string> outputDirectory;
        std::optional<std::string> sourceDirectory;
        /// Missions to queue once the scan finishes, by name or part of the scene path.
        std::vector<std::string> queue;
        bool start = false;
        bool closeWhenDone = false;
        bool keepSceneFiles = false;
    };

    /// Where a mission is, as a fraction of its work, when each phase starts; from the measured average phase times.
    double phaseStart(const Phase phase) {
        switch (phase) {
        case Phase::FindingResources:
            return 0.0;
        case Phase::LoadingScene:
            return 0.01;
        case Phase::BuildingGlb:
            return 0.2;
        case Phase::BuildingNavp:
            return 0.47;
        case Phase::BuildingAirg:
            return 0.5;
        case Phase::Saving:
            return 0.97;
        case Phase::BuildingCrmd:
            return 0.98;
        }
        return 0.0;
    }

    /// A play triangle or two pause bars, drawn so no image files have to be shipped.
    wxBitmap makeIcon(const bool play, const int size, const wxColour& colour) {
        wxImage image(size, size);
        image.InitAlpha();
        std::fill_n(image.GetAlpha(), size * size, static_cast<unsigned char>(0));
        {
            const std::unique_ptr<wxGraphicsContext> context(wxGraphicsContext::Create(image));
            context->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            context->SetBrush(wxBrush(colour));
            context->SetPen(*wxTRANSPARENT_PEN);
            const double margin = size * 0.15;
            if (play) {
                wxGraphicsPath path = context->CreatePath();
                path.MoveToPoint(margin * 1.4, margin);
                path.AddLineToPoint(size - margin, size / 2.0);
                path.AddLineToPoint(margin * 1.4, size - margin);
                path.CloseSubpath();
                context->FillPath(path);
            } else {
                const double barWidth = size * 0.25;
                context->DrawRectangle(margin, margin, barWidth, size - 2 * margin);
                context->DrawRectangle(size - margin - barWidth, margin, barWidth, size - 2 * margin);
            }
        }
        return wxBitmap(image);
    }

    /// A progress bar with the percentage drawn in its centre.
    class PercentBar final : public wxPanel {
    public:
        explicit PercentBar(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 26)) {
            SetBackgroundStyle(wxBG_STYLE_PAINT);
            Bind(wxEVT_PAINT, &PercentBar::onPaint, this);
            Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
                Refresh();
                event.Skip();
            });
        }

        void setFraction(const double value) {
            const double clamped = std::clamp(value, 0.0, 1.0);
            if (clamped != fraction) {
                fraction = clamped;
                Refresh();
            }
        }

    private:
        void onPaint(wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            const wxSize size = GetClientSize();
            dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
            dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
            dc.DrawRectangle(0, 0, size.x, size.y);
            const int filled = static_cast<int>((size.x - 2) * fraction);
            if (filled > 0) {
                dc.SetBrush(wxBrush(wxColour(6, 176, 37)));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRectangle(1, 1, filled, size.y - 2);
            }
            const wxString text = wxString::Format("%d%%", static_cast<int>(fraction * 100.0 + 0.5));
            dc.SetFont(GetFont().Bold());
            dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
            const wxSize textSize = dc.GetTextExtent(text);
            dc.DrawText(text, (size.x - textSize.x) / 2, (size.y - textSize.y) / 2);
        }

        double fraction = 0.0;
    };

    class BulkGenerationFrame final : public wxFrame {
    public:
        explicit BulkGenerationFrame(Automation automation) :
            wxFrame(nullptr, wxID_ANY, toolTitle, wxDefaultPosition, wxSize(980, 920)),
            automation(std::move(automation)) {
            buildMenuBar();
            buildLayout();
            loadSettings();
            NavKit::LivePipeline::setLogSink([this](const LogLevel level, const std::string& message) {
                // Detail is step-by-step progress; the operations themselves are shown at every level.
                if (level == LogLevel::Detail && logLevel.load() == 0) {
                    return;
                }
                appendLog(level, message);
            });
            Logger::setListener([this](const LogCategory category, const std::string& message) {
                const int level = logLevel.load();
                if (level == 0 || (category == NK_DEBUG && level < 2)) {
                    return;
                }
                appendLog(category == NK_ERROR ? LogLevel::Error
                        : category == NK_WARN  ? LogLevel::Warning
                                               : LogLevel::Detail,
                    (category == NK_DEBUG ? "[NavKit debug] " : "[NavKit] ") + message);
            });
            Bind(wxEVT_CLOSE_WINDOW, &BulkGenerationFrame::onClose, this);
            updateButtons();
            startScan();
        }

        ~BulkGenerationFrame() override {
            Logger::setListener({});
            NavKit::LivePipeline::setLogSink({});
            if (worker.joinable()) {
                worker.join();
            }
        }

    private:
        // ------------------------------------------------------------------------------------------------- layout

        void buildLayout() {
            // A menu bar needs a frame, and a frame needs a panel for tab navigation and the dialog background.
            auto* panel = new wxPanel(this);
            auto* root = new wxBoxSizer(wxVERTICAL);

            // Top: output folders and file types.
            auto* directories = new wxFlexGridSizer(2, 3, 6, 8);
            directories->AddGrowableCol(1, 1);
            const auto addDirectoryRow = [&](const char* label, const char* tooltip, const char* browseTitle,
                                             wxTextCtrl*& box, wxButton*& browse) {
                directories->Add(new wxStaticText(panel, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
                box = new wxTextCtrl(panel, wxID_ANY);
                box->SetToolTip(tooltip);
                directories->Add(box, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
                browse = new wxButton(panel, wxID_ANY, "Browse...");
                directories->Add(browse, 0, wxALIGN_CENTER_VERTICAL);
                wxTextCtrl* boxForEvents = box;
                browse->Bind(wxEVT_BUTTON, [this, boxForEvents, browseTitle](wxCommandEvent&) {
                    browseForDirectory(boxForEvents, browseTitle);
                });
                box->Bind(wxEVT_KILL_FOCUS, [this, boxForEvents](wxFocusEvent& event) {
                    saveSettings();
                    showPathStart(boxForEvents);
                    event.Skip();
                });
                box->Bind(wxEVT_SIZE, [this, boxForEvents](wxSizeEvent& event) {
                    showPathStart(boxForEvents);
                    event.Skip();
                });
            };
            wxButton* gameBrowseButton = nullptr;
            addDirectoryRow("Game files directory",
                "Files are written as <directory>\\<chunk>\\<mission>\\<hash>.<type>. When this is a Simple Mod "
                "Framework mod's content folder, the mod is deployed and checked once the queue is finished.",
                "Choose the game files directory", outputDirectory, gameBrowseButton);
            addDirectoryRow("Source files directory",
                "With Keep .nav.json, .glb and .blend ticked, each mission's files are saved to "
                "<directory>\\<mission>. It cannot be inside the game files directory.",
                "Choose the source files directory", sourceDirectory, sourceBrowseButton);
            root->Add(directories, 0, wxEXPAND | wxALL, 10);

            auto* typesBox = new wxStaticBoxSizer(wxHORIZONTAL, panel, "File types");
            navpCheck = new wxCheckBox(typesBox->GetStaticBox(), wxID_ANY, "NAVP");
            airgCheck = new wxCheckBox(typesBox->GetStaticBox(), wxID_ANY, "AIRG");
            crmdCheck = new wxCheckBox(typesBox->GetStaticBox(), wxID_ANY, "CRMD");
            gfxfCheck = new wxCheckBox(typesBox->GetStaticBox(), wxID_ANY, "GFXF");
            gfxfCheck->Disable();
            gfxfCheck->SetToolTip("GFXF generation is not available yet.");
            for (wxCheckBox* check : {navpCheck, airgCheck, crmdCheck, gfxfCheck}) {
                typesBox->Add(check, 0, wxALL, 6);
            }
            typesBox->AddStretchSpacer();
            keepSceneFilesCheck = new wxCheckBox(typesBox->GetStaticBox(), wxID_ANY, "Keep .nav.json, .glb and .blend");
            keepSceneFilesCheck->SetToolTip("Also saves each mission's scene, geometry and Blender file to the source "
                                            "files directory.");
            typesBox->Add(keepSceneFilesCheck, 0, wxALL, 6);
            root->Add(typesBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

            // Middle: mission selector, buttons, queue.
            auto* middle = new wxBoxSizer(wxHORIZONTAL);
            auto* missionsBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Mission Selector");
            missionList = new wxListBox(missionsBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(-1, 260), 0,
                nullptr, wxLB_EXTENDED | wxLB_NEEDED_SB);
            missionsBox->Add(missionList, 1, wxEXPAND | wxALL, 4);
            middle->Add(missionsBox, 1, wxEXPAND | wxRIGHT, 8);

            auto* buttons = new wxBoxSizer(wxVERTICAL);
            auto* selectAllButton = new wxButton(panel, wxID_ANY, "Select All");
            auto* deselectAllButton = new wxButton(panel, wxID_ANY, "Deselect All");
            auto* addButton = new wxButton(panel, wxID_ANY, "Add to Queue >");
            auto* removeButton = new wxButton(panel, wxID_ANY, "Remove from Queue <");
            selectAllButton->SetToolTip("Selects every mission in the list you last clicked.");
            deselectAllButton->SetToolTip("Clears the selection in the list you last clicked.");
            buttons->AddStretchSpacer();
            for (wxButton* button : {selectAllButton, deselectAllButton, addButton, removeButton}) {
                buttons->Add(button, 0, wxEXPAND | wxBOTTOM, 6);
            }
            buttons->AddStretchSpacer();
            middle->Add(buttons, 0, wxEXPAND | wxRIGHT, 8);

            auto* queueBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Queue");
            queueList = new wxListBox(queueBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(-1, 260), 0,
                nullptr, wxLB_EXTENDED | wxLB_NEEDED_SB);
            queueBox->Add(queueList, 1, wxEXPAND | wxALL, 4);
            middle->Add(queueBox, 1, wxEXPAND);
            root->Add(middle, 1, wxEXPAND | wxALL, 10);

            // Bottom: start and pause, progress, status, log level and log.
            auto* controls = new wxBoxSizer(wxHORIZONTAL);
            const wxColour iconColour = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
            startButton = new wxButton(panel, wxID_ANY, "Start", wxDefaultPosition, wxSize(-1, 48));
            startButton->SetBitmap(makeIcon(true, 28, wxColour(6, 150, 37)));
            startButton->SetFont(startButton->GetFont().Bold().Scaled(1.4f));
            pauseButton = new wxButton(panel, wxID_ANY, "Pause", wxDefaultPosition, wxSize(-1, 48));
            pauseButton->SetBitmap(makeIcon(false, 22, iconColour));
            pauseButton->SetToolTip("Stops after the mission in progress; Start continues the queue.");
            controls->Add(startButton, 3, wxEXPAND | wxRIGHT, 8);
            controls->Add(pauseButton, 1, wxEXPAND);
            root->Add(controls, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

            progressBar = new PercentBar(panel);
            root->Add(progressBar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
            statusText =
                new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxTE_CENTRE);
            root->Add(statusText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

            const wxString levels[] = {"Basic", "Full", "Debug"};
            logLevelChoice = new wxRadioBox(
                panel, wxID_ANY, "Log level", wxDefaultPosition, wxDefaultSize, 3, levels, 1, wxRA_SPECIFY_ROWS);
            logLevelChoice->SetItemToolTip(0, "The operations for each mission and their results.");
            logLevelChoice->SetItemToolTip(1, "Everything NavKit normally logs.");
            logLevelChoice->SetItemToolTip(2, "Everything NavKit logs with debug logging enabled.");
            root->Add(logLevelChoice, 0, wxLEFT | wxRIGHT | wxTOP, 10);
            logText = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 220),
                wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
            logText->SetFont(wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            root->Add(logText, 1, wxEXPAND | wxALL, 10);
            panel->SetSizer(root);
            SetMinSize(wxSize(760, 800));

            selectAllButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { selectAll(true); });
            deselectAllButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { selectAll(false); });
            addButton->Bind(wxEVT_BUTTON, &BulkGenerationFrame::onAddToQueue, this);
            removeButton->Bind(wxEVT_BUTTON, &BulkGenerationFrame::onRemoveFromQueue, this);
            startButton->Bind(wxEVT_BUTTON, &BulkGenerationFrame::onStart, this);
            pauseButton->Bind(wxEVT_BUTTON, &BulkGenerationFrame::onPause, this);
            missionList->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
                lastFocusedList = missionList;
                event.Skip();
            });
            queueList->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
                lastFocusedList = queueList;
                event.Skip();
            });
            missionList->Bind(wxEVT_LISTBOX_DCLICK, &BulkGenerationFrame::onAddToQueue, this);
            queueList->Bind(wxEVT_LISTBOX_DCLICK, &BulkGenerationFrame::onRemoveFromQueue, this);
            logLevelChoice->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent&) {
                logLevel = logLevelChoice->GetSelection();
                saveSettings();
            });
            for (wxCheckBox* check : {navpCheck, airgCheck, crmdCheck, keepSceneFilesCheck}) {
                check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
                    updateSourceDirectoryControls();
                    saveSettings();
                });
            }
        }

        void buildMenuBar() {
            auto* bar = new wxMenuBar();
            auto* file = new wxMenu();
            file->Append(ID_MENU_HELP, "&Help");
            file->Append(ID_MENU_ABOUT, "&About NavKit Bulk Generation Tool");
            file->AppendSeparator();
            file->Append(wxID_EXIT, "E&xit");
            bar->Append(file, "&File");
            auto* settings = new wxMenu();
            settings->Append(ID_MENU_NAVKIT_SETTINGS, "&NavKit Settings");
            bar->Append(settings, "&Settings");
            auto* tools = new wxMenu();
            tools->Append(ID_MENU_OPEN_NAVKIT, "Open &NavKit");
            bar->Append(tools, "&Tools");
            SetMenuBar(bar);

            Bind(wxEVT_MENU, [this](wxCommandEvent&) { showHelp(); }, ID_MENU_HELP);
            Bind(wxEVT_MENU, [this](wxCommandEvent&) { showAbout(); }, ID_MENU_ABOUT);
            Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
            Bind(wxEVT_MENU, [this](wxCommandEvent&) { openNavKitSettings(); }, ID_MENU_NAVKIT_SETTINGS);
            Bind(
                wxEVT_MENU, [this](wxCommandEvent&) { Menu::openSiblingApplication("NavKit.exe", this); },
                ID_MENU_OPEN_NAVKIT);
        }

        void showHelp() {
            wxDialog dialog(this, wxID_ANY, "NavKit Bulk Generation Tool Help", wxDefaultPosition, wxSize(760, 620),
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
            auto* layout = new wxBoxSizer(wxVERTICAL);
            layout->Add(new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(helpText), wxDefaultPosition,
                            wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY),
                1, wxEXPAND | wxALL, 10);
            auto* close = new wxButton(&dialog, wxID_OK, "Close");
            layout->Add(close, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 10);
            dialog.SetSizer(layout);
            dialog.SetEscapeId(wxID_OK);
            close->SetDefault();
            close->SetFocus();
            dialog.CentreOnParent();
            dialog.ShowModal();
        }

        void showAbout() {
            wxMessageBox(wxString::Format("NavKit Bulk Generation Tool\nVersion %s.%s.%s", NavKit_VERSION_MAJOR,
                             NavKit_VERSION_MINOR, NavKit_VERSION_PATCH),
                "About NavKit Bulk Generation Tool", wxOK | wxICON_INFORMATION, this);
        }

        /// Opens NavKit's settings dialog. It shares NavKit's settings object, which a scan or run points at its work
        /// folder, so it is refreshed from NavKit.ini first and the tool waits until the dialog is closed.
        void openNavKitSettings() {
            if (scanning || running) {
                return;
            }
            PersistedSettings& settings = PersistedSettings::getInstance();
            settings.load();
            NavKitSettings& navKit = NavKitSettings::getInstance();
            navKit.outputFolder = settings.getValue("NavKit", "output", navKit.outputFolder);
            navKit.showNavKitSettingsDialog(this);
            wxDialog* dialog = NavKitSettings::hSettingsDialog;
            if (dialog == nullptr) {
                return;
            }
            Disable();
            dialog->Bind(wxEVT_DESTROY, [this, dialog](wxWindowDestroyEvent& event) {
                event.Skip();
                if (event.GetEventObject() == dialog) {
                    CallAfter([this] { onNavKitSettingsClosed(); });
                }
            });
        }

        void onNavKitSettingsClosed() {
            Enable();
            Raise();
            const PersistedSettings& settings = PersistedSettings::getInstance();
            const std::filesystem::path newHitman = std::filesystem::u8path(settings.getValue("NavKit", "hitman", ""));
            const bool hitmanChanged = newHitman != hitmanDirectory;
            hitmanDirectory = newHitman;
            blenderExecutable = std::filesystem::u8path(settings.getValue("NavKit", "blender", ""));
            if (hitmanChanged) {
                std::lock_guard lock(queueMutex);
                queue.clear();
            }
            if (hitmanChanged || missions.empty()) {
                refreshQueueList();
                startScan();
            }
        }

        void updateSourceDirectoryControls() {
            const bool keep = keepSceneFilesCheck->GetValue();
            sourceDirectory->Enable(keep);
            sourceBrowseButton->Enable(keep);
        }

        void setPath(wxTextCtrl* box, const wxString& path) {
            box->ChangeValue(path);
            showPathStart(box);
        }

        /// A native text box keeps the scroll offset it had when it was narrower, hiding the start of the path. This
        /// runs after the pending layout, and leaves the caret alone while the user is editing.
        void showPathStart(wxTextCtrl* box) {
            CallAfter([box] {
                if (!box->HasFocus()) {
                    box->SetInsertionPoint(0);
                }
            });
        }

        // ------------------------------------------------------------------------------------------------- settings

        /// The tool keeps its settings in its own file: NavKit and this tool can be open together, and each rewrites
        /// all of NavKit.ini from its memory when it saves.
        static std::filesystem::path toolSettingsPath() {
            return std::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring()).parent_path() /
                "NavKitBulkGeneration.ini";
        }

        void loadToolSettings(const PersistedSettings& shared) {
            toolSettings.SetUnicode();
            const std::filesystem::path path = toolSettingsPath();
            if (std::filesystem::exists(path)) {
                toolSettings.LoadFile(path.c_str());
                return;
            }
            // Earlier versions kept these in NavKit.ini.
            for (const char* key : {"outputDirectory", "navp", "airg", "crmd", "keepSceneFiles", "logLevel"}) {
                if (const std::string value = shared.getValue(settingsSection, key, ""); !value.empty()) {
                    toolSettings.SetValue(settingsSection, key, value.c_str());
                }
            }
        }

        void loadSettings() {
            PersistedSettings& settings = PersistedSettings::getInstance();
            settings.load();
            loadToolSettings(settings);
            hitmanDirectory = std::filesystem::u8path(settings.getValue("NavKit", "hitman", ""));
            blenderExecutable = std::filesystem::u8path(settings.getValue("NavKit", "blender", ""));
            std::string output = toolSettings.GetValue(settingsSection, "outputDirectory", "");
            if (output.empty() && !hitmanDirectory.empty()) {
                if (const std::filesystem::path demoMod =
                        hitmanDirectory / "Simple Mod Framework" / "Mods" / "NavKitDemoMod" / "content";
                    std::filesystem::is_directory(demoMod)) {
                    output = utf8(demoMod);
                }
            }
            setPath(outputDirectory, wxString::FromUTF8(output));
            std::string source = toolSettings.GetValue(settingsSection, "sourceDirectory", "");
            if (source.empty() && !output.empty()) {
                source = utf8(NavKit::Bulk::defaultSourceDirectory(std::filesystem::u8path(output)));
            }
            setPath(sourceDirectory, wxString::FromUTF8(source));
            const auto flag = [this](const char* key) {
                return std::string(toolSettings.GetValue(settingsSection, key, "true")) == "true";
            };
            navpCheck->SetValue(flag("navp"));
            airgCheck->SetValue(flag("airg"));
            crmdCheck->SetValue(flag("crmd"));
            keepSceneFilesCheck->SetValue(
                std::string(toolSettings.GetValue(settingsSection, "keepSceneFiles", "false")) == "true");
            updateSourceDirectoryControls();
            const int level = std::clamp(std::atoi(toolSettings.GetValue(settingsSection, "logLevel", "0")), 0, 2);
            logLevelChoice->SetSelection(level);
            logLevel = level;
        }

        void saveSettings() {
            const auto setValue = [this](const char* key, const std::string& value) {
                toolSettings.SetValue(settingsSection, key, value.c_str());
            };
            setValue("outputDirectory", outputDirectory->GetValue().utf8_string());
            setValue("sourceDirectory", sourceDirectory->GetValue().utf8_string());
            setValue("navp", navpCheck->GetValue() ? "true" : "false");
            setValue("airg", airgCheck->GetValue() ? "true" : "false");
            setValue("crmd", crmdCheck->GetValue() ? "true" : "false");
            setValue("keepSceneFiles", keepSceneFilesCheck->GetValue() ? "true" : "false");
            setValue("logLevel", std::to_string(logLevelChoice->GetSelection()));
            if (toolSettings.SaveFile(toolSettingsPath().c_str()) < 0) {
                Logger::log(NK_ERROR, "Could not save %s.", toolSettingsPath().string().c_str());
            }
        }

        // ------------------------------------------------------------------------------------------------- UI updates

        /// Adds a timestamped line to the log; may be called from any thread.
        void appendLog(const LogLevel level, const std::string& message) {
            const std::string prefix = level == LogLevel::Error ? "[error] "
                : level == LogLevel::Warning                    ? "[warning] "
                                                                : "";
            const wxString line = wxString::FromUTF8("[" + timestamp() + "] " + prefix + message + "\n");
            CallAfter([this, line, level] {
                const long end = logText->GetLastPosition();
                logText->AppendText(line);
                if (level == LogLevel::Error || level == LogLevel::Warning) {
                    logText->SetStyle(end, logText->GetLastPosition(),
                        wxTextAttr(level == LogLevel::Error ? *wxRED : wxColour(190, 120, 0)));
                }
            });
        }

        /// Logs @p message as an operation, shown at every log level; may be called from any thread.
        void logOperation(const std::string& message) {
            appendLog(LogLevel::Info, message);
        }

        void setStatus(const std::string& message) {
            const wxString text = wxString::FromUTF8(message);
            CallAfter([this, text] { statusText->ChangeValue(text); });
        }

        void setProgress(const double fraction) {
            CallAfter([this, fraction] { progressBar->setFraction(fraction); });
        }

        void updateButtons() {
            const bool busy = scanning || running;
            startButton->Enable(!busy);
            pauseButton->Enable(running && !pauseRequested);
            GetMenuBar()->Enable(ID_MENU_NAVKIT_SETTINGS, !busy);
        }

        void refreshQueueList() {
            std::vector<wxString> names;
            {
                std::lock_guard lock(queueMutex);
                for (const Mission& mission : queue) {
                    names.push_back(wxString::FromUTF8(mission.name));
                }
            }
            queueList->Freeze();
            queueList->Clear();
            for (const wxString& name : names) {
                queueList->Append(name);
            }
            queueList->Thaw();
        }

        // ------------------------------------------------------------------------------------------------- events

        void browseForDirectory(wxTextCtrl* box, const char* title) {
            wxDirDialog dialog(this, title, box->GetValue(), wxDD_DEFAULT_STYLE | wxDD_NEW_DIR_BUTTON);
            if (dialog.ShowModal() == wxID_OK) {
                setPath(box, dialog.GetPath());
                saveSettings();
            }
        }

        void selectAll(const bool select) {
            wxListBox* list = lastFocusedList != nullptr ? lastFocusedList : missionList;
            for (unsigned int index = 0; index < list->GetCount(); ++index) {
                if (select) {
                    list->SetSelection(static_cast<int>(index));
                } else {
                    list->Deselect(static_cast<int>(index));
                }
            }
        }

        void onAddToQueue(wxCommandEvent&) {
            wxArrayInt selections;
            missionList->GetSelections(selections);
            size_t added = 0;
            {
                std::lock_guard lock(queueMutex);
                for (const int index : selections) {
                    const Mission& mission = missions[static_cast<size_t>(index)];
                    if (std::ranges::none_of(
                            queue, [&mission](const Mission& queued) { return queued.scene == mission.scene; })) {
                        queue.push_back(mission);
                        ++added;
                    }
                }
            }
            refreshQueueList();
            if (added > 0) {
                logOperation("Added " + std::to_string(added) + " missions to the queue.");
            }
        }

        void onRemoveFromQueue(wxCommandEvent&) {
            wxArrayInt selections;
            queueList->GetSelections(selections);
            std::set<std::string> removedScenes;
            {
                std::lock_guard lock(queueMutex);
                for (const int index : selections) {
                    if (static_cast<size_t>(index) < queue.size()) {
                        removedScenes.insert(queue[static_cast<size_t>(index)].scene);
                    }
                }
                std::erase_if(
                    queue, [&removedScenes](const Mission& mission) { return removedScenes.contains(mission.scene); });
            }
            refreshQueueList();
        }

        void onStart(wxCommandEvent&) {
            const std::filesystem::path output = std::filesystem::u8path(outputDirectory->GetValue().utf8_string());
            const bool keepSourceFiles = keepSceneFilesCheck->GetValue();
            std::filesystem::path source = std::filesystem::u8path(sourceDirectory->GetValue().utf8_string());
            if (keepSourceFiles && source.empty() && !output.empty()) {
                source = NavKit::Bulk::defaultSourceDirectory(output);
                setPath(sourceDirectory, wxString::FromUTF8(utf8(source)));
            }
            std::string problem;
            if (output.empty()) {
                problem = "Choose a game files directory.";
            } else if (keepSourceFiles && isInsideDirectory(source, output)) {
                problem = "Choose a source files directory outside the game files directory.";
            } else if (!navpCheck->GetValue() && !airgCheck->GetValue() && !crmdCheck->GetValue()) {
                problem = "Select at least one file type.";
            } else if (queueList->GetCount() == 0) {
                problem = "Add missions to the queue.";
            } else if (!std::filesystem::is_directory(hitmanDirectory / "Retail")) {
                problem = "Set the Hitman folder in Settings > NavKit Settings first.";
            } else if (!std::filesystem::is_regular_file(blenderExecutable)) {
                problem = "Set the Blender executable in Settings > NavKit Settings first.";
            }
            if (!problem.empty()) {
                wxMessageBox(problem, toolTitle, wxOK | wxICON_INFORMATION, this);
                return;
            }
            std::error_code error;
            std::filesystem::create_directories(output, error);
            if (!error && keepSourceFiles) {
                std::filesystem::create_directories(source, error);
            }
            if (error) {
                wxMessageBox("Could not create the output directories: " + error.message(), toolTitle,
                    wxOK | wxICON_ERROR, this);
                return;
            }
            saveSettings();

            options.hitmanDirectory = hitmanDirectory;
            options.blenderExecutable = blenderExecutable;
            options.contentDirectory = output;
            options.types = {navpCheck->GetValue(), airgCheck->GetValue(), crmdCheck->GetValue()};
            options.keepSceneFiles = keepSourceFiles;
            options.sourceDirectory = source;
            if (workRoot.empty()) {
                const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
                workRoot = std::filesystem::path(wxStandardPaths::Get().GetTempDir().ToStdWstring()) /
                    "NavKitBulkGeneration" / ("run-" + std::to_string(stamp));
            }
            if (worker.joinable()) {
                worker.join();
            }
            running = true;
            pauseRequested = false;
            updateButtons();
            worker = std::thread([this] { runQueue(); });
        }

        void onPause(wxCommandEvent&) {
            pauseRequested = true;
            updateButtons();
            logOperation("Pausing after the current mission.");
            setStatus(currentMissionName.empty() ? "Pausing..." : "Pausing after " + currentMissionName + "...");
        }

        void onClose(wxCloseEvent& event) {
            if ((running || scanning) && event.CanVeto()) {
                wxMessageBox(scanning ? "Wait for the mission scan to finish before closing."
                                      : "Press Pause and wait for the current mission to finish before closing.",
                    toolTitle, wxOK | wxICON_INFORMATION, this);
                event.Veto();
                return;
            }
            saveSettings();
            Destroy();
        }

        // ------------------------------------------------------------------------------------------------- scan

        void startScan() {
            if (worker.joinable()) {
                worker.join();
            }
            if (!std::filesystem::is_directory(hitmanDirectory / "Retail")) {
                setStatus("Set the Hitman folder in Settings > NavKit Settings.");
                logOperation("No Hitman folder is set in NavKit's settings.");
                return;
            }
            scanning = true;
            updateButtons();
            setStatus("Scanning the RPKG files for missions...");
            logOperation("Scanning the RPKG files in " + utf8(hitmanDirectory) + " for missions.");
            worker = std::thread([this] {
                std::vector<NavKit::Bulk::DiscoveredMission> found;
                std::string error;
                try {
                    // Loading the settings starts NavKit's own RPKG scan, which must not overlap this one.
                    Rpkg::backgroundWorker.reset();
                    const std::filesystem::path scanDirectory =
                        std::filesystem::path(wxStandardPaths::Get().GetTempDir().ToStdWstring()) /
                        "NavKitBulkGeneration";
                    NavKit::LivePipeline::initRpkg(hitmanDirectory, blenderExecutable, scanDirectory);
                    found = NavKit::Bulk::discoverMissions(nullptr, [this](const size_t done, const size_t total) {
                        setProgress(total == 0 ? 0.0 : static_cast<double>(done) / static_cast<double>(total));
                    });
                } catch (const std::exception& exception) {
                    error = exception.what();
                }
                CallAfter([this, found = std::move(found), error] {
                    scanning = false;
                    if (worker.joinable()) {
                        worker.join();
                    }
                    missions.clear();
                    listedScenes.clear();
                    for (const auto& discovered : found) {
                        missions.push_back(discovered.mission);
                        listedScenes.insert(NavKit::Bulk::toLower(discovered.mission.scene));
                    }
                    std::ranges::sort(missions, [](const Mission& left, const Mission& right) {
                        const std::string leftName = NavKit::Bulk::toLower(left.name);
                        const std::string rightName = NavKit::Bulk::toLower(right.name);
                        return leftName != rightName ? leftName < rightName : left.scene < right.scene;
                    });
                    missionList->Freeze();
                    missionList->Clear();
                    for (const Mission& mission : missions) {
                        missionList->Append(wxString::FromUTF8(mission.name));
                    }
                    missionList->Thaw();
                    progressBar->setFraction(0.0);
                    if (error.empty()) {
                        statusText->ChangeValue(wxString::Format("Found %zu missions. Add missions to the queue, then "
                                                                 "press Start.",
                            missions.size()));
                        logOperation("Found " + std::to_string(missions.size()) + " missions with a NAVP or AIRG.");
                    } else {
                        statusText->ChangeValue("The mission scan failed.");
                        appendLog(LogLevel::Error, "The mission scan failed: " + error);
                    }
                    updateButtons();
                    if (error.empty()) {
                        applyAutomation();
                    }
                });
            });
        }

        /// Fills in the options given on the command line once the missions are known.
        void applyAutomation() {
            if (automation.outputDirectory) {
                setPath(outputDirectory, wxString::FromUTF8(*automation.outputDirectory));
            }
            if (automation.sourceDirectory) {
                setPath(sourceDirectory, wxString::FromUTF8(*automation.sourceDirectory));
            }
            if (automation.keepSceneFiles) {
                keepSceneFilesCheck->SetValue(true);
                updateSourceDirectoryControls();
            }
            for (const std::string& wanted : automation.queue) {
                const std::string filter = NavKit::Bulk::toLower(wanted);
                bool found = false;
                for (size_t index = 0; index < missions.size(); ++index) {
                    const Mission& mission = missions[index];
                    if (NavKit::Bulk::toLower(mission.name) == filter ||
                        NavKit::Bulk::toLower(mission.scene).find(filter) != std::string::npos) {
                        missionList->SetSelection(static_cast<int>(index));
                        found = true;
                    }
                }
                if (!found) {
                    appendLog(LogLevel::Warning, "No mission matches --queue " + wanted + ".");
                }
            }
            if (!automation.queue.empty()) {
                wxCommandEvent event;
                onAddToQueue(event);
            }
            if (automation.start) {
                wxCommandEvent event;
                onStart(event);
            }
        }

        // ------------------------------------------------------------------------------------------------- queue

        void runQueue() {
            size_t completedThisRun = 0;
            const auto overallFraction = [this, &completedThisRun](const double missionFraction) {
                size_t remaining = 0;
                {
                    std::lock_guard lock(queueMutex);
                    remaining = queue.size();
                }
                const double total = static_cast<double>(completedThisRun + 1 + remaining);
                return (static_cast<double>(completedThisRun) + missionFraction) / total;
            };

            try {
                NavKit::LivePipeline::initRpkg(options.hitmanDirectory, options.blenderExecutable, workRoot);
            } catch (const std::exception& error) {
                appendLog(LogLevel::Error, std::string("Could not scan the RPKG files: ") + error.what());
                finishRun("Could not scan the RPKG files.");
                return;
            }

            while (!pauseRequested) {
                std::optional<Mission> next;
                {
                    std::lock_guard lock(queueMutex);
                    if (!queue.empty()) {
                        next = queue.front();
                        queue.erase(queue.begin());
                    }
                }
                if (!next) {
                    break;
                }
                CallAfter([this] { refreshQueueList(); });
                const Mission mission = *next;
                currentMissionName = mission.name;
                logOperation("Starting " + mission.name + " (" + mission.scene + ").");
                setProgress(overallFraction(0.0));

                MissionResult result;
                const std::filesystem::path workDirectory = workRoot / NavKit::Bulk::slugify(mission.name);
                try {
                    NavKit::Bulk::clearMissionFolders(options.contentDirectory, mission);
                    NavKit::Bulk::generateMission(options, mission, listedScenes, workDirectory, result,
                        [&](const Phase phase, const std::string& description) {
                            logOperation(mission.name + ": " + description + ".");
                            setStatus(mission.name + ": " + description + "...");
                            setProgress(overallFraction(phaseStart(phase)));
                        });
                    std::map<std::string, size_t> counts;
                    for (const auto& file : result.files) {
                        ++counts[file.type];
                    }
                    logOperation("Finished " + mission.name + ": " + std::to_string(counts["NAVP"]) + " NAVP, " +
                        std::to_string(counts["AIRG"]) + " AIRG, " + std::to_string(counts["CRMD"]) + " CRMD.");
                    for (const std::string& warning : result.warnings) {
                        appendLog(LogLevel::Warning, mission.name + ": " + warning);
                    }
                    std::error_code ignored;
                    std::filesystem::remove_all(workDirectory, ignored);
                } catch (const std::exception& error) {
                    result.error = error.what();
                    appendLog(LogLevel::Error,
                        mission.name + " failed: " + result.error + " Its work files are in " + utf8(workDirectory) +
                            ".");
                    try {
                        NavKit::Bulk::ensureHitmanClosed(options.hitmanDirectory);
                    } catch (const std::exception& closeError) {
                        appendLog(LogLevel::Error, std::string("Could not close HITMAN: ") + closeError.what());
                    }
                    NavKit::Bulk::clearMissionFolders(options.contentDirectory, mission);
                    result.files.clear();
                }
                pendingResults.push_back(std::move(result));
                ++completedThisRun;
                setProgress(overallFraction(0.0));
            }
            currentMissionName.clear();

            if (pauseRequested) {
                size_t remaining = 0;
                {
                    std::lock_guard lock(queueMutex);
                    remaining = queue.size();
                }
                logOperation("Paused with " + std::to_string(remaining) + " missions in the queue.");
                finishRun("Paused. " + std::to_string(remaining) + " missions are left in the queue.");
                return;
            }
            deployAndVerify();
        }

        void deployAndVerify() {
            const size_t generated = static_cast<size_t>(std::ranges::count_if(
                pendingResults, [](const MissionResult& result) { return result.error.empty(); }));
            const size_t failed = pendingResults.size() - generated;
            const std::optional<std::filesystem::path> smfDirectory =
                NavKit::Bulk::smfDirectoryForContent(options.contentDirectory);
            if (generated > 0 && smfDirectory) {
                try {
                    setStatus("Deploying the mod with Simple Mod Framework...");
                    logOperation("Deploying the mod with Simple Mod Framework.");
                    NavKit::Bulk::deployWithSmf(options.hitmanDirectory, *smfDirectory, workRoot / "deploy.log");
                    setStatus("Verifying the deployed files...");
                    logOperation("Verifying the deployed files.");
                    NavKit::Bulk::verifyDeployedFiles(options, pendingResults, workRoot);
                } catch (const std::exception& error) {
                    appendLog(LogLevel::Error, std::string("Deploying or verifying failed: ") + error.what());
                }
            } else if (generated > 0) {
                logOperation("The game files directory is not a Simple Mod Framework mod's content folder, so nothing "
                             "was deployed.");
            }
            size_t verifiedFailures = 0;
            for (const MissionResult& result : pendingResults) {
                if (result.error.empty()) {
                    continue;
                }
                ++verifiedFailures;
                appendLog(LogLevel::Error, result.mission.name + ": " + result.error);
            }
            const std::string summary = "Done: " + std::to_string(pendingResults.size() - verifiedFailures) + " of " +
                std::to_string(pendingResults.size()) + " missions generated" +
                (smfDirectory && generated > 0 ? " and verified." : ".");
            logOperation(summary);
            (void)failed;
            pendingResults.clear();
            workRoot.clear();
            setProgress(1.0);
            finishRun(summary);
        }

        void finishRun(const std::string& status) {
            setStatus(status);
            CallAfter([this] {
                const bool paused = pauseRequested;
                running = false;
                pauseRequested = false;
                updateButtons();
                if (automation.closeWhenDone && !paused) {
                    Close(true);
                }
            });
        }

        // ------------------------------------------------------------------------------------------------- state

        Automation automation;
        wxTextCtrl* outputDirectory = nullptr;
        wxTextCtrl* sourceDirectory = nullptr;
        wxButton* sourceBrowseButton = nullptr;
        CSimpleIniA toolSettings;
        wxCheckBox* navpCheck = nullptr;
        wxCheckBox* airgCheck = nullptr;
        wxCheckBox* crmdCheck = nullptr;
        wxCheckBox* gfxfCheck = nullptr;
        wxCheckBox* keepSceneFilesCheck = nullptr;
        wxListBox* missionList = nullptr;
        wxListBox* queueList = nullptr;
        wxListBox* lastFocusedList = nullptr;
        wxButton* startButton = nullptr;
        wxButton* pauseButton = nullptr;
        PercentBar* progressBar = nullptr;
        wxTextCtrl* statusText = nullptr;
        wxRadioBox* logLevelChoice = nullptr;
        wxTextCtrl* logText = nullptr;

        std::filesystem::path hitmanDirectory;
        std::filesystem::path blenderExecutable;
        std::vector<Mission> missions;
        std::set<std::string> listedScenes;
        std::mutex queueMutex;
        std::vector<Mission> queue;

        NavKit::Bulk::GenerationOptions options;
        std::filesystem::path workRoot;
        std::vector<MissionResult> pendingResults;
        std::string currentMissionName;
        std::thread worker;
        std::atomic<bool> scanning = false;
        std::atomic<bool> running = false;
        std::atomic<bool> pauseRequested = false;
        std::atomic<int> logLevel = 0;
    };

    class BulkGenerationApp final : public wxApp {
    public:
        // wxApp::OnInit would reject this tool's own command-line options, which are parsed below instead.
        bool OnInit() override {
            Logger::getInstance().start();
            Automation automation;
            for (int index = 1; index < argc; ++index) {
                const std::string argument = argv[index].utf8_string();
                const auto value = [&]() -> std::string {
                    return index + 1 < argc ? argv[++index].utf8_string() : std::string{};
                };
                if (argument == "--output") {
                    automation.outputDirectory = value();
                } else if (argument == "--source-output") {
                    automation.sourceDirectory = value();
                } else if (argument == "--queue") {
                    automation.queue.push_back(value());
                } else if (argument == "--start") {
                    automation.start = true;
                } else if (argument == "--close-when-done") {
                    automation.closeWhenDone = true;
                } else if (argument == "--keep-scene-files") {
                    automation.keepSceneFiles = true;
                }
            }
            auto* frame = new BulkGenerationFrame(std::move(automation));
            frame->Show();
            SetTopWindow(frame);
            return true;
        }

        int OnExit() override {
            Logger::getInstance().stop();
            return wxApp::OnExit();
        }
    };
} // namespace

// NavKitLib registers NavKit's own wxApp, so this tool installs its app before wxEntry instead of using
// wxIMPLEMENT_APP.
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    wxApp::SetInstance(new BulkGenerationApp());
    return wxEntry(instance, nullptr, nullptr, show);
}
#else
int main(int argc, char** argv) {
    wxApp::SetInstance(new BulkGenerationApp());
    return wxEntry(argc, argv);
}
#endif
