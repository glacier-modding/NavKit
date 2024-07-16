#pragma once

#include <direct.h>
#include <iostream>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "..\NavWeakness\NavPower.h"
#include "..\RecastDemo\SampleInterfaces.h"
#include "..\..\extern\simdjson\simdjson.h"
#include "..\Editor\JsonHelpers.h"
#include "..\easywsclient\easywsclient.hpp"
#ifdef _WIN32
#pragma comment( lib, "ws2_32" )
#include <WinSock2.h>
#endif
#include <assert.h>
#include <stdio.h>
#include <string>
#include <memory>
using easywsclient::WebSocket;

class GameConnection {
public:
	GameConnection(BuildContext* ctx);
	~GameConnection();
	void SendNavp(NavPower::NavMesh* navMesh);
private:
	void SendHelloMessage();
	void HandleMessages();
	int ConnectToGame();
	BuildContext* m_Ctx;
	int rc;
	WSADATA wsaData;
	std::unique_ptr<WebSocket> ws;
};