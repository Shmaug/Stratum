#pragma once

#include <Core/EnginePlugin.hpp>

#ifdef WINDOWS
typedef HMODULE PluginHandle;
#else
typedef void* PluginHandle;
#endif

class PluginManager {
public:
	STRATUM_API ~PluginManager();

	const std::vector<EnginePlugin*>& Plugins() const { return mPlugins; }
	
	template<class T>
	inline T* GetPlugin() {
		for (EnginePlugin* p : mPlugins)
			if (T * t = dynamic_cast<T*>(p))
				return t;
		return nullptr;
	}
	
private:
	friend class Instance;
	friend class Scene;

	STRATUM_API void LoadPlugins(const fs::path& folder = "Plugins/");
	STRATUM_API void UnloadPlugins();

	std::vector<PluginHandle> mPluginModules;
	std::vector<EnginePlugin*> mPlugins;
};