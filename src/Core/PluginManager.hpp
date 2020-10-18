#pragma once

#include "../Stratum.hpp"

namespace stm {

class PluginManager {
public:
	template<class T>
	inline T* GetPlugin() const {
		for (const EnginePlugin* p : mPlugins)
			if (const T* t = dynamic_cast<T*>(p))
				return t;
		return nullptr;
	}
	inline void ForEach(std::function<void(EnginePlugin*)> func) {
		for (auto& p : mPlugins)
			func((EnginePlugin*)p);
	}
	
private:
	friend class Instance;
	STRATUM_API PluginManager(const fs::path& folder = "Plugins/");

	class PluginModule {
	private:
		EnginePlugin* mPlugin = nullptr;
		#ifdef WINDOWS
		HMODULE mModule = NULL;
		#endif

	public:
		STRATUM_API PluginModule(const fs::path& filename);
		STRATUM_API ~PluginModule();
		inline PluginModule(const PluginModule& cpy) = default;
		inline PluginModule(PluginModule&& mv) : mModule(mv.mModule), mPlugin(mv.mPlugin) {
			mv.mModule = nullptr;
			mv.mPlugin = nullptr;
		}

		inline PluginModule& operator =(PluginModule& rhs) = default;
		inline PluginModule& operator =(PluginModule&& rhs) {
			mModule = rhs.mModule;
			mPlugin = rhs.mPlugin;
			rhs.mModule = NULL;
			rhs.mPlugin = nullptr;
			return *this;
		}

		STRATUM_API bool operator <(const PluginModule& rhs);
		inline explicit operator EnginePlugin*() const { return mPlugin; }
	};

	std::vector<PluginModule> mPlugins;
};

}