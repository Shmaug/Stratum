#pragma once

#include "Core/SpirvModule.hpp"

namespace stm {
  
static const unordered_map<string, VertexAttributeType> gAttributeMap {
    { "binormal", 	  VertexAttributeType::eBinormal },
    { "blendindices", VertexAttributeType::eBlendIndices },
    { "blendweight", 	VertexAttributeType::eBlendWeight },
    { "color", 			  VertexAttributeType::eColor },
    { "normal", 		  VertexAttributeType::eNormal },
    { "pointsize", 	  VertexAttributeType::ePointSize },
    { "psize", 			  VertexAttributeType::ePointSize },
    { "position", 	  VertexAttributeType::ePosition },
    { "tangent", 		  VertexAttributeType::eTangent },
    { "texcoord", 	  VertexAttributeType::eTexcoord }
  };

class ShaderCompiler {
private:
  set<fs::path> mIncludePaths;

	typedef vector<string>::iterator word_iterator;
	typedef void (ShaderCompiler::*DirectiveFunc)(unordered_map<string, SpirvModule>& modules, word_iterator begin, const word_iterator& end);

  void DirectiveCompile(unordered_map<string, SpirvModule>& modules, word_iterator begin, const word_iterator& end);
	inline static const unordered_map<string, DirectiveFunc> gCompilerDirectives { { "compile", &ShaderCompiler::DirectiveCompile } };

	unordered_map<string, SpirvModule> ParseCompilerDirectives(const fs::path& filename, const string& source, uint32_t includeDepth = 0);
	vector<uint32_t> CompileSpirv(const fs::path& filename, const string& entryPoint, vk::ShaderStageFlagBits stage, const unordered_set<string>& defines);
	void SpirvReflection(SpirvModule& shaderModule);

public:
  template<ranges::range R> requires(convertible_to<ranges::range_value_t<R>, fs::path>)
	inline ShaderCompiler(const R& includePaths) : mIncludePaths(set<fs::path>(includePaths.begin(), includePaths.end())) {}
  inline unordered_map<string, SpirvModule> operator()(const fs::path& filename) {
    string source = ReadFile(filename);
    if (source.empty()) throw logic_error("failed to open file for reading");
    unordered_map<string, SpirvModule> modules = ParseCompilerDirectives(filename, source);
    for (auto& [id,shaderModule] : modules) {
      shaderModule.mSpirv = CompileSpirv(filename, shaderModule.mEntryPoint, shaderModule.mStage, {});
      SpirvReflection(shaderModule);
    }
    return modules;
  }
};

}
