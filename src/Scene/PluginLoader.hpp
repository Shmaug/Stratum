#pragma once

#include "Scene.hpp"

namespace stm {

class PluginLoader : public Scene::Node {
public:
  inline PluginLoader(Scene& scene, const string& name) : Scene::Node(scene, name) {
	}

  STRATUM_API void Update(CommandBuffer& commandBuffer);

private:
    
  Scene::Node* LoadPlugin(const fs::path& filename) {
    try {
      
  #ifdef WIN32
      char* msgBuf;
      auto throw_if_null = [&](auto ptr){
        if (ptr == NULL) {
          FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |  FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&msgBuf, 0, NULL );
          throw runtime_error(msgBuf);
        }
      };

      HMODULE m = LoadLibraryW(filename.c_str());
      throw_if_null(m);
      FARPROC funcPtr = GetProcAddress(m, "stm::CreateComponent");
      throw_if_null(funcPtr);
  #endif
  #ifdef __linux
      throw; // TODO loadplugin linux
  #endif
      Scene::Node* plugin = ((Scene::Node*(*)(Scene::Node*))funcPtr)(this);
      mPlugins.push_back(plugin);
      return plugin;
    } catch(exception e) {}
    return nullptr;
  }
};

}
