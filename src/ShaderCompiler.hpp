#pragma once

#include "Core/SpirvModule.hpp"

namespace stm {
  
static const map<string, VertexAttributeType> gAttributeMap {
    { "position", 	VertexAttributeType::ePosition },
    { "normal", 		VertexAttributeType::eNormal },
    { "tangent", 		VertexAttributeType::eTangent },
    { "bitangent", 	VertexAttributeType::eBitangent },
    { "texcoord", 	VertexAttributeType::eTexcoord },
    { "color", 			VertexAttributeType::eColor },
    { "psize", 			VertexAttributeType::ePointSize },
    { "pointsize", 	VertexAttributeType::ePointSize }
  };

class ShaderCompiler {
private:
  set<fs::path> mIncludePaths;

	typedef vector<string>::iterator word_iterator;
	typedef void (ShaderCompiler::*DirectiveFunc)(SpirvModuleGroup& modules, word_iterator begin, const word_iterator& end);


  void DirectiveCompile(SpirvModuleGroup& modules, word_iterator begin, const word_iterator& end);
	inline static const map<string, DirectiveFunc> gCompilerDirectives { { "compile", &ShaderCompiler::DirectiveCompile } };
	

	SpirvModuleGroup ParseCompilerDirectives(const fs::path& filename, const string& source, uint32_t includeDepth = 0);
	vector<uint32_t> CompileSpirv(const fs::path& filename, const string& entryPoint, vk::ShaderStageFlagBits stage, const set<string>& defines);
	void SpirvReflection(SpirvModule& shaderModule);

public:
	inline ShaderCompiler(const set<fs::path>& includePaths) : mIncludePaths(includePaths) {}
  inline SpirvModuleGroup operator()(const fs::path& filename) {
    string source = ReadFile(filename);
    if (source.empty()) throw logic_error("failed to open file for reading");
    SpirvModuleGroup modules = ParseCompilerDirectives(filename, source);
    for (SpirvModule& shaderModule : modules) {
      shaderModule.mSpirv = CompileSpirv(filename, shaderModule.mEntryPoint, shaderModule.mStage, {});
      SpirvReflection(shaderModule);
    }
    return modules;
  }
};

}
