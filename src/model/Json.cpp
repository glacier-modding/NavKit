#include "../../include/NavKit/model/Json.h"

#include <algorithm>
#include <initializer_list>
#include <cmath>
#include <type_traits>
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/ResourceLib_HM3/Generated/HM3/ZHMGen.h"

std::string Json::toString(double val) {
    return std::to_string(val);
}

std::string Json::toString(int64_t val) {
    return std::to_string(val);
}

std::string Json::toString(bool val) {
    return val ? "true" : "false";
}

std::string Json::toString(std::string_view val) {
    return std::string(val);
}

std::ostream& Json::operator<<(std::ostream& f, const Escaped& escaped) {
    static constexpr char hexDigits[] = "0123456789abcdef";
    for (const char c : escaped.value) {
        switch (c) {
        case '"':
            f << R"(\")";
            break;
        case '\\':
            f << R"(\\)";
            break;
        case '\b':
            f << R"(\b)";
            break;
        case '\f':
            f << R"(\f)";
            break;
        case '\n':
            f << R"(\n)";
            break;
        case '\r':
            f << R"(\r)";
            break;
        case '\t':
            f << R"(\t)";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                f << R"(\u00)" << hexDigits[c >> 4] << hexDigits[c & 0xF];
            } else {
                f << c;
            }
        }
    }
    return f;
}

template <typename t, typename> Json::JsonValueProxy::operator t() {
    using simdT = typename SimdJsonTypeMap<t>::type;
    simdT val;

    if (simdjson::ondemand::value jsonValue = json[field]; jsonValue.get(val) == simdjson::SUCCESS) {
        Logger::log(NK_DEBUG, "Field: %s value: %s", field.c_str(), toString(val).c_str());
        return static_cast<t>(val);
    } else if constexpr (std::is_arithmetic_v<t>) {
        std::string_view strVal;
        if (simdjson::error_code err; (err = jsonValue.get_string().get(strVal)) == simdjson::SUCCESS) {
            try {
                if constexpr (std::is_integral_v<t>) {
                    val = static_cast<simdT>(std::stoll(std::string(strVal)));
                } else if constexpr (std::is_floating_point_v<t>) {
                    val = static_cast<simdT>(std::stod(std::string(strVal)));
                }
                Logger::log(
                    NK_DEBUG, "Field: %s value (from string conversion): %s", field.c_str(), toString(val).c_str());
                return static_cast<t>(val);
            } catch (const std::exception& e) {
                Logger::log(NK_ERROR, "Error converting string '%s' to numeric type for field: %s. Exception: %s",
                    std::string(strVal).c_str(), field.c_str(), e.what());
            }
        } else {
            Logger::log(NK_ERROR,
                "Error getting value for field: %s. Not a direct numeric or string value. Simdjson error: %s",
                field.c_str(), simdjson::error_message(err));
        }
    }
    Logger::log(NK_ERROR, "Error getting value for field: %s", field.c_str());
    return {};
}

Json::JsonValueProxy Json::getValue(simdjson::ondemand::object json, const std::string& field) {
    return {json, field};
}

void Json::Vec3::readJson(simdjson::ondemand::object json) {
    x = getValue(json, "x");
    y = getValue(json, "y");
    z = getValue(json, "z");
}

void Json::Vec3::writeJson(std::ostream& f, const std::string& propertyName) const {
    f << R"(")" << escaped(propertyName) << R"(":{"x":)" << x << R"(,"y":)" << y << R"(,"z":)" << z << "}";
}

void Json::Rotation::readJson(simdjson::ondemand::object json) {
    x = getValue(json, "x");
    y = getValue(json, "y");
    z = getValue(json, "z");
    w = getValue(json, "w");
}

void Json::Rotation::writeJson(std::ostream& f) const {
    f << R"("rotation":{"x":)" << x << R"(,"y":)" << y << R"(,"z":)" << z << R"(,"w":)" << w << "}";
}

void Json::PfBoxType::readJson(simdjson::ondemand::object json) {
    type = getValue(json, "type");
    data = getValue(json, "data");
}

void Json::PfBoxType::writeJson(std::ostream& f) const {
    f << R"("type":{"type":"EPathFinderBoxType","data":")" << escaped(data) << R"("})";
}

void Json::Vec3Wrapped::readJson(simdjson::ondemand::object json) {
    type = getValue(json, "type");
    const simdjson::ondemand::object dataJson = json["data"];
    data.readJson(dataJson);
}

void Json::Vec3Wrapped::writeJson(std::ostream& f, std::string propertyName) const {
    f << R"(")" << escaped(propertyName) << R"(":{"type":")" << escaped(type) << R"(","data":{"x":)" << data.x
      << R"(,"y":)" << data.y << R"(,"z":)" << data.z << "}}";
}

void Json::Entity::readJson(simdjson::ondemand::object json) {
    id = getValue(json, "id");
    name = getValue(json, "name");
    const simdjson::ondemand::object positionJson = json["position"];
    position.readJson(positionJson);
    const simdjson::ondemand::object rotationJson = json["rotation"];
    rotation.readJson(rotationJson);
    if (json["type"].error() == simdjson::SUCCESS) {
        const simdjson::ondemand::object pfBoxTypeJson = json["type"];
        type.readJson(pfBoxTypeJson);
    }
    if (json["scale"].error() == simdjson::SUCCESS) {
        const simdjson::ondemand::object scaleJson = json["scale"];
        scale.readJson(scaleJson);
    }
}

void Json::Entity::writeJson(std::ostream& f) const {
    f << "{";
    writeJsonFields(f);
    f << "}";
}

void Json::Entity::writeJsonFields(std::ostream& f) const {
    f << R"("id":")" << escaped(id) << R"(","name":")" << escaped(name) << R"(",)";
    position.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << ",";
    if (!type.data.empty()) {
        type.writeJson(f);
        f << ",";
    }
    const Vec3Wrapped scaleJson("SVector3", {scale.data.x, scale.data.y, scale.data.z});
    scaleJson.writeJson(f);
}

void Json::Mesh::readJson(simdjson::ondemand::object json) {
    alocHash = getValue(json, "alocHash");
    primHash = getValue(json, "primHash");
    roomName = getValue(json, "roomName");
    roomFolderName = getValue(json, "roomFolderName");
    const simdjson::ondemand::object entityJson = json["entity"];
    entity.readJson(entityJson);
}

void Json::Mesh::writeJson(std::ostream& f) const {
    f << R"({"alocHash":")" << escaped(alocHash) << R"(","primHash":")" << escaped(primHash) << R"(","roomName":")"
      << escaped(roomName) << R"(","roomFolderName":")" << escaped(roomFolderName) << R"(","entity":{"id":")"
      << escaped(entity.id) << R"(","name":")" << escaped(entity.name) << R"(",)";
    entity.position.writeJson(f);
    f << ",";
    entity.rotation.writeJson(f);
    f << ",";
    entity.scale.writeJson(f);
    f << "}}";
}

Json::Meshes::Meshes(simdjson::ondemand::array meshesJson) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> meshJsonResult : meshesJson) {
        simdjson::ondemand::value meshJson = meshJsonResult.value();
        Mesh mesh;
        mesh.readJson(meshJson.get_object().value());
        meshes.push_back(mesh);
    }
}

void Json::PfBox::writeJson(std::ostream& f) const {
    f << R"({"id":")" << escaped(id) << R"(","name":")" << escaped(name) << R"(",)";
    pos.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << ",";
    if (!type.data.empty()) {
        type.writeJson(f);
        f << ",";
    }
    const Vec3Wrapped scaleJson("SVector3", scale);
    scaleJson.writeJson(f);
    f << "}";
}

Json::PfBoxes::PfBoxes(simdjson::ondemand::array pfBoxes) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> entityJsonResult : pfBoxes) {
        simdjson::ondemand::value entityJson = entityJsonResult.value();
        Entity entity;
        entity.readJson(entityJson.get_object().value());
        entities.push_back(entity);
    }
}

void Json::PfBoxes::readPathfindingBBoxes() {
    Scene& scene = Scene::getInstance();
    scene.includeBox = {};
    scene.exclusionBoxes.clear();
    scene.plannerExcludedBridgeBoxes.clear();
    bool includeBoxFound = false;
    for (Entity& entity : entities) {
        if (entity.name.starts_with(PLANNER_EXCLUDED_BRIDGE_NAME_PREFIX)) {
            scene.plannerExcludedBridgeBoxes.emplace_back(
                entity.id, entity.name, entity.position, entity.scale.data, entity.rotation, entity.type);
            continue;
        }
        if (entity.id == "48b22d0153942122" && abs(entity.position.x - -26.9811001) < 0.1) {
            entity.type.data = INCLUDE_TYPE; // Workaround for bug in Sapienza's include box
        }
        if (entity.type.data == INCLUDE_TYPE) {
            std::string id = entity.id;
            std::string name = entity.name;
            Vec3 p = entity.position;
            Vec3 s = entity.scale.data;
            Rotation r = entity.rotation;
            PfBoxType type = entity.type;
            if (scene.includeBox.id.empty()) {
                scene.includeBox = {id, name, p, s, r, type};
            } else {
                // Set scene.includeBox pos, scale, and rotation to be the span of the two boxes
                float minX = std::min(scene.includeBox.pos.x - scene.includeBox.scale.x / 2.0f, p.x - s.x / 2.0f);
                float minY = std::min(scene.includeBox.pos.y - scene.includeBox.scale.y / 2.0f, p.y - s.y / 2.0f);
                float minZ = std::min(scene.includeBox.pos.z - scene.includeBox.scale.z / 2.0f, p.z - s.z / 2.0f);
                float maxX = std::max(scene.includeBox.pos.x + scene.includeBox.scale.x / 2.0f, p.x + s.x / 2.0f);
                float maxY = std::max(scene.includeBox.pos.y + scene.includeBox.scale.y / 2.0f, p.y + s.y / 2.0f);
                float maxZ = std::max(scene.includeBox.pos.z + scene.includeBox.scale.z / 2.0f, p.z + s.z / 2.0f);

                scene.includeBox.pos = {(minX + maxX) / 2.0f, (minY + maxY) / 2.0f, (minZ + maxZ) / 2.0f};
                scene.includeBox.scale = {maxX - minX, maxY - minY, maxZ - minZ};
                scene.includeBox.rotation = {0, 0, 0, 1};
            }
            includeBoxFound = true;
        }
        if (entity.type.data == EXCLUDE_TYPE) {
            std::string id = entity.id;
            std::string name = entity.name;
            Vec3 p = entity.position;
            Vec3 s = entity.scale.data;
            Rotation r = entity.rotation;
            PfBoxType type = entity.type;
            scene.exclusionBoxes.emplace_back(id, name, p, s, r, type);
        }
    }
    if (!includeBoxFound) {
        Vec3 pos(0, 0, 0);
        Vec3 scale(1000, 1000, 1000);
        Rotation r;
        std::string id = NO_INCLUDE_BOX_FOUND;
        std::string name;
        PfBoxType type;
        type.type = "EPathFinderBoxType";
        type.data = INCLUDE_TYPE;
        scene.includeBox = {id, name, pos, scale, r, type};
    }
}

Json::Gates::Gates(simdjson::ondemand::array gatesJson) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> gateJsonResult : gatesJson) {
        simdjson::ondemand::value gateJson = gateJsonResult.value();
        Gate gate;
        gate.readJson(gateJson.get_object().value());
        gates.push_back(gate);
    }
}

void Json::Room::writeJson(std::ostream& f) const {
    f << R"({"id":")" << escaped(id) << R"(","name":")" << escaped(name) << R"(",)";
    position.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << ",";
    roomExtentMin.writeJson(f, "roomExtentMin");
    f << ",";
    roomExtentMax.writeJson(f, "roomExtentMax");
    f << "}";
}

void Json::Room::readJson(simdjson::ondemand::object json) {
    id = getValue(json, "id");
    name = getValue(json, "name");
    const simdjson::ondemand::object positionJson = json["position"];
    position.readJson(positionJson);
    const simdjson::ondemand::object rotationJson = json["rotation"];
    rotation.readJson(rotationJson);
    if (json["roomExtentMin"].error() == simdjson::SUCCESS) {
        roomExtentMin.readJson(json["roomExtentMin"]);
    }
    if (json["roomExtentMax"].error() == simdjson::SUCCESS) {
        roomExtentMax.readJson(json["roomExtentMax"]);
    }
}

Json::Rooms::Rooms(simdjson::ondemand::array roomsJson) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> roomJsonResult : roomsJson) {
        simdjson::ondemand::value roomJson = roomJsonResult.value();
        Room room;
        room.readJson(roomJson.get_object().value());
        rooms.push_back(room);
    }
}

void Json::Gate::writeJson(std::ostream& f) const {
    f << R"({"id":")" << escaped(id) << R"(","name":")" << escaped(name) << R"(",)";
    position.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << ",";
    bboxCenter.writeJson(f, "bboxCenter");
    f << ",";
    bboxHalfSize.writeJson(f, "bboxHalfSize");
    f << "}";
}

void Json::Gate::readJson(simdjson::ondemand::object json) {
    id = getValue(json, "id");
    name = getValue(json, "name");
    const simdjson::ondemand::object positionJson = json["position"];
    position.readJson(positionJson);
    const simdjson::ondemand::object rotationJson = json["rotation"];
    rotation.readJson(rotationJson);
    if (json["bboxCenter"].error() == simdjson::SUCCESS) {
        bboxCenter.readJson(json["bboxCenter"]);
    }
    if (json["bboxHalfSize"].error() == simdjson::SUCCESS) {
        bboxHalfSize.readJson(json["bboxHalfSize"]);
    }
}

void Json::AiAreaWorld::writeJson(std::ostream& f) const {
    f << R"({"id":")" << escaped(id) << R"(","name":")" << escaped(name);
    f << R"(",)";
    rotation.writeJson(f);
    f << "}";
}

void Json::AiAreaWorld::readJson(simdjson::ondemand::object json) {
    id = getValue(json, "id");
    name = getValue(json, "name");
    const simdjson::ondemand::object rotationJson = json["rotation"];
    rotation.readJson(rotationJson);
}

Json::AiAreaWorlds::AiAreaWorlds(simdjson::ondemand::array aiAreaWorldsJson) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> aiAreaWorldJsonResult : aiAreaWorldsJson) {
        simdjson::ondemand::value aiAreaWorldJson = aiAreaWorldJsonResult.value();
        AiAreaWorld aiAreaWorld;
        aiAreaWorld.readJson(aiAreaWorldJson.get_object().value());
        aiAreaWorlds.push_back(aiAreaWorld);
    }
}

void Json::ParentData::writeJson(std::ostream& f) const {
    f << R"("data":{"id":")" << escaped(id) << R"(","name":")" << escaped(name) << R"(","source":")" << escaped(source)
      << R"(","type":")" << escaped(type) << R"("})";
}

void Json::ParentData::readJson(simdjson::ondemand::object json) {
    id = getValue(json, "id");
    name = getValue(json, "name");
    source = getValue(json, "source");
    type = getValue(json, "type");
}

void Json::Parent::writeJson(std::ostream& f) const {
    f << R"("parent":{"type":")" << escaped(type) << R"(",)";
    if (hasData) {
        data.writeJson(f);
    } else {
        f << R"("data":null)";
    }
    f << "}";
}

void Json::Parent::readJson(simdjson::ondemand::object json) {
    type = getValue(json, "type");
    simdjson::ondemand::value dataJson = json["data"];
    hasData = !dataJson.is_null();
    data = {};
    if (hasData) {
        data.readJson(dataJson.get_object());
    }
}

void Json::AiArea::writeJson(std::ostream& f) const {
    f << R"({"id":")" << escaped(id) << R"(","name":")" << escaped(name) << R"(",)";
    rotation.writeJson(f);
    f << R"(,"logicalParent":[)";
    auto separator = "";
    for (auto logicalParent : logicalParents) {
        f << separator;
        f << R"(")" << escaped(logicalParent) << R"(")";
        separator = ",";
    }
    f << R"(],"areaVolumeNames":[)";
    separator = "";
    for (auto areaVolumeName : areaVolumeNames) {
        f << separator;
        f << R"(")" << escaped(areaVolumeName) << R"(")";
        separator = ",";
    }
    f << R"(],)";
    parent.writeJson(f);
    f << "}";
}

void Json::AiArea::readJson(simdjson::ondemand::object json) {
    id = getValue(json, "id");
    name = getValue(json, "name");
    const simdjson::ondemand::object rotationJson = json["rotation"];
    rotation.readJson(rotationJson);
    logicalParents.clear();
    for (simdjson::ondemand::array logicalParentsJson = json["logicalParent"];
        auto logicalParentJson : logicalParentsJson) {
        logicalParents.push_back(std::string{std::string_view(logicalParentJson)});
    }
    areaVolumeNames.clear();
    for (simdjson::ondemand::array areaVolumeNamesJson = json["areaVolumeNames"];
        auto areaVolumeNameJson : areaVolumeNamesJson) {
        areaVolumeNames.push_back(std::string{std::string_view(areaVolumeNameJson)});
    }
    const simdjson::ondemand::object parentJson = json["parent"];
    parent.readJson(parentJson);
}

Json::AiAreas::AiAreas(simdjson::ondemand::array aiAreasJson) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> aiAreaJsonResult : aiAreasJson) {
        simdjson::ondemand::value aiAreaJson = aiAreaJsonResult.value();
        AiArea aiArea;
        aiArea.readJson(aiAreaJson.get_object().value());
        aiAreas.push_back(aiArea);
    }
}

Json::VolumeBoxes::VolumeBoxes(simdjson::ondemand::array volumeBoxesJson) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> volumeBoxJsonResult : volumeBoxesJson) {
        simdjson::ondemand::value volumeBoxJson = volumeBoxJsonResult.value();
        Entity volumeBox;
        volumeBox.readJson(volumeBoxJson.get_object().value());
        volumeBoxes.push_back(volumeBox);
    }
}

void Json::Radius::writeJson(std::ostream& f) const {
    f << R"("radius":{"type":")" << escaped(type) << R"(","data":")" << data << R"("})";
}

void Json::Radius::readJson(simdjson::ondemand::object json) {
    type = getValue(json, "type");
    data = getValue(json, "data");
}

void Json::VolumeSphere::writeJson(std::ostream& f) const {
    f << R"({"id":")" << escaped(id) << R"(","name":")" << escaped(name) << R"(",)";
    position.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << ",";
    radius.writeJson(f);
    f << "}";
}

void Json::VolumeSphere::readJson(simdjson::ondemand::object json) {
    id = getValue(json, "id");
    name = getValue(json, "name");
    position.readJson(json["position"]);
    rotation.readJson(json["rotation"]);
    if (json["radius"].error() == simdjson::SUCCESS) {
        radius.readJson(json["radius"]);
    }
}

Json::VolumeSpheres::VolumeSpheres(simdjson::ondemand::array volumeSpheresJson) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> volumeSphereJsonResult : volumeSpheresJson) {
        simdjson::ondemand::value volumeSphereJson = volumeSphereJsonResult.value();
        VolumeSphere volumeSphere;
        volumeSphere.readJson(volumeSphereJson.get_object().value());
        volumeSpheres.push_back(volumeSphere);
    }
}

void Json::PfSeedPoint::writeJson(std::ostream& f) const {
    f << R"({"id":")" << escaped(id) << R"(","name":")" << escaped(name) << R"(",)";
    pos.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << "}";
}

Json::PfSeedPoints::PfSeedPoints(simdjson::ondemand::array pfSeedPoints) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> entityJsonResult : pfSeedPoints) {
        simdjson::ondemand::value entityJson = entityJsonResult.value();
        Entity entity;
        entity.readJson(entityJson.get_object().value());
        entities.push_back(entity);
    }
}

std::vector<Json::PfSeedPoint> Json::PfSeedPoints::readPfSeedPoints() const {
    std::vector<PfSeedPoint> pfSeedPoints;
    for (const Entity& entity : entities) {
        std::string id = entity.id;
        std::string name = entity.name;
        Vec3 p = entity.position;
        Rotation r = entity.rotation;
        pfSeedPoints.emplace_back(id, name, p, r);
    }
    return pfSeedPoints;
}

namespace {
    template <typename Enum> using EnumNames = std::initializer_list<std::pair<Enum, std::string_view>>;

    const EnumNames<ECrowdFlowCandidates> crowdFlowCandidatesNames = {
        {ECrowdFlowCandidates::NONE, "NONE"},
        {ECrowdFlowCandidates::EVERYONE, "EVERYONE"},
        {ECrowdFlowCandidates::SPAWNED_ON_FLOW, "SPAWNED_ON_FLOW"},
    };

    const EnumNames<TeleportActionType> teleportActionNames = {
        {TeleportActionType::STAY, "STAY"},
        {TeleportActionType::TELEPORT, "TELEPORT"},
        {TeleportActionType::LEAVE_CROWD, "LEAVE_CROWD"},
    };

    template <typename Enum>
    void readEnum(simdjson::ondemand::object& json, const char* field, const EnumNames<Enum>& names, Enum& out) {
        std::string_view text;
        if (json[field].get_string().get(text) != simdjson::SUCCESS) {
            Logger::log(NK_ERROR, "Field: %s is missing or not a string.", field);
            return;
        }
        for (const auto& [value, name] : names) {
            if (name == text) {
                out = value;
                return;
            }
        }
        Logger::log(NK_ERROR, "Field: %s has unknown value '%s'.", field, std::string(text).c_str());
    }

    template <typename Enum> void writeEnum(std::ostream& f, const EnumNames<Enum>& names, const Enum value) {
        for (const auto& [candidate, name] : names) {
            if (candidate == value) {
                f << R"(")" << name << R"(")";
                return;
            }
        }
        f << "null";
    }

    template <typename T> void readObject(simdjson::ondemand::object& json, const char* field, T& out) {
        simdjson::ondemand::object objectJson;
        if (json[field].get_object().get(objectJson) != simdjson::SUCCESS) {
            Logger::log(NK_ERROR, "Field: %s is missing or not an object.", field);
            return;
        }
        out.readJson(objectJson);
    }

    template <typename T> std::optional<T> readNullableObject(simdjson::ondemand::object& json, const char* field) {
        simdjson::ondemand::value valueJson;
        if (json[field].get(valueJson) == simdjson::SUCCESS) {
            if (bool isNull; valueJson.is_null().get(isNull) == simdjson::SUCCESS && isNull) {
                return std::nullopt;
            }
            if (simdjson::ondemand::object objectJson; valueJson.get_object().get(objectJson) == simdjson::SUCCESS) {
                T item;
                item.readJson(objectJson);
                return item;
            }
        }
        Logger::log(NK_ERROR, "Field: %s is missing or not an object or null.", field);
        return std::nullopt;
    }

    template <typename T> std::vector<T> readArray(simdjson::ondemand::object& json, const char* field) {
        std::vector<T> items;
        simdjson::ondemand::array arrayJson;
        if (json[field].get_array().get(arrayJson) != simdjson::SUCCESS) {
            Logger::log(NK_ERROR, "Field: %s is missing or not an array.", field);
            return items;
        }
        for (auto itemResult : arrayJson) {
            simdjson::ondemand::object itemJson;
            if (itemResult.get_object().get(itemJson) == simdjson::SUCCESS) {
                T item;
                item.readJson(itemJson);
                items.push_back(item);
            } else {
                Logger::log(NK_ERROR, "Field: %s has a non-object entry, skipping it.", field);
            }
        }
        return items;
    }

    std::vector<size_t> readIndexArray(
        simdjson::ondemand::object& json, const char* field, const bool required = true) {
        std::vector<size_t> indices;
        simdjson::ondemand::array arrayJson;
        if (json[field].get_array().get(arrayJson) != simdjson::SUCCESS) {
            if (required) {
                Logger::log(NK_ERROR, "Field: %s is missing or not an array.", field);
            }
            return indices;
        }
        for (auto indexResult : arrayJson) {
            if (uint64_t index; indexResult.get_uint64().get(index) == simdjson::SUCCESS) {
                indices.push_back(static_cast<size_t>(index));
            } else {
                Logger::log(NK_ERROR, "Field: %s has a non-index entry, skipping it.", field);
            }
        }
        return indices;
    }

    void writeIndexArray(std::ostream& f, const char* key, const std::vector<size_t>& indices) {
        f << R"(,")" << Json::escaped(key) << R"(":[)";
        auto separator = "";
        for (const size_t index : indices) {
            f << separator << index;
            separator = ",";
        }
        f << "]";
    }

    void writeVec3(std::ostream& f, const Json::Vec3& vec) {
        f << R"({"x":)" << vec.x << R"(,"y":)" << vec.y << R"(,"z":)" << vec.z << "}";
    }

    void writePoints(std::ostream& f, const std::vector<Json::Vec3>& points) {
        f << R"("points":[)";
        auto separator = "";
        for (const auto& point : points) {
            f << separator;
            writeVec3(f, point);
            separator = ",";
        }
        f << "]";
    }

    template <typename T> void writeArray(std::ostream& f, const char* key, const std::vector<T>& items) {
        f << R"(,")" << Json::escaped(key) << R"(":[)";
        auto separator = "";
        for (const auto& item : items) {
            f << separator;
            item.writeJson(f);
            separator = ",";
        }
        f << "]";
    }

    void writeOptionalBox(std::ostream& f, const char* key, const std::optional<Json::Entity>& box) {
        f << R"(")" << Json::escaped(key) << R"(":)";
        if (box) {
            box->writeJson(f);
        } else {
            f << "null";
        }
    }
} // namespace

void Json::Transform::readJson(simdjson::ondemand::object json) {
    readObject(json, "x", x);
    readObject(json, "y", y);
    readObject(json, "z", z);
    readObject(json, "t", t);
}

void Json::Transform::writeJson(std::ostream& f) const {
    f << R"("transform":{)";
    x.writeJson(f, "x");
    f << ",";
    y.writeJson(f, "y");
    f << ",";
    z.writeJson(f, "z");
    f << ",";
    t.writeJson(f, "t");
    f << "}";
}

void Json::CrowdEntityIdentity::readJson(simdjson::ondemand::object json) {
    id = getValue(json, "id");
    name = getValue(json, "name");
    if (std::string_view tbluJson; json["tblu"].get_string().get(tbluJson) == simdjson::SUCCESS) {
        tblu = tbluJson;
    }
}

void Json::CrowdEntityIdentity::writeJson(std::ostream& f) const {
    f << R"("id":")" << escaped(id) << R"(","name":")" << escaped(name) << R"(",)";
    if (!tblu.empty()) {
        f << R"("tblu":")" << escaped(tblu) << R"(",)";
    }
}

void Json::VolumeShape::readJson(simdjson::ondemand::object json) {
    CrowdEntityIdentity::readJson(json);
    readObject(json, "transform", transform);
    points = readArray<Vec3>(json, "points");
    extrusionHeight = getValue(json, "extrusionHeight");
    flipSurfaceNormal = getValue(json, "flipSurfaceNormal");
    closed = getValue(json, "closed");
}

void Json::VolumeShape::writeJson(std::ostream& f) const {
    f << "{";
    CrowdEntityIdentity::writeJson(f);
    transform.writeJson(f);
    f << ",";
    writePoints(f, points);
    f << R"(,"extrusionHeight":)" << extrusionHeight << R"(,"flipSurfaceNormal":)"
      << (flipSurfaceNormal ? "true" : "false") << R"(,"closed":)" << (closed ? "true" : "false") << "}";
}

Json::VolumeShapes::VolumeShapes(simdjson::ondemand::array volumeShapesJson) {
    for (auto volumeShapeJsonResult : volumeShapesJson) {
        VolumeShape volumeShape;
        volumeShape.readJson(volumeShapeJsonResult.get_object().value());
        volumeShapes.push_back(volumeShape);
    }
}

void Json::CrowdTeleport::readJson(simdjson::ondemand::object json) {
    CrowdEntityIdentity::readJson(json);
    exitZone = readNullableObject<Entity>(json, "exitZone");
    enterZone = readNullableObject<Entity>(json, "enterZone");
    readEnum(json, "exitActionAmbient", teleportActionNames, exitActionAmbient);
    readEnum(json, "exitActionScared", teleportActionNames, exitActionScared);
}

void Json::CrowdTeleport::writeJson(std::ostream& f) const {
    f << "{";
    CrowdEntityIdentity::writeJson(f);
    writeOptionalBox(f, "exitZone", exitZone);
    f << ",";
    writeOptionalBox(f, "enterZone", enterZone);
    f << R"(,"exitActionAmbient":)";
    writeEnum(f, teleportActionNames, exitActionAmbient);
    f << R"(,"exitActionScared":)";
    writeEnum(f, teleportActionNames, exitActionScared);
    f << "}";
}

void Json::CrowdGroupBox::readJson(simdjson::ondemand::object json) {
    Entity::readJson(json);
    if (simdjson::ondemand::object groupSizeJson;
        json["groupSize"].get_object().get(groupSizeJson) == simdjson::SUCCESS) {
        groupSize = getValue(groupSizeJson, "data");
    } else {
        Logger::log(NK_ERROR, "Field: groupSize is missing or not an object.");
    }
    exclusionBoxIndices = readIndexArray(json, "exclusionBoxes", false);
    exclusionShapeIndices = readIndexArray(json, "exclusionShapes", false);
    seedPointIndices = readIndexArray(json, "seedPoints", false);
}

void Json::CrowdGroupBox::writeJson(std::ostream& f) const {
    f << "{";
    writeJsonFields(f);
    f << R"(,"groupSize":{"type":"uint32","data":)" << groupSize << "}";
    for (const auto& [key, indices] : {std::pair{"exclusionBoxes", &exclusionBoxIndices},
             {"exclusionShapes", &exclusionShapeIndices}, {"seedPoints", &seedPointIndices}}) {
        if (!indices->empty()) {
            writeIndexArray(f, key, *indices);
        }
    }
    f << "}";
}

void Json::CrowdSplineFlow::readJson(simdjson::ondemand::object json) {
    CrowdEntityIdentity::readJson(json);
    readObject(json, "transform", transform);
    flowWidth = getValue(json, "flowWidth");
    const float lengthTessellationJson = getValue(json, "lengthTessellation");
    lengthTessellation = static_cast<int>(std::lround(lengthTessellationJson));
    const float widthTessellationJson = getValue(json, "widthTessellation");
    widthTessellation = static_cast<int>(std::lround(widthTessellationJson));
    extrusionHeight = getValue(json, "extrusionHeight");
    closed = getValue(json, "closed");
    points = readArray<Vec3>(json, "points");
}

void Json::CrowdSplineFlow::writeJson(std::ostream& f) const {
    f << "{";
    CrowdEntityIdentity::writeJson(f);
    transform.writeJson(f);
    f << R"(,"flowWidth":)" << flowWidth << R"(,"lengthTessellation":)" << lengthTessellation
      << R"(,"widthTessellation":)" << widthTessellation << R"(,"extrusionHeight":)" << extrusionHeight
      << R"(,"closed":)" << (closed ? "true" : "false") << ",";
    writePoints(f, points);
    f << "}";
}

void Json::Crowd::readJson(simdjson::ondemand::object json) {
    CrowdEntityIdentity::readJson(json);
    readObject(json, "transform", transform);
    readObject(json, "globalSize", globalSize);
    cellSize = getValue(json, "cellSize");
    navMeshShrink = getValue(json, "navMeshShrink");
    generatorOffset = getValue(json, "generatorOffset");
    smoothenExitFlows = getValue(json, "smoothenExitFlows");
    readEnum(json, "flowCandidates", crowdFlowCandidatesNames, flowCandidates);
    exclusionBoxIndices = readIndexArray(json, "exclusionBoxes");
    inclusionBoxIndices = readIndexArray(json, "inclusionBoxes");
    panicOnlyBoxIndices = readIndexArray(json, "panicOnlyBoxes");
    flowBoxIndices = readIndexArray(json, "flowBoxes");
    flowBoxTwoWayIndices = readIndexArray(json, "flowBoxesTwoWay");
    exclusionSphereIndices = readIndexArray(json, "exclusionSpheres");
    exclusionShapeIndices = readIndexArray(json, "exclusionShapes");
    inclusionShapeIndices = readIndexArray(json, "inclusionShapes");
    panicOnlyShapeIndices = readIndexArray(json, "panicOnlyShapes");
    teleports = readArray<CrowdTeleport>(json, "teleports");
    groupBoxes = readArray<CrowdGroupBox>(json, "groupBoxes");
    splineFlows = readArray<CrowdSplineFlow>(json, "splineFlows");
}

void Json::Crowd::writeJson(std::ostream& f) const {
    f << "{";
    CrowdEntityIdentity::writeJson(f);
    transform.writeJson(f);
    f << ",";
    globalSize.writeJson(f, "globalSize");
    f << R"(,"cellSize":)" << cellSize << R"(,"navMeshShrink":)" << navMeshShrink << R"(,"generatorOffset":)"
      << generatorOffset << R"(,"smoothenExitFlows":)" << (smoothenExitFlows ? "true" : "false")
      << R"(,"flowCandidates":)";
    writeEnum(f, crowdFlowCandidatesNames, flowCandidates);
    writeIndexArray(f, "exclusionBoxes", exclusionBoxIndices);
    writeIndexArray(f, "inclusionBoxes", inclusionBoxIndices);
    writeIndexArray(f, "panicOnlyBoxes", panicOnlyBoxIndices);
    writeIndexArray(f, "flowBoxes", flowBoxIndices);
    writeIndexArray(f, "flowBoxesTwoWay", flowBoxTwoWayIndices);
    writeIndexArray(f, "exclusionSpheres", exclusionSphereIndices);
    writeIndexArray(f, "exclusionShapes", exclusionShapeIndices);
    writeIndexArray(f, "inclusionShapes", inclusionShapeIndices);
    writeIndexArray(f, "panicOnlyShapes", panicOnlyShapeIndices);
    writeArray(f, "teleports", teleports);
    writeArray(f, "groupBoxes", groupBoxes);
    writeArray(f, "splineFlows", splineFlows);
    f << "}";
}

Json::Crowds::Crowds(simdjson::ondemand::array crowdsJson) {
    for (auto crowdJsonResult : crowdsJson) {
        Crowd crowd;
        crowd.readJson(crowdJsonResult.get_object().value());
        crowds.push_back(crowd);
    }
}

void Json::Mati::readJsonFromMatiFile(simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    hash = getValue(jsonDocument.get_object(), "id");
    simdjson::ondemand::object propertiesJson = jsonDocument["properties"];
    auto result = propertiesJson["mapTexture2D_01"];
    if (result.error() == simdjson::SUCCESS) {
        const simdjson::ondemand::object diffuseJson = propertiesJson["mapTexture2D_01"];
        diffuse = getValue(diffuseJson, "value");
    } else {
        Logger::log(NK_ERROR, "No diffuse texture found for %s", hash.c_str());
    }
    result = propertiesJson["mapTexture2DNormal_01"];
    if (result.error() == simdjson::SUCCESS) {
        simdjson::ondemand::object normalJson = propertiesJson["mapTexture2DNormal_01"];
        normal = getValue(normalJson, "value");
    }
    result = propertiesJson["mapTexture2D_03"];
    if (result.error() == simdjson::SUCCESS) {
        simdjson::ondemand::object specularJson = propertiesJson["mapTexture2D_03"];
        specular = getValue(specularJson, "value");
    }
}

void Json::Mati::readJsonFromScene(simdjson::ondemand::object json) {
    hash = getValue(json, "hash");
    diffuse = getValue(json, "diffuse");
    normal = getValue(json, "normal");
    specular = getValue(json, "specular");
}

void Json::Mati::writeJson(std::ostream& f) const {
    f << R"({"hash":")" << escaped(hash) << R"(","diffuse":")" << escaped(diffuse) << R"(","normal":")"
      << escaped(normal) << R"(","specular":")" << escaped(specular);
    f << R"("})";
}

Json::Matis::Matis(simdjson::ondemand::array matisJson) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> matiJsonResult : matisJson) {
        simdjson::ondemand::value matiJson = matiJsonResult.value();
        Mati mati;
        mati.readJsonFromScene(matiJson.get_object().value());
        matis.push_back(mati);
    }
}

void Json::PrimMati::readJson(simdjson::ondemand::object json) {
    primHash = getValue(json, "primHash");
    simdjson::ondemand::array matiHashesJson = json["matiHashes"];
    matiHashes.clear();
    for (simdjson::ondemand::value matiHashJson : matiHashesJson) {
        matiHashes.push_back(std::string(std::string_view(matiHashJson)));
    }
}

void Json::PrimMati::writeJson(std::ostream& f) const {
    f << R"({"primHash":")" << escaped(primHash) << R"(","matiHashes":[)";
    bool first = true;
    for (auto matiHash : matiHashes) {
        if (!first) {
            f << ",";
        } else {
            first = false;
        }
        f << R"(")" << escaped(matiHash) << R"(")";
    }
    f << R"(]})";
}

Json::PrimMatis::PrimMatis(simdjson::ondemand::array primMatisJson) {
    for (simdjson::simdjson_result<simdjson::ondemand::value> primMatiJsonResult : primMatisJson) {
        simdjson::ondemand::value primMatiJson = primMatiJsonResult.value();
        PrimMati primMati;
        primMati.readJson(primMatiJson.get_object().value());
        primMatis.push_back(primMati);
    }
}
