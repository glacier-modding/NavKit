#include "../../include/NavKit/module/GameConnection.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include "../../extern/simdjson/simdjson.h"
#include "../../include/ConcurrentQueue/ConcurrentQueue.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavWeakness/NavPower.h"

// 46735 is a phoneword for HMSDK
constinit const char* c_EditorHost = "127.0.0.1";
constexpr uint16_t c_EditorPort = 46735;

using easywsclient::WebSocket;

GameConnection::GameConnection() = default;

GameConnection::~GameConnection() {
#ifdef _WIN32
    if (winsockInitialized) {
        WSACleanup();
    }
#endif
}

int GameConnection::connectToGame() {
#ifdef _WIN32
    if (!winsockInitialized) {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
            Logger::log(NK_ERROR, "GameConnection: WSAStartup Failed.");
            return 1;
        }
        winsockInitialized = true;
    }
#endif
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
    const auto start_time = clock::now();
    const auto timeout = std::chrono::seconds(2);

    while (ws && ws->getReadyState() != WebSocket::CLOSED) {
        if (clock::now() - start_time > timeout) {
            Logger::log(NK_WARN, "GameConnection: Timeout waiting for socket to close.");
            break;
        }

        ws->poll(10);
        ws->dispatch([&](const std::string& message) {
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
        ws->dispatch([&](const std::string& message) {
            Logger::log(NK_INFO, ("Received message: " + message).c_str());
            if (message == R"({"type":"entityTreeRebuilt"})") {
                done = true;
            }
        });
    }
    return 0;
}

int GameConnection::listNavKitSceneEntities() const {
    std::stringstream m;
    std::string onlyCollidableStr = SceneMesh::getInstance().onlyCollidable ? "true" : "false";
    m << R"({"type":"listAlocPfBoxAndSeedPointEntities","onlyCollidable":)" << onlyCollidableStr << R"(})";
    const std::string msg = m.str();
    ws->send(msg);
    if (!ws) {
        Logger::log(
            NK_ERROR, "GameConnection: listAlocPfBoxAndSeedPointEntities failed because the socket is not open.");
        return 1;
    }
    const auto file_write_queue = std::make_unique<rsj::ConcurrentQueue<std::string>>();
    const std::string SENTINEL_MESSAGE = "::DONE_WRITING::";
    std::atomic<bool> writeFailed = false;
    std::jthread writer_thread([&](std::stop_token stoken) {
        Logger::log(NK_INFO, "Writer thread started. Opening file...");
        const std::string outputFolder = NavKitSettings::getInstance().outputFolder;
        const std::filesystem::path outputFile = std::filesystem::u8path(outputFolder) / Scene::OUTPUT_SCENE_FILE_NAME;
        constexpr auto openMode = std::ios::binary | std::ios::app;
        std::ofstream f(outputFile, openMode);
        if (!f.is_open()) {
            Logger::log(NK_ERROR, "Writer thread failed to open output file: %s", outputFile.string().c_str());
            writeFailed = true;
            return;
        }

        std::error_code sizeError;
        std::uintmax_t bytesWritten = std::filesystem::file_size(outputFile, sizeError);
        if (sizeError) {
            bytesWritten = 0;
        }

        // A failed write puts the stream into a bad state where every later write is silently dropped,
        // so check each message, and on failure roll the file back to the last good size and retry.
        auto writeMessage = [&](const std::string& message) -> bool {
            constexpr int maxAttempts = 5;
            for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
                f.write(message.data(), static_cast<std::streamsize>(message.size()));
                f.flush();
                if (f) {
                    bytesWritten += message.size();
                    return true;
                }
                const int err = errno;
                Logger::log(NK_WARN, "Writer thread: write to '%s' failed (attempt %d of %d): %s",
                    outputFile.string().c_str(), attempt, maxAttempts, std::strerror(err));
                f.close();
                f.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
                std::error_code resizeError;
                std::filesystem::resize_file(outputFile, bytesWritten, resizeError);
                if (resizeError) {
                    Logger::log(
                        NK_WARN, "Writer thread: could not roll back output file: %s", resizeError.message().c_str());
                }
                f.open(outputFile, openMode);
            }
            return false;
        };

        while (!stoken.stop_requested()) {
            if (auto message_to_write = file_write_queue->try_pop(); message_to_write.has_value()) {
                if (message_to_write.value() == SENTINEL_MESSAGE) {
                    break;
                }
                if (!writeFailed && !writeMessage(message_to_write.value())) {
                    Logger::log(NK_ERROR, "Writer thread: giving up writing '%s'. The scene file is incomplete.",
                        outputFile.string().c_str());
                    writeFailed = true;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        f.close();
        if (!f) {
            Logger::log(NK_ERROR, "Writer thread: failed to close '%s'.", outputFile.string().c_str());
            writeFailed = true;
        }
        Logger::log(NK_INFO, "Writer thread finished writing.");
    });

    bool done = false;
    int messagesReceived = 0;
    // A game that stops answering would otherwise leave this waiting forever.
    constexpr auto idleTimeout = std::chrono::minutes(5);
    auto lastMessageTime = std::chrono::steady_clock::now();
    while (!done) {
        if (!ws || ws->getReadyState() == WebSocket::CLOSED) {
            Logger::log(NK_ERROR, "GameConnection: Editor connection closed unexpectedly.");
            file_write_queue->push(SENTINEL_MESSAGE);
            if (writer_thread.joinable()) {
                writer_thread.join();
            }
            return 1;
        }
        if (std::chrono::steady_clock::now() - lastMessageTime > idleTimeout) {
            Logger::log(NK_ERROR, "GameConnection: The game sent nothing for 5 minutes after %d entities; giving up.",
                messagesReceived);
            file_write_queue->push(SENTINEL_MESSAGE);
            if (writer_thread.joinable()) {
                writer_thread.join();
            }
            return 1;
        }

        ws->poll(10);
        if (!ws || ws->getReadyState() == WebSocket::CLOSED) {
            continue;
        }

        ws->dispatch([&](const std::string& message) {
            messagesReceived++;
            lastMessageTime = std::chrono::steady_clock::now();
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
                Logger::log(NK_ERROR, "Failed to get entities from game. Is ZHMModSDK up to date?");
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

    if (writeFailed) {
        Logger::log(NK_ERROR, "Failed to save entities to file.");
        return 1;
    }
    Logger::log(NK_INFO, "Saved entities to file.");
    return 0;
}

void GameConnection::sendHelloMessage() const {
    ws->send(R"({"type":"hello","identifier":"NavKit"})");
}
