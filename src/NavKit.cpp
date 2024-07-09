/**
 * Copyright (c) 2024 Daniel Bierek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/
#include "..\include\NavKit\NavKit.h"

#undef main

using std::string;
using std::vector;

int main(int argc, char** argv)
{
	// Init SDL
	if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
	{
		printf("Could not initialise SDL.\nError: %s\n", SDL_GetError());
		return -1;
	}

	// Use OpenGL render driver.
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

	// Enable depth buffer.
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	// Set color channel depth.
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

	// 4x MSAA.
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

	SDL_DisplayMode displayMode;
	SDL_GetCurrentDisplayMode(0, &displayMode);
	bool presentationMode = false;
	Uint32 flags = SDL_WINDOW_OPENGL;
	int width;
	int height;
	if (presentationMode)
	{
		// Create a fullscreen window at the native resolution.
		width = displayMode.w;
		height = displayMode.h;
		flags |= SDL_WINDOW_FULLSCREEN;
	}
	else
	{
		float aspect = 16.0f / 9.0f;
		width = rcMin(displayMode.w, (int)(displayMode.h * aspect)) - 80;
		height = displayMode.h - 80;
	}

	SDL_Window* window;
	SDL_Renderer* renderer;
	int errorCode = SDL_CreateWindowAndRenderer(width, height, flags, &window, &renderer);

	if (errorCode != 0 || !window || !renderer)
	{
		printf("Could not initialise SDL opengl\nError: %s\n", SDL_GetError());
		return -1;
	}

	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_SetWindowTitle(window, "NavKit v0.1");

	if (!imguiRenderGLInit("DroidSans.ttf"))
	{
		printf("Could not init GUI renderer.\n");
		SDL_Quit();
		return -1;
	}

	float timeAcc = 0.0f;
	Uint32 prevFrameTime = SDL_GetTicks();
	int mousePos[2] = { 0, 0 };
	int origMousePos[2] = { 0, 0 }; // Used to compute mouse movement totals across frames.

	float cameraEulers[] = { 45, 0 };
	float cameraPos[] = { 0, 20, 20 };
	float camr = 1000;
	float origCameraEulers[] = { 0, 0 }; // Used to compute rotational changes across frames.

	float moveFront = 0.0f, moveBack = 0.0f, moveLeft = 0.0f, moveRight = 0.0f, moveUp = 0.0f, moveDown = 0.0f;

	float scrollZoom = 0;
	bool rotate = false;
	bool movedDuringRotate = false;
	float rayStart[3];
	float rayEnd[3];
	bool mouseOverMenu = false;

	bool showMenu = !presentationMode;
	bool showLog = false;
	bool showTools = true;
	bool showLevels = false;
	bool showSample = false;
	bool showTestCases = false;

	// Window scroll positions.
	int propScroll = 0;
	int logScroll = 0;
	int toolsScroll = 0;

	string navpName = "Choose NAVP file...";
	string lastNavpFolder = "C:\\";
	bool navpLoaded = false;
	bool showNavp = true;
	NavPower::NavMesh* navMesh = new NavPower::NavMesh();
	string airgName = "Choose AIRG file...";
	string lastAirgFolder = "C:\\";
	bool airgLoaded = false;
	bool showAirg = true;
	ResourceConverter* airgResourceConverter = HM3_GetConverterForResource("AIRG");;
	ResourceGenerator* airgResourceGenerator = HM3_GetGeneratorForResource("AIRG");
	Airg* airg = new Airg();

	string objName = "Choose OBJ file...";
	string lastObjFolder = "C:\\";
	bool objLoaded = false;
	bool showObj = true;
	vector<string> files;
	const string meshesFolder = "OBJ";

	float markerPosition[3] = { 0, 0, 0 };
	bool markerPositionSet = false;

	InputGeom* geom = 0;
	BuildContext ctx;
	DebugDrawGL m_dd;

	// Fog.
	float fogColor[4] = { 0.32f, 0.31f, 0.30f, 1.0f };
	glEnable(GL_FOG);
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glFogf(GL_FOG_START, camr * 0.1f);
	glFogf(GL_FOG_END, camr * 1.25f);
	glFogfv(GL_FOG_COLOR, fogColor);

	//glEnable(GL_CULL_FACE);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glClearColor(0.0, 0.0, 0.0, 0.0);

	double angle = 0.0;
	srand(time(NULL));

	bool done = false;
	while (!done)
	{
		angle += .01;
		// Handle input events.
		int mouseScroll = 0;
		bool processHitTest = false;
		bool processHitTestShift = false;
		SDL_Event event;

		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_KEYDOWN:
				// Handle any key presses here.
				if (event.key.keysym.sym == SDLK_ESCAPE)
				{
					done = true;
				}
				else if (event.key.keysym.sym == SDLK_TAB)
				{
					showMenu = !showMenu;
				}
				break;

			case SDL_MOUSEWHEEL:
				if (event.wheel.y < 0)
				{
					// wheel down
					if (mouseOverMenu)
					{
						mouseScroll++;
					}
					else
					{
						scrollZoom += 1.0f;
					}
				}
				else
				{
					if (mouseOverMenu)
					{
						mouseScroll--;
					}
					else
					{
						scrollZoom -= 1.0f;
					}
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (event.button.button == SDL_BUTTON_RIGHT)
				{
					if (!mouseOverMenu)
					{
						// Rotate view
						rotate = true;
						movedDuringRotate = false;
						origMousePos[0] = mousePos[0];
						origMousePos[1] = mousePos[1];
						origCameraEulers[0] = cameraEulers[0];
						origCameraEulers[1] = cameraEulers[1];
					}
				}
				break;

			case SDL_MOUSEBUTTONUP:
				// Handle mouse clicks here.
				if (event.button.button == SDL_BUTTON_RIGHT)
				{
					rotate = false;
					if (!mouseOverMenu)
					{
						if (!movedDuringRotate)
						{
							processHitTest = true;
							processHitTestShift = true;
						}
					}
				}
				else if (event.button.button == SDL_BUTTON_LEFT)
				{
					if (!mouseOverMenu)
					{
						processHitTest = true;
						processHitTestShift = (SDL_GetModState() & KMOD_SHIFT) ? true : false;
					}
				}

				break;

			case SDL_MOUSEMOTION:
				mousePos[0] = event.motion.x;
				mousePos[1] = height - 1 - event.motion.y;

				if (rotate)
				{
					int dx = mousePos[0] - origMousePos[0];
					int dy = mousePos[1] - origMousePos[1];
					cameraEulers[0] = origCameraEulers[0] - dy * 0.25f;
					cameraEulers[1] = origCameraEulers[1] + dx * 0.25f;
					if (dx * dx + dy * dy > 3 * 3)
					{
						movedDuringRotate = true;
					}
				}
				break;

			case SDL_QUIT:
				done = true;
				break;

			default:
				break;
			}
		}

		unsigned char mouseButtonMask = 0;
		if (SDL_GetMouseState(0, 0) & SDL_BUTTON_LMASK)
			mouseButtonMask |= IMGUI_MBUT_LEFT;
		if (SDL_GetMouseState(0, 0) & SDL_BUTTON_RMASK)
			mouseButtonMask |= IMGUI_MBUT_RIGHT;

		Uint32 time = SDL_GetTicks();
		float dt = (time - prevFrameTime) / 1000.0f;
		prevFrameTime = time;


		// Update sample simulation.
		const float SIM_RATE = 20;
		const float DELTA_TIME = 1.0f / SIM_RATE;
		timeAcc = rcClamp(timeAcc + dt, -1.0f, 1.0f);
		int simIter = 0;
		while (timeAcc > DELTA_TIME)
		{
			timeAcc -= DELTA_TIME;
			simIter++;
		}

		// Clamp the framerate so that we do not hog all the CPU.
		const float MIN_FRAME_TIME = 1.0f / 40.0f;
		if (dt < MIN_FRAME_TIME)
		{
			int ms = (int)((MIN_FRAME_TIME - dt) * 1000.0f);
			if (ms > 10) ms = 10;
			if (ms >= 0) SDL_Delay(ms);
		}

		// Set the viewport.
		glViewport(0, 0, width, height);
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);

		// Clear the screen
		glClearColor(0.3f, 0.3f, 0.32f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_DEPTH_TEST);

		// Compute the projection matrix.
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluPerspective(50.0f, (float)width / (float)height, 1.0f, camr);
		GLdouble projectionMatrix[16];
		glGetDoublev(GL_PROJECTION_MATRIX, projectionMatrix);

		// Compute the modelview matrix.
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glRotatef(cameraEulers[0], 1, 0, 0);
		glRotatef(cameraEulers[1], 0, 1, 0);
		glTranslatef(-cameraPos[0], -cameraPos[1], -cameraPos[2]);
		GLdouble modelviewMatrix[16];
		glGetDoublev(GL_MODELVIEW_MATRIX, modelviewMatrix);

		// Get hit ray position and direction.
		GLdouble x, y, z;
		gluUnProject(mousePos[0], mousePos[1], 0.0f, modelviewMatrix, projectionMatrix, viewport, &x, &y, &z);
		rayStart[0] = (float)x;
		rayStart[1] = (float)y;
		rayStart[2] = (float)z;
		gluUnProject(mousePos[0], mousePos[1], 1.0f, modelviewMatrix, projectionMatrix, viewport, &x, &y, &z);
		rayEnd[0] = (float)x;
		rayEnd[1] = (float)y;
		rayEnd[2] = (float)z;

		// Handle keyboard movement.
		const Uint8* keystate = SDL_GetKeyboardState(NULL);
		moveFront = rcClamp(moveFront + dt * 4 * ((keystate[SDL_SCANCODE_W] || keystate[SDL_SCANCODE_UP]) ? 1 : -1), 0.0f, 1.0f);
		moveLeft = rcClamp(moveLeft + dt * 4 * ((keystate[SDL_SCANCODE_A] || keystate[SDL_SCANCODE_LEFT]) ? 1 : -1), 0.0f, 1.0f);
		moveBack = rcClamp(moveBack + dt * 4 * ((keystate[SDL_SCANCODE_S] || keystate[SDL_SCANCODE_DOWN]) ? 1 : -1), 0.0f, 1.0f);
		moveRight = rcClamp(moveRight + dt * 4 * ((keystate[SDL_SCANCODE_D] || keystate[SDL_SCANCODE_RIGHT]) ? 1 : -1), 0.0f, 1.0f);
		moveUp = rcClamp(moveUp + dt * 4 * ((keystate[SDL_SCANCODE_Q] || keystate[SDL_SCANCODE_PAGEUP]) ? 1 : -1), 0.0f, 1.0f);
		moveDown = rcClamp(moveDown + dt * 4 * ((keystate[SDL_SCANCODE_E] || keystate[SDL_SCANCODE_PAGEDOWN]) ? 1 : -1), 0.0f, 1.0f);

		float keybSpeed = 22.0f;
		if (SDL_GetModState() & KMOD_SHIFT)
		{
			keybSpeed *= 4.0f;
		}

		float movex = (moveRight - moveLeft) * keybSpeed * dt;
		float movey = (moveBack - moveFront) * keybSpeed * dt + scrollZoom * 2.0f;
		scrollZoom = 0;

		cameraPos[0] += movex * (float)modelviewMatrix[0];
		cameraPos[1] += movex * (float)modelviewMatrix[4];
		cameraPos[2] += movex * (float)modelviewMatrix[8];

		cameraPos[0] += movey * (float)modelviewMatrix[2];
		cameraPos[1] += movey * (float)modelviewMatrix[6];
		cameraPos[2] += movey * (float)modelviewMatrix[10];

		cameraPos[1] += (moveUp - moveDown) * keybSpeed * dt;

		glFrontFace(GL_CW);
		if (navpLoaded && showNavp) {
			renderNavMesh(navMesh);
		}
		if (airgLoaded && showAirg) {
			renderAirg(airg);
		}
		if (objLoaded && showObj) {
			renderObj(geom, &m_dd);
		}
		// Render GUI

		glFrontFace(GL_CCW);
		glDisable(GL_DEPTH_TEST);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluOrtho2D(0, width, 0, height);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		mouseOverMenu = false;

		imguiBeginFrame(mousePos[0], mousePos[1], mouseButtonMask, mouseScroll);
		if (showMenu)
		{
			const char msg[] = "W/S/A/D: Move  RMB: Rotate";
			imguiDrawText(280, height - 20, IMGUI_ALIGN_LEFT, msg, imguiRGBA(255, 255, 255, 128));
			char cameraPosMessage[128];
			snprintf(cameraPosMessage, sizeof cameraPosMessage, "Camera position: %f, %f, %f", cameraPos[0], cameraPos[1], cameraPos[2]);
			imguiDrawText(280, height - 40, IMGUI_ALIGN_LEFT, cameraPosMessage, imguiRGBA(255, 255, 255, 128));
			char cameraAngleMessage[128];
			snprintf(cameraAngleMessage, sizeof cameraAngleMessage, "Camera angles: %f, %f", cameraEulers[0], cameraEulers[1]);
			imguiDrawText(280, height - 60, IMGUI_ALIGN_LEFT, cameraAngleMessage, imguiRGBA(255, 255, 255, 128));

			if (imguiBeginScrollArea("Load menu", width - 250 - 10, height - 300 - 10, 250, 300, &propScroll))
				mouseOverMenu = true;

			if (imguiCheck("Show Navp", showNavp))
				showNavp = !showNavp;
			if (imguiCheck("Show Obj", showObj))
				showObj = !showObj;

			//imguiSeparator();
			imguiLabel("Load NAVP from file");
			if (imguiButton(navpName.c_str()))
			{
				char* fileName = openNavpFileDialog(lastNavpFolder.data());
				if (fileName)
				{
					navpName = fileName;
					lastNavpFolder = navpName.data();
					navpName = navpName.substr(navpName.find_last_of("/\\") + 1);
					string extension = navpName.substr(navpName.length() - 4, navpName.length());
					std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

					if (extension == "JSON") {
						navpLoaded = true;
						NavPower::NavMesh newNavMesh = LoadNavMeshFromJson(fileName);
						std::swap(*navMesh, newNavMesh);
					}
					else if (extension == "NAVP") {
						navpLoaded = true;
						NavPower::NavMesh newNavMesh = LoadNavMeshFromBinary(fileName);
						std::swap(*navMesh, newNavMesh);
					}
				}
			}
			imguiLabel("Load AIRG from file");
			if (imguiButton(airgName.c_str()))
			{
				char* fileName = openAirgFileDialog(lastAirgFolder.data());
				if (fileName)
				{
					airgName = fileName;
					lastAirgFolder = airgName.data();
					airgName = airgName.substr(airgName.find_last_of("/\\") + 1);
					string extension = airgName.substr(airgName.length() - 4, airgName.length());
					std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

					if (extension == "JSON") {
						airgLoaded = true;
						airg->readJson(fileName);
					}
					else if (extension == "AIRG") {
						airgLoaded = true;
						airgResourceConverter->FromResourceFileToJsonFile(fileName, airgName.c_str());
						airg->readJson(airgName.c_str());
					}
				}
			}

			imguiSeparator();
			imguiLabel("Load OBJ file");
			if (imguiButton(objName.c_str())) {
				char* fileName = openObjFileDialog(objName.data());
				if (fileName)
				{
					objName = fileName;
					lastObjFolder = objName.data();
					objName = objName.substr(objName.find_last_of("/\\") + 1);
					geom = new InputGeom;
					objLoaded = true;

					if (!geom->load(&ctx, fileName))
					{
						showLog = true;
						logScroll = 0;
						ctx.dumpLog("Geom load log %s:", objName.c_str());

						if (geom)
						{
							const float* bmin = 0;
							const float* bmax = 0;
							if (geom)
							{
								bmin = geom->getNavMeshBoundsMin();
								bmax = geom->getNavMeshBoundsMax();
							}
							// Reset camera and fog to match the mesh bounds.
							if (bmin && bmax)
							{
								camr = sqrtf(rcSqr(bmax[0] - bmin[0]) +
									rcSqr(bmax[1] - bmin[1]) +
									rcSqr(bmax[2] - bmin[2])) / 2;
								cameraPos[0] = (bmax[0] + bmin[0]) / 2 + camr;
								cameraPos[1] = (bmax[1] + bmin[1]) / 2 + camr;
								cameraPos[2] = (bmax[2] + bmin[2]) / 2 + camr;
								camr *= 3;
							}
							cameraEulers[0] = 45;
							cameraEulers[1] = -45;
						}
					}
				}
			}
			imguiSeparator();

			//imguiSeparatorLine();


			//if (imguiButton("Build"))
			//{
			//	printf("Build pressed");
			//}

			//imguiSeparator();
			imguiEndScrollArea();
		}
		// Marker
		if (markerPositionSet && gluProject((GLdouble)markerPosition[0], (GLdouble)markerPosition[1], (GLdouble)markerPosition[2],
			modelviewMatrix, projectionMatrix, viewport, &x, &y, &z))
		{
			// Draw marker circle
			glLineWidth(5.0f);
			glColor4ub(240, 220, 0, 196);
			glBegin(GL_LINE_LOOP);
			const float r = 25.0f;
			for (int i = 0; i < 20; ++i)
			{
				const float a = (float)i / 20.0f * RC_PI * 2;
				const float fx = (float)x + cosf(a) * r;
				const float fy = (float)y + sinf(a) * r;
				glVertex2f(fx, fy);
			}
			glEnd();
			glLineWidth(1.0f);
		}
		imguiEndFrame();
		imguiRenderGLDraw();

		glEnable(GL_DEPTH_TEST);
		SDL_GL_SwapWindow(window);
	}

	imguiRenderGLDestroy();

	SDL_Quit();
	
	return 0;
}

char* openNavpFileDialog(char* lastNavpFolder) {
	char* lTheOpenFileName;
	char const* lFilterPatterns[2] = { "*.navp", "*.navp.json" };
	return tinyfd_openFileDialog(
		"Open Navp file",
		lastNavpFolder,
		2,
		lFilterPatterns,
		"Navp files",
		0);
	
	//OutputNavMesh_NAVP("C:\\Program Files (x86)\\Steam\\steamapps\\common\\HITMAN 3\\Simple Mod Framework\\Mods\\NavpTestWorld\\content\\chunk2\\009F622BC6A91CC4.NAVP.json", "009F622BC6A91CC4.NAVP", true);
	//OutputNavMesh_JSON("C:\\workspace\\ZHMTools-dbierek\\Debug\\miami vanilla.NAVP", "miami.navp.json", false);

}

char* openAirgFileDialog(char* lastAirgFolder) {
	char* lTheOpenFileName;
	char const* lFilterPatterns[2] = { "*.airg", "*.airg.json" };
	return tinyfd_openFileDialog(
		"Open Airg file",
		lastAirgFolder,
		2,
		lFilterPatterns,
		"Airg files",
		0);
}

char* openObjFileDialog(char* lastObjFolder) {
	char* lTheOpenFileName;
	char const* lFilterPatterns[1] = { "*.obj" };
	return tinyfd_openFileDialog(
		"Open Objfile",
		lastObjFolder,
		1,
		lFilterPatterns,
		"Obj files",
		0);
}

void renderObj(InputGeom* m_geom, DebugDrawGL* m_dd) {
	// Draw mesh
	duDebugDrawTriMesh(m_dd, m_geom->getMesh()->getVerts(), m_geom->getMesh()->getVertCount(),
		m_geom->getMesh()->getTris(), m_geom->getMesh()->getNormals(), m_geom->getMesh()->getTriCount(), 0, 1.0f);
	// Draw bounds
	const float* bmin = m_geom->getMeshBoundsMin();
	const float* bmax = m_geom->getMeshBoundsMax();
	duDebugDrawBoxWire(m_dd, bmin[0], bmin[1], bmin[2], bmax[0], bmax[1], bmax[2], duRGBA(255, 255, 255, 128), 1.0f);
}

void renderArea(NavPower::Area area) {
	glColor4f(0.0, 0.0, 0.5, 0.6);
	glBegin(GL_POLYGON);
	for (auto vertex : area.m_edges) {
		glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, vertex->m_pos.Y);
	}
	glEnd();
	glColor3f(0.0, 0.0, 1.0);
	glBegin(GL_LINES);
	for (auto vertex : area.m_edges) {
		glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, vertex->m_pos.Y);
	}
	glEnd();
}

void renderNavMesh(NavPower::NavMesh* navMesh) {
	for (auto area : navMesh->m_areas) {
		renderArea(area);
	}
}

void renderAirg(Airg* airg) {
	printf("Render AIRG");
	//TArray<SGWaypoint> waypoints = airg->m_WaypointList;
	//for (SGWaypoint waypoint : airg->m_WaypointList) {
	//	glColor4f(1.0, 0.0, 0.5, 0.6);
	//	glBegin(GL_LINE_LOOP);
	//	const float r = 5.0f;
	//	for (int i = 0; i < 8; ++i)
	//	{
	//		const float a = (float)i / 8.0f * RC_PI * 2;
	//		const float fx = (float)waypoint.vPos.x + cosf(a) * r;
	//		const float fy = (float)waypoint.vPos.y + sinf(a) * r;
	//		const float fz = (float)waypoint.vPos.z + sinf(a) * r;
	//		glVertex3f(fx, fy, fz);
	//	}
	//	glEnd();
	//}
}