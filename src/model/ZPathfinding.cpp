#include "../../include/NavKit/model/ZPathfinding.h"
#include "../../include/NavKit/module/Scene.h"

#include <fstream>

void ZPathfinding::Vec3::readJson(simdjson::ondemand::object json) {
    x = static_cast<float>(static_cast<double>(json["x"]));
    y = static_cast<float>(static_cast<double>(json["y"]));
    z = static_cast<float>(static_cast<double>(json["z"]));
}

void ZPathfinding::Vec3::writeJson(std::ofstream &f) const {
    f << R"("position":{"x":)" << x << R"(,"y":)" << y << R"(,"z": )" << z << "}";
}

void ZPathfinding::Rotation::readJson(simdjson::ondemand::object json) {
    x = static_cast<float>(static_cast<double>(json["x"]));
    y = static_cast<float>(static_cast<double>(json["y"]));
    z = static_cast<float>(static_cast<double>(json["z"]));
    w = static_cast<float>(static_cast<double>(json["w"]));
}

void ZPathfinding::Rotation::writeJson(std::ofstream &f) const {
    f << R"("rotation":{"x":)" << x << R"(,"y":)" << y << R"(,"z": )" << z << R"(,"w": )" << w << "}";
}

void ZPathfinding::PfBoxType::readJson(simdjson::ondemand::object json) {
    type = std::string{std::string_view(json["type"])};
    data = std::string{std::string_view(json["data"])};
}

void ZPathfinding::PfBoxType::writeJson(std::ofstream &f) const {
    f << R"("type":{"type":"EPathFinderBoxType","data":")" << data << R"("})";
}

void ZPathfinding::Scale::readJson(simdjson::ondemand::object json) {
    type = std::string{std::string_view(json["type"])};
    simdjson::ondemand::object dataJson = json["data"];
    data.readJson(dataJson);
}

void ZPathfinding::Scale::writeJson(std::ofstream &f) const {
    f << R"("scale":{"type":"SVector3","data":{"x":)" << data.x << R"(,"y":)" << data.y << R"(,"z": )" << data.z <<
            "}}";
}

void ZPathfinding::Entity::readJson(simdjson::ondemand::object json) {
    id = std::string{std::string_view(json["id"])};
    auto result = json["name"];
    if (result.error() == simdjson::SUCCESS) {
        name = std::string{std::string_view(json["name"])};
    }
    result = json["tblu"];
    if (result.error() == simdjson::SUCCESS) {
        tblu = std::string{std::string_view(json["tblu"])};
    }
    const simdjson::ondemand::object positionJson = json["position"];
    position.readJson(positionJson);
    const simdjson::ondemand::object rotationJson = json["rotation"];
    rotation.readJson(rotationJson);
    result = json["type"];
    if (result.error() == simdjson::SUCCESS) {
        const simdjson::ondemand::object pfBoxTypeJson = json["type"];
        type.readJson(pfBoxTypeJson);
    }
    result = json["scale"];
    if (result.error() == simdjson::SUCCESS) {
        const simdjson::ondemand::object scaleJson = json["scale"];
        scale.readJson(scaleJson);
    }
}

void ZPathfinding::HashAndEntity::readJson(simdjson::ondemand::object json) {
    hash = std::string{std::string_view(json["hash"])};
    simdjson::ondemand::object entityJson = json["entity"];
    entity.readJson(entityJson);
}

void ZPathfinding::Aloc::writeJson(std::ofstream &f) const {
    f << R"({"hash":")" << hash <<
            R"(","entity":{"id":")" << id <<
            R"(","name": ")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
    pos.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << ",";
    scale.writeJson(f);
    f << "}}";
}

ZPathfinding::Alocs::Alocs(std::string fileName) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string json = simdjson::padded_string::load(fileName);
    auto jsonDocument = parser.iterate(json);
    simdjson::ondemand::array alocs = jsonDocument["alocs"];
    for (simdjson::ondemand::value hashAndEntityJson: alocs) {
        HashAndEntity hashAndEntity;
        hashAndEntity.readJson(hashAndEntityJson);
        hashesAndEntities.push_back(hashAndEntity);
    }
}

std::vector<ZPathfinding::Aloc> ZPathfinding::Alocs::readAlocs() const {
    std::vector<Aloc> alocs;
    for (const HashAndEntity &hashAndEntity: hashesAndEntities) {
        Aloc aloc;
        aloc.hash = hashAndEntity.hash;
        aloc.id = hashAndEntity.entity.id;
        aloc.name = hashAndEntity.entity.name;
        aloc.tblu = hashAndEntity.entity.tblu;
        aloc.pos = hashAndEntity.entity.position;
        aloc.rotation = hashAndEntity.entity.rotation;
        aloc.scale = hashAndEntity.entity.scale;
        alocs.emplace_back(aloc);
    }
    return alocs;
}

void ZPathfinding::PfBox::writeJson(std::ofstream &f) const {
    f << R"({"hash":"00724CDE424AFE76","entity":{"id":")" << id <<
            R"(","name": ")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
    pos.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << ",";
    type.writeJson(f);
    f << ",";
    Scale scaleJson("SVector3", scale);
    scaleJson.writeJson(f);
    f << "}}";
}

ZPathfinding::PfBoxes::PfBoxes(std::string fileName) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string json = simdjson::padded_string::load(fileName);
    auto jsonDocument = parser.iterate(json);
    simdjson::ondemand::array pfBoxes = jsonDocument["pfBoxes"];
    for (simdjson::ondemand::value hashAndEntityJson: pfBoxes) {
        HashAndEntity hashAndEntity;
        hashAndEntity.readJson(hashAndEntityJson);
        hashesAndEntities.push_back(hashAndEntity);
    }
}

void ZPathfinding::PfBoxes::readPathfindingBBoxes() {
    Scene &scene = Scene::getInstance();
    scene.exclusionBoxes.clear();
    bool includeBoxFound = false;
    for (HashAndEntity &hashAndEntity: hashesAndEntities) {
        if (hashAndEntity.entity.id == "48b22d0153942122" && abs(hashAndEntity.entity.position.x - -26.9811001) < 0.1) {
            hashAndEntity.entity.type.data = INCLUDE_TYPE; // Workaround for bug in Sapienza's include box
        }
        if (hashAndEntity.entity.type.data == INCLUDE_TYPE) {
            std::string id = hashAndEntity.entity.id;
            std::string name = hashAndEntity.entity.name;
            std::string tblu = hashAndEntity.entity.tblu;
            Vec3 p = hashAndEntity.entity.position;
            Vec3 s = hashAndEntity.entity.scale.data;
            Rotation r = hashAndEntity.entity.rotation;
            PfBoxType type = hashAndEntity.entity.type;
            scene.includeBox = {id, name, tblu, p, s, r, type};
            includeBoxFound = true;
        }
        if (hashAndEntity.entity.type.data == EXCLUDE_TYPE) {
            std::string id = hashAndEntity.entity.id;
            std::string name = hashAndEntity.entity.name;
            std::string tblu = hashAndEntity.entity.tblu;
            Vec3 p = hashAndEntity.entity.position;
            Vec3 s = hashAndEntity.entity.scale.data;
            Rotation r = hashAndEntity.entity.rotation;
            PfBoxType type = hashAndEntity.entity.type;
            scene.exclusionBoxes.emplace_back(id, name, tblu, p, s, r, type);
        }
    }
    if (!includeBoxFound) {
        Vec3 pos(0, 0, 0);
        Vec3 scale(1000, 1000, 1000);
        Rotation r;
        std::string id;
        std::string name;
        std::string tblu;
        PfBoxType type;
        scene.includeBox = {id, name, tblu, pos, scale, r, type};
    }
}

std::vector<ZPathfinding::PfBox> ZPathfinding::PfBoxes::readExclusionBoxes() const {
    std::vector<PfBox> exclusionBoxes;
    for (const HashAndEntity &hashAndEntity: hashesAndEntities) {
    }
    return exclusionBoxes;
}

void ZPathfinding::PfSeedPoint::writeJson(std::ofstream &f) {
    f << R"({"hash":"00280B8C4462FAC8","entity":{"id":")" << id <<
            R"(","name": ")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
    pos.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << "}}";
}

ZPathfinding::PfSeedPoints::PfSeedPoints(std::string fileName) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string json = simdjson::padded_string::load(fileName);
    auto jsonDocument = parser.iterate(json);
    simdjson::ondemand::array pfSeedPoints = jsonDocument["pfSeedPoints"];
    for (simdjson::ondemand::value hashAndEntityJson: pfSeedPoints) {
        HashAndEntity hashAndEntity;
        hashAndEntity.readJson(hashAndEntityJson);
        hashesAndEntities.push_back(hashAndEntity);
    }
}

std::vector<ZPathfinding::PfSeedPoint> ZPathfinding::PfSeedPoints::readPfSeedPoints() const {
    std::vector<PfSeedPoint> pfSeedPoints;
    for (const HashAndEntity &hashAndEntity: hashesAndEntities) {
        std::string id = hashAndEntity.entity.id;
        std::string name = hashAndEntity.entity.name;
        std::string tblu = hashAndEntity.entity.tblu;
        Vec3 p = hashAndEntity.entity.position;
        Rotation r = hashAndEntity.entity.rotation;
        pfSeedPoints.emplace_back(id, name, tblu, p, r);
    }
    return pfSeedPoints;
}
