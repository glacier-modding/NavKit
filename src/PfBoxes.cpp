#include "..\include\NavKit\PfBoxes.h"

void PfBoxes::Vec3::readJson(simdjson::ondemand::object json) {
    x = double(json["x"]);
    y = double(json["y"]);
    z = double(json["z"]);
}

void PfBoxes::Rotation::readJson(simdjson::ondemand::object json) {
    yaw = double(json["yaw"]);
    pitch = double(json["pitch"]);
    roll = double(json["roll"]);
}

void PfBoxes::PfBoxType::readJson(simdjson::ondemand::object json) {
    type = std::string{ std::string_view(json["type"]) };
    data = std::string{ std::string_view(json["data"]) };
}

void PfBoxes::Size::readJson(simdjson::ondemand::object json) {
    type = std::string{ std::string_view(json["type"]) };
    simdjson::ondemand::object dataJson = json["data"];
    data.readJson(dataJson);
}

void PfBoxes::Entity::readJson(simdjson::ondemand::object json) {
    id = std::string{ std::string_view(json["id"]) };
    auto result = json["name"];
    if (result.error() == simdjson::SUCCESS) {
        name = std::string{ std::string_view(json["name"]) };
    }

    simdjson::ondemand::object positionJson = json["position"];
    position.readJson(positionJson);
    simdjson::ondemand::object rotationJson = json["rotation"];
    rotation.readJson(rotationJson);
    simdjson::ondemand::object pfBoxTypeJson = json["type"];
    type.readJson(pfBoxTypeJson);
    simdjson::ondemand::object sizeJson = json["size"];
    size.readJson(sizeJson);
}

void PfBoxes::HashAndEntity::readJson(simdjson::ondemand::object json) {
    hash = std::string{ std::string_view(json["hash"]) };
    simdjson::ondemand::object entityJson = json["entity"];
    entity.readJson(entityJson);
}

PfBoxes::PfBoxes::PfBoxes(char* fileName) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string json = simdjson::padded_string::load(fileName);
    auto jsonDocument = parser.iterate(json);
    simdjson::ondemand::array entitiesJson = jsonDocument["entities"];
    for (simdjson::ondemand::value hashAndEntityJson : entitiesJson) {
        HashAndEntity hashAndEntity;
        hashAndEntity.readJson(hashAndEntityJson);
        hashesAndEntities.push_back(hashAndEntity);
    }
}

std::pair<float*, float*> PfBoxes::PfBoxes::getPathfindingBBox() {
    float pos[3] = { 0, 0, 0 };
    float size[3] = { 600, 600, 600 };
    for (HashAndEntity hashAndEntity : hashesAndEntities) {
        if (hashAndEntity.entity.type.data == INCLUDE_TYPE) {
            Vec3 p = hashAndEntity.entity.position;
            Vec3 s = hashAndEntity.entity.size.data;
            pos[0] = p.x;
            pos[1]  = p.y;
            pos[2] = p.z;
            size[0] = s.x;
            size[1] = s.y;
            size[2] = s.z;
            break;
        }
    }
    return std::pair<float*, float*>(pos, size);
}