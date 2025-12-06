#include "../../include/NavKit/module/GameConnection.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <memory>
#include "../../include/ConcurrentQueue/ConcurrentQueue.h"
#include "../../extern/simdjson/simdjson.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/Settings.h"
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
    auto file_write_queue = std::make_unique<rsj::ConcurrentQueue<std::string> >();
    const std::string SENTINEL_MESSAGE = "::DONE_WRITING::";
    std::thread writer_thread([&] {
        Logger::log(NK_INFO, "Writer thread started. Opening file...");
        const std::string outputFolder = Settings::getInstance().outputFolder;
        std::ofstream f(outputFolder + "\\output.nav.json", std::ios::app);
        if (!f.is_open()) {
            Logger::log(NK_ERROR, "Writer thread failed to open output file: %s",
                        (outputFolder + "\\output.nav.json").c_str());
            return;
        }

        while (true) {
            if (auto message_to_write = file_write_queue->try_pop(); message_to_write.has_value()) {
                if (message_to_write.value() == SENTINEL_MESSAGE) {
                    break;
                }
                f << message_to_write.value();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        f.close();
        Logger::log(NK_INFO, "Writer thread finished writing.");
    });

    bool done = false;
    int messagesReceived = 0;
    while (!done) {
        ws->poll(10);
        ws->dispatch([&](const std::string &message) {
            messagesReceived++;
            if (message == "Done sending entities.") {
                done = true;
                return;
            }
            if (message == R"({"type":"entityTreeRebuilt"})") {
                Logger::log(NK_INFO, "Received entityTreeRebuilt message");
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
                            "Failed to get ALOCs from game. Is ZHMModSDK up to date?");
                done = true;
                return;
            }

            file_write_queue->push(message);

            if (messagesReceived % 1000 == 0) {
                Logger::log(NK_INFO, "Entities received: %d", messagesReceived);
            }
        });
    }

    Logger::log(NK_INFO, "Received all entities (%d). Signaling writer thread to finish.", messagesReceived);
    file_write_queue->push(SENTINEL_MESSAGE);

    writer_thread.join();

    Logger::log(NK_INFO, "Saved entities to file.");
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
