#include <Core/PluginManager.hpp>

using namespace std;

#ifdef WINDOWS
#define NULL_PLUGIN NULL
#define LoadPluginLibrary(x) LoadLibraryA(x)
#define UnloadPluginLibrary(x) FreeLibrary(x)
#define GetPluginFunction GetProcAddress
#define PLUGIN_EXTENSION ".dll"
#else
#include <dlfcn.h>
#define NULL_PLUGIN nullptr
#define LoadPluginLibrary(x) dlsym(x, RTLD_NOW)
#define UnloadPluginLibrary(x) dlclose(x)
#define GetPluginFunction dlsym
#define PLUGIN_EXTENSION ".so"
#endif

PluginManager::~PluginManager() {
	UnloadPlugins();
}

void PluginManager::LoadPlugins(const string& pluginFolder) {
	UnloadPlugins();

	for (const auto& p : fs::directory_iterator(pluginFolder)) {
		// load plugin DLLs
		if (p.path().extension().string() == PLUGIN_EXTENSION) {
			PluginHandle handle = LoadPluginLibrary(p.path().string().c_str());
			if (handle == NULL_PLUGIN) { fprintf_color(COLOR_RED, stderr, "%s: Failed to load plugin module\n", p.path().string().c_str()); continue; }
			EnginePlugin*(*createPlugin)(void) = (EnginePlugin*(*)(void))GetPluginFunction(handle, "CreatePlugin");
			if (!createPlugin) {
				fprintf_color(COLOR_RED, stderr, "%s: Failed to find CreatePlugin() function\n", p.path().string().c_str());
				UnloadPluginLibrary(handle);
				continue;
			}
			EnginePlugin* plugin = createPlugin();
			if (!plugin) {
				fprintf_color(COLOR_RED, stderr, "%s: Failed to create plugin\n", p.path().string().c_str());
				UnloadPluginLibrary(handle);
				continue;
			}			
			mPluginModules.push_back(handle);
			mPlugins.push_back(plugin);
			printf_color(COLOR_BLUE, "Loaded %s\n", p.path().string().c_str());
		}
	}

	sort(mPlugins.begin(), mPlugins.end(), [](const auto& a, const auto& b) { return a->Priority() > b->Priority(); });
}

void PluginManager::UnloadPlugins() {
	for (auto& p : mPlugins) safe_delete(p);
	mPlugins.clear();
	for (const auto& m : mPluginModules) UnloadPluginLibrary(m);
	mPluginModules.clear();
}