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

int main(int argc, char** argv) {
	NavKit navKitProgram;
	return navKitProgram.runProgram(argc, argv);
}

int NavKit::runProgram(int argc, char** argv) {
	if (!renderer->initWindowAndRenderer()) {
		return -1;
	}

	// TODO: Add mutex for tbuffer to keep processing messages in the background.
	// std::thread handleMessagesThread([=] { HandleMessages(); });
	// handleMessagesThread.detach();
	log(RC_LOG_PROGRESS, "NavKit initialized.");
	while (!done)
	{
		inputHandler->handleInput();
		if (inputHandler->resized) {
			renderer->handleResize();
			inputHandler->resized = false;
		}
		renderer->renderFrame();
		inputHandler->hitTest();
		gui->drawGui();

		sceneExtract->finalizeExtract();
		navp->finalizeLoad();
		obj->finalizeLoad();
		airg->finalizeLoad();

		renderer->finalizeFrame();
	}

	return 0;
}

void NavKit::log(rcLogCategory category, const char* message, ...) {
	std::pair<rcLogCategory, std::string> logMessage = {category, message};
	logQueue.push(logMessage);
}

void NavKit::logRunner(NavKit* navKit) {
	std::optional<std::pair<rcLogCategory, std::string>> message;
	while (true) {
		while (!navKit->logQueue.empty()) {
			message = navKit->logQueue.try_pop();
			if (message.has_value()) {
				navKit->ctx.log(message.value().first, message.value().second.c_str());
			}
		}
	}
}

void NavKit::loadSettings() {
	sceneExtract->setHitmanFolder(ini.GetValue("Paths", "hitman", "default"));
	log(rcLogCategory::RC_LOG_PROGRESS, sceneExtract->lastHitmanFolder.c_str());
	sceneExtract->setOutputFolder(ini.GetValue("Paths", "output", "default"));
	log(rcLogCategory::RC_LOG_PROGRESS, sceneExtract->lastOutputFolder.c_str());
	sceneExtract->setBlenderFile(ini.GetValue("Paths", "blender", "default"));
	log(rcLogCategory::RC_LOG_PROGRESS, sceneExtract->lastBlenderFile.c_str());
}

NavKit::NavKit() {
	sceneExtract = new SceneExtract(this);
	navp = new Navp(this);
	obj = new Obj(this);
	airg = new Airg(this);
	renderer = new Renderer(this);
	inputHandler = new InputHandler(this);
	gui = new Gui(this);

	sample = new Sample_SoloMesh();
	sample->setContext(&ctx);

	gameConnection = 0;
	geom = 0;

	scrollZoom = 0;
	rotate = false;
	movedDuringRotate = false;

	ini.SetUnicode();
	
	std::thread logThread(NavKit::logRunner, this);
	logThread.detach();
	logQueue = rsj::ConcurrentQueue<std::pair<rcLogCategory, std::string>>();

	SI_Error rc = ini.LoadFile("NavKit.ini");
	if (rc < 0) {
		log(rcLogCategory::RC_LOG_ERROR, "Error loading settings from NavKit.ini");
	}
	else {
		log(rcLogCategory::RC_LOG_PROGRESS, "Loading settings from NavKit.ini...");
		loadSettings();
	}
	done = false;
}

NavKit::~NavKit() {
	delete sceneExtract;
	delete navp;
	delete obj;
	delete airg;
	delete renderer;
	delete inputHandler;
	imguiRenderGLDestroy();
	SDL_Quit();
	NFD_Quit();
}
