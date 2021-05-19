#pragma once

#include "Core/SpirvModule.hpp"

namespace stm {
  
inline VertexAttributeId stovertexattribute(const string& semantic) {
  static const unordered_map<string, VertexAttributeType> gAttributeMap {
    { "binormal", 	    VertexAttributeType::eBinormal },
    { "blendindices",   VertexAttributeType::eBlendIndices },
    { "blendweight", 	  VertexAttributeType::eBlendWeight },
    { "color", 			    VertexAttributeType::eColor },
    { "normal", 		    VertexAttributeType::eNormal },
    { "pointsize", 	    VertexAttributeType::ePointSize },
    { "psize", 			    VertexAttributeType::ePointSize },
    { "sv_position", 	  VertexAttributeType::ePosition },
    { "position", 	    VertexAttributeType::ePosition },
    { "vertex", 	      VertexAttributeType::ePosition },
    { "tangent", 		    VertexAttributeType::eTangent },
    { "texcoord", 	    VertexAttributeType::eTexcoord }
  };
  VertexAttributeId id;
  size_t l = semantic.find_first_of("0123456789");
  if (l != string::npos)
    id.mTypeIndex = atoi(semantic.c_str() + l);
  else {
    id.mTypeIndex = 0;
    l = semantic.length();
  }
  if (l > 0 && semantic[l-1] == '_') l--;
  string ls(semantic, 0, l);
  for (auto& c : ls) c = std::tolower(c, {});
  if (auto it = gAttributeMap.find(ls); it != gAttributeMap.end())
    id.mType = it->second;
  else
    if (ls.starts_with("sv_"))
      id.mType = VertexAttributeType::eSystemGenerated;
    else {
      id.mType = VertexAttributeType::eTexcoord;
      cout << "Warning: Unknown attribute type " << ls << endl;
    }
  return id;
};

class ShaderCompiler {
private:
  set<fs::path> mIncludePaths;

	vector<SpirvModule> ParseCompilerDirectives(const fs::path& filename, const string& source, uint32_t includeDepth = 0);
	vector<uint32_t> CompileSpirv(const fs::path& filename, const string& entryPoint, vk::ShaderStageFlagBits stage, const unordered_set<string>& defines);
	void SpirvReflection(SpirvModule& shaderModule);

public:
  template<ranges::range R> requires(convertible_to<ranges::range_value_t<R>, fs::path>)
	inline ShaderCompiler(const R& includePaths) : mIncludePaths(set<fs::path>(includePaths.begin(), includePaths.end())) {}
  
  inline vector<SpirvModule> operator()(const fs::path& filename) {
    string source = read_file<string>(filename);
    if (source.empty()) throw logic_error("failed to open file for reading");
    vector<SpirvModule> modules = ParseCompilerDirectives(filename, source);
    for (auto& shaderModule : modules) {
      shaderModule.mSpirv = CompileSpirv(filename, shaderModule.mEntryPoint, shaderModule.mStage, {});
      SpirvReflection(shaderModule);
    }
    return modules;
  }
};

}