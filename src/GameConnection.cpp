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
        m_Ctx->log(RC_LOG_ERROR, "WSAStartup Failed.");
        return 1;
    }
    ws.reset(WebSocket::from_url("ws://localhost:46735/socket"));
    assert(ws);
    SendHelloMessage();
    std::thread handleMessagesThread([=] { HandleMessages(); });
    handleMessagesThread.detach();

    return 0;
}
void GameConnection::HandleMessages() {
    while (ws->getReadyState() != WebSocket::CLOSED) {
        WebSocket::pointer wsp = &*ws;
        ws->poll();
        ws->dispatch([&](const std::string& message) {
            m_Ctx->log(RC_LOG_PROGRESS, "Received message: {}", message.c_str());
            if (message == "Done loading Navp.") { wsp->close(); }
        });
    }
}

void GameConnection::SendHelloMessage() {
    ws->send("{\"type\":\"hello\",\"identifier\":\"glacier2obj\"}");
}

void GameConnection::SendNavp(NavPower::NavMesh* navMesh) {
    //for (auto area : navMesh->m_areas) {
    //    for (auto)
    //}
    ws->send("{\"type\":\"loadNavp\",\"areas\":[[[4.0,-6.0,0.0], [8.0,-6.0,0.0], [8.0,6.0,0.0], [4.0,6.0,0.0]],[[-8.0,-6.0,0.0], [-4.0,-6.0,0.0], [-4.0,6.0,0.0], [-8.0,6.0,0.0]],[[-4.0,-22.0,0.0], [4.0,-22.0,0.0], [4.0,-6.0,0.0], [-4.0,-6.0,0.0]],[[-4.0,-6.0,0.0], [4.0,-6.0,0.0], [4.0,6.0,0.0], [-4.0,6.0,0.0]]]}");
}
