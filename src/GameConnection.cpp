#include "..\include\NavKit\GameConnection.h"

// 46735 is a phoneword for HMSDK
constinit const char* c_EditorHost = "127.0.0.1";
constinit const uint16_t c_EditorPort = 46735;

using easywsclient::WebSocket;

GameConnection::GameConnection(BuildContext* ctx) {
    m_Ctx = ctx;
    ConnectToGame();
}

GameConnection::~GameConnection() {
    WSACleanup();
}

int GameConnection::ConnectToGame() {
    rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (rc) {
        m_Ctx->log(RC_LOG_ERROR, "GameConnection: WSAStartup Failed.");
        return 1;
    }
    ws.reset(WebSocket::from_url("ws://localhost:46735/socket"));
    if (!ws) {
        m_Ctx->log(RC_LOG_ERROR, "GameConnection: Could not connect to game. Is the game running?");
        return 0;
    }
    SendHelloMessage();

    return 0;
}
void GameConnection::HandleMessages() {
    if (!ws) {
        m_Ctx->log(RC_LOG_ERROR, "GameConnection: HandleMessages failed because the socket is not open.");
        return;
    }
    while (ws->getReadyState() != WebSocket::CLOSED) {
        WebSocket::pointer wsp = &*ws;
        ws->poll();
        ws->dispatch([&](const std::string& message) {
            m_Ctx->log(RC_LOG_PROGRESS, "Received message: %s", message.c_str());
            if (message == "Done loading Navp.") { 
                wsp->close();
            }
            if (message.find("Unknown editor message type: loadNavp") != -1) {
                m_Ctx->log(RC_LOG_ERROR, "Failed to send Navp to game. Is the included Editor.dll in the Retail/mods folder?");
                wsp->close();
            }
        });
    }
}

void GameConnection::SendHelloMessage() {
    ws->send("{\"type\":\"hello\",\"identifier\":\"glacier2obj\"}");
}

void GameConnection::SendNavp(NavPower::NavMesh* navMesh) {
    if (!ws) {
        m_Ctx->log(RC_LOG_ERROR, "GameConnection: Send Navp failed because the socket is not open.");
        return;
    }
    int chunkSize = 30;
    auto chunkHead = navMesh->m_areas.begin();
    int areaCount = navMesh->m_areas.size();
    int chunkCount = (areaCount % chunkSize == 0) ? (areaCount / chunkSize) : ((areaCount / chunkSize) + 1);
    for (int chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++) {
        int curChunkSize = (chunkIndex < (chunkCount - 1)) ? chunkSize : (areaCount % chunkSize);
        std::vector<NavPower::Area> areas = std::vector<NavPower::Area>(chunkHead, chunkHead + curChunkSize);
        SendChunk(areas, chunkIndex, chunkCount);
        chunkHead += curChunkSize;
    }
}

void GameConnection::SendChunk(std::vector<NavPower::Area> areas, int chunkIndex, int chunkCount) {
    std::stringstream m;
    m << std::fixed << std::setprecision(3);
    m << "{\"type\":\"loadNavp\",\"chunkCount\":";
    m << chunkCount;
    m << ",\"chunk\":";
    m << chunkIndex;
    m << ",\"areas\":[";
    int numArea = 0;
    
    for (auto& navPowerArea : areas) {
        numArea++;
        m << "[";
        int numEdge = 0;
        for (auto& edge : navPowerArea.m_edges) {
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
    std::string msg = m.str();
    ws->send(msg);
}
