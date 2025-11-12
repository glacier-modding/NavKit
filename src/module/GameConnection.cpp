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
        Logger::log(NK_ERROR, "GameConnection: closeConnection failed because the Editor connection is not open.");
        return 1;
    }
    const WebSocket::pointer wsp = &*ws;
    wsp->close();

    using clock = std::chrono::steady_clock;
    auto start_time = clock::now();
    auto timeout = std::chrono::seconds(2);

    while (ws && ws->getReadyState() != WebSocket::CLOSED) {
        if (clock::now() - start_time > timeout) {
            Logger::log(NK_WARN, "GameConnection: Timeout waiting for socket to close.");
            break;
        }

        ws->poll(10);
        ws->dispatch([&](const std::string &message) {
            Logger::log(NK_INFO, ("GameConnection: Received message during close attempt: " + message).c_str());
        });
    }

    if (ws && ws->getReadyState() == WebSocket::CLOSED) {
        Logger::log(NK_INFO, "GameConnection: Editor connection closed.");
    } else if (!ws) {
        Logger::log(NK_WARN, "GameConnection: Editor connection became invalid during close polling.");
    } else {
        Logger::log(NK_WARN, "GameConnection: Socket state is still '%d' after close attempt.",
                    static_cast<int>(ws->getReadyState()));
    }

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

int GameConnection::listAlocPfBoxAndSeedPointEntities() const {
    std::stringstream m;
    m << R"({"type":"listAlocPfBoxAndSeedPointEntities"})";
    const std::string msg = m.str();
    ws->send(msg);
    if (!ws) {
        Logger::log(
            NK_ERROR, "GameConnection: listAlocPfBoxAndSeedPointEntities failed because the socket is not open.");
        return 1;
    }
    std::ofstream f(SceneExtract::getInstance().lastOutputFolder + "\\output.nav.json", std::ios::app);
    bool done = false;
    int messagesReceived = 0;
    while (!done) {
        ws->poll();
        ws->dispatch([&](const std::string &message) {
            messagesReceived++;
            if (messagesReceived % 100 == 0) {
                Logger::log(NK_INFO, ("Entities found: " + std::to_string(messagesReceived)).c_str());
            }
            if (message == "Done sending entities.") {
                done = true;
                return;
            }
            if (message == "Rebuilding tree.") {
                Logger::log(NK_INFO, "Rebuilding Entity Tree...");
                return;
            }
            if (message == R"({"type":"welcome"})") {
                return;
            }
            if (message.find("Unknown editor message type: listAlocPfBoxAndSeedPointEntities") != -1) {
                Logger::log(NK_ERROR,
                            "Failed to get ALOCs from game. Is the included Editor.dll in the Retail/mods folder?");
                done = true;
                return;
            }
            f << message;
        });
    }
    f.close();
    return 0;
}

void GameConnection::sendHelloMessage() const {
    ws->send(R"({"type":"hello","identifier":"NavKit"})");
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
