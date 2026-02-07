#include "../../include/NavKit/model/Json.h"

#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Scene.h"

void Json::Vec3::readJson(simdjson::ondemand::object json) {
    x = static_cast<float>(static_cast<double>(json["x"]));
    y = static_cast<float>(static_cast<double>(json["y"]));
    z = static_cast<float>(static_cast<double>(json["z"]));
}

void Json::Vec3::writeJson(std::ostream &f, const std::string& propertyName) const {
    f << R"(")" << propertyName << R"(":{"x":)" << x << R"(,"y":)" << y << R"(,"z": )" << z << "}";
}

void Json::Rotation::readJson(simdjson::ondemand::object json) {
    x = static_cast<float>(static_cast<double>(json["x"]));
    y = static_cast<float>(static_cast<double>(json["y"]));
    z = static_cast<float>(static_cast<double>(json["z"]));
    w = static_cast<float>(static_cast<double>(json["w"]));
}

void Json::Rotation::writeJson(std::ostream &f) const {
    f << R"("rotation":{"x":)" << x << R"(,"y":)" << y << R"(,"z": )" << z << R"(,"w": )" << w << "}";
}

void Json::PfBoxType::readJson(simdjson::ondemand::object json) {
    type = std::string{std::string_view(json["type"])};
    data = std::string{std::string_view(json["data"])};
}

void Json::PfBoxType::writeJson(std::ostream &f) const {
    f << R"("type":{"type":"EPathFinderBoxType","data":")" << data << R"("})";
}

void Json::Vec3Wrapped::readJson(simdjson::ondemand::object json) {
    type = std::string{std::string_view(json["type"])};
    simdjson::ondemand::object dataJson = json["data"];
    data.readJson(dataJson);
}

void Json::Vec3Wrapped::writeJson(std::ostream &f, std::string propertyName) const {
    f << R"(")" << propertyName << R"(":{"type":")" << type << R"(","data":{"x":)" << data.x << R"(,"y":)" << data.y << R"(,"z": )" << data.z <<
            "}}";
}

void Json::Entity::readJson(simdjson::ondemand::object json) {
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

void Json::Entity::writeJson(std::ostream& f) const {
    f << R"({"id":")" << id <<
            R"(","name":")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
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
    f << "}";
}

void Json::HashesAndEntity::readJson(simdjson::ondemand::object json) {
    alocHash = std::string{std::string_view(json["alocHash"])};
    primHash = std::string{std::string_view(json["primHash"])};
    roomName = std::string{std::string_view(json["roomName"])};
    roomFolderName = std::string{std::string_view(json["roomFolderName"])};
    simdjson::ondemand::object entityJson = json["entity"];
    entity.readJson(entityJson);
}

void Json::Mesh::writeJson(std::ostream &f) const {
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

Json::Meshes::Meshes(simdjson::ondemand::array alocs) {
    for (simdjson::ondemand::value hashAndEntityJson: alocs) {
        HashesAndEntity hashAndEntity;
        hashAndEntity.readJson(hashAndEntityJson);
        hashesAndEntities.push_back(hashAndEntity);
    }
}

std::vector<Json::Mesh> Json::Meshes::readMeshes() const {
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

void Json::PfBox::writeJson(std::ostream &f) const {
    f << R"({"id":")" << id <<
            R"(","name":")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
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
    for (simdjson::ondemand::value entityJson: pfBoxes) {
        Entity entity;
        entity.readJson(entityJson);
        entities.push_back(entity);
    }
}

void Json::PfBoxes::readPathfindingBBoxes() {
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
        type.type = "EPathFinderBoxType";
        type.data = INCLUDE_TYPE;
        scene.includeBox = {id, name, tblu, pos, scale, r, type};
    }
}

Json::Gates::Gates(simdjson::ondemand::array gatesJson) {
    for (simdjson::ondemand::value gateJson: gatesJson) {
        Gate gate;
        gate.readJson(gateJson);
        gates.push_back(gate);
    }
}

void Json::Room::writeJson(std::ostream& f) const {
    f << R"({"id":")" << id <<
            R"(","name":")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
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
    const simdjson::ondemand::object roomExtentMinJson = json["roomExtentMin"];
    roomExtentMin.readJson(roomExtentMinJson);
    const simdjson::ondemand::object roomExtentMaxJson = json["roomExtentMax"];
    roomExtentMax.readJson(roomExtentMaxJson);
}

Json::Rooms::Rooms(simdjson::ondemand::array roomsJson) {
    for (simdjson::ondemand::value roomJson: roomsJson) {
        Room room;
        room.readJson(roomJson);
        rooms.push_back(room);
    }
}

void Json::Gate::writeJson(std::ostream &f) const {
    f << R"({"id":")" << id <<
            R"(","name":")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
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
    const simdjson::ondemand::object bboxCenterJson = json["bboxCenter"];
    bboxCenter.readJson(bboxCenterJson);
    const simdjson::ondemand::object bboxHalfSizeJson = json["bboxHalfSize"];
    bboxHalfSize.readJson(bboxHalfSizeJson);
}

void Json::AiAreaWorld::writeJson(std::ostream& f) const {
    f << R"({"id":")" << id <<
            R"(","name":")" << name;
    if (!tblu.empty()) {
        f << R"(","tblu":")" << tblu;
    }
    f <<R"(",)";
    rotation.writeJson(f);
    f << "}";
}

void Json::AiAreaWorld::readJson(simdjson::ondemand::object json) {
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
    const simdjson::ondemand::object rotationJson = json["rotation"];
    rotation.readJson(rotationJson);
}

Json::AiAreaWorlds::AiAreaWorlds(simdjson::ondemand::array aiAreaWorldsJson) {
    for (simdjson::ondemand::value aiAreaWorldJson: aiAreaWorldsJson) {
        AiAreaWorld aiAreaWorld;
        aiAreaWorld.readJson(aiAreaWorldJson);
        aiAreaWorlds.push_back(aiAreaWorld);
    }
}

void Json::ParentData::writeJson(std::ostream& f) const {
    f << R"("data":{"id":")" << id <<
            R"(","name":")" << name <<
            R"(","source":")" << source <<
            R"(","tblu":")" << tblu <<
            R"(","type":")" << type << R"("})";
}

void Json::ParentData::readJson(simdjson::ondemand::object json) {
    id = std::string{std::string_view(json["id"])};
    name = std::string{std::string_view(json["name"])};
    source = std::string{std::string_view(json["source"])};
    tblu = std::string{std::string_view(json["tblu"])};
    type = std::string{std::string_view(json["type"])};}

void Json::Parent::writeJson(std::ostream& f) const {
    f << R"("parent":{"type":")" << type <<
            R"(",)";
    data.writeJson(f);
    f << "}";
}

void Json::Parent::readJson(simdjson::ondemand::object json) {
    type = std::string{std::string_view(json["type"])};
    const simdjson::ondemand::object dataJson = json["data"];
    data.readJson(dataJson);
}

void Json::AiArea::writeJson(std::ostream& f) const {
    f << R"({"id":")" << id <<
            R"(","name":")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
    rotation.writeJson(f);
    f << R"(,"logicalParent":[)";
    auto separator = "";
    for (auto logicalParent : logicalParents) {
        f << separator;
        f << R"(")" << logicalParent << R"(")";
        separator = ",";
    }
    f << R"(],"areaVolumeNames":[)";
    separator = "";
    for (auto areaVolumeName : areaVolumeNames) {
        f << separator;
        f << R"(")" << areaVolumeName << R"(")";
        separator = ",";
    }
    f << R"(],)";
    parent.writeJson(f);
    f << "}";
}

void Json::AiArea::readJson(simdjson::ondemand::object json) {
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
    for (simdjson::ondemand::value aiAreaJson: aiAreasJson) {
        AiArea aiArea;
        aiArea.readJson(aiAreaJson);
        aiAreas.push_back(aiArea);
    }
}

Json::VolumeBoxes::VolumeBoxes(simdjson::ondemand::array volumeBoxesJson) {
    for (simdjson::ondemand::value volumeBoxJson: volumeBoxesJson) {
        Entity volumeBox;
        volumeBox.readJson(volumeBoxJson);
        volumeBoxes.push_back(volumeBox);
    }
}

void Json::Radius::writeJson(std::ostream& f) const {
    f << R"("radius":{"type":")" << type <<
            R"(","data":")" << data << R"("})";
}

void Json::Radius::readJson(simdjson::ondemand::object json) {
    type = std::string{std::string_view(json["type"])};
    data = static_cast<float>(static_cast<double>(json["data"]));
}

void Json::VolumeSphere::writeJson(std::ostream& f) const {
    f << R"({"id":")" << id <<
            R"(","name":")" << name << R"(",)";
    position.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << ",";
    radius.writeJson(f);
    f << "}";
}

void Json::VolumeSphere::readJson(simdjson::ondemand::object json) {
    auto result = json["id"];
    if (result.error() == simdjson::SUCCESS) {
        id = std::string{std::string_view(json["id"])};
    }
    result = json["name"];
    if (result.error() == simdjson::SUCCESS) {
        name = std::string{std::string_view(json["name"])};
    }
    const simdjson::ondemand::object positionJson = json["position"];
    position.readJson(positionJson);
    const simdjson::ondemand::object radiusJson = json["radius"];
    radius.readJson(radiusJson);
}

Json::VolumeSpheres::VolumeSpheres(simdjson::ondemand::array volumeSpheresJson) {
    for (simdjson::ondemand::value volumeSphereJson: volumeSpheresJson) {
        VolumeSphere volumeSphere;
        volumeSphere.readJson(volumeSphereJson);
        volumeSpheres.push_back(volumeSphere);
    }
}

void Json::PfSeedPoint::writeJson(std::ostream &f) const {
    f << R"({"id":")" << id <<
            R"(","name":")" << name <<
            R"(","tblu":")" << tblu << R"(",)";
    pos.writeJson(f);
    f << ",";
    rotation.writeJson(f);
    f << "}";
}

Json::PfSeedPoints::PfSeedPoints(simdjson::ondemand::array pfSeedPoints) {
    for (simdjson::ondemand::value entityJson: pfSeedPoints) {
        Entity entity;
        entity.readJson(entityJson);
        entities.push_back(entity);
    }
}

std::vector<Json::PfSeedPoint> Json::PfSeedPoints::readPfSeedPoints() const {
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

void Json::Mati::readJsonFromMatiFile(simdjson::simdjson_result<simdjson::ondemand::document> &jsonDocument) {
    hash = std::string{std::string_view(jsonDocument["id"])};
    className = std::string{std::string_view(jsonDocument["class"])};
    simdjson::ondemand::object propertiesJson = jsonDocument["properties"];
    auto result = propertiesJson["mapTexture2D_01"];
    if (result.error() == simdjson::SUCCESS) {
        simdjson::ondemand::object diffuseJson = propertiesJson["mapTexture2D_01"];
        diffuse = std::string{std::string_view(diffuseJson["value"])};
    } else {
        Logger::log(NK_ERROR, "No diffuse texture found for %s", hash.c_str());
    }
    result = propertiesJson["mapTexture2DNormal_01"];
    if (result.error() == simdjson::SUCCESS) {
        simdjson::ondemand::object normalJson = propertiesJson["mapTexture2DNormal_01"];
        normal = std::string{std::string_view(normalJson["value"])};
    }
    result = propertiesJson["mapTexture2D_03"];
    if (result.error() == simdjson::SUCCESS) {
        simdjson::ondemand::object specularJson = propertiesJson["mapTexture2D_03"];
        specular = std::string{std::string_view(specularJson["value"])};
    }
}

void Json::Mati::readJsonFromScene(simdjson::ondemand::object jsonDocument) {
    hash = std::string{std::string_view(jsonDocument["hash"])};
    className = std::string{std::string_view(jsonDocument["class"])};
    diffuse = std::string{std::string_view(jsonDocument["diffuse"])};
    normal = std::string{std::string_view(jsonDocument["normal"])};
    specular = std::string{std::string_view(jsonDocument["specular"])};
}


void Json::Mati::writeJson(std::ostream &f) const {
    f << R"({"hash":")" << hash <<
            R"(","class":")" << className <<
            R"(","diffuse":")" << diffuse <<
            R"(","normal":")" << normal <<
            R"(","specular":")" << specular;
    f << R"("})";
}

Json::Matis::Matis(simdjson::ondemand::array matisJson) {
    for (simdjson::ondemand::value matiJson: matisJson) {
        Mati mati;
        mati.readJsonFromScene(matiJson);
        matis.push_back(mati);
    }
}

void Json::PrimMati::readJson(simdjson::ondemand::object jsonDocument) {
    primHash = std::string{std::string_view(jsonDocument["primHash"])};
    simdjson::ondemand::array matiHashesJson = jsonDocument["matiHashes"];
    matiHashes.clear();
    for (simdjson::ondemand::value matiHashJson: matiHashesJson) {
        matiHashes.push_back(std::string{std::string_view(matiHashJson)});
    }
}

void Json::PrimMati::writeJson(std::ostream& f) const {
    f << R"({"primHash":")" << primHash << R"(","matiHashes":[)";
    bool first = true;
    for (auto matiHash : matiHashes) {
        if (!first) {
            f << ",";
        } else {
            first = false;
        }
        f << R"(")" << matiHash << R"(")";
    }
    f << R"(]})";
}

Json::PrimMatis::PrimMatis(simdjson::ondemand::array primMatisJson) {
    for (const simdjson::ondemand::object primMatiJson: primMatisJson) {
        PrimMati primMati;
        primMati.readJson(primMatiJson);
        primMatis.push_back(primMati);
    }
}
