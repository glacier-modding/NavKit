#include "../include/NavKit/util/GfxMapLayerSwf.h"

#include <iostream>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
        return GfxMapLayerSwf::createMapLayerSwfXml(templateXml, layerCount);
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
        check(GfxMapLayerSwf::sliceName(3) == "SLICE_03" && GfxMapLayerSwf::sliceName(12) == "SLICE_12",
            "slice names use two-digit layer numbers");
    }
} // namespace

int main(const int argc, char** argv) {
    try {
        if (argc == 5 && std::string_view(argv[1]) == "--generate") {
            std::ifstream input(argv[2], std::ios::binary);
            const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            if (!input && source.empty()) {
                throw std::runtime_error("could not read input XML");
            }
            const std::string generated =
                GfxMapLayerSwf::createMapLayerSwfXml(source, static_cast<size_t>(std::stoull(argv[4])));
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
        testFiveMapLayers();
        testFewerMapLayers();
        testValidationRejectsOtherFormats();
        testBackgroundMapSpaceNames();
        std::cout << "GFX builder tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "GFX builder tests failed: " << exception.what() << '\n';
        return 1;
    }
}
