#include "../../include/NavKit/model/ZPathfinding.h"
#include "../../include/NavKit/module/Scene.h"

void ZPathfinding::Vec3::readJson(simdjson::ondemand::object json) {
    x = static_cast<float>(static_cast<double>(json["x"]));
    y = static_cast<float>(static_cast<double>(json["y"]));
    z = static_cast<float>(static_cast<double>(json["z"]));
}

void ZPathfinding::Vec3::writeJson(std::ostream &f) const {
    f << R"("position":{"x":)" << x << R"(,"y":)" << y << R"(,"z": )" << z << "}";
}

void ZPathfinding::Rotation::readJson(simdjson::ondemand::object json) {
    x = static_cast<float>(static_cast<double>(json["x"]));
    y = static_cast<float>(static_cast<double>(json["y"]));
    z = static_cast<float>(static_cast<double>(json["z"]));
    w = static_cast<float>(static_cast<double>(json["w"]));
}

void ZPathfinding::Rotation::writeJson(std::ostream &f) const {
    f << R"("rotation":{"x":)" << x << R"(,"y":)" << y << R"(,"z": )" << z << R"(,"w": )" << w << "}";
}

void ZPathfinding::PfBoxType::readJson(simdjson::ondemand::object json) {
    type = std::string{std::string_view(json["type"])};
    data = std::string{std::string_view(json["data"])};
}

void ZPathfinding::PfBoxType::writeJson(std::ostream &f) const {
    f << R"("type":{"type":"EPathFinderBoxType","data":")" << data << R"("})";
}

void ZPathfinding::Scale::readJson(simdjson::ondemand::object json) {
    type = std::string{std::string_view(json["type"])};
    simdjson::ondemand::object dataJson = json["data"];
    data.readJson(dataJson);
}

void ZPathfinding::Scale::writeJson(std::ostream &f) const {
    f << R"("scale":{"type":"SVector3","data":{"x":)" << data.x << R"(,"y":)" << data.y << R"(,"z": )" << data.z <<
            "}}";
}

void ZPathfinding::Entity::readJson(simdjson::ondemand::object json) {
    auto result = json["id"];
    if (result.error() == simdjson::SUCCESS) {
        id = std::string{std::string_view(json["id"])};
    }
    result = json["name"];
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

void ZPathfinding::HashesAndEntity::readJson(simdjson::ondemand::object json) {
    alocHash = std::string{std::string_view(json["alocHash"])};
    primHash = std::string{std::string_view(json["primHash"])};
    roomName = std::string{std::string_view(json["roomName"])};
    roomFolderName = std::string{std::string_view(json["roomFolderName"])};
    simdjson::ondemand::object entityJson = json["entity"];
    entity.readJson(entityJson);
}

void ZPathfinding::Mesh::writeJson(std::ostream &f) const {
    f << R"({"alocHash":")" << alocHash <<
            R"(","primHash":")" << primHash <<
            R"(","roomName":")" << roomName <<
            R"(","roomFolderName":")" << roomFolderName <<
            R"(","entity":{"id":")" << id <<
            R"(","name":")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
    pos.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << ",";
    scale.writeJson(f);
    f << "}}";
}

ZPathfinding::Meshes::Meshes(simdjson::ondemand::array alocs) {
    for (simdjson::ondemand::value hashAndEntityJson: alocs) {
        HashesAndEntity hashAndEntity;
        hashAndEntity.readJson(hashAndEntityJson);
        hashesAndEntities.push_back(hashAndEntity);
    }
}

std::vector<ZPathfinding::Mesh> ZPathfinding::Meshes::readMeshes() const {
    std::vector<Mesh> meshes;
    for (const HashesAndEntity &hashAndEntity: hashesAndEntities) {
        Mesh mesh;
        mesh.alocHash = hashAndEntity.alocHash;
        mesh.primHash = hashAndEntity.primHash;
        mesh.roomName = hashAndEntity.roomName;
        mesh.roomFolderName = hashAndEntity.roomFolderName;
        mesh.id = hashAndEntity.entity.id;
        mesh.name = hashAndEntity.entity.name;
        mesh.tblu = hashAndEntity.entity.tblu;
        mesh.pos = hashAndEntity.entity.position;
        mesh.rotation = hashAndEntity.entity.rotation;
        mesh.scale = hashAndEntity.entity.scale;
        meshes.emplace_back(mesh);
    }
    return meshes;
}

void ZPathfinding::PfBox::writeJson(std::ostream &f) const {
    f << R"({"id":")" << id <<
            R"(","name":")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
    pos.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << ",";
    type.writeJson(f);
    f << ",";
    Scale scaleJson("SVector3", scale);
    scaleJson.writeJson(f);
    f << "}";
}

ZPathfinding::PfBoxes::PfBoxes(simdjson::ondemand::array pfBoxes) {
    for (simdjson::ondemand::value entityJson: pfBoxes) {
        Entity entity;
        entity.readJson(entityJson);
        entities.push_back(entity);
    }
}

void ZPathfinding::PfBoxes::readPathfindingBBoxes() {
    Scene &scene = Scene::getInstance();
    scene.exclusionBoxes.clear();
    bool includeBoxFound = false;
    for (Entity &entity: entities) {
        if (entity.id == "48b22d0153942122" && abs(entity.position.x - -26.9811001) < 0.1) {
            entity.type.data = INCLUDE_TYPE; // Workaround for bug in Sapienza's include box
        }
        if (entity.type.data == INCLUDE_TYPE) {
            std::string id = entity.id;
            std::string name = entity.name;
            std::string tblu = entity.tblu;
            Vec3 p = entity.position;
            Vec3 s = entity.scale.data;
            Rotation r = entity.rotation;
            PfBoxType type = entity.type;
            scene.includeBox = {id, name, tblu, p, s, r, type};
            includeBoxFound = true;
        }
        if (entity.type.data == EXCLUDE_TYPE) {
            std::string id = entity.id;
            std::string name = entity.name;
            std::string tblu = entity.tblu;
            Vec3 p = entity.position;
            Vec3 s = entity.scale.data;
            Rotation r = entity.rotation;
            PfBoxType type = entity.type;
            scene.exclusionBoxes.emplace_back(id, name, tblu, p, s, r, type);
        }
    }
    if (!includeBoxFound) {
        Vec3 pos(0, 0, 0);
        Vec3 scale(1000, 1000, 1000);
        Rotation r;
        std::string id = NO_INCLUDE_BOX_FOUND;
        std::string name;
        std::string tblu;
        PfBoxType type;
        scene.includeBox = {id, name, tblu, pos, scale, r, type};
    }
}

void ZPathfinding::PfSeedPoint::writeJson(std::ostream &f) const {
    f << R"({"id":")" << id <<
            R"(","name":")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
    pos.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << "}";
}

ZPathfinding::PfSeedPoints::PfSeedPoints(simdjson::ondemand::array pfSeedPoints) {
    for (simdjson::ondemand::value entityJson: pfSeedPoints) {
        Entity entity;
        entity.readJson(entityJson);
        entities.push_back(entity);
    }
}

std::vector<ZPathfinding::PfSeedPoint> ZPathfinding::PfSeedPoints::readPfSeedPoints() const {
    std::vector<PfSeedPoint> pfSeedPoints;
    for (const Entity &entity: entities) {
        std::string id = entity.id;
        std::string name = entity.name;
        std::string tblu = entity.tblu;
        Vec3 p = entity.position;
        Rotation r = entity.rotation;
        pfSeedPoints.emplace_back(id, name, tblu, p, r);
    }
    return pfSeedPoints;
}
