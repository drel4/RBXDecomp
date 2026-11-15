#include "physics.h"
#include "player.h"
#include "profile.h"
#include "scene.h"
#include "script.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>
#include <stdexcept>
#include <unistd.h>

PSP_MODULE_INFO("OpenRBX PSP", 0, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
// PSP-3008 target: PARAM.SFO requests extended RAM; retain headroom for the OS
// and future gameplay instead of committing the entire 64 MB to this heap.
PSP_HEAP_SIZE_KB(32768);

using namespace openrbx;
extern "C"
{
	RuntimeProfile openrbxProfile;
}
static volatile bool running = true;
static unsigned int __attribute__((aligned(16))) commands[131072];
static const int Width = 480, Height = 272, Stride = 512;
static void* drawBuffer = 0;
static Scene scene;
static ScriptRuntime scripts;
static PhysicsWorld physics;
static LocalPlayer game;
static std::string contentDirectory;
static float physicsMs = 0;
static float physicsSyncMs = 0, physicsStepMs = 0, physicsPublishMs = 0;
static bool runtimeJustStarted = false;
static double collectionTime = 0;
static float collectionMs = 0;
static size_t liveInstances = 0, collectedInstances = 0;
static std::string runtimeError;
// Runtime property accounting walks the instance tree. Cache it on load, not
// every 30 Hz HUD draw; the heap counter still reports current allocations.
static size_t loadedSceneBytes = 0;
static bool browser = true, hud = true;
static std::string directory, error, loadedName;
struct Entry
{
	std::string name;
	bool directory;
};
static std::vector<Entry> entries;
static int selection = 0, scroll = 0;
static Vec3 target;
static float yaw = .65f, pitch = .45f, distance = 60;
static int visible = 0;

// Use PSPSDK's existing bitmap font, uploaded once as a GU texture. CPU writes
// to a rendered framebuffer are not reliably coherent on emulator backends.
extern "C" unsigned char msx[];
static unsigned int __attribute__((aligned(16))) fontAtlas[128 * 64];
static int textX = 0, textY = 0;
struct TextVertex
{
	float u, v;
	unsigned int color;
	float x, y, z;
};
static void textSetXY(int x, int y)
{
	textX = x;
	textY = y;
}
static void textPrint(const char* format, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	size_t count = std::strlen(buffer);
	TextVertex* vertices = (TextVertex*) sceGuGetMemory(count * 2 * sizeof(TextVertex));
	int n = 0;
	for (size_t i = 0; i < count; ++i) {
		unsigned char c = buffer[i];
		if (c == '\n') {
			textX = 0;
			++textY;
			continue;
		}
		if (textY < 34 && textX < 68 && c >= 32 && c < 128) {
			float u = (c % 16) * 8, v = (c / 16) * 8, x = textX * 7, y = textY * 8;
			TextVertex a = {u, v, 0xffffffff, x, y, 0}, b = {u + 8, v + 8, 0xffffffff, x + 8, y + 8, 0};
			vertices[n++] = a;
			vertices[n++] = b;
		}
		++textX;
	}
	if (n) {
		sceGuDrawArray(
			GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n, 0, vertices
		);
	}
}
static void beginText()
{
	sceGuDisable(GU_DEPTH_TEST);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuEnable(GU_BLEND);
	sceGuTexMode(GU_PSM_8888, 0, 0, 0);
	sceGuTexImage(0, 128, 64, 128, fontAtlas);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	sceGuTexScale(1, 1);
	sceGuTexOffset(0, 0);
}

static int onExit(int, int, void*)
{
	running = false;
	return 0;
}
static int callbackThread(SceSize, void*)
{
	int cb = sceKernelCreateCallback("OpenRBX Exit", onExit, 0);
	sceKernelRegisterExitCallback(cb);
	sceKernelSleepThreadCB();
	return 0;
}
static void setup()
{
	int t = sceKernelCreateThread("OpenRBX callbacks", callbackThread, 0x11, 4096, 0, 0);
	if (t >= 0) {
		sceKernelStartThread(t, 0, 0);
	}
	scePowerSetClockFrequency(333, 333, 166);
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	sceGuInit();
	sceGuStart(GU_DIRECT, commands);
	const unsigned frameBytes = Stride * Height * 2;
	sceGuDrawBuffer(GU_PSM_5650, (void*) 0, Stride);
	sceGuDispBuffer(Width, Height, (void*) frameBytes, Stride);
	sceGuDepthBuffer((void*) (frameBytes * 2), Stride);
	sceGuOffset(2048 - Width / 2, 2048 - Height / 2);
	sceGuViewport(2048, 2048, Width, Height);
	sceGuScissor(0, 0, Width, Height);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDepthRange(65535, 0);
	sceGuDepthFunc(GU_GEQUAL);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuEnable(GU_CLIP_PLANES);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuDisable(GU_LIGHTING);
	// Two-sided during the renderer bring-up: XML mirrors/negative rotations
	// must not make places disappear. Geometry is still depth-tested.
	sceGuDisable(GU_CULL_FACE);
	sceGuShadeModel(GU_FLAT);
	sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
	sceGuFinish();
	sceGuSync(0, 0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
	for (int c = 0; c < 128; ++c) {
		for (int y = 0; y < 8; ++y) {
			for (int x = 0; x < 8; ++x) {
				fontAtlas[((c / 16) * 8 + y) * 128 + (c % 16) * 8 + x] = (msx[c * 8 + y] & (128 >> x)) ? 0xffffffff : 0;
			}
		}
	}
	sceKernelDcacheWritebackAll();
}
static bool supported(const std::string& s)
{
	std::string lower = s;
	for (size_t i = 0; i < lower.size(); ++i) {
		if (lower[i] >= 'A' && lower[i] <= 'Z') {
			lower[i] += 'a' - 'A';
		}
	}
	return (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".rbxl") ||
		   (lower.size() >= 6 && lower.substr(lower.size() - 6) == ".rbxlx");
}
static bool order(const Entry& a, const Entry& b)
{
	if (a.directory != b.directory) {
		return a.directory;
	}
	return a.name < b.name;
}
static void listDirectory()
{
	entries.clear();
	selection = scroll = 0;
	SceUID d = sceIoDopen(directory.c_str());
	if (d < 0) {
		error = "Cannot read folder: " + directory;
		return;
	}
	SceIoDirent entry;
	std::memset(&entry, 0, sizeof(entry));
	while (sceIoDread(d, &entry) > 0 && entries.size() < 512) {
		std::string name = entry.d_name;
		bool isDir = FIO_S_ISDIR(entry.d_stat.st_mode);
		if (name != "." && name != ".." && (isDir || supported(name))) {
			Entry e;
			e.name = name;
			e.directory = isDir;
			entries.push_back(e);
		}
		std::memset(&entry, 0, sizeof(entry));
	}
	sceIoDclose(d);
	std::sort(entries.begin(), entries.end(), order);
}
static void parentDirectory()
{
	while (directory.size() > 1 && directory.back() == '/') {
		directory.pop_back();
	}
	size_t slash = directory.find_last_of('/');
	if (slash != std::string::npos) {
		directory = directory.substr(0, slash + 1);
	}
	else {
		size_t colon = directory.find(':');
		if (colon != std::string::npos) {
			directory = directory.substr(0, colon + 1) + "/";
		}
	}
	error.clear();
	listDirectory();
}
static void fitScene()
{
	if (game.player()) {
		target = game.position() + Vec3(0, 1, 0);
		distance = 14;
		pitch = .35f;
		yaw = .65f;
		return;
	}
	Vec3 extent = scene.maximum - scene.minimum;
	target = (scene.minimum + scene.maximum) * .5f;
	distance = std::max(20.0f, length(extent) * .75f);
	yaw = .65f;
	pitch = .5f;
}
static void startRuntime()
{
	collectionTime = 0;
	collectionMs = 0;
	liveInstances = collectedInstances = 0;
	runtimeError.clear();
	try {
		scripts.open(scene.model);
		physics.open(scene.model);
		game.open(scene.model, physics, contentDirectory, false);
		scripts.primePlaceScripts();
		if (!game.player()) {
			game.create();
		}
		if (!game.character()) {
			game.loadCharacter();
		}
	}
	catch (const std::exception& e) {
		runtimeError = e.what();
		game.close();
		scripts.close();
		physics.close();
	}
	runtimeJustStarted = true;
}
static void loadSelection()
{
	if (entries.empty()) {
		return;
	}
	Entry e = entries[selection];
	if (e.directory) {
		directory += e.name + "/";
		error.clear();
		listDirectory();
		return;
	}
	// Do not retain the previous place while parsing another one.
	sceGuSync(0, 0);
	game.close();
	scripts.close();
	physics.close();
	scene.clear();
	loadedSceneBytes = 0;
	std::string path = directory + e.name;
	if (loadPlace(path.c_str(), scene, error)) {
		startRuntime();
		loadedSceneBytes = scene.bytes();
		loadedName = e.name;
		fitScene();
		browser = false;
		sceKernelDcacheWritebackAll();
	}
}
static void beginFrame()
{
	sceGuStart(GU_DIRECT, commands);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuDisable(GU_BLEND);
	sceGuClearColor(0xffba9673);
	sceGuClearDepth(0);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}
static void renderScene()
{
	Vec3 outward(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
	Vec3 eye = target + outward * distance, forward = outward * -1;
	Vec3 right = normalized(cross(forward, Vec3(0, 1, 0))), up = cross(right, forward);
	sceGumMatrixMode(GU_PROJECTION);
	sceGumLoadIdentity();
	sceGumPerspective(65.0f, float(Width) / Height, .5f, 16000.0f);
	sceGumMatrixMode(GU_VIEW);
	sceGumLoadIdentity();
	ScePspFVector3 e = {eye.x, eye.y, eye.z}, c = {target.x, target.y, target.z}, u = {0, 1, 0};
	sceGumLookAt(&e, &c, &u);
	sceGumMatrixMode(GU_MODEL);
	sceGumLoadIdentity();
	visible = 0;
	// Coarse CPU frustum culling; GE does triangle clipping. Never build
	// per-frame vertex arrays or keep a second copy of the original XML.
	for (int pass = 0; pass < 2; ++pass) {
		if (pass) {
			sceGuEnable(GU_BLEND);
		}
		else {
			sceGuDisable(GU_BLEND);
		}
		sceGuDepthMask(pass ? GU_TRUE : GU_FALSE);
		for (size_t i = 0; i < scene.parts.size(); ++i) {
			const Part& p = scene.parts[i];
			if ((p.transparency > .01f) != (pass != 0)) {
				continue;
			}
			Vec3 delta = p.position - eye;
			float z = dot(delta, forward), r = p.radius;
			if (z + r < .5f || z - r > 16000 || std::fabs(dot(delta, right)) > z * 1.14f + r * 2 ||
				std::fabs(dot(delta, up)) > z * .64f + r * 2) {
				continue;
			}
			sceGumDrawArray(
				GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, p.count, 0, &scene.vertices[p.first]
			);
			++visible;
		}
	}
	sceGuDepthMask(GU_FALSE);
	sceGuDisable(GU_BLEND);
}
static void textFrame(float fps, float ms)
{

	textSetXY(0, 0);
	if (browser) {
		textPrint("OpenRBX PSP - RBXL browser           %.1f FPS\n", fps);
		textPrint("%.65s\n", directory.c_str());
		textPrint("X open  O parent  START return to scene\n\n");
		if (selection < scroll) {
			scroll = selection;
		}
		if (selection >= scroll + 20) {
			scroll = selection - 19;
		}
		for (int i = scroll; i < (int) entries.size() && i < scroll + 20; ++i) {
			textPrint(
				"%c %c %.60s\n", i == selection ? '>' : ' ', entries[i].directory ? '+' : ' ', entries[i].name.c_str()
			);
		}
		if (entries.empty()) {
			textPrint("No RBXL files here. Use O to browse another folder.\n");
		}
		textSetXY(0, 27);
		textPrint("%.66s\n%.66s", error.substr(0, 66).c_str(), error.size() > 66 ? error.substr(66, 66).c_str() : "");
		textSetXY(0, 32);
		textPrint("XML places | local play | START pauses to browse places");
	}
	else if (hud) {
		struct mallinfo mem = mallinfo();
		const Instance* humanoid = scene.model.get(game.humanoid());
		Vec3 playerPosition = game.position();
		openrbxProfile = {
			1,
			fps,
			(uint32_t) mem.uordblks,
			(uint32_t) liveInstances,
			(uint32_t) scene.model.size(),
			(uint32_t) collectedInstances,
			collectionMs,
			physicsMs,
			game.player(),
			game.character(),
			humanoid ? std::strtof(humanoid->property("Health")->text.c_str(), nullptr) : 0,
			game.onGround() ? 1u : 0u,
			runtimeError.empty() ? 0u : 1u,
			(uint32_t) scripts.errorCount(),
			playerPosition.x,
			playerPosition.y,
			playerPosition.z
		};
		textPrint("OpenRBX PSP  %.1f FPS / 30 cap  %.1f ms\n", fps, ms);
		textPrint(
			"Parts %d/%u  Triangles %u  Scene %.2f MB\n",
			visible,
			(unsigned) scene.parts.size(),
			(unsigned) scene.vertices.size() / 3,
			loadedSceneBytes / 1048576.0f
		);
		textPrint("Heap %.2f/32 MB | %.42s\n", mem.uordblks / 1048576.0f, loadedName.c_str());
		textPrint("Lua %u threads / %.2f MB\n", (unsigned) scripts.threadCount(), scripts.bytes() / 1048576.f);
		const auto& physical = physics.statistics();
		textPrint(
			"Physics %.1f ms | %u awake / %u assemblies\n",
			physicsMs,
			(unsigned) physical.awake,
			(unsigned) physical.assemblies
		);
		textPrint("Sync %.1f Step %.1f Publish %.1f ms\n", physicsSyncMs, physicsStepMs, physicsPublishMs);
		textPrint(
			"Instances %u/%u | GC %.1f ms / %u freed\n",
			(unsigned) liveInstances,
			(unsigned) scene.model.size(),
			collectionMs,
			(unsigned) collectedInstances
		);
		if (!runtimeError.empty()) {
			textPrint("Runtime: %.58s\n", runtimeError.c_str());
		}
		else if (!scripts.messages().empty()) {
			textPrint("Lua: %.62s\n", scripts.messages().back().c_str());
		}
		textSetXY(0, 31);
		textPrint("Analog move | X jump | L/R orbit | D-pad camera\n");
		textPrint("Triangle camera | Square reset | START files | SELECT HUD");
	}
}
int main(int argc, char** argv)
{
	setup();
	char cwd[1024];
	directory = getcwd(cwd, sizeof(cwd)) ? cwd : "ms0:/PSP/GAME/OpenRBX";
	if (directory.back() != '/') {
		directory += '/';
	}
	contentDirectory = directory + "content";
	std::string places = directory + "places";
	SceUID d = sceIoDopen(places.c_str());
	if (d >= 0) {
		sceIoDclose(d);
		directory = places + "/";
	}
	listDirectory();
	// Optional path argument is useful for emulator smoke tests.
	if (argc > 1 && supported(argv[1])) {
		if (loadPlace(argv[1], scene, error)) {
			startRuntime();
			loadedSceneBytes = scene.bytes();
			loadedName = argv[1];
			fitScene();
			browser = false;
			sceKernelDcacheWritebackAll();
		}
	}
	unsigned previous = 0, lastVblank = sceDisplayGetVcount(), repeatAt = 0;
	uint64_t previousTime = sceKernelGetSystemTimeWide(), fpsTime = previousTime;
	unsigned frames = 0;
	float fps = 0, ms = 0;
	while (running) {
		uint64_t now = sceKernelGetSystemTimeWide();
		double simulationDt = (now - previousTime) / 1000000.0;
		float dt = std::min(.1f, (float) simulationDt);
		previousTime = now;
		SceCtrlData pad;
		sceCtrlPeekBufferPositive(&pad, 1);
		unsigned pressed = pad.Buttons & ~previous;
		unsigned navigation = pad.Buttons & (PSP_CTRL_UP | PSP_CTRL_DOWN);
		if (navigation && now / 1000 >= repeatAt) {
			pressed |= navigation;
			repeatAt = (unsigned) (now / 1000) + ((previous & navigation) ? 130 : 350);
		}
		previous = pad.Buttons;
		if (pressed & PSP_CTRL_START) {
			if (!scene.parts.empty()) {
				browser = !browser;
			}
			else {
				browser = true;
			}
			if (browser) {
				listDirectory();
			}
		}
		if (browser) {
			if (!entries.empty()) {
				if (pressed & PSP_CTRL_UP) {
					selection = (selection + (int) entries.size() - 1) % entries.size();
				}
				if (pressed & PSP_CTRL_DOWN) {
					selection = (selection + 1) % entries.size();
				}
			}
			if (pressed & PSP_CTRL_CIRCLE) {
				parentDirectory();
			}
			if (pressed & PSP_CTRL_CROSS) {
				loadSelection();
			}
		}
		else {
			if (pressed & PSP_CTRL_SELECT) {
				hud = !hud;
			}
			float ax = (int(pad.Lx) - 128) / 128.0f, ay = (int(pad.Ly) - 128) / 128.0f;
			if (std::fabs(ax) < .15f) {
				ax = 0;
			}
			if (std::fabs(ay) < .15f) {
				ay = 0;
			}
			Vec3 forward(-std::sin(yaw), 0, -std::cos(yaw)), right(std::cos(yaw), 0, -std::sin(yaw));
			game.input(forward * (-ay) + right * ax, (pad.Buttons & PSP_CTRL_CROSS) != 0);
			if (pad.Buttons & PSP_CTRL_UP) {
				pitch = std::min(1.3f, pitch + dt);
			}
			if (pad.Buttons & PSP_CTRL_DOWN) {
				pitch = std::max(-.2f, pitch - dt);
			}
			if (pad.Buttons & PSP_CTRL_LEFT) {
				distance = std::max(4.f, distance - dt * 12);
			}
			if (pad.Buttons & PSP_CTRL_RIGHT) {
				distance = std::min(45.f, distance + dt * 12);
			}
			if (pad.Buttons & PSP_CTRL_LTRIGGER) {
				yaw -= dt * 2;
			}
			if (pad.Buttons & PSP_CTRL_RTRIGGER) {
				yaw += dt * 2;
			}
			if (pressed & PSP_CTRL_TRIANGLE) {
				fitScene();
			}
			if (pressed & PSP_CTRL_SQUARE) {
				if (game.humanoid()) {
					scene.model.setProperty(game.humanoid(), {"Health", "float", "0"});
				}
			}
		}
		if (!browser) {
			if (runtimeJustStarted) {
				simulationDt = 0;
				previousTime = sceKernelGetSystemTimeWide();
				runtimeJustStarted = false;
			}
			try {
				scripts.step(simulationDt);
				uint64_t physicsStart = sceKernelGetSystemTimeWide();
				physics.synchronize();
				uint64_t syncEnd = sceKernelGetSystemTimeWide();
				physics.advance(simulationDt);
				uint64_t stepEnd = sceKernelGetSystemTimeWide();
				physics.publish();
				game.advance(simulationDt);
				physics.synchronize();
				collectionTime += simulationDt;
				if (collectionTime >= 1) {
					collectionTime = 0;
					uint64_t collectionStart = sceKernelGetSystemTimeWide();
					collectedInstances = scripts.collect();
					liveInstances = scene.model.liveCount();
					collectionMs = (sceKernelGetSystemTimeWide() - collectionStart) / 1000.f;
					physics.synchronize();
				}
				if (game.player()) {
					target = game.position() + Vec3(0, 1, 0);
				}
				uint64_t publishEnd = sceKernelGetSystemTimeWide();
				physicsSyncMs = (syncEnd - physicsStart) / 1000.f;
				physicsStepMs = (stepEnd - syncEnd) / 1000.f;
				physicsPublishMs = (publishEnd - stepEnd) / 1000.f;
				physicsMs = (publishEnd - physicsStart) / 1000.f;
				bool geometryChanged = false;
				std::string geometryError;
				if (!refreshScene(scene, geometryChanged, geometryError)) {
					runtimeError = geometryError;
					scripts.close();
				}
				if (geometryChanged) {
					sceKernelDcacheWritebackAll();
				}
			}
			catch (const std::exception& e) {
				runtimeError = e.what();
				game.close();
				scripts.close();
				physics.close();
			}
		}
		beginFrame();
		if (!browser) {
			renderScene();
		}
		beginText();
		textFrame(fps, ms);
		sceGuFinish();
		sceGuSync(0, 0);
		ms = (sceKernelGetSystemTimeWide() - now) / 1000.0f;
		// Two display refreshes per completed frame. Never run faster than 30 Hz,
		// and never busy-wait or speed up simulation to compensate for a slow frame.
		while (running && (unsigned) (sceDisplayGetVcount() - lastVblank) < 2) {
			sceDisplayWaitVblankStartCB();
		}
		lastVblank = sceDisplayGetVcount();
		drawBuffer = sceGuSwapBuffers();
		++frames;
		uint64_t end = sceKernelGetSystemTimeWide();
		if (end - fpsTime >= 500000) {
			fps = frames * 1000000.0f / (end - fpsTime);
			frames = 0;
			fpsTime = end;
		}
	}
	game.close();
	scripts.close();
	physics.close();
	scene.clear();
	sceGuTerm();
	sceKernelExitGame();
	return 0;
}
