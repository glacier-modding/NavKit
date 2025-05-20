#pragma once

#include <vector>
#include "../../easywsclient/easywsclient.hpp"
#ifdef _WIN32
#pragma comment( lib, "ws2_32" )
#include <WinSock2.h>
#endif
#include <memory>

namespace NavPower {
    class Area;
    class NavMesh;
}

using easywsclient::WebSocket;


class GameConnection {
    explicit GameConnection();

public:
    static GameConnection &getInstance() {
        static GameConnection instance;
        return instance;
    }

    ~GameConnection();

    void sendNavp(NavPower::NavMesh *navMesh) const;

    void sendChunk(const std::vector<NavPower::Area> &areas, int chunkIndex, int chunkCount) const;

    int connectToGame();

    [[nodiscard]] int closeConnection() const;

    [[nodiscard]] int rebuildEntityTree() const;

    [[nodiscard]] int listAlocPfBoxAndSeedPointEntities() const;

private:
    void sendHelloMessage() const;

    WSADATA wsaData{};
    std::unique_ptr<WebSocket> ws;
};
