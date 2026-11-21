#include "../include/NavKit/util/GfxMapAlignment.h"
#include "../include/NavKit/util/GfxMapLayerSwf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <pugixml.hpp>

namespace {
    void check(const bool condition, const std::string_view message) {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    // A cut-down map GFX template in FFDec's XML format, laid out like polarbear005.gfx: two frameless map-layer
    // sprites bound to "_Pkg.SLICE_NN_ZMapView" classes, each defined in its own ABC script.
    const std::string templateXml = R"xml(
<swf gfx="true" frameCount="1" hasEndTag="true">
  <tags>
    <item type="ExporterInfo" swfName="_Pkg"/>
    <item type="FileAttributesTag" actionScript3="true"/>
    <item type="FrameLabelTag" name="_Pkg_Main"/>
    <item type="DefineShape3Tag" shapeId="42">
      <shapeBounds Xmin="7600" Ymin="7600" Xmax="10400" Ymax="10400" nbits="15"/>
    </item>
    <item type="DefineSpriteTag" spriteId="43" frameCount="0">
      <subTags>
        <item type="PlaceObject2Tag" characterId="42" depth="1">
          <matrix scaleX="1.0" scaleY="1.0" translateX="160" translateY="320"/>
        </item>
      </subTags>
    </item>
    <item type="DefineShape3Tag" shapeId="44">
      <shapeBounds Xmin="7600" Ymin="7600" Xmax="10400" Ymax="10400" nbits="15"/>
    </item>
    <item type="DefineSpriteTag" spriteId="45" frameCount="0">
      <subTags>
        <item type="PlaceObject2Tag" characterId="44" depth="1">
          <matrix scaleX="1.0" scaleY="1.0" translateX="0" translateY="0"/>
        </item>
      </subTags>
    </item>
    <item type="ExportAssetsTag">
      <tags><item>45</item><item>43</item></tags>
      <names><item>_Pkg.SLICE_01_ZMapView</item><item>_Pkg.SLICE_00_ZMapView</item></names>
    </item>
    <item type="DoABC2Tag" flags="1" name="frame1">
      <abc type="ABC">
        <constants type="AVM2ConstantPool">
          <constant_string>
            <item isNull="true"/>
            <item/>
            <item>_Pkg</item>
            <item>_Pkg_Main</item>
            <item>SLICE_00</item>
            <item>SLICE_00_ZMapView</item>
            <item>SLICE_01</item>
            <item>SLICE_01_ZMapView</item>
            <item>_Pkg:SLICE_00</item>
            <item>SLICE_00 setLayer layer and total </item>
          </constant_string>
          <constant_namespace>
            <item isNull="true"/>
            <item type="Namespace" kind="22" name_index="1"/>
            <item type="Namespace" kind="22" name_index="2"/>
          </constant_namespace>
          <constant_multiname>
            <item isNull="true"/>
            <item type="Multiname" kind="7" name_index="3" namespace_index="1"/>
            <item type="Multiname" kind="7" name_index="4" namespace_index="2"/>
            <item type="Multiname" kind="7" name_index="5" namespace_index="2"/>
            <item type="Multiname" kind="7" name_index="6" namespace_index="2"/>
            <item type="Multiname" kind="7" name_index="7" namespace_index="2"/>
          </constant_multiname>
        </constants>
        <script_info>
          <item type="ScriptInfo" init_index="0"><traits><traits>
            <item type="TraitClass" class_info="0" name_index="1"/>
          </traits></traits></item>
          <item type="ScriptInfo" init_index="1"><traits><traits>
            <item type="TraitClass" class_info="1" name_index="4"/>
          </traits></traits></item>
          <item type="ScriptInfo" init_index="2"><traits><traits>
            <item type="TraitClass" class_info="2" name_index="2"/>
          </traits></traits></item>
          <item type="ScriptInfo" init_index="3"><traits><traits>
            <item type="TraitClass" class_info="3" name_index="5"/>
          </traits></traits></item>
          <item type="ScriptInfo" init_index="4"><traits><traits>
            <item type="TraitClass" class_info="4" name_index="3"/>
          </traits></traits></item>
        </script_info>
      </abc>
    </item>
    <item type="SymbolClassTag">
      <tags><item>43</item><item>45</item><item>0</item></tags>
      <names><item>_Pkg.SLICE_00_ZMapView</item><item>_Pkg.SLICE_01_ZMapView</item><item>_Pkg_Main</item></names>
    </item>
    <item type="ShowFrameTag"/>
  </tags>
</swf>)xml";

    std::string generate(const size_t layerCount) {
        return GfxMapLayerSwf::createMapLayerSwfXml(templateXml, layerCount, 900.0, 900.0);
    }

    pugi::xml_document parse(const std::string& xml) {
        pugi::xml_document document;
        check(static_cast<bool>(document.load_string(xml.c_str())), "generated map-layer XML is invalid");
        return document;
    }

    std::vector<std::pair<unsigned int, std::string>> namePairs(const pugi::xml_node& tag) {
        std::vector<std::pair<unsigned int, std::string>> pairs;
        for (auto id = tag.child("tags").child("item"), name = tag.child("names").child("item"); id && name;
            id = id.next_sibling("item"), name = name.next_sibling("item")) {
            pairs.emplace_back(id.text().as_uint(), name.text().as_string());
        }
        return pairs;
    }

    void testFiveMapLayers() {
        const std::string xml = generate(5);
        pugi::xml_document document = parse(xml);
        const pugi::xml_node tags = document.child("swf").child("tags");
        size_t shapeCount = 0;
        size_t spriteCount = 0;
        size_t abcCount = 0;
        std::set<std::string> clonedClasses;
        pugi::xml_node exportAssets;
        pugi::xml_node symbolClass;
        pugi::xml_node fileAttributes;
        unsigned int previousCharacterId = 0;
        for (pugi::xml_node item : tags.children("item")) {
            const std::string_view type = item.attribute("type").as_string();
            if (type == "DefineShape3Tag") {
                ++shapeCount;
                const unsigned int shapeId = item.attribute("shapeId").as_uint();
                check(shapeId == previousCharacterId + 1, "map-layer shapes must directly precede their sprite");
                previousCharacterId = shapeId;
            } else if (type == "DefineSpriteTag") {
                const size_t layer = spriteCount++;
                const unsigned int spriteId = item.attribute("spriteId").as_uint();
                check(spriteId == GfxMapLayerSwf::spriteId(layer) && spriteId == previousCharacterId + 1,
                    "map-layer sprite IDs must follow their shapes");
                previousCharacterId = spriteId;
                check(item.attribute("frameCount").as_uint(1) == 0,
                    "map-layer sprites must be frameless like the game's map layers");
                size_t placements = 0;
                for (pugi::xml_node subTag : item.child("subTags").children("item")) {
                    check(std::string_view(subTag.attribute("type").as_string()) == "PlaceObject2Tag",
                        "map-layer sprites must only place shapes, with no ShowFrame");
                    const unsigned int expectedShape = placements == 0 ? GfxMapLayerSwf::backgroundShapeId(layer)
                                                                       : GfxMapLayerSwf::layerShapeId(layer);
                    check(subTag.attribute("characterId").as_uint() == expectedShape,
                        "map-layer sprite does not place its own background and map shapes");
                    check(subTag.attribute("depth").as_uint() == ++placements, "map-layer placements have bad depths");
                    const pugi::xml_node matrix = subTag.child("matrix");
                    check(matrix.attribute("scaleX").as_double() == 1.0 &&
                            matrix.attribute("scaleY").as_double() == 1.0 &&
                            matrix.attribute("translateX").as_int() == 0 &&
                            matrix.attribute("translateY").as_int() == 0,
                        "map-layer shapes must be placed with an identity matrix");
                }
                check(placements == 2, "each map-layer sprite must place a background and a map shape");
            } else if (type == "DoABC2Tag") {
                ++abcCount;
                if (abcCount > 1) {
                    std::string strings;
                    for (pugi::xml_node string :
                        item.child("abc").child("constants").child("constant_string").children("item")) {
                        strings += string.text().as_string();
                        strings += '\n';
                    }
                    check(strings.find("SLICE_00") == std::string::npos, "a cloned slice ABC still names SLICE_00");
                    size_t scripts = 0;
                    for ([[maybe_unused]] pugi::xml_node script :
                        item.child("abc").child("script_info").children("item")) {
                        ++scripts;
                    }
                    check(scripts == 2, "a cloned slice ABC must only define its slice and map-view classes");
                    for (size_t layer = 2; layer < 5; ++layer) {
                        const std::string slice = GfxMapLayerSwf::sliceName(layer);
                        if (strings.find(slice + " setLayer") != std::string::npos) {
                            clonedClasses.insert(slice);
                        }
                    }
                }
            } else if (type == "ExportAssetsTag") {
                exportAssets = item;
            } else if (type == "SymbolClassTag") {
                symbolClass = item;
            } else if (type == "FileAttributesTag") {
                fileAttributes = item;
            }
        }
        check(shapeCount == 10 && spriteCount == 5, "five map layers must create five sprites with two shapes each");
        check(abcCount == 4 && clonedClasses.size() == 3,
            "layers beyond the template's slices must get their own ActionScript classes");
        check(fileAttributes && fileAttributes.attribute("actionScript3").as_bool(),
            "the generated GFX must keep the template's ActionScript 3 classes");
        const auto exports = namePairs(exportAssets);
        const auto symbols = namePairs(symbolClass);
        check(exports.size() == 5 && symbols.size() == 6, "every map layer must be exported and bound to a class");
        for (size_t layer = 0; layer < 5; ++layer) {
            const std::string expected = "_Pkg." + GfxMapLayerSwf::sliceName(layer) + "_ZMapView";
            check(exports[layer] == std::make_pair(GfxMapLayerSwf::spriteId(layer), expected),
                "a map layer is not exported under its class name");
            check(symbols[layer] == std::make_pair(GfxMapLayerSwf::spriteId(layer), expected),
                "a map layer is not bound to its class");
        }
        check(symbols.back() == std::make_pair(0u, std::string("_Pkg_Main")),
            "the document class must stay bound to the main timeline");
        const std::string error = GfxMapLayerSwf::validateMapLayerSwfXml(xml, 5);
        check(error.empty(), "the generated GFX shell must validate: " + error);
    }

    void testFewerMapLayers() {
        const std::string xml = generate(1);
        pugi::xml_document document = parse(xml);
        size_t spriteCount = 0;
        size_t abcCount = 0;
        for (pugi::xml_node item : document.child("swf").child("tags").children("item")) {
            spriteCount += std::string_view(item.attribute("type").as_string()) == "DefineSpriteTag";
            abcCount += std::string_view(item.attribute("type").as_string()) == "DoABC2Tag";
        }
        check(spriteCount == 1, "one map layer must create exactly one sprite");
        check(abcCount == 1, "layers covered by the template's classes must not add ActionScript");
        check(GfxMapLayerSwf::validateMapLayerSwfXml(xml, 1).empty(), "a one-layer GFX shell must validate");
        check(!GfxMapLayerSwf::validateMapLayerSwfXml(xml, 2).empty(), "validation must check the layer count");
    }

    void testValidationRejectsOtherFormats() {
        const auto replaceOnce = [](std::string text, const std::string_view from, const std::string_view to) {
            const size_t position = text.find(from);
            check(position != std::string::npos, "test fixture text not found");
            return text.replace(position, from.size(), to);
        };
        const std::string valid = generate(2);
        check(!GfxMapLayerSwf::validateMapLayerSwfXml(
                  replaceOnce(valid, R"(spriteId="6" frameCount="0")", R"(spriteId="6" frameCount="1")"), 2)
                  .empty(),
            "a map layer with its own frame must be rejected");
        check(!GfxMapLayerSwf::validateMapLayerSwfXml(
                  replaceOnce(valid, R"(actionScript3="true")", R"(actionScript3="false")"), 2)
                  .empty(),
            "a GFX without ActionScript 3 must be rejected");
        check(!GfxMapLayerSwf::validateMapLayerSwfXml(
                  replaceOnce(valid, "<item>_Pkg_Main</item>", "<item>Other</item>"), 2)
                  .empty(),
            "a GFX whose document class is not defined must be rejected");
        check(!GfxMapLayerSwf::validateMapLayerSwfXml(replaceOnce(valid, R"(scaleX="1.0")", R"(scaleX="0.5")"), 2)
                  .empty(),
            "a scaled map-layer placement must be rejected");
    }

    void testBackgroundMapSpaceNames() {
        check(GfxMapLayerSwf::isBackgroundMapSpaceName("Map Background"), "background configs must be detected");
        check(GfxMapLayerSwf::isBackgroundMapSpaceName("BACKGROUND_volume"), "detection must ignore case");
        check(!GfxMapLayerSwf::isBackgroundMapSpaceName("Level 01"), "layer configs are not backgrounds");
        check(GfxMapLayerSwf::isBackgroundMapSpaceName("MapSpaceConfig_SAT"), "satellite configs are backgrounds");
        check(GfxMapLayerSwf::isBackgroundMapSpaceName("SAT"), "a config named SAT is a background");
        check(!GfxMapLayerSwf::isBackgroundMapSpaceName("MapSpaceConfig_SATURN"), "SAT must end the name");
        check(!GfxMapLayerSwf::isBackgroundMapSpaceName("MapSpaceConfig_S0"), "slice configs are not backgrounds");
        check(GfxMapLayerSwf::sliceName(3) == "SLICE_03" && GfxMapLayerSwf::sliceName(12) == "SLICE_12",
            "slice names use two-digit layer numbers");
    }

    // Wall rectangles (left, top, right, bottom) of a made-up floor plan, in metres on the map plane.
    const std::vector<std::array<float, 4>> planWalls = {
        {10, 10, 70, 11},
        {10, 10, 11, 50},
        {69, 10, 70, 35},
        {10, 49, 45, 50},
        {30, 10, 31, 30},
        {44, 30, 45, 50},
        {50, 35, 70, 36},
        {20, 30, 31, 31},
        {55, 20, 56, 26},
        {80, 5, 95, 18},
    };

    void testCanvasSizedShapes() {
        // FFDec fits imported SVGs into the shapes' bounds, so they must be the canvas, in twips.
        const pugi::xml_document document = parse(GfxMapLayerSwf::createMapLayerSwfXml(templateXml, 2, 1100.0, 600.0));
        size_t shapes = 0;
        for (const pugi::xml_node item : document.child("swf").child("tags").children("item")) {
            if (std::string_view(item.attribute("type").value()) != "DefineShape3Tag") {
                continue;
            }
            const pugi::xml_node bounds = item.child("shapeBounds");
            check(bounds.attribute("Xmin").as_int() == 0 && bounds.attribute("Ymin").as_int() == 0 &&
                    bounds.attribute("Xmax").as_int() == 22000 && bounds.attribute("Ymax").as_int() == 12000,
                "map shapes must span the canvas");
            check(bounds.attribute("nbits").as_int() >= 16, "the bounds must fit their bit width");
            ++shapes;
        }
        check(shapes == 4, "each layer has a background and a map shape");
        const auto size = GfxMapLayerSwf::svgCanvasSize(
            R"(<?xml version="1.0"?><svg xmlns="http://www.w3.org/2000/svg" width="1098.22" height="723.398" )"
            R"(viewBox="0 0 1098.22 723.398"><rect/></svg>)");
        check(size && std::abs(size->first - 1098.22) < 1e-6 && std::abs(size->second - 723.398) < 1e-6,
            "the canvas size comes from the SVG's viewBox");
        check(!GfxMapLayerSwf::svgCanvasSize("<html/>"), "a document without an SVG has no canvas size");
    }

    void testMapLayerPlacement() {
        // Draw the plan into an image at two pixels per metre whose top-left corner lies at (-123.5, 47) on the
        // map plane, then place it on the plan's wall outlines.
        const glm::vec2 corner(-123.5f, 47.0f);
        GfxMapAlignment::LayerImage image;
        image.width = 240;
        image.height = 160;
        image.pixelsPerMetre = 2.0f;
        image.rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / image.pixelsPerMetre;
                const float v = (static_cast<float>(y) + 0.5f) / image.pixelsPerMetre;
                const bool wall = std::ranges::any_of(planWalls, [&](const auto& rectangle) {
                    return u >= rectangle[0] && u <= rectangle[2] && v >= rectangle[1] && v <= rectangle[3];
                });
                unsigned char* pixel = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
                pixel[0] = wall ? 0x76 : 0xbe;
                pixel[1] = wall ? 0x7f : 0xc3;
                pixel[2] = wall ? 0x86 : 0xc6;
                pixel[3] = 255;
            }
        }
        std::vector<GfxMapAlignment::Segment> segments;
        for (const auto& [left, top, right, bottom] : planWalls) {
            const glm::vec2 a = corner + glm::vec2(left, top);
            const glm::vec2 b = corner + glm::vec2(right, top);
            const glm::vec2 c = corner + glm::vec2(right, bottom);
            const glm::vec2 d = corner + glm::vec2(left, bottom);
            segments.insert(segments.end(), {{a, b}, {b, c}, {c, d}, {d, a}});
        }
        const auto placement = GfxMapAlignment::placeLayer(image, segments);
        check(placement.has_value(), "the plan must be placed");
        check(std::abs(placement->topLeft.x - corner.x) <= 0.5f && std::abs(placement->topLeft.y - corner.y) <= 0.5f,
            "the plan must be placed where it was drawn");
        check(placement->confidence > 1.5f, "the placement must stand out from random placements");
    }

    void testSectionsAndFloorLevels() {
        // A 4 x 2 x 3 m box standing on z = 5 (game coordinates), as twelve triangles, and a floor at z = 1.
        const glm::vec3 low(0, 0, 5);
        const glm::vec3 high(4, 2, 8);
        const auto corner = [&](const int x, const int y, const int z) {
            return glm::vec3(x ? high.x : low.x, y ? high.y : low.y, z ? high.z : low.z);
        };
        std::vector<glm::vec3> triangles;
        const auto quad = [&](const glm::vec3 a, const glm::vec3 b, const glm::vec3 c, const glm::vec3 d) {
            triangles.insert(triangles.end(), {a, b, c, a, c, d});
        };
        quad(corner(0, 0, 0), corner(1, 0, 0), corner(1, 0, 1), corner(0, 0, 1));
        quad(corner(1, 0, 0), corner(1, 1, 0), corner(1, 1, 1), corner(1, 0, 1));
        quad(corner(1, 1, 0), corner(0, 1, 0), corner(0, 1, 1), corner(1, 1, 1));
        quad(corner(0, 1, 0), corner(0, 0, 0), corner(0, 0, 1), corner(0, 1, 1));
        quad(corner(0, 0, 1), corner(1, 0, 1), corner(1, 1, 1), corner(0, 1, 1));
        quad(corner(0, 0, 0), corner(0, 1, 0), corner(1, 1, 0), corner(1, 0, 0));
        quad({-10, -10, 1}, {10, -10, 1}, {10, 10, 1}, {-10, 10, 1});
        const auto segments = GfxMapAlignment::sectionSegments(triangles, 6.0f);
        check(segments.size() == 8, "a box cut through its sides gives two segments per side");
        float perimeter = 0.0f;
        for (const auto& [start, end] : segments) {
            perimeter += glm::length(end - start);
            check(start.y <= 0.0f && start.y >= -2.0f, "sections use the map plane's v = -y");
        }
        check(std::abs(perimeter - 12.0f) < 1e-3f, "the cut outlines the box");
        const auto levels = GfxMapAlignment::floorLevels(triangles, -100.0f, 100.0f, 2.0f);
        check(levels.size() == 1 && std::abs(levels.front() - 1.0f) < 0.2f, "the large floor is the level");
    }
} // namespace

int main(const int argc, char** argv) {
    try {
        if ((argc == 5 || argc == 7) && std::string_view(argv[1]) == "--generate") {
            std::ifstream input(argv[2], std::ios::binary);
            const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            if (!input && source.empty()) {
                throw std::runtime_error("could not read input XML");
            }
            const std::string generated =
                GfxMapLayerSwf::createMapLayerSwfXml(source, static_cast<size_t>(std::stoull(argv[4])),
                    argc == 7 ? std::stod(argv[5]) : 900.0, argc == 7 ? std::stod(argv[6]) : 900.0);
            std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
            output.write(generated.data(), static_cast<std::streamsize>(generated.size()));
            if (!output) {
                throw std::runtime_error("could not write generated XML");
            }
            return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "--validate") {
            std::ifstream input(argv[2], std::ios::binary);
            const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            const std::string error =
                GfxMapLayerSwf::validateMapLayerSwfXml(source, static_cast<size_t>(std::stoull(argv[3])));
            if (!error.empty()) {
                throw std::runtime_error(error);
            }
            std::cout << "GFX map layers are valid\n";
            return 0;
        }
        if (argc == 6 && std::string_view(argv[1]) == "--place") {
            // --place <raw RGBA file> <width> <height> <segments file: "u0 v0 u1 v1" per line, map plane metres>
            GfxMapAlignment::LayerImage image;
            image.width = std::stoi(argv[3]);
            image.height = std::stoi(argv[4]);
            std::ifstream pixels(argv[2], std::ios::binary);
            image.rgba.assign(std::istreambuf_iterator<char>(pixels), std::istreambuf_iterator<char>());
            check(image.rgba.size() == static_cast<size_t>(image.width) * image.height * 4, "RGBA size mismatch");
            std::ifstream lines(argv[5]);
            std::vector<GfxMapAlignment::Segment> segments;
            for (GfxMapAlignment::Segment segment;
                lines >> segment[0].x >> segment[0].y >> segment[1].x >> segment[1].y;) {
                segments.push_back(segment);
            }
            const auto placement = GfxMapAlignment::placeLayer(image, segments);
            check(placement.has_value(), "nothing to place");
            std::cout << placement->topLeft.x << ' ' << placement->topLeft.y << ' ' << placement->score << ' '
                      << placement->confidence << '\n';
            return 0;
        }
        testFiveMapLayers();
        testFewerMapLayers();
        testValidationRejectsOtherFormats();
        testBackgroundMapSpaceNames();
        testCanvasSizedShapes();
        testMapLayerPlacement();
        testSectionsAndFloorLevels();
        std::cout << "GFX builder tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "GFX builder tests failed: " << exception.what() << '\n';
        return 1;
    }
}
