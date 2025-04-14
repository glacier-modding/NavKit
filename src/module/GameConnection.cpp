#define NOMINMAX
#include "../../include/NavKit/module/GameConnection.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include "../../extern/simdjson/simdjson.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavWeakness/NavPower.h"

// 46735 is a phoneword for HMSDK
constinit const char *c_EditorHost = "127.0.0.1";
constexpr uint16_t c_EditorPort = 46735;

using easywsclient::WebSocket;

GameConnection::GameConnection() {
}

GameConnection::~GameConnection() {
    WSACleanup();
}

int GameConnection::connectToGame() {
    if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
        Logger::log(NK_ERROR, "GameConnection: WSAStartup Failed.");
        return 1;
    }
    ws.reset(WebSocket::from_url("ws://localhost:46735/socket"));
    if (!ws) {
        Logger::log(NK_ERROR, "GameConnection: Could not connect to game. Is the game running?");
        return 1;
    }
    sendHelloMessage();

    return 0;
}

int GameConnection::closeConnection() const {
    if (!ws || ws->getReadyState() == WebSocket::CLOSED) {
        Logger::log(NK_ERROR, "GameConnection: closeConnection failed because the socket is not open.");
        return 1;
    }
    const WebSocket::pointer wsp = &*ws;
    wsp->close();
    return 0;
}

int GameConnection::rebuildEntityTree() const {
    std::stringstream m;
    m << R"({"type":"rebuildEntityTree"})";
    const std::string msg = m.str();
    ws->send(msg);
    if (!ws) {
        Logger::log(NK_ERROR, "GameConnection: rebuildEntityTree failed because the socket is not open.");
        return 1;
    }
    bool done = false;
    while (!done) {
        ws->poll();
        ws->dispatch([&](const std::string &message) {
            Logger::log(NK_INFO, ("Received message: " + message).c_str());
            if (message == R"({"type":"entityTreeRebuilt"})") {
                done = true;
            }
        });
    }
    return 0;
}

int GameConnection::listAlocEntities() const {
    std::stringstream m;
    m << R"({"type":"listAlocEntities"})";
    const std::string msg = m.str();
    ws->send(msg);
    if (!ws) {
        Logger::log(NK_ERROR, "GameConnection: listAlocEntities failed because the socket is not open.");
        return 1;
    }
    std::ofstream f(SceneExtract::getInstance().lastOutputFolder + "\\output.nav.json", std::ios::app);
    f << R"({"alocs":[)";
    bool done = false;
    bool isFirst = true;
    while (!done) {
        ws->poll();
        ws->dispatch([&](const std::string &message) {
            Logger::log(NK_INFO, ("Received message: " + message).c_str());
            if (message == "Done sending entities.") {
                done = true;
                return;
            }
            if (message.find("Unknown editor message type: listAlocEntities") != -1) {
                Logger::log(NK_ERROR,
                            "Failed to get ALOCs from game. Is the included Editor.dll in the Retail/mods folder?");
                done = true;
                return;
            }
            if (!isFirst) {
                f << ",";
            }
            isFirst = false;
            f << message;
        });
    }
    f << R"(],"pfBoxes":[)";
    f.close();
    return 0;
}

int GameConnection::listPfBoxEntities() const {
    std::stringstream m;
    m << R"({"type":"listPfBoxEntities"})";
    const std::string msg = m.str();
    ws->send(msg);
    if (!ws) {
        Logger::log(NK_ERROR, "GameConnection: listPfBoxEntities failed because the socket is not open.");
        return 1;
    }
    std::ofstream f(SceneExtract::getInstance().lastOutputFolder + "\\output.nav.json", std::ios::app);
    bool done = false;
    bool isFirst = true;
    while (!done) {
        ws->poll();
        ws->dispatch([&](const std::string &message) {
            Logger::log(NK_INFO, ("Received message: " + message).c_str());
            if (message == "Done sending entities.") {
                done = true;
                return;
            }
            if (message.find("Unknown editor message type: listPfBoxEntities") != -1) {
                Logger::log(NK_ERROR,
                            "Failed to get Pathfinding Boxes from game. Is the included Editor.dll in the Retail/mods folder?");
                done = true;
                return;
            }
            if (!isFirst) {
                f << ",";
            }
            isFirst = false;
            f << message;
        });
    }
    f << R"(],"pfSeedPoints":[)";
    f.close();
    return 0;
}

int GameConnection::listPfSeedPointEntities() const {
    std::stringstream m;
    m << R"({"type":"listPfSeedPointEntities"})";
    const std::string msg = m.str();
    ws->send(msg);
    if (!ws) {
        Logger::log(NK_ERROR, "GameConnection: listPfSeedPointEntities failed because the socket is not open.");
        return 1;
    }
    std::ofstream f(SceneExtract::getInstance().lastOutputFolder + "\\output.nav.json", std::ios::app);
    bool done = false;
    bool isFirst = true;
    while (!done) {
        ws->poll();
        ws->dispatch([&](const std::string &message) {
            Logger::log(NK_INFO, ("Received message: " + message).c_str());
            if (message == "Done sending entities.") {
                done = true;
                return;
            }
            if (message.find("Unknown editor message type: listPfSeedPointEntities") != -1) {
                Logger::log(NK_ERROR,
                            "Failed to get Pathfinding Seed Points from game. Is the included Editor.dll in the Retail/mods folder?");
                done = true;
                return;
            }
            if (!isFirst) {
                f << ",";
            }
            isFirst = false;
            f << message;
        });
    }
    f << "]}";
    f.close();
    return 0;
}

void GameConnection::sendHelloMessage() const {
    ws->send(R"({"type":"hello","identifier":"glacier2obj"})");
}

void GameConnection::sendNavp(NavPower::NavMesh *navMesh) const {
    if (!ws) {
        Logger::log(NK_ERROR, "GameConnection: Send Navp failed because the socket is not open.");
        return;
    }
    int chunkSize = 30;
    auto chunkHead = navMesh->m_areas.begin();
    int areaCount = navMesh->m_areas.size();
    int chunkCount = (areaCount % chunkSize == 0) ? (areaCount / chunkSize) : ((areaCount / chunkSize) + 1);
    for (int chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++) {
        int curChunkSize = (chunkIndex < (chunkCount - 1)) ? chunkSize : (areaCount % chunkSize);
        std::vector<NavPower::Area> areas = std::vector<NavPower::Area>(chunkHead, chunkHead + curChunkSize);
        sendChunk(areas, chunkIndex, chunkCount);
        chunkHead += curChunkSize;
    }
    if (!ws) {
        Logger::log(NK_ERROR, "GameConnection: HandleMessages failed because the socket is not open.");
        return;
    }
    while (ws->getReadyState() != WebSocket::CLOSED) {
        WebSocket::pointer wsp = &*ws;
        ws->poll();
        ws->dispatch([&](const std::string &message) {
            Logger::log(NK_INFO, ("Received message: " + message).c_str());
            if (message == "Done loading Navp." || message == "Done sending entities." || message ==
                R"({"type":"entityTreeRebuilt"})") {
                wsp->close();
            }
            if (message.find("Unknown editor message type: loadNavp") != -1) {
                Logger::log(NK_ERROR,
                            "Failed to send Navp to game. Is the included Editor.dll in the Retail/mods folder?");
                wsp->close();
            }
        });
    }
}

void GameConnection::sendChunk(const std::vector<NavPower::Area> &areas, int chunkIndex, int chunkCount) const {
    std::stringstream m;
    m << std::fixed << std::setprecision(3);
    m << R"({"type":"loadNavp","chunkCount":)";
    m << chunkCount;
    m << ",\"chunk\":";
    m << chunkIndex;
    m << ",\"areas\":[";
    int numArea = 0;

    for (auto &navPowerArea: areas) {
        numArea++;
        m << "[";
        int numEdge = 0;
        for (auto &edge: navPowerArea.m_edges) {
            numEdge++;
            auto point = edge->m_pos;
            m << "[";
            m << point.X;
            m << ",";
            m << point.Y;
            m << ",";
            m << point.Z;
            m << "]";
            if (numEdge != navPowerArea.m_edges.size()) {
                m << ",";
            }
        }
        m << "]";
        if (numArea != areas.size()) {
            m << ",";
        }
    }
    m << "]}";
    const std::string msg = m.str();
    ws->send(msg);
}
