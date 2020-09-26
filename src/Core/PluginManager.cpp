#include "PluginManager.hpp"
#include "EnginePlugin.hpp"

using namespace std;
using namespace stm;

#define stm_str(x) #x
#define stm_xstr(x) stm_str(x)

#ifdef WINDOWS
const char* gPluginExtension = ".dll";
void ThrowLastError() {
	char* msgBuf;
	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |  FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
								NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&msgBuf, 0, NULL );
	throw runtime_error(msgBuf);
}
PluginManager::PluginModule::PluginModule(const fs::path& filename) {
	mModule = LoadLibrary(filename.string().c_str());
	if (mModule == NULL) ThrowLastError();
	FARPROC fp = GetProcAddress(mModule, stm_xstr(CREATE_PLUGIN_FUNCTION));
	if (fp == NULL) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Error: Failed to find %s\n", stm_xstr(CREATE_PLUGIN_FUNCTION));
		ThrowLastError();
	}
	EnginePlugin* (*createPlugin)();
	createPlugin = reinterpret_cast<EnginePlugin*(__cdecl*)(void)>(fp);
	mPlugin = createPlugin();
}
#endif
#ifdef __linux
const char* gPluginExtension = ".so";
#endif
PluginManager::PluginModule::~PluginModule() {
		if (mPlugin) delete mPlugin;
		#ifdef WINDOWS
		if (mModule) FreeLibrary(mModule);
		#endif
}
bool PluginManager::PluginModule::operator<(const PluginModule& rhs) { return mPlugin->Priority() < rhs.mPlugin->Priority(); }

PluginManager::PluginManager(const fs::path& pluginFolder) {
	if (!fs::exists(pluginFolder)) return;
	for (const auto& p : fs::recursive_directory_iterator(pluginFolder)) {
		if (p.path().extension().string() != gPluginExtension) continue;
		try {
			mPlugins.emplace_back(p.path().string());
			printf_color(ConsoleColorBits::eBlue, "Loaded %s\n", p.path().string().c_str());
		} catch (runtime_error& e) {
			fprintf_color(ConsoleColorBits::eRed, stderr, "Error: Failed to load %s: %s", p.path().string().c_str(), e.what());
		}
	}
	sort(mPlugins.begin(), mPlugins.end());
}