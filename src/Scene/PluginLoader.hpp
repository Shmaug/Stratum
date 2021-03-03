#pragma once

#include "SceneNode.hpp"

namespace stm {

class PluginLoader : public SceneNode::Component {
public:
  inline PluginLoader(SceneNode& node, const string& name) : SceneNode::Component(node, name) {
	}

  STRATUM_API void Update(CommandBuffer& commandBuffer);

private:
    
  SceneNode::Component* LoadPlugin(const fs::path& filename) {
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
      SceneNode::Component* plugin = ((SceneNode::Component*(*)(SceneNode::Component*))funcPtr)(this);
      mPlugins.push_back(plugin);
      return plugin;
    } catch(exception e) {}
    return nullptr;
  }
};

}
