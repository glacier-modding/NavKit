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
	string navKitVersion = NavKit_VERSION_MAJOR "." NavKit_VERSION_MINOR;
	string title = "NavKit v";
	title += navKitVersion;
	SDL_SetWindowTitle(window, title.data());

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
	int lastLogCount = -1;

	bool showMenu = !presentationMode;
	bool showLog = true;
	bool showTools = true;
	bool showLevels = false;
	bool showSample = false;
	bool showTestCases = false;

	// Window scroll positions.
	int propScroll = 0;
	int logScroll = 0;
	int toolsScroll = 0;

	string loadNavpName = "Choose NAVP file...";
	string lastLoadNavpFolder = loadNavpName;
	string saveNavpName = "Choose NAVP file...";
	string lastSaveNavpFolder = saveNavpName;
	bool navpLoaded = false;
	bool showNavp = true;
	NavPower::NavMesh* navMesh = new NavPower::NavMesh();
	vector<bool> navpBuildDone;

	string airgName = "Choose AIRG file...";
	string lastAirgFolder = airgName;
	vector<bool> airgLoaded;
	bool showAirg = true;
	ResourceConverter* airgResourceConverter = HM3_GetConverterForResource("AIRG");;
	ResourceGenerator* airgResourceGenerator = HM3_GetGeneratorForResource("AIRG");
	Airg* airg = new Airg();

	string objName = "Choose OBJ file...";
	string lastObjFileName = objName;
	bool objLoaded = false;
	bool showObj = true;
	vector<string> files;
	const string meshesFolder = "OBJ";
	string objToLoad = "";
	vector<bool> objLoadDone;

	vector<bool> extractionDone;
	bool startedObjGeneration = false;

	bool showExtractMenu = false;
	string hitmanFolderName = "Choose Hitman folder...";
	string lastHitmanFolder = hitmanFolderName;
	bool hitmanSet = false;
	string sceneName = "Choose Scene TEMP...";
	bool sceneSet = false;
	string outputFolderName = "Choose Output folder...";
	string lastOutputFolder = outputFolderName;
	bool outputSet = false;

	float markerPosition[3] = { 0, 0, 0 };
	bool markerPositionSet = false;
	
	Sample* sample = new Sample_SoloMesh();
	InputGeom* geom = 0;
	BuildContext ctx;
	DebugDrawGL m_dd;
	sample->setContext(&ctx);

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

		// Hit test mesh.
		if (processHitTest && geom && objLoaded)
		{
			float hitTime;
			bool hit = geom->raycastMesh(rayStart, rayEnd, hitTime);

			if (hit)
			{
				if (SDL_GetModState() & KMOD_CTRL)
				{
					// Marker
					markerPositionSet = true;
					markerPosition[0] = rayStart[0] + (rayEnd[0] - rayStart[0]) * hitTime;
					markerPosition[1] = rayStart[1] + (rayEnd[1] - rayStart[1]) * hitTime;
					markerPosition[2] = rayStart[2] + (rayEnd[2] - rayStart[2]) * hitTime;
				}
				else
				{
					float pos[3];
					pos[0] = rayStart[0] + (rayEnd[0] - rayStart[0]) * hitTime;
					pos[1] = rayStart[1] + (rayEnd[1] - rayStart[1]) * hitTime;
					pos[2] = rayStart[2] + (rayEnd[2] - rayStart[2]) * hitTime;
					sample->handleClick(rayStart, pos, processHitTestShift);
				}
			}
			else
			{
				if (SDL_GetModState() & KMOD_CTRL)
				{
					// Marker
					markerPositionSet = false;
				}
			}
		}

		// Update sample simulation.
		const float SIM_RATE = 20;
		const float DELTA_TIME = 1.0f / SIM_RATE;
		timeAcc = rcClamp(timeAcc + dt, -1.0f, 1.0f);
		int simIter = 0;
		while (timeAcc > DELTA_TIME)
		{
			timeAcc -= DELTA_TIME;
			if (simIter < 5)
			{
				sample->handleUpdate(DELTA_TIME);
			}
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
		if (!airgLoaded.empty() && showAirg) {
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
		sample->handleRenderOverlay((double*)projectionMatrix, (double*)modelviewMatrix, (int*)viewport);
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

			if (imguiBeginScrollArea("Main menu", width - 250 - 10, height - (1175 - ((!geom || !objLoaded) ? 800 : 0)) - 10, 250, 1175 - ((!geom || !objLoaded) ? 800: 0), &propScroll))
				mouseOverMenu = true;

			if (imguiCheck("Show Navp", showNavp))
				showNavp = !showNavp;
			if (imguiCheck("Show Obj", showObj))
				showObj = !showObj;
			if (imguiCheck("Show Airg", showAirg))
				showAirg = !showAirg;

			imguiSeparator();

			imguiLabel("Extract scene from game");
			if (imguiButton(showExtractMenu ? "Close Extract Menu"  : "Open Extract Menu")) {
				showExtractMenu = !showExtractMenu;
			}

			imguiLabel("Load NAVP from file");
			if (imguiButton(loadNavpName.c_str())) {
				char* fileName = openLoadNavpFileDialog(lastLoadNavpFolder.data());
				if (fileName)
				{
					loadNavpName = fileName;
					lastLoadNavpFolder = loadNavpName.data();
					loadNavpName = loadNavpName.substr(loadNavpName.find_last_of("/\\") + 1);
					string extension = loadNavpName.substr(loadNavpName.length() - 4, loadNavpName.length());
					std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

					if (extension == "JSON") {
						navpLoaded = true;
						string msg = "Loading NAVP.JSON file: '";
						msg += fileName;
						msg += "'...";
						ctx.log(RC_LOG_PROGRESS, msg.data());
						std::thread loadNavMeshThread(loadNavMesh, navMesh, &ctx, fileName, true);
						loadNavMeshThread.detach();
					}
					else if (extension == "NAVP") {
						navpLoaded = true;
						string msg = "Loading NAVP file: '";
						msg += fileName;
						msg += "'...";
						ctx.log(RC_LOG_PROGRESS, msg.data());
						std::thread loadNavMeshThread(loadNavMesh, navMesh, &ctx, fileName, false);
						loadNavMeshThread.detach();
					}
				}
			}

			imguiLabel("Save NAVP to file");
			if (imguiButton(saveNavpName.c_str(), navpLoaded)) {
				char* fileName = openSaveNavpFileDialog(lastLoadNavpFolder.data());
				if (fileName)
				{
					saveNavpName = fileName;
					lastSaveNavpFolder = saveNavpName.data();
					saveNavpName = saveNavpName.substr(saveNavpName.find_last_of("/\\") + 1);
					string extension = saveNavpName.substr(saveNavpName.length() - 4, saveNavpName.length());
					string msg = "Saving NAVP";
					if (extension == "JSON") {
						msg += ".JSON";
					}
					msg += " file: '";
					msg += fileName;
					msg += "'...";
					ctx.log(RC_LOG_PROGRESS, msg.data());
					std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

					if (extension == "JSON") {
						OutputNavMesh_JSON_Write(navMesh, fileName);
					}
					else if (extension == "NAVP") {
						OutputNavMesh_NAVP_Write(navMesh, fileName);
					}
				}
			}

			imguiLabel("Load AIRG from file");
			if (imguiButton(airgName.c_str())) {
				char* fileName = openAirgFileDialog(lastAirgFolder.data());
				if (fileName)
				{
					airgName = fileName;
					lastAirgFolder = airgName.data();
					airgName = airgName.substr(airgName.find_last_of("/\\") + 1);
					string extension = airgName.substr(airgName.length() - 4, airgName.length());
					std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

					if (extension == "JSON") {
						delete airg;
						airg = new Airg();
						string msg = "Loading AIRG.JSON file: '";
						msg += fileName;
						msg += "'...";
						ctx.log(RC_LOG_PROGRESS, msg.data());
						std::thread loadAirgThread(loadAirg, airg, &ctx, airgResourceConverter, fileName, true, &airgLoaded);
						loadAirgThread.detach();
					}
					else if (extension == "AIRG") {
						delete airg;
						airg = new Airg();
						string msg = "Loading AIRG file: '";
						msg += fileName;
						msg += "'...";
						ctx.log(RC_LOG_PROGRESS, msg.data());
						std::thread loadAirgThread(loadAirg, airg, &ctx, airgResourceConverter, fileName, false, &airgLoaded);
						loadAirgThread.detach();
					}
				}
			}

			imguiLabel("Load OBJ file");
			if (imguiButton(objName.c_str())) {
				char* fileName = openObjFileDialog(objName.data());
				if (fileName)
				{
					objName = fileName;
					string msg = "Loading OBJ file: '";
					msg += fileName;
					msg += "'...";
					ctx.log(RC_LOG_PROGRESS, msg.data());

					lastObjFileName = objName.data();
					objName = objName.substr(objName.find_last_of("/\\") + 1);
					objToLoad = fileName;
				}
			}
			imguiSeparator();

			if (imguiCheck("Show Log", showLog))
				showLog = !showLog;


			if (geom && objToLoad.empty() && objLoaded)
			{
				imguiSeparatorLine();
				char text[64];
				snprintf(text, 64, "Verts: %.1fk  Tris: %.1fk",
					geom->getMesh()->getVertCount() / 1000.0f,
					geom->getMesh()->getTriCount() / 1000.0f);
				imguiValue(text);
			}

			if (geom && objLoaded)
			{
				imguiSeparatorLine();

				sample->handleCommonSettings();

				imguiSeparator();
				if (imguiButton("Build"))
				{
					std::thread buildNavpThread(buildNavp, sample, &ctx, &navpBuildDone);
					buildNavpThread.detach();
				}

				imguiSeparator();
			}

			imguiEndScrollArea();

			if (showExtractMenu) {
				imguiBeginScrollArea("Extract menu", width - 500 - 20, height - 215 - 10, 250, 215, &propScroll);
				imguiLabel("Set Hitman Directory");
				if (imguiButton(hitmanFolderName.c_str())) {
					char* folderName = openHitmanFolderDialog(lastHitmanFolder.data());
					if (folderName){
						hitmanSet = true;
						lastHitmanFolder = folderName;
						hitmanFolderName = folderName;
						hitmanFolderName = hitmanFolderName.substr(hitmanFolderName.find_last_of("/\\") + 1);
					}
				}
				imguiLabel("Select Scene file");
				if (imguiButton(sceneName.c_str())) {
					char* inputText = openSceneInputDialog(sceneName.data());
					if (inputText) {
						sceneName = inputText;
						sceneSet = true;
					}
				}
				imguiLabel("Set Output Directory");
				if (imguiButton(outputFolderName.c_str())) {
					char* folderName = openOutputFolderDialog(lastOutputFolder.data());
					if (folderName) {
						outputSet = true;
						lastOutputFolder = folderName;
						outputFolderName = folderName;
						outputFolderName = outputFolderName.substr(outputFolderName.find_last_of("/\\") + 1);
					}
				}
				imguiLabel("Extract from game");
				if (imguiButton("Extract", hitmanSet && sceneSet && outputSet)) {
					showLog = true;
					extractScene(&ctx, lastHitmanFolder.data(), sceneName.data(), lastOutputFolder.data(), &extractionDone);
				}
				imguiEndScrollArea();
			}
		}
		if (extractionDone.size() == 1 && !startedObjGeneration) {
			startedObjGeneration = true;
			generateObj(&ctx, lastOutputFolder.data(), &extractionDone);
		}

		if (extractionDone.size() == 2) {
			startedObjGeneration = false;
			extractionDone.clear();
			objToLoad = lastOutputFolder;
			objToLoad += "\\output.obj";
			string msg = "Loading OBJ file: '";
			msg += objToLoad;
			msg += "'...";
			ctx.log(RC_LOG_PROGRESS, msg.data());
			geom = new InputGeom;
		}

		if (!navpBuildDone.empty()) {
			navpLoaded = true;
			sample->saveAll("output.navp.json");
			NavPower::NavMesh newNavMesh = LoadNavMeshFromJson("output.navp.json");
			std::swap(*navMesh, newNavMesh);
			navpBuildDone.clear();
		}

		if (!objToLoad.empty()) {
			geom = new InputGeom;
			string fileName = objToLoad;
			std::thread loadObjThread(loadObjMesh, geom, &ctx, lastObjFileName.data(), &objLoadDone);
			loadObjThread.detach();
			objToLoad = "";
		}

		if (!objLoadDone.empty()) {
			showLog = true;
			logScroll = 0;
			ctx.dumpLog("Geom load log %s:", objName.c_str());

			if (geom)
			{
				sample->handleMeshChanged(geom);
				const float* bmin = 0;
				const float* bmax = 0;
				bmin = geom->getNavMeshBoundsMin();
				// Reset camera and fog to match the mesh bounds.
				if (bmin && bmax)
				{
					//camr = sqrtf(rcSqr(bmax[0] - bmin[0]) +
					//	rcSqr(bmax[1] - bmin[1]) +
					//	rcSqr(bmax[2] - bmin[2])) / 2;
					cameraPos[0] = (bmax[0] + bmin[0]) / 2 + camr;
					cameraPos[1] = (bmax[1] + bmin[1]) / 2 + camr;
					cameraPos[2] = (bmax[2] + bmin[2]) / 2 + camr;
					bmax = geom->getNavMeshBoundsMax();
				}
				//camr *= 3;
				cameraEulers[0] = 45;
				cameraEulers[1] = -45;
				objLoaded = true;
			}
			objLoadDone.clear();
		}
		if (showLog && showMenu)
		{
			if (imguiBeginScrollArea("Log", 250 + 20, 10, width - 300 - 250, 200, &logScroll))
				mouseOverMenu = true;
			for (int i = 0; i < ctx.getLogCount(); ++i)
				imguiLabel(ctx.getLogText(i));
			if (lastLogCount != ctx.getLogCount()) {
				logScroll = std::max(0, ctx.getLogCount() * 20 - 160);
				lastLogCount = ctx.getLogCount();
			}
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

void loadNavMesh(NavPower::NavMesh* navMesh, BuildContext* ctx, char* fileName, bool isFromJson) {
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	string msg = "Loading NAVP from file at ";
	msg += std::ctime(&start_time);
	ctx->log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();

	NavPower::NavMesh newNavMesh = isFromJson ? LoadNavMeshFromJson(fileName) : LoadNavMeshFromBinary(fileName);
	std::swap(*navMesh, newNavMesh);

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	msg = "Finished loading NAVP in ";
	msg += std::to_string(duration.count());
	msg += " seconds";
	ctx->log(RC_LOG_PROGRESS, msg.data());
}

void buildNavp(Sample* sample, BuildContext* ctx, vector<bool>* navpBuildDone) {
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	string msg = "Building NAVP at ";
	msg += std::ctime(&start_time);
	ctx->log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

	if (sample->handleBuild()) {
		navpBuildDone->push_back(true);
		msg = "Finished building NAVP in ";
		msg += std::to_string(duration.count());
		msg += " seconds";
		ctx->log(RC_LOG_PROGRESS, msg.data());
	}
	else {
		ctx->log(RC_LOG_ERROR, "Error building NAVP");
	}
}

void loadAirg(Airg* airg, BuildContext* ctx, ResourceConverter* airgResourceConverter, char* fileName, bool isFromJson, std::vector<bool>* airgLoaded) {
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	string msg = "Loading AIRG from file at ";
	msg += std::ctime(&start_time);
	ctx->log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();

	string jsonFileName = fileName;
	if (!isFromJson) {
		jsonFileName += ".JSON";
		airgResourceConverter->FromResourceFileToJsonFile(fileName, jsonFileName.data());
	}
	airg->readJson(jsonFileName.data());
	if (airgLoaded->empty()) {
		airgLoaded->push_back(true);
	}
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	msg = "Finished loading AIRG in ";
	msg += std::to_string(duration.count());
	msg += " seconds";
	ctx->log(RC_LOG_PROGRESS, msg.data());
}

void loadObjMesh(InputGeom* geom, BuildContext* ctx, char* objToLoad, std::vector<bool>* objLoadDone) {
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	string msg = "Loading OBJ from file at ";
	msg += std::ctime(&start_time);
	ctx->log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();

	if (geom->load(ctx, objToLoad)) {
		if (objLoadDone->empty()) {
			objLoadDone->push_back(true);
			auto end = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
			msg = "Finished loading OBJ in ";
			msg += std::to_string(duration.count());
			msg += " seconds";
			ctx->log(RC_LOG_PROGRESS, msg.data());
		}
	}
	else {
		ctx->log(RC_LOG_ERROR, "Error loading obj.");
	}

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	msg = "Finished loading NAVP in ";
	msg += std::to_string(duration.count());
	msg += " seconds";
	ctx->log(RC_LOG_PROGRESS, msg.data());
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
		glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, -vertex->m_pos.Y);
	}
	glEnd();
	glColor3f(0.0, 0.0, 1.0);
	glBegin(GL_LINES);
	for (auto vertex : area.m_edges) {
		glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, -vertex->m_pos.Y);
	}
	glEnd();
}

void renderNavMesh(NavPower::NavMesh* navMesh) {
	for (const NavPower::Area& area : navMesh->m_areas) {
		renderArea(area);
	}
}

void renderAirg(Airg* airg) {
	int numWaypoints = airg->m_WaypointList.size();
	for (size_t i = 0; i < numWaypoints; i++) {
		const Waypoint& waypoint = airg->m_WaypointList[i];
		glColor4f(1.0, 0.0, 0, 0.6);
		glBegin(GL_LINE_LOOP);
		const float r = 0.1f;
		for (int i = 0; i < 8; i++) {
			const float a = (float)i / 8.0f * RC_PI * 2;
			const float fx = (float)waypoint.vPos.x + cosf(a) * r;
			const float fy = (float)waypoint.vPos.y + sinf(a) * r;
			const float fz = (float)waypoint.vPos.z;
			glVertex3f(fx, fz, -fy);
		}
		glEnd();
		for (int neighborIndex = 0; neighborIndex < 8; neighborIndex++) {
			if (waypoint.nNeighbors[neighborIndex] != -1) {
				const Waypoint& neighbor = airg->m_WaypointList[waypoint.nNeighbors[neighborIndex]];
				glBegin(GL_LINES);
				glVertex3f(waypoint.vPos.x, waypoint.vPos.z, -waypoint.vPos.y);
				glVertex3f(neighbor.vPos.x, neighbor.vPos.z, -neighbor.vPos.y);
				glEnd();
			}
		}
	}
}

char* openLoadNavpFileDialog(char* lastNavpFolder) {
	char const* lFilterPatterns[2] = { "*.navp", "*.navp.json" };
	return tinyfd_openFileDialog(
		"Open Navp file",
		lastNavpFolder,
		2,
		lFilterPatterns,
		"Navp files",
		0);
}

char* openSaveNavpFileDialog(char* lastNavpFolder) {
	char const* lFilterPatterns[2] = { "*.navp", "*.navp.json" };
	return tinyfd_saveFileDialog(
		"Save Navp file",
		lastNavpFolder,
		2,
		lFilterPatterns,
		"Navp files");
}

char* openHitmanFolderDialog(char* lastHitmanFolder) {
	return tinyfd_selectFolderDialog(
		"Choose Hitman folder",
		lastHitmanFolder);
}

char* openSceneInputDialog(char* lastScene) {
	return tinyfd_inputBox(
		"Choose scene TEMP",
		"Select a scene TEMP to extract, either as a hash or an ioi string",
		"");
}

char* openOutputFolderDialog(char* lastOutputFolder) {
	return tinyfd_selectFolderDialog(
		"Choose output folder",
		lastOutputFolder);
}

char* openAirgFileDialog(char* lastAirgFolder) {
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
	char const* lFilterPatterns[1] = { "*.obj" };
	return tinyfd_openFileDialog(
		"Open Objfile",
		lastObjFolder,
		1,
		lFilterPatterns,
		"Obj files",
		0);
}