#include "../../include/NavKit/model/PfBoxes.h"

void PfBoxes::Vec3::readJson(simdjson::ondemand::object json) {
    x = static_cast<float>(static_cast<double>(json["x"]));
    y = static_cast<float>(static_cast<double>(json["y"]));
    z = static_cast<float>(static_cast<double>(json["z"]));
}

void PfBoxes::Rotation::readJson(simdjson::ondemand::object json) {
    x = static_cast<float>(static_cast<double>(json["x"]));
    y = static_cast<float>(static_cast<double>(json["y"]));
    z = static_cast<float>(static_cast<double>(json["z"]));
    w = static_cast<float>(static_cast<double>(json["w"]));
}

void PfBoxes::PfBoxType::readJson(simdjson::ondemand::object json) {
    type = std::string{std::string_view(json["type"])};
    data = std::string{std::string_view(json["data"])};
}

void PfBoxes::Size::readJson(simdjson::ondemand::object json) {
    type = std::string{std::string_view(json["type"])};
    simdjson::ondemand::object dataJson = json["data"];
    data.readJson(dataJson);
}

void PfBoxes::Entity::readJson(simdjson::ondemand::object json) {
    id = std::string{std::string_view(json["id"])};
    auto result = json["name"];
    if (result.error() == simdjson::SUCCESS) {
        name = std::string{std::string_view(json["name"])};
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
    hash = std::string{std::string_view(json["hash"])};
    simdjson::ondemand::object entityJson = json["entity"];
    entity.readJson(entityJson);
}

PfBoxes::PfBoxes::PfBoxes(const char *fileName) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string json = simdjson::padded_string::load(fileName);
    auto jsonDocument = parser.iterate(json);
    simdjson::ondemand::array entitiesJson = jsonDocument["entities"];
    for (simdjson::ondemand::value hashAndEntityJson: entitiesJson) {
        HashAndEntity hashAndEntity;
        hashAndEntity.readJson(hashAndEntityJson);
        hashesAndEntities.push_back(hashAndEntity);
    }
}

PfBoxes::PfBox PfBoxes::PfBoxes::getPathfindingBBox() {
    for (HashAndEntity hashAndEntity: hashesAndEntities) {
        if (hashAndEntity.entity.type.data == INCLUDE_TYPE) {
            Vec3 p = hashAndEntity.entity.position;
            Vec3 s = hashAndEntity.entity.size.data;
            return PfBox(p, s);
        }
    }
    Vec3 pos(0, 0, 0);
    Vec3 size(-1, -1, -1);
    return PfBox(pos, size);
}
