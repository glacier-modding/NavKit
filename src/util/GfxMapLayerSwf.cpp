#include "../../include/NavKit/util/GfxMapLayerSwf.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <pugixml.hpp>

namespace {
    constexpr std::string_view sliceSuffix = "_ZMapView";
    constexpr std::string_view templateSlice = "SLICE_00";

    pugi::xml_node findTag(const pugi::xml_node& tags, const std::string_view type) {
        for (pugi::xml_node item : tags.children("item")) {
            if (std::string_view(item.attribute("type").as_string()) == type) {
                return item;
            }
        }
        return {};
    }

    std::vector<pugi::xml_node> findTags(const pugi::xml_node& tags, const std::string_view type) {
        std::vector<pugi::xml_node> found;
        for (pugi::xml_node item : tags.children("item")) {
            if (std::string_view(item.attribute("type").as_string()) == type) {
                found.push_back(item);
            }
        }
        return found;
    }

    pugi::xml_document copyNode(const pugi::xml_node& node) {
        pugi::xml_document copy;
        if (!node || !copy.append_copy(node)) {
            throw std::runtime_error("Could not copy a GFX template tag.");
        }
        return copy;
    }

    void setAttribute(pugi::xml_node node, const char* name, const char* value) {
        pugi::xml_attribute attribute = node.attribute(name);
        if (!attribute) {
            attribute = node.append_attribute(name);
        }
        attribute.set_value(value);
    }

    void setAttribute(pugi::xml_node node, const char* name, const unsigned int value) {
        setAttribute(node, name, std::to_string(value).c_str());
    }

    // Splits "<package>.SLICE_NN_ZMapView" into its package and layer number.
    bool parseSliceClassName(const std::string_view name, std::string& package, size_t& layer) {
        const size_t slice = name.rfind(".SLICE_");
        if (slice == std::string_view::npos || !name.ends_with(sliceSuffix)) {
            return false;
        }
        const std::string_view digits = name.substr(slice + 7, name.size() - slice - 7 - sliceSuffix.size());
        if (digits.size() < 2 ||
            !std::ranges::all_of(digits, [](const char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            return false;
        }
        package = std::string(name.substr(0, slice));
        layer = std::stoul(std::string(digits));
        return true;
    }

    struct SymbolClassEntry {
        unsigned int characterId;
        std::string name;
    };

    std::vector<SymbolClassEntry> readNamePairs(const pugi::xml_node& tag) {
        std::vector<SymbolClassEntry> entries;
        for (auto id = tag.child("tags").child("item"), name = tag.child("names").child("item"); id && name;
            id = id.next_sibling("item"), name = name.next_sibling("item")) {
            entries.push_back({id.text().as_uint(), name.text().as_string()});
        }
        return entries;
    }

    void writeNamePairs(pugi::xml_node tag, const std::vector<SymbolClassEntry>& entries) {
        tag.remove_child("tags");
        tag.remove_child("names");
        pugi::xml_node ids = tag.append_child("tags");
        pugi::xml_node names = tag.append_child("names");
        for (const auto& [characterId, name] : entries) {
            ids.append_child("item").text().set(std::to_string(characterId).c_str());
            names.append_child("item").text().set(name.c_str());
        }
    }

    std::string replaceAll(std::string text, const std::string_view from, const std::string_view to) {
        for (size_t position = text.find(from); position != std::string::npos;
            position = text.find(from, position + to.size())) {
            text.replace(position, from.size(), to);
        }
        return text;
    }

    // Returns the class names defined by each script of an FFDec DoABC/DoABC2 XML tag, in script order.
    std::vector<std::string> scriptClassNames(const pugi::xml_node& abc) {
        const pugi::xml_node constants = abc.child("constants");
        std::vector<std::string> strings;
        for (pugi::xml_node item : constants.child("constant_string").children("item")) {
            strings.emplace_back(item.text().as_string());
        }
        std::vector<pugi::xml_node> multinames;
        for (pugi::xml_node item : constants.child("constant_multiname").children("item")) {
            multinames.push_back(item);
        }
        std::vector<std::string> names;
        for (pugi::xml_node script : abc.child("script_info").children("item")) {
            std::string name;
            for (pugi::xml_node trait : script.child("traits").child("traits").children("item")) {
                if (std::string_view(trait.attribute("type").as_string()) != "TraitClass") {
                    continue;
                }
                const size_t multiname = trait.attribute("name_index").as_uint();
                if (multiname < multinames.size()) {
                    const size_t string = multinames[multiname].attribute("name_index").as_uint();
                    if (string < strings.size()) {
                        name = strings[string];
                    }
                }
                break;
            }
            names.push_back(std::move(name));
        }
        return names;
    }

    // Makes a DoABC2 tag that defines only "<package>.SLICE_NN" and "<package>.SLICE_NN_ZMapView", by renaming the
    // template's SLICE_00 classes. The common base classes they extend resolve to the template's own ABC block.
    pugi::xml_document createSliceAbc(const pugi::xml_node& templateAbcTag, const size_t layerIndex) {
        pugi::xml_document copy = copyNode(templateAbcTag);
        pugi::xml_node tag = copy.first_child();
        const std::string slice = GfxMapLayerSwf::sliceName(layerIndex);
        setAttribute(tag, "name", ("NavKit_" + slice).c_str());
        pugi::xml_node abc = tag.child("abc");
        for (pugi::xml_node item : abc.child("constants").child("constant_string").children("item")) {
            const std::string text = item.text().as_string();
            if (text.find(templateSlice) != std::string::npos) {
                item.text().set(replaceAll(text, templateSlice, slice).c_str());
            }
        }

        const std::vector<std::string> classNames = scriptClassNames(abc);
        pugi::xml_node scripts = abc.child("script_info");
        std::vector<pugi::xml_node> removed;
        size_t kept = 0;
        size_t index = 0;
        for (pugi::xml_node script : scripts.children("item")) {
            const std::string& name = classNames[index++];
            if (name == slice || name == slice + std::string(sliceSuffix)) {
                ++kept;
            } else {
                removed.push_back(script);
            }
        }
        if (kept != 2) {
            throw std::runtime_error("The GFX template's ActionScript does not define " + std::string(templateSlice) +
                " and " + std::string(templateSlice) + std::string(sliceSuffix) + " in their own scripts.");
        }
        for (pugi::xml_node script : removed) {
            scripts.remove_child(script);
        }
        return copy;
    }

    // Replaces `from` with `to` in every attribute value and text of `node` and its descendants.
    void renameEverywhere(pugi::xml_node node, const std::string_view from, const std::string_view to) {
        for (pugi::xml_attribute attribute : node.attributes()) {
            if (const std::string value = attribute.value(); value.find(from) != std::string::npos) {
                attribute.set_value(replaceAll(value, from, to).c_str());
            }
        }
        for (pugi::xml_node child : node.children()) {
            if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
                if (const std::string value = child.value(); value.find(from) != std::string::npos) {
                    child.set_value(replaceAll(value, from, to).c_str());
                }
            } else {
                renameEverywhere(child, from, to);
            }
        }
    }

    std::string formatTwoDigits(const size_t value) {
        std::ostringstream text;
        text.width(2);
        text.fill('0');
        text << value;
        return text.str();
    }
} // namespace

namespace GfxMapLayerSwf {
    unsigned int backgroundShapeId(const size_t layerIndex) {
        return static_cast<unsigned int>(layerIndex * 3 + 1);
    }

    unsigned int layerShapeId(const size_t layerIndex) {
        return static_cast<unsigned int>(layerIndex * 3 + 2);
    }

    unsigned int spriteId(const size_t layerIndex) {
        return static_cast<unsigned int>(layerIndex * 3 + 3);
    }

    std::string sliceName(const size_t layerIndex) {
        return "SLICE_" + formatTwoDigits(layerIndex);
    }

    bool isBackgroundMapSpaceName(const std::string_view name) {
        std::string lower(name);
        std::ranges::transform(lower, lower.begin(), [](const unsigned char c) { return std::tolower(c); });
        // The game's scenes name it "...Background" or, with a satellite image, "..._SAT".
        return lower.find("background") != std::string::npos || lower == "sat" ||
            (lower.size() > 4 && lower.ends_with("_sat"));
    }

    std::string mapLayerPackage(const std::string_view className) {
        const size_t slice = className.rfind(".SLICE_");
        if (slice == std::string_view::npos || slice == 0) {
            return {};
        }
        const std::string_view digits = className.substr(slice + 7);
        if (digits.size() < 2 ||
            !std::ranges::all_of(digits, [](const char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            return {};
        }
        return std::string(className.substr(0, slice));
    }

    bool isValidMapPackageName(const std::string_view package) {
        const auto isIdentifierStart = [](const char c) {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
        };
        return !package.empty() && isIdentifierStart(package.front()) &&
            std::ranges::all_of(package,
                [&](const char c) { return isIdentifierStart(c) || std::isdigit(static_cast<unsigned char>(c)); });
    }

    std::optional<std::pair<double, double>> svgCanvasSize(const std::string_view svg) {
        const size_t start = svg.find("<svg");
        if (start == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view tag = svg.substr(start, svg.find('>', start) - start);
        const auto attribute = [&tag](const std::string_view name) -> std::optional<std::string> {
            const std::string key = " " + std::string(name) + "=\"";
            const size_t position = tag.find(key);
            if (position == std::string_view::npos) {
                return std::nullopt;
            }
            const size_t valueStart = position + key.size();
            const size_t valueEnd = tag.find('"', valueStart);
            if (valueEnd == std::string_view::npos) {
                return std::nullopt;
            }
            return std::string(tag.substr(valueStart, valueEnd - valueStart));
        };
        try {
            if (const auto viewBox = attribute("viewBox")) {
                std::istringstream values(*viewBox);
                double x = 0;
                double y = 0;
                double width = 0;
                double height = 0;
                if (values >> x >> y >> width >> height && width > 0 && height > 0) {
                    return std::pair{width, height};
                }
            }
            const auto width = attribute("width");
            const auto height = attribute("height");
            if (width && height && std::stod(*width) > 0 && std::stod(*height) > 0) {
                return std::pair{std::stod(*width), std::stod(*height)};
            }
        } catch (const std::exception&) {
        }
        return std::nullopt;
    }

    std::string createMapLayerSwfXml(const std::string_view templateXml, const size_t layerCount,
        const double canvasWidth, const double canvasHeight, const std::string_view newPackage) {
        if (layerCount == 0) {
            throw std::runtime_error("The loaded scene has no map layers.");
        }
        // Shape bounds are signed twips; keep well inside FFDec's 31-bit limit.
        if (!(canvasWidth > 0) || !(canvasHeight > 0) || canvasWidth > 1e6 || canvasHeight > 1e6) {
            throw std::runtime_error("The map canvas size is not valid.");
        }
        // Character IDs are 16-bit; each layer uses three of them.
        if (layerCount > (std::numeric_limits<std::uint16_t>::max() - 1) / 3) {
            throw std::runtime_error("The scene has too many map layers for a GFX file.");
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
        const pugi::xml_node abcTemplate = findTag(tags, "DoABC2Tag");
        const pugi::xml_node symbolClassTemplate = findTag(tags, "SymbolClassTag");
        const pugi::xml_node exportTemplate = findTag(tags, "ExportAssetsTag");
        if (!swf || !tags || !shapeTemplate || !shapeTemplate.child("shapeBounds") || !spriteTemplate || !showFrame ||
            !fileAttributes) {
            throw std::runtime_error("The GFX template is missing required shape, sprite, or frame tags.");
        }
        if (!abcTemplate || !symbolClassTemplate || !exportTemplate) {
            throw std::runtime_error("The GFX template is missing its ActionScript, SymbolClass, or export tags.");
        }
        const pugi::xml_node placeTemplate = findTag(spriteTemplate.child("subTags"), "PlaceObject2Tag");
        if (!placeTemplate || !placeTemplate.child("matrix")) {
            throw std::runtime_error("The GFX template sprite has no replaceable shape placement.");
        }

        // The template binds its map sprites to "<package>.SLICE_NN_ZMapView" and the main timeline (ID 0) to the
        // document class; the generated GFX keeps that ActionScript and binding.
        std::string package;
        std::string mainClass;
        std::set<size_t> templateSlices;
        for (const auto& [characterId, name] : readNamePairs(symbolClassTemplate)) {
            std::string slicePackage;
            size_t layer = 0;
            if (characterId == 0) {
                mainClass = name;
            } else if (parseSliceClassName(name, slicePackage, layer)) {
                if (!package.empty() && package != slicePackage) {
                    throw std::runtime_error("The GFX template binds map layers from more than one package.");
                }
                package = slicePackage;
                templateSlices.insert(layer);
            }
        }
        if (package.empty() || mainClass.empty() || !templateSlices.contains(0)) {
            throw std::runtime_error("The GFX template does not bind a document class and SLICE_00 map layer.");
        }
        if (!newPackage.empty() && newPackage != package) {
            if (!isValidMapPackageName(newPackage)) {
                throw std::runtime_error(
                    "The map class package '" + std::string(newPackage) + "' is not an ActionScript identifier.");
            }
            // The package names the ActionScript namespace and classes, the SymbolClass and export names, the
            // document class and the ABC tag, so it is renamed wherever the template has it.
            renameEverywhere(swf, package, newPackage);
            mainClass = replaceAll(mainClass, package, newPackage);
            package = std::string(newPackage);
        }

        const pugi::xml_document shapeCopy = copyNode(shapeTemplate);
        const pugi::xml_document spriteCopy = copyNode(spriteTemplate);
        const pugi::xml_document placeCopy = copyNode(placeTemplate);
        const pugi::xml_document showFrameCopy = copyNode(showFrame);
        const pugi::xml_document abcCopy = copyNode(abcTemplate);
        const pugi::xml_document symbolClassCopy = copyNode(symbolClassTemplate);
        const pugi::xml_document exportCopy = copyNode(exportTemplate);
        pugi::xml_node generatedShapeBounds = shapeCopy.child("item").child("shapeBounds");
        // FFDec's -importShapes fits each SVG into these bounds, so they are the canvas at one unit per metre.
        const long long widthTwips = std::llround(canvasWidth * 20.0);
        const long long heightTwips = std::llround(canvasHeight * 20.0);
        int bits = 1;
        while ((1LL << (bits - 1)) <= std::max(widthTwips, heightTwips)) {
            ++bits;
        }
        generatedShapeBounds.attribute("Xmin").set_value(0);
        generatedShapeBounds.attribute("Ymin").set_value(0);
        generatedShapeBounds.attribute("Xmax").set_value(widthTwips);
        generatedShapeBounds.attribute("Ymax").set_value(heightTwips);
        generatedShapeBounds.attribute("nbits").set_value(bits);

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
        findTag(tags, "FileAttributesTag").attribute("actionScript3").set_value(true);
        swf.attribute("frameCount").set_value(1);

        const auto addPlacement = [&placeCopy](pugi::xml_node subTags, const unsigned int characterId,
                                      const unsigned int depth) {
            pugi::xml_node placement = subTags.append_copy(placeCopy.first_child());
            placement.attribute("characterId").set_value(characterId);
            placement.attribute("depth").set_value(depth);
            pugi::xml_node matrix = placement.child("matrix");
            matrix.attribute("scaleX").set_value("1.0");
            matrix.attribute("scaleY").set_value("1.0");
            matrix.attribute("translateX").set_value(0);
            matrix.attribute("translateY").set_value(0);
        };

        std::vector<SymbolClassEntry> layerClasses;
        for (size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
            pugi::xml_node background = tags.append_copy(shapeCopy.first_child());
            background.attribute("shapeId").set_value(backgroundShapeId(layerIndex));
            pugi::xml_node shape = tags.append_copy(shapeCopy.first_child());
            shape.attribute("shapeId").set_value(layerShapeId(layerIndex));

            // Like the game's map GFX files, a layer sprite has no frames of its own: just its shape placements.
            pugi::xml_node sprite = tags.append_copy(spriteCopy.first_child());
            sprite.attribute("spriteId").set_value(spriteId(layerIndex));
            sprite.attribute("frameCount").set_value(0);
            pugi::xml_node subTags = sprite.child("subTags");
            subTags.remove_children();
            addPlacement(subTags, backgroundShapeId(layerIndex), 1);
            addPlacement(subTags, layerShapeId(layerIndex), 2);
            layerClasses.push_back(
                {spriteId(layerIndex), package + "." + sliceName(layerIndex) + std::string(sliceSuffix)});
        }

        pugi::xml_node exportAssets = tags.append_copy(exportCopy.first_child());
        writeNamePairs(exportAssets, layerClasses);
        tags.append_copy(abcCopy.first_child());
        for (size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
            if (!templateSlices.contains(layerIndex)) {
                const pugi::xml_document sliceAbc = createSliceAbc(abcCopy.first_child(), layerIndex);
                tags.append_copy(sliceAbc.first_child());
            }
        }
        std::vector<SymbolClassEntry> symbolClasses = layerClasses;
        symbolClasses.push_back({0, mainClass});
        pugi::xml_node symbolClass = tags.append_copy(symbolClassCopy.first_child());
        writeNamePairs(symbolClass, symbolClasses);
        tags.append_copy(showFrameCopy.first_child());

        std::ostringstream output;
        document.save(output, "  ", pugi::format_default, pugi::encoding_utf8);
        return output.str();
    }

    std::string validateMapLayerSwfXml(const std::string_view gfxXml, const size_t layerCount) {
        pugi::xml_document document;
        if (const pugi::xml_parse_result result = document.load_buffer(gfxXml.data(), gfxXml.size()); !result) {
            return "Could not parse FFDec's GFX XML: " + std::string(result.description());
        }
        const pugi::xml_node swf = document.child("swf");
        const pugi::xml_node tags = swf.child("tags");
        if (!swf || !tags) {
            return "The GFX XML has no tags.";
        }
        if (!swf.attribute("gfx").as_bool()) {
            return "The output is not a GFX file.";
        }
        const pugi::xml_node fileAttributes = findTag(tags, "FileAttributesTag");
        if (!fileAttributes || !fileAttributes.attribute("actionScript3").as_bool()) {
            return "The GFX file does not enable ActionScript 3.";
        }
        const std::vector<pugi::xml_node> abcTags = findTags(tags, "DoABC2Tag");
        const pugi::xml_node symbolClass = findTag(tags, "SymbolClassTag");
        const pugi::xml_node exportAssets = findTag(tags, "ExportAssetsTag");
        if (abcTags.empty() || !symbolClass || !exportAssets) {
            return "The GFX file is missing its ActionScript, SymbolClass, or export tags.";
        }

        std::set<std::string> definedClasses;
        for (const pugi::xml_node& abcTag : abcTags) {
            const pugi::xml_node abc = abcTag.child("abc");
            const pugi::xml_node constants = abc.child("constants");
            std::vector<std::string> strings;
            for (pugi::xml_node item : constants.child("constant_string").children("item")) {
                strings.emplace_back(item.text().as_string());
            }
            std::vector<pugi::xml_node> namespaces;
            for (pugi::xml_node item : constants.child("constant_namespace").children("item")) {
                namespaces.push_back(item);
            }
            std::vector<pugi::xml_node> multinames;
            for (pugi::xml_node item : constants.child("constant_multiname").children("item")) {
                multinames.push_back(item);
            }
            for (pugi::xml_node script : abc.child("script_info").children("item")) {
                for (pugi::xml_node trait : script.child("traits").child("traits").children("item")) {
                    if (std::string_view(trait.attribute("type").as_string()) != "TraitClass") {
                        continue;
                    }
                    const size_t multinameIndex = trait.attribute("name_index").as_uint();
                    if (multinameIndex >= multinames.size()) {
                        continue;
                    }
                    const pugi::xml_node multiname = multinames[multinameIndex];
                    const size_t nameIndex = multiname.attribute("name_index").as_uint();
                    const size_t namespaceIndex = multiname.attribute("namespace_index").as_uint();
                    if (nameIndex >= strings.size() || namespaceIndex >= namespaces.size()) {
                        continue;
                    }
                    const size_t packageIndex = namespaces[namespaceIndex].attribute("name_index").as_uint();
                    const std::string packageName = packageIndex < strings.size() ? strings[packageIndex] : "";
                    definedClasses.insert(
                        packageName.empty() ? strings[nameIndex] : packageName + "." + strings[nameIndex]);
                }
            }
        }

        std::map<unsigned int, std::string> boundClasses;
        for (const auto& [characterId, name] : readNamePairs(symbolClass)) {
            boundClasses[characterId] = name;
        }
        std::map<unsigned int, std::string> exportedNames;
        for (const auto& [characterId, name] : readNamePairs(exportAssets)) {
            exportedNames[characterId] = name;
        }
        const auto mainClass = boundClasses.find(0);
        if (mainClass == boundClasses.end() || !definedClasses.contains(mainClass->second)) {
            return "The GFX file does not bind its document class.";
        }

        std::map<unsigned int, pugi::xml_node> sprites;
        std::set<unsigned int> shapes;
        size_t layerSprites = 0;
        for (pugi::xml_node item : tags.children("item")) {
            const std::string_view type = item.attribute("type").as_string();
            if (type.starts_with("DefineShape")) {
                shapes.insert(item.attribute("shapeId").as_uint());
            } else if (type == "DefineSpriteTag") {
                sprites[item.attribute("spriteId").as_uint()] = item;
            }
        }

        std::string package;
        for (const auto& [characterId, className] : boundClasses) {
            std::string classPackage;
            size_t layer = 0;
            if (characterId == 0 || !parseSliceClassName(className, classPackage, layer)) {
                continue;
            }
            ++layerSprites;
            const std::string slice = sliceName(layer);
            if (layer >= layerCount) {
                return "The GFX file binds an unexpected map layer " + slice + ".";
            }
            if (!package.empty() && classPackage != package) {
                return "The GFX file binds map layers from more than one package.";
            }
            package = classPackage;
            if (!definedClasses.contains(className) || !definedClasses.contains(classPackage + "." + slice)) {
                return "The GFX file's ActionScript does not define the classes for map layer " + slice + ".";
            }
            const auto exported = exportedNames.find(characterId);
            if (exported == exportedNames.end() || exported->second != className) {
                return "The GFX file does not export map layer " + slice + " under its class name.";
            }
            const auto sprite = sprites.find(characterId);
            if (sprite == sprites.end()) {
                return "The GFX file binds map layer " + slice + " to a missing sprite.";
            }
            if (sprite->second.attribute("frameCount").as_uint(1) != 0) {
                return "Map layer " + slice + " is not a frameless sprite like the game's map layers.";
            }
            unsigned int expectedDepth = 1;
            for (pugi::xml_node subTag : sprite->second.child("subTags").children("item")) {
                if (std::string_view(subTag.attribute("type").as_string()) != "PlaceObject2Tag") {
                    return "Map layer " + slice + " contains a " + subTag.attribute("type").as_string() +
                        "; the game's map layers only place shapes.";
                }
                const pugi::xml_node matrix = subTag.child("matrix");
                if (subTag.attribute("depth").as_uint() != expectedDepth++ ||
                    !shapes.contains(subTag.attribute("characterId").as_uint()) || !matrix ||
                    matrix.attribute("scaleX").as_double() != 1.0 || matrix.attribute("scaleY").as_double() != 1.0 ||
                    matrix.attribute("translateX").as_int() != 0 || matrix.attribute("translateY").as_int() != 0) {
                    return "Map layer " + slice + " does not place its shapes like the game's map layers.";
                }
            }
            if (expectedDepth == 1) {
                return "Map layer " + slice + " has no shapes.";
            }
        }
        if (layerSprites != layerCount) {
            return "The GFX file has " + std::to_string(layerSprites) + " map layers instead of " +
                std::to_string(layerCount) + ".";
        }
        return {};
    }
} // namespace GfxMapLayerSwf
