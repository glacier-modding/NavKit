#include "../../include/NavKit/module/HelpDialog.h"

#include <array>
#include <string>
#include <vector>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

#include "../../include/NavKit/module/WxApplication.h"

namespace {
    struct HelpEntry {
        const char* name;
        const char* description;
        const char* details;
        const char* requirement;
    };

    struct HelpPage {
        const char* title;
        const char* introduction;
        std::vector<HelpEntry> entries;
    };

    std::string makePageText(const HelpPage& page) {
        std::string text = page.introduction;
        for (const HelpEntry& entry : page.entries) {
            text += "\n\n";
            text += entry.name;
            text += " - ";
            text += entry.description;
            text += "\n  ";
            text += entry.details;
            if (entry.requirement[0] != '\0') {
                text += "\n  Requirement: ";
                text += entry.requirement;
            }
        }
        return text;
    }
} // namespace

void showHelpDialog() {
    const std::array<HelpPage, 8> pages = {{
        {"General",
            "NavKit helps you inspect and create navigation data for HITMAN: World of Assassination and 007 First "
            "Light.\nLoad a scene, NAVP, or AIRG file to explore it in the viewport, edit navigation data, or build "
            "new files for your project.\nUse the tabs here to learn what each menu command does and what you need "
            "before using it.",
            {}},
        {"File", "Load source data, save your work, or exit NavKit.",
            {
                {"Open Scene", "Loads a NavKit scene file.",
                    "Provides scene geometry for inspection and as input to builds.", ""},
                {"Open GLB", "Loads a glTF binary scene mesh.",
                    "Provides mesh geometry to inspect or use as input for a Navp build.",
                    "Another GLB load must not be in progress."},
                {"Open Navp", "Loads a NavPower navigation mesh.",
                    "Provides an existing navigation mesh to inspect or edit.", ""},
                {"Open Navp from Rpkg", "Opens a picker to extract and load a Navp from game resource packages.",
                    "Loads a Navp from game resource packages without a separate manual extraction step.",
                    "Set the HITMAN game directory in NavKit Settings and use a recognized game version so the "
                    "package index is ready."},
                {"Open Airg", "Loads an AIRG file.", "Provides existing AIRG data to inspect or edit.",
                    "An AIRG load or save must not be in progress."},
                {"Open Airg from Rpkg", "Opens a picker to extract and load an AIRG from game resource packages.",
                    "Loads game-packaged AIRG data directly, without a separate extraction step.",
                    "Set the HITMAN game directory in NavKit Settings and use a recognized game version so the "
                    "package index is ready."},
                {"Save Scene", "Saves the currently loaded scene.", "Preserves changes made to the scene.",
                    "Load a scene."},
                {"Save GLB", "Saves the loaded scene mesh as a GLB file with embedded materials and textures.",
                    "Exports mesh geometry for use in other tools or as input to a Navp build.", "Load a scene mesh."},
                {"Save Blend", "Saves the generated Blender scene.",
                    "Preserves a completed Blender export for later use.", "Complete a Blender scene build."},
                {"Save Navp", "Saves the loaded Navp navigation mesh.",
                    "Preserves Navp edits or creates a file for use in game.", "Load a Navp."},
                {"Save Airg", "Saves the loaded AIRG data.", "Preserves changes made to AIRG data.",
                    "Load an AIRG and make sure no AIRG save is in progress."},
                {"Exit", "Closes NavKit.", "Ends the current NavKit session.", ""},
            }},
        {"Edit", "These commands change navigation data or control AIRG waypoint editing modes.",
            {
                {"Stairs Area", "Toggles the stairs flag on the selected Navp area.",
                    "Marks the selected area as stairs, or clears its stairs flag.",
                    "Load a Navp and select an area in the viewport."},
                {"Connect Airg Waypoint Mode", "Enters mode for connecting the selected AIRG waypoint.",
                    "Enables adding a connection from the selected waypoint to another waypoint.",
                    "Load an AIRG and select a waypoint in the viewport."},
                {"Disconnect Airg Waypoint Mode",
                    "Enters mode for disconnecting a connection from the selected "
                    "AIRG waypoint.",
                    "Enables removing a connection from the selected waypoint.",
                    "Load an AIRG and select a waypoint in the viewport."},
            }},
        {"Settings",
            "Use these dialogs to set up NavKit and tune how it handles scene meshes, navigation, and AIRG data.",
            {
                {"NavKit Settings",
                    "Sets the HITMAN installation, output folder, Blender executable, background "
                    "color, and debug logging preference.",
                    "Sets up game extraction and controls where generated files and Blender exports are stored.", ""},
                {"Scene Settings", "Adjusts the scene bounding box position and scale.",
                    "Sets scene bounds used to frame or filter scene geometry.", ""},
                {"Recast Settings", "Configures Recast navigation mesh generation parameters.",
                    "Controls navigation mesh detail and agent or region behavior during Navp generation.", ""},
                {"Scene Mesh Settings",
                    "Configures mesh type, level of detail, extraction, filtering, and Blender "
                    "build options.",
                    "Selects which game meshes are included and how the exported scene mesh is built.", ""},
                {"Airg Settings", "Configures AIRG grid spacing and offsets.",
                    "Controls the grid used to build AIRG data from a Navp.", ""},
            }},
        {"View",
            "Toggle these options to focus on the scene details you need. Data-specific options are most useful "
            "when the corresponding scene, Navp, AIRG, or GLB is loaded.",
            {
                {"Show Bounding Box", "Shows or hides the scene bounding box.",
                    "Makes it easier to check scene bounds in the viewport.", ""},
                {"Show Axes", "Shows or hides the scene axes.", "Provides an orientation reference in the 3D viewport.",
                    ""},
                {"Show Navp", "Shows or hides the loaded Navp mesh.",
                    "Makes it easier to compare the navigation mesh with the scene.", ""},
                {"Show Navp Indices", "Shows or hides Navp area indices.",
                    "Displays area indices to identify areas during inspection and editing.", ""},
                {"Show KD Tree", "Shows or hides the selected Navp area's KD tree.",
                    "Reveals the selected area's spatial subdivisions.", ""},
                {"Show Exclusion Boxes", "Shows or hides Navp pathfinding exclusion boxes.",
                    "Displays the pathfinding regions excluded by the Navp.", ""},
                {"Show Seed Points", "Shows or hides Navp pathfinding seed points.",
                    "Displays the seed point locations used by pathfinding.", ""},
                {"Z Render Offset", "Offsets Navp rendering in the Z direction.",
                    "Separates overlapping geometry in the viewport.", ""},
                {"Show Recast Debug Info (Navp)", "Shows or hides Recast debug visualization for Navp data.",
                    "Displays intermediate geometry used by Recast for the Navp.", ""},
                {"Show Airg", "Shows or hides loaded AIRG data.", "Displays AIRG cells and waypoints for inspection.",
                    ""},
                {"Show Airg Indices", "Shows or hides AIRG cell and waypoint indices.",
                    "Displays indices to identify AIRG elements during inspection and editing.", ""},
                {"Show Grid", "Shows or hides the generated grid overlay.",
                    "Makes it easier to compare AIRG grid placement with the scene.", ""},
                {"Show Recast Debug Info (Airg)", "Shows or hides Recast debug visualization for AIRG data.",
                    "Displays intermediate geometry used by Recast for AIRG data.", ""},
                {"Airg Cell: None", "Disables AIRG cell coloring.", "Displays cells without a data-coloring overlay.",
                    ""},
                {"Airg Cell: Area Bitmap", "Colors AIRG cells using area bitmap data.",
                    "Reveals area classifications across the grid.", ""},
                {"Airg Cell: Vision Data", "Colors AIRG cells using vision data.",
                    "Visualizes vision information stored for the grid.", ""},
                {"Airg Cell: Layer", "Colors AIRG cells by layer.", "Reveals how cells are distributed across layers.",
                    ""},
                {"Show GLB", "Shows or hides the loaded GLB scene mesh.",
                    "Makes it easier to compare scene mesh geometry with navigation data.", ""},
                {"Show Log", "Shows or hides the application log panel.",
                    "Displays operation progress, warnings, and errors.", ""},
            }},
        {"Extract", "Extract a scene from the running game, optionally continuing into a mesh build.",
            {
                {"Extract Scene from game", "Connects to the game, extracts scene data, and loads it into NavKit.",
                    "Provides scene data from the current game level for navigation or mesh workflows.",
                    "Use a recognized game version, set the game installation in NavKit Settings, and make sure no "
                    "other scene extraction is in progress."},
                {"Extract Scene from game and build GLB", "Extracts and loads the scene, then builds a GLB scene mesh.",
                    "Continues scene extraction by generating a GLB scene mesh.",
                    "Meet the scene extraction requirements, set Blender in NavKit Settings, and make sure no mesh "
                    "build is running or already complete."},
                {"Extract Scene, build navp and airg",
                    "Extracts the scene and starts the combined scene-mesh, Navp, "
                    "and AIRG build workflow.",
                    "Runs scene extraction through scene-mesh, Navp, and AIRG generation as one workflow.",
                    "Meet the scene extraction requirements, set Blender in NavKit Settings, and make sure no mesh "
                    "build is running or already complete."},
            }},
        {"Build", "Generate interchange meshes and navigation data from loaded scene, GLB, and Navp inputs.",
            {
                {"Build GLB from Scene", "Extracts game meshes and builds a GLB scene mesh.",
                    "Creates mesh geometry from a loaded scene for inspection or Navp generation.",
                    "Load a scene, set the HITMAN folder, output folder, and Blender in NavKit Settings, and make "
                    "sure game resource extraction is ready and no mesh build is running or already complete."},
                {"Build GLB from Navp", "Builds a GLB representation from a Navp.",
                    "Exports navigation geometry for inspection or use in another tool.",
                    "Load a Navp and set the output folder in NavKit Settings."},
                {"Build GLB and Blend from Scene", "Builds both GLB and Blender outputs from the loaded scene.",
                    "Creates both export formats in a single operation.",
                    "Meet the same scene, game resource, and Blender setup requirements as Build GLB from Scene."},
                {"Build Blend from Scene", "Builds a Blender scene from the loaded scene.",
                    "Exports scene geometry and supported scene content to Blender.",
                    "Meet the same scene, game resource, and Blender setup requirements as Build GLB from Scene."},
                {"Build Navp from GLB and Scene", "Builds a Navp using scene geometry and a loaded GLB.",
                    "Creates a navigation mesh from the loaded scene geometry and GLB.",
                    "Load a scene and GLB, generate Recast input geometry (for example, by building a GLB from the "
                    "scene), and make sure no Navp or AIRG build is in progress."},
                {"Build Airg from Navp", "Builds AIRG grid and waypoint data from a Navp.",
                    "Generates AIRG grid and waypoint data from the loaded Navp.",
                    "Load a Navp and make sure no AIRG load, save, or build is in progress."},
            }},
        {"Help", "Find help for NavKit or check the installed version.",
            {
                {"Help Contents", "Opens this tabbed guide.",
                    "Explains menu commands and the data or setup they require.", ""},
                {"About", "Displays the NavKit version.", "Identifies the running NavKit version.", ""},
            }},
    }};

    wxDialog dialog(getMainFrame(), wxID_ANY, "NavKit Help", wxDefaultPosition, wxSize(900, 650));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* notebook = new wxNotebook(&dialog, wxID_ANY);
    for (const HelpPage& helpPage : pages) {
        auto* page = new wxPanel(notebook);
        auto* pageLayout = new wxBoxSizer(wxVERTICAL);
        auto* content = new wxTextCtrl(page, wxID_ANY, wxString::FromUTF8(makePageText(helpPage)), wxDefaultPosition,
            wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
        pageLayout->Add(content, 1, wxEXPAND | wxALL, 8);
        page->SetSizer(pageLayout);
        notebook->AddPage(page, helpPage.title);
    }
    layout->Add(notebook, 1, wxEXPAND | wxALL, 8);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* close = new wxButton(&dialog, wxID_OK, "Close");
    buttons->Add(close, 0, wxALL, 8);
    layout->Add(buttons, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 4);
    dialog.SetSizer(layout);
    dialog.SetAffirmativeId(wxID_OK);
    dialog.SetEscapeId(wxID_OK);
    close->SetDefault();
    dialog.CentreOnParent();
    dialog.ShowModal();
}
