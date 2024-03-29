#pragma once

#include "common.hpp"

namespace stm {

struct dynamic_library {
private:
#ifdef _WIN32
  HMODULE mHandle;
#elif defined(__linux)
  void* mHandle;
#endif
  unordered_map<string, void*> mFunctionPtrs;

public:
  inline dynamic_library(const fs::path& filename) {
#ifdef _WIN32
    char* msgBuf;
    mHandle = LoadLibraryW(filename.c_str());
    if (mHandle == NULL) {
      FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |  FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&msgBuf, 0, NULL );
      throw runtime_error(msgBuf);
    }
#elif defined(__linux)
    mHandle = dlopen(filename.string().c_str(), RTLD_NOW);
    if (mHandle == NULL)
      throw runtime_error("Failed to load " + filename.string());
#endif
  }

  inline ~dynamic_library() {
#ifdef _WIN32
    FreeLibrary(mHandle);
#elif defined(__linux)
    dlclose(mHandle);
#endif
  };

  template<typename return_t, typename... Args>
  inline return_t invoke(const string& name, Args&&... args) {
    auto it = mFunctionPtrs.find(name);
    if (it == mFunctionPtrs.end()) {
#ifdef _WIN32
      it = mFunctionPtrs.emplace(name, GetProcAddress(mHandle, name.c_str())).first;
#elif defined(__linux)
      it = mFunctionPtrs.emplace(name, dlsym(mHandle, name.c_str())).first;
#endif
    }
    if (it->second == nullptr) throw invalid_argument("Could not find function " + name);
    
#ifdef _WIN32
    typedef return_t(__stdcall * fn_t)(Args...);
#elif defined(__linux)
    typedef return_t( * fn_t)(Args...);
#endif
    fn_t fn = (fn_t)it->second;
    return std::invoke(fn, forward<Args>(args)...);
  }
};

}
