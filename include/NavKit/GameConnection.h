#pragma once

#include <vector>
#include "../NavWeakness/NavPower.h"
#include "../easywsclient/easywsclient.hpp"
#ifdef _WIN32
#pragma comment( lib, "ws2_32" )
#include <WinSock2.h>
#endif
#include <memory>
using easywsclient::WebSocket;

class NavKit;

class GameConnection {
public:
    explicit GameConnection(NavKit *nKit);

    ~GameConnection();

    void SendNavp(NavPower::NavMesh *navMesh);

    void SendChunk(std::vector<NavPower::Area> areas, int chunkIndex, int chunkCount);

    void HandleMessages();

private:
    void SendHelloMessage();

    int ConnectToGame();

    NavKit *navKit;
    int rc;
    WSADATA wsaData;
    std::unique_ptr<WebSocket> ws;
};
