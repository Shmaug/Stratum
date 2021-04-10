#pragma once

#include "Scene.hpp"

#ifdef __linux
#include <dlfcn.h>
#endif

namespace stm {

struct PluginModule {
private:
#ifdef WIN32
  HMODULE
#elif defined(__linux)
  void* 
#endif
  mHandle;

  unordered_map<string, void*> mFunctionPtrs;

public:
  inline PluginModule(const fs::path& filename) {
#ifdef WIN32
    char* msgBuf;
    mHandle = LoadLibraryW(filename.c_str());
    if (mHandle == NULL) {
      FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |  FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&msgBuf, 0, NULL );
      throw runtime_error(msgBuf);
    }
#elif defined(__linux)
    mHandle = dlsym(filename.c_str(), RTLD_NOW);
#endif
  }
  inline ~PluginModule() {
#ifdef WIN32
    FreeLibrary(mHandle);
#endif
  };

  template<typename Ret, typename... Args>
  inline Ret invoke(const string& name, Args&&... args) {
    auto it = mFunctionPtrs.find(name);
    if (it == mFunctionPtrs.end()) {
#ifdef WIN32
      it = mFunctionPtrs.emplace(name, GetProcAddress(mHandle, name.c_str())).first;
#elif defined(__linux)
      it = mFunctionPtrs.emplace(name, dlsym(mHandle, name.c_str())).first;
#endif
    }
    typedef Ret (*fn_ptr) (Args...);
    return invoke((fn_ptr)it->second, forward<Args>(args)...);
  }

};

}
