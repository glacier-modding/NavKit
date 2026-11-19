#include "../include/NavKit/util/GfxMapLayerSwf.h"

#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <pugixml.hpp>

namespace {
    void check(const bool condition, const std::string_view message) {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    pugi::xml_document createLayerShell(const size_t layerCount) {
        const std::string source = R"xml(
<swf gfx="true" frameCount="1" hasEndTag="true">
  <tags>
    <item type="ExporterInfo"/>
    <item type="FileAttributesTag" actionScript3="true"/>
    <item type="DefineShape3Tag" shapeId="42"/>
    <item type="DefineSpriteTag" spriteId="43" frameCount="1">
      <subTags>
        <item type="PlaceObject2Tag" characterId="42" depth="3">
          <matrix scaleX="1" scaleY="1" translateX="160" translateY="320"/>
        </item>
        <item type="ShowFrameTag"/>
      </subTags>
    </item>
    <item type="ShowFrameTag"/>
  </tags>
</swf>)xml";
        const std::string output = GfxMapLayerSwf::createMapLayerSwfXml(source, layerCount);
        pugi::xml_document document;
        check(static_cast<bool>(document.load_string(output.c_str())), "generated map-layer XML is invalid");
        return document;
    }

    void testFiveMapLayers() {
        pugi::xml_document document = createLayerShell(5);
        const pugi::xml_node tags = document.child("swf").child("tags");
        size_t shapeCount = 0;
        size_t spriteCount = 0;
        std::string exportedNames;
        for (pugi::xml_node item : tags.children("item")) {
            const std::string_view type = item.attribute("type").as_string();
            shapeCount += type == "DefineShape3Tag";
            if (type == "DefineSpriteTag") {
                ++spriteCount;
                const pugi::xml_node placement = item.child("subTags").child("item");
                check(placement.attribute("depth").as_uint() == 1, "map-layer sprite placement has an invalid depth");
                check(placement.attribute("characterId").as_uint() > 0,
                    "map-layer sprite does not reference its generated shape");
                check(placement.child("matrix").attribute("scaleX").as_double() == 0.5,
                    "map-layer sprite is not scaled for the GFX view");
            }
            if (type == "ExportAssetsTag") {
                for (pugi::xml_node name : item.child("names").children("item")) {
                    exportedNames += name.text().as_string();
                    exportedNames += '\n';
                }
            }
        }
        check(shapeCount == 5 && spriteCount == 5, "five map-space configurations must create five sprites");
        for (size_t layer = 0; layer < 5; ++layer) {
            const std::string expected = "SLICE_0" + std::to_string(layer) + "_ZMapView";
            check(exportedNames.find(expected) != std::string::npos, "a generated layer export name is missing");
        }
        pugi::xml_node fileAttributes;
        for (pugi::xml_node item : tags.children("item")) {
            if (std::string_view(item.attribute("type").as_string()) == "FileAttributesTag") {
                fileAttributes = item;
                break;
            }
        }
        check(fileAttributes && !fileAttributes.attribute("actionScript3").as_bool(true),
            "the generated GFX shell must not require the template's fixed ActionScript layer list");
    }

    void testFewerMapLayers() {
        pugi::xml_document document = createLayerShell(2);
        const pugi::xml_node tags = document.child("swf").child("tags");
        size_t spriteCount = 0;
        for (pugi::xml_node item : tags.children("item")) {
            spriteCount += std::string_view(item.attribute("type").as_string()) == "DefineSpriteTag";
        }
        check(spriteCount == 2, "two map-space configurations must create exactly two sprites");
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
        testFiveMapLayers();
        testFewerMapLayers();
        std::cout << "GFX builder tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "GFX builder tests failed: " << exception.what() << '\n';
        return 1;
    }
}
