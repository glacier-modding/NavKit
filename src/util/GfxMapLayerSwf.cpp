#include "../../include/NavKit/util/GfxMapLayerSwf.h"

#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <pugixml.hpp>

namespace {
    pugi::xml_node findTag(const pugi::xml_node& tags, const std::string_view type) {
        for (pugi::xml_node item : tags.children("item")) {
            if (std::string_view(item.attribute("type").as_string()) == type) {
                return item;
            }
        }
        return {};
    }

    pugi::xml_document copyNode(const pugi::xml_node& node) {
        pugi::xml_document copy;
        if (!node || !copy.append_copy(node)) {
            throw std::runtime_error("Could not copy a GFX template tag.");
        }
        return copy;
    }
} // namespace

namespace GfxMapLayerSwf {
    std::string createMapLayerSwfXml(const std::string_view templateXml, const size_t layerCount) {
        if (layerCount == 0) {
            throw std::runtime_error("The loaded scene has no map-space configurations.");
        }
        if (layerCount > std::numeric_limits<unsigned int>::max() / 2) {
            throw std::runtime_error("The scene has too many map-space configurations for a GFX file.");
        }

        pugi::xml_document document;
        if (const pugi::xml_parse_result result = document.load_buffer(templateXml.data(), templateXml.size());
            !result) {
            throw std::runtime_error("Could not parse FFDec's GFX template XML: " + std::string(result.description()));
        }
        pugi::xml_node swf = document.child("swf");
        pugi::xml_node tags = swf.child("tags");
        const pugi::xml_node shapeTemplate = findTag(tags, "DefineShape3Tag");
        const pugi::xml_node spriteTemplate = findTag(tags, "DefineSpriteTag");
        const pugi::xml_node showFrame = findTag(tags, "ShowFrameTag");
        const pugi::xml_node fileAttributes = findTag(tags, "FileAttributesTag");
        if (!swf || !tags || !shapeTemplate || !spriteTemplate || !showFrame || !fileAttributes) {
            throw std::runtime_error("The GFX template is missing required shape, sprite, or frame tags.");
        }

        const pugi::xml_node placeTemplate = findTag(spriteTemplate.child("subTags"), "PlaceObject2Tag");
        if (!placeTemplate || !placeTemplate.child("matrix")) {
            throw std::runtime_error("The GFX template sprite has no replaceable shape placement.");
        }
        const pugi::xml_document shapeCopy = copyNode(shapeTemplate);
        const pugi::xml_document spriteCopy = copyNode(spriteTemplate);
        const pugi::xml_document placeCopy = copyNode(placeTemplate);
        const pugi::xml_document showFrameCopy = copyNode(showFrame);

        std::vector<pugi::xml_document> headerCopies;
        for (const std::string_view type :
            {"ExporterInfo", "FileAttributesTag", "MetadataTag", "SetBackgroundColorTag", "FrameLabelTag"}) {
            if (const pugi::xml_node item = findTag(tags, type)) {
                headerCopies.push_back(copyNode(item));
            }
        }

        tags.remove_children();
        for (const pugi::xml_document& header : headerCopies) {
            tags.append_copy(header.first_child());
        }
        findTag(tags, "FileAttributesTag").attribute("actionScript3").set_value(false);
        swf.attribute("frameCount").set_value(1);

        const unsigned int shapeBase = 1;
        const unsigned int spriteBase = static_cast<unsigned int>(layerCount) + 1;
        for (size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
            const unsigned int shapeId = shapeBase + static_cast<unsigned int>(layerIndex);
            const unsigned int spriteId = spriteBase + static_cast<unsigned int>(layerIndex);

            pugi::xml_node shape = tags.append_copy(shapeCopy.first_child());
            shape.attribute("shapeId").set_value(shapeId);

            pugi::xml_node sprite = tags.append_copy(spriteCopy.first_child());
            sprite.attribute("spriteId").set_value(spriteId);
            sprite.attribute("frameCount").set_value(1);
            pugi::xml_node subTags = sprite.child("subTags");
            subTags.remove_children();
            pugi::xml_node placeObject = subTags.append_copy(placeCopy.first_child());
            placeObject.attribute("characterId").set_value(shapeId);
            placeObject.attribute("depth").set_value(1);
            pugi::xml_node matrix = placeObject.child("matrix");
            matrix.attribute("scaleX").set_value(0.5);
            matrix.attribute("scaleY").set_value(0.5);
            matrix.attribute("translateX").set_value(0);
            matrix.attribute("translateY").set_value(0);
            subTags.append_copy(showFrameCopy.first_child());
        }

        pugi::xml_node exportAssets = tags.append_child("item");
        exportAssets.append_attribute("type") = "ExportAssetsTag";
        exportAssets.append_attribute("forceWriteAsLong") = "false";
        pugi::xml_node spriteIds = exportAssets.append_child("tags");
        pugi::xml_node spriteNames = exportAssets.append_child("names");
        for (size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
            const unsigned int spriteId = spriteBase + static_cast<unsigned int>(layerIndex);
            const std::string layerNumber = [&layerIndex] {
                std::ostringstream value;
                value.width(2);
                value.fill('0');
                value << layerIndex;
                return value.str();
            }();
            pugi::xml_node id = spriteIds.append_child("item");
            id.text().set(std::to_string(spriteId).c_str());
            pugi::xml_node name = spriteNames.append_child("item");
            name.text().set(("_NavKit.SLICE_" + layerNumber + "_ZMapView").c_str());
        }
        tags.append_copy(showFrameCopy.first_child());

        std::ostringstream output;
        document.save(output, "  ", pugi::format_default, pugi::encoding_utf8);
        return output.str();
    }
} // namespace GfxMapLayerSwf
