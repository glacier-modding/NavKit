#include "../../include/NavKit/adapter/RecastAdapter.h"

#include <DetourNavMeshQuery.h>
#include <RecastDebugDraw.h>

#include "../../include/NavKit/UiIds.h"
#include "../../include/NavKit/model/ReasoningGrid.h"
#include "../../include/NavKit/model/Json.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/util/Math.h"
#include "../../include/NavKit/module/WxApplication.h"
#include "../../include/RecastDemo/InputGeom.h"
#include <wx/button.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <iomanip>
#include <queue>
#include <array>
#include <algorithm>
#include <SDL_keyboard.h>
#include <sstream>

#include "../../include/NavKit/module/PersistedSettings.h"
#include <glm/gtc/matrix_transform.hpp>

RecastAdapter::RecastAdapter() {
    buildContext = new BuildContext();
    sample = new Sample_TileMesh();
    sample->setContext(buildContext);
    debugDraw = new DebugDrawGL();
    inputGeom = new InputGeom();
    filter = new dtQueryFilter();
    filter->setIncludeFlags(SAMPLE_POLYFLAGS_ALL ^ SAMPLE_POLYFLAGS_DISABLED);
    filter->setExcludeFlags(0);
    filter->setAreaCost(SAMPLE_POLYAREA_GROUND, 1.0f);
    filter->setAreaCost(SAMPLE_POLYAREA_WATER, 10000.0f);
    filter->setAreaCost(SAMPLE_POLYAREA_ROAD, 1.0f);
    filter->setAreaCost(SAMPLE_POLYAREA_DOOR, 1.0f);
    filter->setAreaCost(SAMPLE_POLYAREA_GRASS, 2.0f);
    filter->setAreaCost(SAMPLE_POLYAREA_JUMP, 1.5f);
    filterWithExcluded = new dtQueryFilter();
    filterWithExcluded->setIncludeFlags(SAMPLE_POLYFLAGS_ALL);
    filterWithExcluded->setExcludeFlags(0);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_GROUND, 1.0f);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_WATER, 10000.0f);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_ROAD, 1.0f);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_DOOR, 1.0f);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_GRASS, 2.0f);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_JUMP, 1.5f);
    markerPositionSet = false;
    markerPosition[0] = 0;
    markerPosition[1] = 0;
    markerPosition[2] = 0;
}

wxDialog* RecastAdapter::hRecastDialog = nullptr;

static std::string formatRecastFloat(const float val, const int precision) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(precision) << val;
    return ss.str();
}

// From Recast
inline unsigned int ilog2(unsigned int v) {
    unsigned int r = (v > 0xffff) << 4;
    v >>= r;
    unsigned int shift = (v > 0xff) << 3;
    v >>= shift;
    r |= shift;
    shift = (v > 0xf) << 2;
    v >>= shift;
    r |= shift;
    shift = (v > 0x3) << 1;
    v >>= shift;
    r |= shift;
    r |= (v >> 1);
    return r;
}

// From Recast
inline unsigned int nextPow2(unsigned int v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

static void updateRecastDialogControls(wxDialog* dialog) {
    const RecastAdapter& adapter = RecastAdapter::getInstance();
    Sample_TileMesh* sample = adapter.sample;
    if (!sample) {
        return;
    }

    auto set_slider = [&](const int sliderId, const int textId, const float value, const float min_val,
                          const float step, const int num_steps, const int precision) {
        const int pos = static_cast<int>((value - min_val) / step);
        auto* slider = static_cast<wxSlider*>(dialog->FindWindow(sliderId));
        slider->SetRange(0, num_steps);
        slider->SetValue(std::clamp(pos, 0, num_steps));
        dialog->FindWindow(textId)->SetLabel(formatRecastFloat(value, precision));
    };

    set_slider(IDC_SLIDER_CELL_SIZE, IDC_STATIC_CELL_SIZE_VAL, sample->m_cellSize, 0.01f, 0.01f, 39, 2);
    // Range: 0.01 to 0.40
    set_slider(IDC_SLIDER_CELL_HEIGHT, IDC_STATIC_CELL_HEIGHT_VAL, sample->m_cellHeight, 0.01f, 0.01f, 39, 2);
    // Range: 0.01 to 0.40

    set_slider(IDC_SLIDER_AGENT_HEIGHT, IDC_STATIC_AGENT_HEIGHT_VAL, sample->m_agentHeight, 0.1f, 0.01f, 490, 2);
    // Range: 0.1 to 5.0
    set_slider(IDC_SLIDER_AGENT_RADIUS, IDC_STATIC_AGENT_RADIUS_VAL, sample->m_agentRadius, 0.0f, 0.01f, 200, 2);
    // Range: 0.0 to 2.0
    set_slider(IDC_SLIDER_AGENT_MAX_CLIMB, IDC_STATIC_AGENT_MAX_CLIMB_VAL, sample->m_agentMaxClimb, 0.0f, 0.01f, 500,
        2); // Range: 0.0 to 5.0
    set_slider(IDC_SLIDER_AGENT_MAX_SLOPE, IDC_STATIC_AGENT_MAX_SLOPE_VAL, sample->m_agentMaxSlope, 0.0f, 1.0f, 90,
        0); // Range: 0 to 90

    set_slider(IDC_SLIDER_REGION_MIN_SIZE, IDC_STATIC_REGION_MIN_SIZE_VAL, sample->m_regionMinSize, 0.0f, 1.0f, 150,
        0); // Range: 0 to 150
    set_slider(IDC_SLIDER_REGION_MERGE_SIZE, IDC_STATIC_REGION_MERGE_SIZE_VAL, sample->m_regionMergeSize, 0.0f, 1.0f,
        150, 0); // Range: 0 to 150

    set_slider(IDC_SLIDER_POLY_MAX_EDGE_LEN, IDC_STATIC_POLY_MAX_EDGE_LEN_VAL, sample->m_edgeMaxLen, 0.0f, 1.0f, 50,
        1); // Range: 0.0 to 50.0
    set_slider(IDC_SLIDER_POLY_MAX_EDGE_ERR, IDC_STATIC_POLY_MAX_EDGE_ERR_VAL, sample->m_edgeMaxError, 0.1f, 0.1f, 29,
        2); // Range: 0.1 to 3.0
    set_slider(IDC_SLIDER_POLY_VERTS_PER_POLY, IDC_STATIC_POLY_VERTS_PER_POLY_VAL, sample->m_vertsPerPoly, 3.0f, 1.0f,
        9, 0); // Range: 3 to 12

    set_slider(IDC_SLIDER_DETAIL_SAMPLE_DIST, IDC_STATIC_DETAIL_SAMPLE_DIST_VAL, sample->m_detailSampleDist, 0.0f, 1.0f,
        16, 1); // Range: 0.0 to 16.0
    set_slider(IDC_SLIDER_DETAIL_SAMPLE_MAX_ERR, IDC_STATIC_DETAIL_SAMPLE_MAX_ERR_VAL, sample->m_detailSampleMaxError,
        0.0f, 1.0f, 16, 1); // Range: 0.0 to 16.0

    set_slider(IDC_SLIDER_TILING_TILE_SIZE, IDC_STATIC_TILING_TILE_SIZE_VAL, sample->m_tileSize, 16.0f, 16.0f, 15,
        0); // Range: 16 to 256

    static_cast<wxRadioButton*>(dialog->FindWindow(IDC_RADIO_PARTITION_WATERSHED))
        ->SetValue(sample->m_partitionType == SAMPLE_PARTITION_WATERSHED);
    static_cast<wxRadioButton*>(dialog->FindWindow(IDC_RADIO_PARTITION_MONOTONE))
        ->SetValue(sample->m_partitionType == SAMPLE_PARTITION_MONOTONE);
    static_cast<wxRadioButton*>(dialog->FindWindow(IDC_RADIO_PARTITION_LAYERS))
        ->SetValue(sample->m_partitionType == SAMPLE_PARTITION_LAYERS);
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_FILTER_LOW_HANGING))
        ->SetValue(sample->m_filterLowHangingObstacles);
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_FILTER_LEDGE_SPANS))->SetValue(sample->m_filterLedgeSpans);
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_FILTER_WALKABLE_LOW))
        ->SetValue(sample->m_filterWalkableLowHeightSpans);

    if (sample->m_geom) {
        int gw = 0, gh = 0;
        const float* bmin = sample->m_geom->getNavMeshBoundsMin();
        const float* bmax = sample->m_geom->getNavMeshBoundsMax();
        rcCalcGridSize(bmin, bmax, sample->m_cellSize, &gw, &gh);
        const int ts = (int)sample->m_tileSize;
        const int tw = (gw + ts - 1) / ts;
        const int th = (gh + ts - 1) / ts;

        int tileBits = rcMin((int)ilog2(nextPow2(tw * th)), 14);
        if (tileBits > 14) {
            tileBits = 14;
        }
        const int polyBits = 22 - tileBits;
        sample->m_maxTiles = 1 << tileBits;
        sample->m_maxPolysPerTile = 1 << polyBits;
        dialog->FindWindow(IDC_STATIC_TILING_INFO_TILES)->SetLabel(wxString::Format("Tiles: %d x %d", tw, th));
        dialog->FindWindow(IDC_STATIC_TILING_INFO_MAX_TILES)
            ->SetLabel(wxString::Format("Max Tiles: %d", sample->m_maxTiles));
        dialog->FindWindow(IDC_STATIC_TILING_INFO_MAX_POLYS)
            ->SetLabel(wxString::Format("Max Polys: %d", sample->m_maxPolysPerTile));
    } else {
        dialog->FindWindow(IDC_STATIC_TILING_INFO_TILES)->SetLabel("Tiles: N/A");
        dialog->FindWindow(IDC_STATIC_TILING_INFO_MAX_TILES)->SetLabel("Max Tiles: N/A");
        dialog->FindWindow(IDC_STATIC_TILING_INFO_MAX_POLYS)->SetLabel("Max Polys: N/A");
    }
}

void RecastAdapter::showRecastDialog() {
    if (hRecastDialog) {
        hRecastDialog->Raise();
        return;
    }
    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "Recast Properties", wxDefaultPosition, wxSize(420, 760));
    hRecastDialog = dialog;
    auto* scroll = new wxScrolledWindow(dialog);
    scroll->SetScrollRate(0, 12);
    auto* content = new wxBoxSizer(wxVERTICAL);
    struct SliderRow {
        int id;
        int labelId;
        const char* name;
        float min;
        float step;
        int max;
        int precision;
    };
    const std::array<SliderRow, 14> sliderRows = {{
        {IDC_SLIDER_CELL_SIZE, IDC_STATIC_CELL_SIZE_VAL, "Cell Size:", 0.01f, 0.01f, 39, 2},
        {IDC_SLIDER_CELL_HEIGHT, IDC_STATIC_CELL_HEIGHT_VAL, "Cell Height:", 0.01f, 0.01f, 39, 2},
        {IDC_SLIDER_AGENT_HEIGHT, IDC_STATIC_AGENT_HEIGHT_VAL, "Agent Height:", 0.1f, 0.01f, 490, 2},
        {IDC_SLIDER_AGENT_RADIUS, IDC_STATIC_AGENT_RADIUS_VAL, "Agent Radius:", 0.0f, 0.01f, 200, 2},
        {IDC_SLIDER_AGENT_MAX_CLIMB, IDC_STATIC_AGENT_MAX_CLIMB_VAL, "Max Climb:", 0.0f, 0.01f, 500, 2},
        {IDC_SLIDER_AGENT_MAX_SLOPE, IDC_STATIC_AGENT_MAX_SLOPE_VAL, "Max Slope:", 0.0f, 1.0f, 90, 0},
        {IDC_SLIDER_REGION_MIN_SIZE, IDC_STATIC_REGION_MIN_SIZE_VAL, "Min Region Size:", 0.0f, 1.0f, 150, 0},
        {IDC_SLIDER_REGION_MERGE_SIZE, IDC_STATIC_REGION_MERGE_SIZE_VAL, "Merged Size:", 0.0f, 1.0f, 150, 0},
        {IDC_SLIDER_POLY_MAX_EDGE_LEN, IDC_STATIC_POLY_MAX_EDGE_LEN_VAL, "Max Edge Len:", 0.0f, 1.0f, 50, 1},
        {IDC_SLIDER_POLY_MAX_EDGE_ERR, IDC_STATIC_POLY_MAX_EDGE_ERR_VAL, "Max Edge Err:", 0.1f, 0.1f, 29, 2},
        {IDC_SLIDER_POLY_VERTS_PER_POLY, IDC_STATIC_POLY_VERTS_PER_POLY_VAL, "Verts Per Poly:", 3.0f, 1.0f, 9, 0},
        {IDC_SLIDER_DETAIL_SAMPLE_DIST, IDC_STATIC_DETAIL_SAMPLE_DIST_VAL, "Sample Dist:", 0.0f, 1.0f, 16, 1},
        {IDC_SLIDER_DETAIL_SAMPLE_MAX_ERR, IDC_STATIC_DETAIL_SAMPLE_MAX_ERR_VAL, "Max Sample Err:", 0.0f, 1.0f, 16, 1},
        {IDC_SLIDER_TILING_TILE_SIZE, IDC_STATIC_TILING_TILE_SIZE_VAL, "Tile Size:", 16.0f, 16.0f, 15, 0},
    }};
    for (const SliderRow& row : sliderRows) {
        if (row.id == wxID_NONE) {
            continue;
        }
        auto* line = new wxBoxSizer(wxHORIZONTAL);
        line->Add(new wxStaticText(scroll, wxID_ANY, row.name), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        line->Add(new wxSlider(scroll, row.id, 0, 0, row.max), 1, wxEXPAND | wxRIGHT, 6);
        line->Add(new wxStaticText(scroll, row.labelId, ""), 0, wxALIGN_CENTER_VERTICAL);
        content->Add(line, 0, wxEXPAND | wxALL, 5);
    }
    content->Add(new wxStaticText(scroll, wxID_ANY, "Partitioning"), 0, wxTOP | wxLEFT, 6);
    auto* partition = new wxBoxSizer(wxHORIZONTAL);
    partition->Add(new wxRadioButton(scroll, IDC_RADIO_PARTITION_WATERSHED, "Watershed", wxDefaultPosition,
                       wxDefaultSize, wxRB_GROUP),
        0, wxRIGHT, 12);
    partition->Add(new wxRadioButton(scroll, IDC_RADIO_PARTITION_MONOTONE, "Monotone"), 0, wxRIGHT, 12);
    partition->Add(new wxRadioButton(scroll, IDC_RADIO_PARTITION_LAYERS, "Layers"));
    content->Add(partition, 0, wxALL, 6);
    content->Add(new wxStaticText(scroll, wxID_ANY, "Filtering"), 0, wxTOP | wxLEFT, 6);
    content->Add(new wxCheckBox(scroll, IDC_CHECK_FILTER_LOW_HANGING, "Low Hanging Obstacles"), 0, wxALL, 4);
    content->Add(new wxCheckBox(scroll, IDC_CHECK_FILTER_LEDGE_SPANS, "Ledge Spans"), 0, wxALL, 4);
    content->Add(new wxCheckBox(scroll, IDC_CHECK_FILTER_WALKABLE_LOW, "Walkable Low Height Spans"), 0, wxALL, 4);
    content->Add(new wxStaticText(scroll, IDC_STATIC_TILING_INFO_TILES, ""), 0, wxALL, 4);
    content->Add(new wxStaticText(scroll, IDC_STATIC_TILING_INFO_MAX_TILES, ""), 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
    content->Add(new wxStaticText(scroll, IDC_STATIC_TILING_INFO_MAX_POLYS, ""), 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
    auto* reset = new wxButton(dialog, IDC_BUTTON_RESET_DEFAULTS, "Reset Defaults");
    auto* outer = new wxBoxSizer(wxVERTICAL);
    scroll->SetSizer(content);
    scroll->FitInside();
    outer->Add(scroll, 1, wxEXPAND | wxALL, 6);
    outer->Add(reset, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    dialog->SetSizer(outer);
    updateRecastDialogControls(dialog);

    for (const SliderRow& row : sliderRows) {
        dialog->Bind(
            wxEVT_SLIDER,
            [dialog, id = row.id](wxCommandEvent& event) {
                Sample_TileMesh* sample = getInstance().sample;
                const int pos = event.GetInt();
                bool updateTiling = false;
                auto updateFloat = [pos, dialog](float& value, const float minimum, const float step, const int labelId,
                                       const int precision) {
                    value = minimum + static_cast<float>(pos) * step;
                    dialog->FindWindow(labelId)->SetLabel(formatRecastFloat(value, precision));
                };
                auto updateInt = [pos, dialog](float& value, const int minimum, const int step, const int labelId) {
                    value = static_cast<float>(minimum + pos * step);
                    dialog->FindWindow(labelId)->SetLabel(std::to_string(static_cast<int>(value)));
                };
                switch (id) {
                case IDC_SLIDER_CELL_SIZE:
                    updateFloat(sample->m_cellSize, 0.01f, 0.01f, IDC_STATIC_CELL_SIZE_VAL, 2);
                    updateTiling = true;
                    break;
                case IDC_SLIDER_CELL_HEIGHT:
                    updateFloat(sample->m_cellHeight, 0.01f, 0.01f, IDC_STATIC_CELL_HEIGHT_VAL, 2);
                    break;
                case IDC_SLIDER_AGENT_HEIGHT:
                    updateFloat(sample->m_agentHeight, 0.1f, 0.01f, IDC_STATIC_AGENT_HEIGHT_VAL, 2);
                    break;
                case IDC_SLIDER_AGENT_RADIUS:
                    updateFloat(sample->m_agentRadius, 0.0f, 0.01f, IDC_STATIC_AGENT_RADIUS_VAL, 2);
                    break;
                case IDC_SLIDER_AGENT_MAX_CLIMB:
                    updateFloat(sample->m_agentMaxClimb, 0.0f, 0.01f, IDC_STATIC_AGENT_MAX_CLIMB_VAL, 2);
                    break;
                case IDC_SLIDER_AGENT_MAX_SLOPE:
                    updateInt(sample->m_agentMaxSlope, 0, 1, IDC_STATIC_AGENT_MAX_SLOPE_VAL);
                    break;
                case IDC_SLIDER_REGION_MIN_SIZE:
                    updateInt(sample->m_regionMinSize, 0, 1, IDC_STATIC_REGION_MIN_SIZE_VAL);
                    break;
                case IDC_SLIDER_REGION_MERGE_SIZE:
                    updateInt(sample->m_regionMergeSize, 0, 1, IDC_STATIC_REGION_MERGE_SIZE_VAL);
                    break;
                case IDC_SLIDER_POLY_MAX_EDGE_LEN:
                    updateFloat(sample->m_edgeMaxLen, 0.0f, 1.0f, IDC_STATIC_POLY_MAX_EDGE_LEN_VAL, 1);
                    break;
                case IDC_SLIDER_POLY_MAX_EDGE_ERR:
                    updateFloat(sample->m_edgeMaxError, 0.1f, 0.1f, IDC_STATIC_POLY_MAX_EDGE_ERR_VAL, 2);
                    break;
                case IDC_SLIDER_POLY_VERTS_PER_POLY:
                    updateInt(sample->m_vertsPerPoly, 3, 1, IDC_STATIC_POLY_VERTS_PER_POLY_VAL);
                    break;
                case IDC_SLIDER_DETAIL_SAMPLE_DIST:
                    updateFloat(sample->m_detailSampleDist, 0.0f, 1.0f, IDC_STATIC_DETAIL_SAMPLE_DIST_VAL, 1);
                    break;
                case IDC_SLIDER_DETAIL_SAMPLE_MAX_ERR:
                    updateFloat(sample->m_detailSampleMaxError, 0.0f, 1.0f, IDC_STATIC_DETAIL_SAMPLE_MAX_ERR_VAL, 1);
                    break;
                case IDC_SLIDER_TILING_TILE_SIZE:
                    updateInt(sample->m_tileSize, 16, 16, IDC_STATIC_TILING_TILE_SIZE_VAL);
                    updateTiling = true;
                    break;
                default:
                    break;
                }
                if (updateTiling) {
                    updateRecastDialogControls(dialog);
                }
                getInstance().saveSettings();
            },
            row.id);
    }
    dialog->Bind(wxEVT_RADIOBUTTON, [](wxCommandEvent& event) {
        auto* sample = getInstance().sample;
        if (event.GetId() == IDC_RADIO_PARTITION_WATERSHED) {
            sample->m_partitionType = SAMPLE_PARTITION_WATERSHED;
        } else if (event.GetId() == IDC_RADIO_PARTITION_MONOTONE) {
            sample->m_partitionType = SAMPLE_PARTITION_MONOTONE;
        } else {
            sample->m_partitionType = SAMPLE_PARTITION_LAYERS;
        }
        getInstance().saveSettings();
    });
    dialog->Bind(wxEVT_CHECKBOX, [](wxCommandEvent& event) {
        const bool checked = event.IsChecked();
        auto* sample = getInstance().sample;
        if (event.GetId() == IDC_CHECK_FILTER_LOW_HANGING) {
            sample->m_filterLowHangingObstacles = checked;
        } else if (event.GetId() == IDC_CHECK_FILTER_LEDGE_SPANS) {
            sample->m_filterLedgeSpans = checked;
        } else {
            sample->m_filterWalkableLowHeightSpans = checked;
        }
        getInstance().saveSettings();
    });
    reset->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) {
        getInstance().resetCommonSettings();
        updateRecastDialogControls(dialog);
        getInstance().saveSettings();
    });
    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent& event) {
        if (hRecastDialog == dialog) {
            hRecastDialog = nullptr;
        }
        event.Skip();
    });
    dialog->CentreOnParent();
    dialog->Show();
}

void RecastAdapter::drawInputGeom() const {
    duDebugDrawTriMesh(debugDraw, inputGeom->getMesh()->getVerts(), inputGeom->getMesh()->getVertCount(),
        inputGeom->getMesh()->getTris(), inputGeom->getMesh()->getNormals(), inputGeom->getMesh()->getTriCount(),
        nullptr, 1.0f);
}

bool RecastAdapter::loadInputGeom(const std::string& fileName) const {
    return inputGeom->load(buildContext, fileName);
}

void RecastAdapter::setTileSettings(const float* bBoxMin, const float* bBoxMax) const {
    int gw = 0, gh = 0;
    rcCalcGridSize(bBoxMin, bBoxMax, sample->m_cellSize, &gw, &gh);
    const int ts = (int)sample->m_tileSize;
    const int tw = (gw + ts - 1) / ts;
    const int th = (gh + ts - 1) / ts;

    int tileBits = rcMin((int)ilog2(nextPow2(tw * th)), 14);
    if (tileBits > 14) {
        tileBits = 14;
    }
    const int polyBits = 22 - tileBits;
    sample->m_maxTiles = 1 << tileBits;
    sample->m_maxPolysPerTile = 1 << polyBits;
}

void RecastAdapter::setMeshBBox(const float* bBoxMin, const float* bBoxMax) const {
    if (inputGeom == nullptr) {
        return;
    }
    inputGeom->m_meshBMin[0] = bBoxMin[0];
    inputGeom->m_meshBMin[1] = bBoxMin[1];
    inputGeom->m_meshBMin[2] = bBoxMin[2];
    inputGeom->m_meshBMax[0] = bBoxMax[0];
    inputGeom->m_meshBMax[1] = bBoxMax[1];
    inputGeom->m_meshBMax[2] = bBoxMax[2];
    setTileSettings(bBoxMin, bBoxMax);
}

const float* RecastAdapter::getBBoxMin() const {
    return inputGeom->getNavMeshBoundsMin();
}

const float* RecastAdapter::getBBoxMax() const {
    return inputGeom->getNavMeshBoundsMax();
}

std::pair<int, int> RecastAdapter::getGridSize() const {
    const float* bmin = inputGeom->getNavMeshBoundsMin();
    const float* bmax = inputGeom->getNavMeshBoundsMax();
    int gw = 0, gh = 0;
    rcCalcGridSize(bmin, bmax, sample->m_cellSize, &gw, &gh);
    return {gw, gh};
}

void RecastAdapter::setSceneBBoxToMesh() const {
    float meshBMin[3], meshBMax[3];
    rcCalcBounds(inputGeom->getMesh()->getVerts(), inputGeom->getMesh()->getVertCount(), meshBMin, meshBMax);
    const float pos[3] = {
        (meshBMin[0] + meshBMax[0]) / 2, (meshBMin[1] + meshBMax[1]) / 2, (meshBMin[2] + meshBMax[2]) / 2};
    const float scale[3] = {
        (meshBMax[0] - meshBMin[0]) * 1.1f, (meshBMax[1] - meshBMin[1]) * 1.1f, (meshBMax[2] - meshBMin[2]) * 1.1f};
    Scene& scene = Scene::getInstance();
    scene.setBBox(pos, scale);
    setTileSettings(meshBMin, meshBMax);
}

void RecastAdapter::handleMeshChanged() const {
    sample->handleMeshChanged(inputGeom);
    // if (const Scene &scene = Scene::getInstance();
    //     !scene.sceneLoaded ||
    //     scene.includeBox.id == Json::PfBoxes::NO_INCLUDE_BOX_FOUND) {
    //     setSceneBBoxToMesh();
    // }
}

void RecastAdapter::cleanup() const {
    sample->cleanup();
}

bool RecastAdapter::handleBuild() const {
    sample->cleanup();
    return sample->handleBuild();
}

bool RecastAdapter::handleBuildForAirg() const {
    const float agentRadius = sample->m_agentRadius;
    const float agentMaxSlope = sample->m_agentMaxSlope;
    const float detailSampleMaxError = sample->m_detailSampleMaxError;
    sample->m_agentRadius = 0;
    sample->m_agentMaxSlope = agentMaxSlope + 1;
    // sample->m_detailSampleMaxError = 0.1;
    sample->handleTileSettingsWithNoUI();
    const bool success = sample->handleBuild();
    sample->m_agentRadius = agentRadius;
    sample->m_agentMaxSlope = agentMaxSlope;
    sample->m_detailSampleMaxError = detailSampleMaxError;
    return success;
}

void RecastAdapter::handleCommonSettings() const {
    sample->handleSettings();
}

void RecastAdapter::resetCommonSettings() const {
    sample->resetCommonSettings();
    sample->m_tileSize = 64;
}

void RecastAdapter::renderRecastNavmesh(const bool isAirgInstance) const {
    const dtNavMesh* mesh = sample->getNavMesh();
    if (!mesh) {
        return;
    }
    dtNavMeshQuery* navQuery = sample->getNavMeshQuery();
    for (int tileIndex = 0; tileIndex < mesh->getMaxTiles(); tileIndex++) {
        const dtMeshTile* tile = mesh->getTile(tileIndex);
        if (!tile || !tile->header) {
            continue;
        }
        const Vec3 redColor = {0.8, 0.0, 0.0};
        const Vec3 purpleColor = {0.8, 0.0, 0.8};
        const Vec3 color = isAirgInstance ? redColor : purpleColor;
        const Vec3 min{tile->header->bmin[0], tile->header->bmin[1], tile->header->bmin[2]};
        const Vec3 max{tile->header->bmax[0], tile->header->bmax[1], tile->header->bmax[2]};
        Renderer& renderer = Renderer::getInstance();
        Vec3 camPos{renderer.cameraPos[0], renderer.cameraPos[1], renderer.cameraPos[2]};

        const float distance = camPos.DistanceTo((min + max) / 2);
        if (distance > 100) {
            continue;
        }
        const Vec3 tileCorners[] = {{min.X, 0, min.Z}, {max.X, 0, min.Z}, {max.X, 0, max.Z}, {min.X, 0, max.Z}};
        for (int i = 0; i < 4; ++i) {
            drawLine(tileCorners[i], tileCorners[(i + 1) % 4], renderer.shader, renderer.view, renderer.projection,
                color, 0.6f);
        }
        renderer.drawText(std::to_string(tileIndex + 1), {min.X, 0, min.Z}, color);
        const Vec3 tealColor = {0.8, 0.0, 0.0};
        const Vec3 greyColor = {0.8, 0.8, 0.8};
        const Vec3 polyColor = isAirgInstance ? tealColor : greyColor;
        for (int polyIndex = 0; polyIndex < tile->header->polyCount; polyIndex++) {
            const dtPolyRef polyRef = getPoly(tileIndex, polyIndex);
            auto edges = getEdges(navQuery, polyRef);
            for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                drawLine(edges[edgeIndex], edges[(edgeIndex + 1) % 3], renderer.shader, renderer.view,
                    renderer.projection, polyColor, 0.6f);
            }
            auto centroid = calculateCentroid(navQuery, polyRef);
            renderer.drawText("ref: " + std::to_string(polyRef) + " idx: " + std::to_string(polyIndex),
                {centroid.X, centroid.Y, centroid.Z}, color);
        }
    }
}

dtPolyRef RecastAdapter::getPoly(const int tileIndex, const int polyIndex) const {
    const dtNavMesh* mesh = sample->getNavMesh();
    if (!mesh) {
        Logger::log(NK_ERROR, "getPoly: NavMesh is null.");
        return 0;
    }

    if (tileIndex < 0 || tileIndex >= mesh->getMaxTiles()) {
        Logger::log(NK_ERROR,
            ("getPoly: Invalid tileIndex " + std::to_string(tileIndex) +
                ". Max tiles: " + std::to_string(mesh->getMaxTiles()))
                .c_str());
        return 0;
    }

    const dtMeshTile* tile = mesh->getTile(tileIndex);
    if (!tile || !tile->header) {
        Logger::log(NK_WARN,
            ("getPoly: Tile at index " + std::to_string(tileIndex) + " is not valid or has no header.").c_str());
        return 0;
    }

    if (polyIndex < 0 || static_cast<unsigned int>(polyIndex) >= tile->header->polyCount) {
        Logger::log(NK_ERROR,
            ("getPoly: Invalid polyIndex " + std::to_string(polyIndex) + " for tile " + std::to_string(tileIndex) +
                ". Poly count: " + std::to_string(tile->header->polyCount))
                .c_str());
        return 0;
    }

    return mesh->encodePolyId(tile->salt, tileIndex, polyIndex);
}

dtStatus RecastAdapter::findNearestPoly(
    const float* recastPos, dtPolyRef* polyRef, float* nearestPt, const bool includeExcludedAreas = false) const {
    const dtNavMesh* navMesh = sample->getNavMesh();
    const dtNavMeshQuery* navQuery = sample->getNavMeshQuery();
    if (!navMesh) {
        return DT_FAILURE;
    }
    constexpr float halfExtents[3] = {2, 4, 2};
    if (navQuery) {
        const dtStatus result = navQuery->findNearestPoly(
            recastPos, halfExtents, includeExcludedAreas ? filterWithExcluded : filter, polyRef, nearestPt);
        return result;
    }
    return DT_FAILURE;
}

void RecastAdapter::findPfSeedPointAreas() {
    pfSeedPointAreas.clear();
    for (const auto& pfSeedPoint : Scene::getInstance().pfSeedPoints) {
        dtPolyRef pfSeedPointRef;
        const Vec3 recastPosVec3 =
            convertFromNavPowerToRecast({pfSeedPoint.pos.x, pfSeedPoint.pos.y, pfSeedPoint.pos.z});
        const float recastPos[3] = {recastPosVec3.X, recastPosVec3.Y, recastPosVec3.Z};
        const dtStatus result = findNearestPoly(recastPos, &pfSeedPointRef, nullptr, true);
        if (result == DT_SUCCESS) {
            if (!pfSeedPointRef) {
                Logger::log(NK_INFO, ("Polygon not found for PF Seed Point: " + pfSeedPoint.id).c_str());
                continue;
            }
            Logger::log(NK_INFO, ("Adding PF Seed Point poly ref: " + std::to_string(pfSeedPointRef)).c_str());
            pfSeedPointAreas.push_back(pfSeedPointRef);
        } else {
            Logger::log(NK_ERROR, ("Failed to find polygon for Seed point: " + std::to_string(pfSeedPointRef)).c_str());
        }
    }
}

void RecastAdapter::excludeNonReachableAreas() const {
    if (pfSeedPointAreas.empty()) {
        Logger::log(NK_INFO, "No PF Seed Points found. Skipping navmesh pruning.");
        return;
    }
    std::map<dtPolyRef, bool> pathFoundForPoly;
    const dtNavMesh* cmesh = sample->getNavMesh();
    dtNavMesh* mesh = sample->getNavMesh();
    constexpr SamplePolyFlags pruneFlagType = SAMPLE_POLYFLAGS_DISABLED;
    if (!mesh) {
        return;
    }
    std::vector<int> tilePolyCounts;
    for (int tileIndex = 0; tileIndex < mesh->getMaxTiles(); ++tileIndex) {
        const dtMeshTile* tile = cmesh->getTile(tileIndex);
        if (!tile || !tile->header || !tile->dataSize) {
            tilePolyCounts.push_back(0);
            continue;
        }
        tilePolyCounts.push_back(tile->header->polyCount);
    }
    int totalAreaCount = 0;
    for (const int count : tilePolyCounts) {
        totalAreaCount += count;
    }
    int validPathsFound = 0;
    std::queue<std::pair<dtPolyRef, bool>> polyAndOverrideExcludeQueue;
    for (const dtPolyRef pfSeedPointRef : pfSeedPointAreas) {
        const unsigned int tileIndex = mesh->decodePolyIdTile(pfSeedPointRef);
        const unsigned int polyIndex = mesh->decodePolyIdPoly(pfSeedPointRef);
        const dtMeshTile* tile = cmesh->getTile(tileIndex);
        dtPoly& poly = tile->polys[polyIndex];
        bool overrideExclude = poly.flags == SAMPLE_POLYFLAGS_DISABLED;
        polyAndOverrideExcludeQueue.push({pfSeedPointRef, overrideExclude});
        pathFoundForPoly[pfSeedPointRef] = true;
        poly.flags = SAMPLE_POLYFLAGS_ALL;
    }

    while (!polyAndOverrideExcludeQueue.empty()) {
        const dtPolyRef currentPolyRef = polyAndOverrideExcludeQueue.front().first;
        bool overrideExclude = polyAndOverrideExcludeQueue.front().second;
        polyAndOverrideExcludeQueue.pop();
        const unsigned int tileIndex = mesh->decodePolyIdTile(currentPolyRef);
        const unsigned int polyIndex = mesh->decodePolyIdPoly(currentPolyRef);
        const dtMeshTile* tile = cmesh->getTile(tileIndex);
        const dtPoly& poly = tile->polys[polyIndex];
        validPathsFound++;
        if (validPathsFound % 100 == 0) {
            Logger::log(NK_INFO,
                ("Found " + std::to_string(validPathsFound) +
                    " areas with valid paths to PF Seed Point areas so far out of " + std::to_string(totalAreaCount) +
                    " total areas.")
                    .c_str());
        }
        for (unsigned int linkIndex = poly.firstLink; linkIndex != DT_NULL_LINK;
            linkIndex = tile->links[linkIndex].next) {
            const dtLink& link = tile->links[linkIndex];
            const dtMeshTile* targetTile = nullptr;
            const dtPoly* targetPoly = nullptr;
            mesh->getTileAndPolyByRef(link.ref, &targetTile, &targetPoly);
            if (targetTile && targetPoly) {
                int targetTileIndex = -1;
                for (int i = 0; i < mesh->getMaxTiles(); ++i) {
                    if (cmesh->getTile(i) == targetTile) {
                        targetTileIndex = i;
                        break;
                    }
                }
                if (targetTileIndex != -1) {
                    int targetPolyIndex = -1;
                    for (int i = 0; i < targetTile->header->polyCount; ++i) {
                        if (&targetTile->polys[i] == targetPoly) {
                            targetPolyIndex = i;
                            break;
                        }
                    }
                    if (targetPolyIndex != -1) {
                        const dtPolyRef adjacentPolyRef =
                            mesh->encodePolyId(targetTile->salt, targetTileIndex, targetPolyIndex);
                        if (!pathFoundForPoly.contains(adjacentPolyRef)) {
                            if (overrideExclude || poly.flags != SAMPLE_POLYFLAGS_DISABLED) {
                                pathFoundForPoly[adjacentPolyRef] = true;
                                polyAndOverrideExcludeQueue.push({adjacentPolyRef, overrideExclude});
                            }
                            if (overrideExclude) {
                                const unsigned int adjacentTileIndex = mesh->decodePolyIdTile(adjacentPolyRef);
                                const unsigned int adjacentPolyIndex = mesh->decodePolyIdPoly(adjacentPolyRef);
                                const dtMeshTile* adjacentTile = cmesh->getTile(adjacentTileIndex);
                                dtPoly& adjacentPoly = adjacentTile->polys[adjacentPolyIndex];
                                adjacentPoly.flags = SAMPLE_POLYFLAGS_ALL;
                            }
                        }
                    }
                }
            }
        }
    }
    Logger::log(NK_INFO,
        ("Found " + std::to_string(validPathsFound) + " areas with valid paths to PF Seed Point areas in total.")
            .c_str());

    int areasPruned = 0;
    for (int tileIndex = 0; tileIndex < cmesh->getMaxTiles(); tileIndex++) {
        const dtMeshTile* tile = cmesh->getTile(tileIndex);
        if (!tile || !tile->header || !tile->dataSize) {
            continue;
        }
        const int polyCount = tile->header->polyCount;
        for (int polyIndex = 0; polyIndex < polyCount; ++polyIndex) {
            const dtPolyRef polyRef = mesh->encodePolyId(tile->salt, tileIndex, polyIndex);
            if (!pathFoundForPoly[polyRef]) {
                mesh->setPolyFlags(polyRef, pruneFlagType);
                areasPruned++;
            }
        }
    }
    Logger::log(
        NK_INFO, ("Pruned " + std::to_string(areasPruned) + " areas with no path to any PF Seed Point.").c_str());
}

void RecastAdapter::save(const std::string& data, bool isKnt) const {
    sample->saveAll(data.c_str(), isKnt);
}

int RecastAdapter::getVertCount() const {
    return inputGeom->getMesh()->getVertCount();
}

int RecastAdapter::getTriCount() const {
    return inputGeom->getMesh()->getTriCount();
}

void RecastAdapter::addConvexVolume(Json::PfBox& pfBox) const {
    float verts[4 * 3];
    verts[0] = -pfBox.scale.x / 2;
    verts[1] = -pfBox.scale.y / 2;
    verts[2] = -pfBox.scale.z / 2;
    verts[3] = pfBox.scale.x / 2;
    verts[4] = -pfBox.scale.y / 2;
    verts[5] = -pfBox.scale.z / 2;
    verts[6] = pfBox.scale.x / 2;
    verts[7] = pfBox.scale.y / 2;
    verts[8] = -pfBox.scale.z / 2;
    verts[9] = -pfBox.scale.x / 2;
    verts[10] = pfBox.scale.y / 2;
    verts[11] = -pfBox.scale.z / 2;
    Vec3 rotated = Math::rotatePoint(
        {verts[0], verts[1], verts[2]}, {pfBox.rotation.x, pfBox.rotation.y, pfBox.rotation.z, pfBox.rotation.w});
    verts[0] = rotated.X;
    verts[1] = rotated.Y;
    verts[2] = rotated.Z;
    rotated = Math::rotatePoint(
        {verts[3], verts[4], verts[5]}, {pfBox.rotation.x, pfBox.rotation.y, pfBox.rotation.z, pfBox.rotation.w});
    verts[3] = rotated.X;
    verts[4] = rotated.Y;
    verts[5] = rotated.Z;
    rotated = Math::rotatePoint(
        {verts[6], verts[7], verts[8]}, {pfBox.rotation.x, pfBox.rotation.y, pfBox.rotation.z, pfBox.rotation.w});
    verts[6] = rotated.X;
    verts[7] = rotated.Y;
    verts[8] = rotated.Z;
    rotated = Math::rotatePoint(
        {verts[9], verts[10], verts[11]}, {pfBox.rotation.x, pfBox.rotation.y, pfBox.rotation.z, pfBox.rotation.w});
    verts[9] = rotated.X;
    verts[10] = rotated.Y;
    verts[11] = rotated.Z;

    verts[0] += pfBox.pos.x;
    verts[1] += pfBox.pos.y;
    verts[2] += pfBox.pos.z;
    verts[3] += pfBox.pos.x;
    verts[4] += pfBox.pos.y;
    verts[5] += pfBox.pos.z;
    verts[6] += pfBox.pos.x;
    verts[7] += pfBox.pos.y;
    verts[8] += pfBox.pos.z;
    verts[9] += pfBox.pos.x;
    verts[10] += pfBox.pos.y;
    verts[11] += pfBox.pos.z;

    for (int i = 0; i < 4; i++) {
        const float recastY = verts[i * 3 + 2];
        const float recastZ = -verts[i * 3 + 1];
        verts[i * 3 + 1] = recastY;
        verts[i * 3 + 2] = recastZ;
    }
    inputGeom->addConvexVolume(verts, 4, pfBox.pos.z - pfBox.scale.z / 2 - 0.5, pfBox.pos.z + pfBox.scale.z / 2,
        1); // areaType 1 is water
}

const ConvexVolume* RecastAdapter::getConvexVolumes() const {
    return inputGeom->getConvexVolumes();
}

int RecastAdapter::getConvexVolumeCount() const {
    return inputGeom->getConvexVolumeCount();
}

void RecastAdapter::clearConvexVolumes() const {
    const int count = inputGeom->getConvexVolumeCount();
    for (int i = 0; i < count; i++) {
        inputGeom->deleteConvexVolume(0);
    }
}

dtPolyRef RecastAdapter::getPolyRefForLink(const dtLink& link) const {
    const dtNavMesh* mesh = sample->getNavMesh();
    const dtNavMesh* cmesh = sample->getNavMesh();
    const dtMeshTile* targetTile = nullptr;
    const dtPoly* targetPoly = nullptr;

    mesh->getTileAndPolyByRef(link.ref, &targetTile, &targetPoly);
    if (targetTile && targetPoly) {
        int targetTileIndex = -1;
        for (int i = 0; i < mesh->getMaxTiles(); ++i) {
            if (cmesh->getTile(i) == targetTile) {
                targetTileIndex = i;
                break;
            }
        }
        if (targetTileIndex != -1) {
            int targetPolyIndex = -1;
            for (int i = 0; i < targetTile->header->polyCount; ++i) {
                if (&targetTile->polys[i] == targetPoly) {
                    targetPolyIndex = i;
                    break;
                }
            }
            if (targetPolyIndex != -1) {
                return mesh->encodePolyId(targetTile->salt, targetTileIndex, targetPolyIndex);
            }
        }
    }
    return 0;
}

bool RecastAdapter::pfLineBlocked(const Vec3& recastStart, const Vec3& recastEnd) const {
    const dtNavMesh* mesh = sample->getNavMesh();
    if (!mesh) {
        return true;
    }
    const dtNavMeshQuery* navQuery = sample->getNavMeshQuery();
    if (!navQuery) {
        return true;
    }

    dtPolyRef startRef;
    const float recastStartPos[3] = {recastStart.X, recastStart.Y, recastStart.Z};
    dtStatus result = findNearestPoly(recastStartPos, &startRef, nullptr);
    if (result != DT_SUCCESS || startRef == 0) {
        Logger::log(NK_ERROR,
            ("PFLineBlocked: Could not find area for start pos (" + std::to_string(recastStartPos[0]) + ", " +
                std::to_string(recastStartPos[1]) + ", " + std::to_string(recastStartPos[2]) + ")")
                .c_str());
        return true;
    }

    dtPolyRef endRef;
    const float recastEndPos[3] = {recastEnd.X, recastEnd.Y, recastEnd.Z};
    result = findNearestPoly(recastEndPos, &endRef, nullptr);
    if (result != DT_SUCCESS || endRef == 0) {
        Logger::log(NK_ERROR,
            ("PFLineBlocked: Could not find area for end pos (" + std::to_string(recastEndPos[0]) + ", " +
                std::to_string(recastEndPos[1]) + ", " + std::to_string(recastEndPos[2]) + ")")
                .c_str());
        return true;
    }

    if (startRef == endRef) {
        return false;
    }

    const Vec3 middle = {(recastStart.X + recastEnd.X) / 2.0f, (recastStart.Y + recastEnd.Y) / 2.0f,
        (recastStart.Z + recastEnd.Z) / 2.0f};
    dtPolyRef middleRef;
    const float recastMiddlePos[3] = {middle.X, middle.Y, middle.Z};
    result = findNearestPoly(recastMiddlePos, &middleRef, nullptr);
    if (result != DT_SUCCESS) {
        return true;
    }

    const float middlePosRecast[3] = {middle.X, middle.Y, middle.Z};
    constexpr float radius = 2.25f;

    dtPolyRef resultRef[20];
    dtPolyRef resultParent[20];
    float resultCost[20];
    int resultCount;
    constexpr int maxResult = 20;

    navQuery->findPolysAroundCircle(
        startRef, middlePosRecast, radius, filter, resultRef, resultParent, resultCost, &resultCount, maxResult);
    std::queue<dtPolyRef> polyQueue;
    std::map<dtPolyRef, bool> pathFoundForPoly;
    polyQueue.push(middleRef);
    pathFoundForPoly[middleRef] = true;
    const dtNavMesh* cmesh = sample->getNavMesh();
    while (!polyQueue.empty()) {
        const dtPolyRef currentPolyRef = polyQueue.front();
        polyQueue.pop();
        const unsigned int tileIndex = mesh->decodePolyIdTile(currentPolyRef);
        const unsigned int polyIndex = mesh->decodePolyIdPoly(currentPolyRef);
        const dtMeshTile* tile = cmesh->getTile(tileIndex);
        const dtPoly& poly = tile->polys[polyIndex];
        for (unsigned int linkIndex = poly.firstLink; linkIndex != DT_NULL_LINK;
            linkIndex = tile->links[linkIndex].next) {
            const dtLink& link = tile->links[linkIndex];
            const dtMeshTile* targetTile = nullptr;
            const dtPoly* targetPoly = nullptr;
            mesh->getTileAndPolyByRef(link.ref, &targetTile, &targetPoly);
            if (targetTile && targetPoly) {
                int targetTileIndex = -1;
                for (int i = 0; i < mesh->getMaxTiles(); ++i) {
                    if (cmesh->getTile(i) == targetTile) {
                        targetTileIndex = i;
                        break;
                    }
                }
                if (targetTileIndex != -1) {
                    int targetPolyIndex = -1;
                    for (int i = 0; i < targetTile->header->polyCount; ++i) {
                        if (&targetTile->polys[i] == targetPoly) {
                            targetPolyIndex = i;
                            break;
                        }
                    }
                    if (targetPolyIndex != -1) {
                        const dtPolyRef adjacentPolyRef =
                            mesh->encodePolyId(targetTile->salt, targetTileIndex, targetPolyIndex);
                        if (!pathFoundForPoly.contains(adjacentPolyRef) && poly.flags != SAMPLE_POLYFLAGS_DISABLED) {
                            for (int i = 0; i < resultCount; i++) {
                                if (resultRef[i] == adjacentPolyRef) {
                                    // if (resultCost[i] < radius * 2) {
                                    pathFoundForPoly[adjacentPolyRef] = true;
                                    polyQueue.push(adjacentPolyRef);
                                    break;
                                    // }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (pathFoundForPoly.contains(startRef) && pathFoundForPoly.contains(endRef) && pathFoundForPoly[startRef] &&
        pathFoundForPoly[endRef]) {
        return false;
    }
    return true;
}

dtPolyRef RecastAdapter::getAdjacentPoly(const dtPolyRef polyRef, const int edgeIndex) const {
    const dtNavMesh* mesh = sample->getNavMesh();
    const dtNavMesh* cmesh = sample->getNavMesh();

    const unsigned int tileIndex = mesh->decodePolyIdTile(polyRef);
    const unsigned int polyIndex = mesh->decodePolyIdPoly(polyRef);
    const dtMeshTile* tile = cmesh->getTile(tileIndex);
    const dtPoly& poly = tile->polys[polyIndex];
    for (unsigned int linkIndex = poly.firstLink; linkIndex != DT_NULL_LINK; linkIndex = tile->links[linkIndex].next) {
        const dtLink& link = tile->links[linkIndex];
        if (link.edge == edgeIndex) {
            return getPolyRefForLink(link);
        }
        //        if (link.edge > edgeIndex) { // TODO: Check if this was needed. I think it was just an optimization
        //            break;
        //        }
    }
    return 0;
}

void RecastAdapter::setMarker(const SceneMeshHitTestResult& result) {
    markerPositionSet = true;
    markerPosition[0] = result.rayStart[0] + (result.rayEnd[0] - result.rayStart[0]) * result.hitTime;
    markerPosition[1] = result.rayStart[1] + (result.rayEnd[1] - result.rayStart[1]) * result.hitTime;
    markerPosition[2] = result.rayStart[2] + (result.rayEnd[2] - result.rayStart[2]) * result.hitTime;
    for (const auto& [object, vertexRange] : SceneMesh::getInstance().objectTriangleRanges) {
        if (result.hitIndex >= vertexRange.first && result.hitIndex < vertexRange.second) {
            selectedObject = object;
            break;
        }
    }
    std::string meshNameString;
    std::string roomString;
    if (Scene::getInstance().sceneLoaded) {
        if (const auto mesh = Scene::getInstance().findMeshByHashAndIdAndPos(
                selectedObject.substr(0, 16), selectedObject.substr(17, 16), markerPosition);
            mesh != nullptr) {
            meshNameString = mesh->entity.name;
            roomString = " Room Folder: " + mesh->roomFolderName + " Room: " + mesh->roomName;
        }
    }
    Logger::log(NK_INFO,
        ("Selected Object: '" + meshNameString + "' Mesh: '" + selectedObject +
            "' Obj vertex: " + std::to_string(result.hitIndex) + roomString +
            ". Setting marker position to: " + std::to_string(markerPosition[0]) + ", " +
            std::to_string(markerPosition[1]) + ", " + std::to_string(markerPosition[2]))
            .c_str());
}

SceneMeshHitTestResult RecastAdapter::doHitTest(const int mx, const int my) {
    float rayStart[3];
    float rayEnd[3];
    float hitTime;
    const Renderer& renderer = Renderer::getInstance();
    const glm::vec4 viewport(renderer.viewport[0], renderer.viewport[1], renderer.viewport[2], renderer.viewport[3]);
    const glm::vec3 start = glm::unProject(
        {static_cast<float>(mx), static_cast<float>(my), 0.0f}, renderer.view, renderer.projection, viewport);
    const glm::vec3 end = glm::unProject(
        {static_cast<float>(mx), static_cast<float>(my), 1.0f}, renderer.view, renderer.projection, viewport);
    rayStart[0] = start.x;
    rayStart[1] = start.y;
    rayStart[2] = start.z;
    rayEnd[0] = end.x;
    rayEnd[1] = end.y;
    rayEnd[2] = end.z;
    SceneMeshHitTestResult result;
    if (const int hitIndex = inputGeom->raycastMesh(rayStart, rayEnd, hitTime); hitIndex != -1) {
        result.hitIndex = hitIndex;
        result.rayStart[0] = rayStart[0];
        result.rayStart[1] = rayStart[1];
        result.rayStart[2] = rayStart[2];
        result.rayEnd[0] = rayEnd[0];
        result.rayEnd[1] = rayEnd[1];
        result.rayEnd[2] = rayEnd[2];
        result.hitTime = hitTime;
        return result;
    }
    result.hitIndex = -1;
    if (SDL_GetModState()) {
        markerPositionSet = false;
    }
    return result;
}

void RecastAdapter::loadSettings() const {
    const PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    sample->m_cellSize = static_cast<float>(atof(persistedSettings.getValue("Recast", "cellSize", "0.1f")));
    sample->m_cellHeight = static_cast<float>(atof(persistedSettings.getValue("Recast", "cellHeight", "0.1f")));
    sample->m_agentHeight = static_cast<float>(atof(persistedSettings.getValue("Recast", "agentHeight", "1.8f")));
    sample->m_agentRadius = static_cast<float>(atof(persistedSettings.getValue("Recast", "agentRadius", "0.2f")));
    sample->m_agentMaxClimb = static_cast<float>(atof(persistedSettings.getValue("Recast", "agentMaxClimb", "0.41f")));
    sample->m_agentMaxSlope = static_cast<float>(atof(persistedSettings.getValue("Recast", "agentMaxSlope", "45.0f")));
    sample->m_regionMinSize = static_cast<float>(atof(persistedSettings.getValue("Recast", "regionMinSize", "25f")));
    sample->m_regionMergeSize =
        static_cast<float>(atof(persistedSettings.getValue("Recast", "regionMergeSize", "30f")));
    if (const char* partitionTypeStr(
            persistedSettings.getValue("Recast", "partitionType", "SAMPLE_PARTITION_WATERSHED"));
        strcmp(partitionTypeStr, "SAMPLE_PARTITION_MONOTONE") == 0) {
        sample->m_partitionType = SAMPLE_PARTITION_MONOTONE;
    } else if (strcmp(partitionTypeStr, "SAMPLE_PARTITION_LAYERS") == 0) {
        sample->m_partitionType = SAMPLE_PARTITION_LAYERS;
    } else {
        sample->m_partitionType = SAMPLE_PARTITION_WATERSHED;
    }
    sample->m_filterLowHangingObstacles =
        static_cast<bool>(atoi(persistedSettings.getValue("Recast", "filterLowHangingObstacles", "1f")));
    sample->m_filterLedgeSpans =
        static_cast<bool>(atoi(persistedSettings.getValue("Recast", "filterLedgeSpans", "1f")));
    sample->m_filterWalkableLowHeightSpans =
        static_cast<bool>(atoi(persistedSettings.getValue("Recast", "filterWalkableLowHeightSpans", "1f")));
    sample->m_edgeMaxLen =
        static_cast<float>(atof(persistedSettings.getValue("Recast", "polygonizationEdgeMaxLen", "5.0f")));
    sample->m_edgeMaxError =
        static_cast<float>(atof(persistedSettings.getValue("Recast", "polygonizationEdgeMaxError", "1.4f")));
    sample->m_vertsPerPoly =
        static_cast<float>(atof(persistedSettings.getValue("Recast", "polygonizationVertsPerPoly", "3")));
    sample->m_detailSampleDist =
        static_cast<float>(atof(persistedSettings.getValue("Recast", "detailSampleDist", "1.5f")));
    sample->m_detailSampleMaxError =
        static_cast<float>(atof(persistedSettings.getValue("Recast", "detailSampleMaxError", "1.4f")));
    sample->m_tileSize = static_cast<float>(atof(persistedSettings.getValue("Recast", "tilingTileSize", "64f")));
}

void RecastAdapter::saveSettings() const {
    PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    persistedSettings.setValue("Recast", "cellSize", std::to_string(sample->m_cellSize));
    persistedSettings.setValue("Recast", "cellHeight", std::to_string(sample->m_cellHeight));
    persistedSettings.setValue("Recast", "agentHeight", std::to_string(sample->m_agentHeight));
    persistedSettings.setValue("Recast", "agentRadius", std::to_string(sample->m_agentRadius));
    persistedSettings.setValue("Recast", "agentMaxClimb", std::to_string(sample->m_agentMaxClimb));
    persistedSettings.setValue("Recast", "agentMaxSlope", std::to_string(sample->m_agentMaxSlope));
    persistedSettings.setValue("Recast", "regionMinSize", std::to_string(sample->m_regionMinSize));
    persistedSettings.setValue("Recast", "regionMergeSize", std::to_string(sample->m_regionMergeSize));
    persistedSettings.setValue(
        "Recast", "filterLowHangingObstacles", std::to_string(sample->m_filterLowHangingObstacles));
    persistedSettings.setValue("Recast", "filterLedgeSpans", std::to_string(sample->m_filterLedgeSpans));
    persistedSettings.setValue(
        "Recast", "filterWalkableLowHeightSpans", std::to_string(sample->m_filterWalkableLowHeightSpans));
    persistedSettings.setValue("Recast", "polygonizationEdgeMaxLen", std::to_string(sample->m_edgeMaxLen));
    persistedSettings.setValue("Recast", "polygonizationEdgeMaxError", std::to_string(sample->m_edgeMaxError));
    persistedSettings.setValue("Recast", "polygonizationVertsPerPoly", std::to_string(sample->m_vertsPerPoly));
    persistedSettings.setValue("Recast", "detailSampleDist", std::to_string(sample->m_detailSampleDist));
    persistedSettings.setValue("Recast", "detailSampleMaxError", std::to_string(sample->m_detailSampleMaxError));
    persistedSettings.setValue("Recast", "tilingTileSize", std::to_string(sample->m_tileSize));
    switch (sample->m_partitionType) {
    case SAMPLE_PARTITION_MONOTONE:
        persistedSettings.setValue("Recast", "partitionType", "SAMPLE_PARTITION_MONOTONE");
        break;
    case SAMPLE_PARTITION_LAYERS:
        persistedSettings.setValue("Recast", "partitionType", "SAMPLE_PARTITION_LAYERS");
        break;
    case SAMPLE_PARTITION_WATERSHED:
    default:
        persistedSettings.setValue("Recast", "partitionType", "SAMPLE_PARTITION_WATERSHED");
        break;
    }
    persistedSettings.save();
}

Vec3 RecastAdapter::convertFromNavPowerToRecast(Vec3 pos) {
    return {pos.X, pos.Z, -pos.Y};
}

Vec3 RecastAdapter::convertFromRecastToNavPower(Vec3 pos) {
    return {pos.X, -pos.Z, pos.Y};
}

std::vector<Vec3> RecastAdapter::getEdges(const dtNavMeshQuery* navQuery, const dtPolyRef polyRef) {
    if (!navQuery) {
        return {};
    }

    const dtNavMesh* mesh = navQuery->getAttachedNavMesh();
    if (!mesh) {
        return {};
    }

    unsigned int salt = 0;
    unsigned int tileIndex = 0;
    unsigned int polyIndex = 0;
    mesh->decodePolyId(polyRef, salt, tileIndex, polyIndex);
    const dtMeshTile* tile = mesh->getTile(tileIndex);
    const dtPoly& poly = tile->polys[polyIndex];
    std::vector<Vec3> edges;
    edges.reserve(poly.vertCount);
    for (int vi = 0; vi < poly.vertCount; vi++) {
        Vec3 recastPos = {
            tile->verts[poly.verts[vi] * 3], tile->verts[poly.verts[vi] * 3 + 1], tile->verts[poly.verts[vi] * 3 + 2]};
        edges.push_back(recastPos);
    }
    return edges;
}

Vec3 RecastAdapter::calculateNormal(dtNavMeshQuery* navQuery, const dtPolyRef polyRef) const {
    const std::vector<Vec3> edges = getEdges(navQuery, polyRef);
    if (edges.size() < 3) {
        return {0.0f, 1.0f, 0.0f};
    }
    const Vec3 v0 = edges.at(0);
    const Vec3 v1 = edges.at(1);
    const Vec3 v2 = edges.at(2);

    const Vec3 vec1 = v1 - v0;
    const Vec3 vec2 = v2 - v0;
    const Vec3 cross = vec1.Cross(vec2);
    return cross.GetUnitVec();
}

Vec3 RecastAdapter::calculateCentroid(dtNavMeshQuery* navQuery, const dtPolyRef polyRef) const {
    const std::vector<Vec3> edges = getEdges(navQuery, polyRef);
    if (edges.empty()) {
        return {0.0f, 0.0f, 0.0f};
    }

    const Vec3 normal = calculateNormal(navQuery, polyRef);
    const Vec3 v0 = edges.at(0);
    const Vec3 v1 = edges.at(1);

    const Vec3 u = (v1 - v0).GetUnitVec();
    const Vec3 v = u.Cross(normal).GetUnitVec();

    std::vector<Vec3> mappedPoints;
    for (auto edge : edges) {
        Vec3 relativePos = edge - v0;
        const float uCoord = relativePos.Dot(u);
        const float vCoord = relativePos.Dot(v);
        auto uvv = Vec3(uCoord, vCoord, 0.0);
        mappedPoints.push_back(uvv);
    }
    float sum = 0;
    for (int i = 0; i < mappedPoints.size(); i++) {
        const int nextI = (i + 1) % mappedPoints.size();
        sum += mappedPoints[i].X * mappedPoints[nextI].Y - mappedPoints[nextI].X * mappedPoints[i].Y;
    }

    float sumX = 0;
    float sumY = 0;
    for (int i = 0; i < mappedPoints.size(); i++) {
        const int nextI = (i + 1) % mappedPoints.size();
        const float x0 = mappedPoints[i].X;
        const float x1 = mappedPoints[nextI].X;
        const float y0 = mappedPoints[i].Y;
        const float y1 = mappedPoints[nextI].Y;

        const float doubleArea = (x0 * y1) - (x1 * y0);
        sumX += (x0 + x1) * doubleArea;
        sumY += (y0 + y1) * doubleArea;
    }

    if (constexpr float areaEpsilon = 1e-9f; std::abs(sum) < areaEpsilon * 2.0f) {
        Logger::log(NK_WARN,
            ("calculateCentroid: Polygon " + std::to_string(polyRef) + " has near-zero area (" +
                std::to_string(sum / 2.0f) + "). Returning average of vertices.")
                .c_str());
        Vec3 averagePos(0.0f, 0.0f, 0.0f);
        if (edges.empty()) {
            return averagePos;
        }
        for (const auto& edge : edges) {
            averagePos = averagePos + edge;
        }
        return averagePos / static_cast<float>(edges.size());
    }

    const float cu = sumX / (3.0 * sum);
    const float cv = sumY / (3.0 * sum);

    const auto cucv = Vec3(1, cu, cv);
    const auto xuv = Vec3(v0.X, u.X, v.X);
    const auto yuv = Vec3(v0.Y, u.Y, v.Y);
    const auto zuv = Vec3(v0.Z, u.Z, v.Z);
    float x = xuv.Dot(cucv);
    float y = yuv.Dot(cucv);
    float z = zuv.Dot(cucv);
    return {x, y, z};
}

const dtNavMesh* RecastAdapter::getNavMesh() const {
    return sample->getNavMesh();
}

/**
 * Find up to the maxPolys closest polys within the specified radius of the position which are reachable from the
 * starting area.
 * @param navpowerPos
 * @param start
 * @param maxPolys
 * @return
 */
std::vector<dtPolyRef> RecastAdapter::getClosestReachablePolys(
    dtNavMeshQuery* navQuery, const Vec3 navpowerPos, const dtPolyRef start, const int maxPolys) const {
    std::vector<dtPolyRef> polys;
    const dtNavMesh* navMesh = sample->getNavMesh();
    if (!navMesh || !navQuery || start == 0) {
        return polys;
    }

    constexpr float radius = 3.0f;
    const Vec3 recastPos = convertFromNavPowerToRecast(navpowerPos);
    const float centerRecastPos[3] = {recastPos.X, recastPos.Y, recastPos.Z};

    dtPolyRef tempPolys[10];
    int actualPolyCount = 0;
    constexpr float halfExtents[3] = {radius, radius, radius};

    if (const dtStatus status =
            navQuery->queryPolygons(centerRecastPos, halfExtents, filter, tempPolys, &actualPolyCount, maxPolys);
        dtStatusFailed(status)) {
        Logger::log(NK_ERROR, "GetClosestReachableAreas failed to query polygons.");
        return polys;
    }
    if (actualPolyCount == 0) {
        return polys;
    }

    for (int i = 0; i < actualPolyCount; ++i) {
        dtPolyRef endRef = tempPolys[i];
        if (start == endRef) {
            polys.push_back(endRef);
            continue;
        }

        float endRecastPos[3];
        if (dtStatusFailed(navQuery->closestPointOnPoly(endRef, centerRecastPos, endRecastPos, nullptr))) {
            continue;
        }
        float startRecastPos[3];
        if (dtStatusFailed(navQuery->closestPointOnPoly(start, centerRecastPos, startRecastPos, nullptr))) {
            continue;
        }

        int pathCount = 0;
        dtPolyRef path[128];

        if (const dtStatus pathStatus =
                navQuery->findPath(start, endRef, startRecastPos, endRecastPos, filter, path, &pathCount, 128);
            dtStatusSucceed(pathStatus) && pathCount > 0 && path[pathCount - 1] == endRef) {
            polys.push_back(endRef);
        }
    }

    return polys;
}

/**
 * Find up to the maxNumPolys closest areas within the specified radius of the position.
 * @param navPowerPos
 * @param maxPolys
 * @return
 */
std::vector<dtPolyRef> RecastAdapter::getClosestPolys(
    dtNavMeshQuery* navQuery, const Vec3 navPowerPos, const int maxPolys) const {
    std::vector<dtPolyRef> polys;
    const dtNavMesh* navMesh = sample->getNavMesh();
    if (!navMesh || !navQuery) {
        return polys;
    }

    constexpr float radius = 1.0f;
    const Vec3 recastPos = convertFromNavPowerToRecast(navPowerPos);
    const float centerRecastPos[3] = {recastPos.X, recastPos.Y, recastPos.Z};

    dtPolyRef tempPolys[20];
    int actualPolyCount = 0;
    constexpr float halfExtents[3] = {radius, radius, radius};

    if (const dtStatus status =
            navQuery->queryPolygons(centerRecastPos, halfExtents, filter, tempPolys, &actualPolyCount, maxPolys);
        dtStatusFailed(status)) {
        Logger::log(NK_ERROR, "getClosestPolys failed to query polygons.");
        return polys;
    }

    for (int i = 0; i < actualPolyCount; ++i) {
        polys.push_back(tempPolys[i]);
    }
    return polys;
}
