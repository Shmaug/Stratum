#include <vulkan/vulkan.hpp>
#include <spirv_cross/spirv_cross.hpp>
#include <shaderc/shaderc.hpp>

#include "Data/Shader.hpp"

using namespace std;
using namespace stm;
using namespace shaderc;

shaderc_shader_kind vk_to_shaderc(vk::ShaderStageFlagBits stage) {
	switch (stage) {
		case vk::ShaderStageFlagBits::eVertex: return shaderc_vertex_shader;
		case vk::ShaderStageFlagBits::eFragment: return shaderc_fragment_shader;
		case vk::ShaderStageFlagBits::eGeometry: return shaderc_geometry_shader;
		case vk::ShaderStageFlagBits::eTessellationControl: return shaderc_tess_control_shader;
		case vk::ShaderStageFlagBits::eTessellationEvaluation: return shaderc_tess_evaluation_shader;
		case vk::ShaderStageFlagBits::eCompute: return shaderc_compute_shader;
	}
	return shaderc_vertex_shader;
}

class Includer : public CompileOptions::IncluderInterface {
private:
	set<string> mIncludePaths;

public:
	inline Includer(const set<string>& paths) : mIncludePaths(paths) {}

	inline shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type, const char* requesting_source, size_t include_depth) override {
		string fullpath;
		
		if (type == shaderc_include_type_relative)
			fullpath = fs::absolute(requesting_source).parent_path().string() + "/" + requested_source;
		else
			for (const string& p : mIncludePaths)
				if (fs::exists(p + "/" + requested_source))
					fullpath = p + "/" + requested_source;

		shaderc_include_result* response = new shaderc_include_result();
		size_t contentSize;
		char* contentSource = ReadFile(fullpath, contentSize);
		if (contentSource == nullptr) {
			char* err = new char[512];
			sprintf(err, "%s: Failed to evaluate %s include", requesting_source, type == shaderc_include_type_relative ? "relative" : "standard");
			response->source_name = new char[1] { '\0' };
			response->source_name_length = 0;
			response->content = err;
			response->content_length = strlen(response->content);
			response->user_data = err;
		} else {
			char* name = new char[fullpath.length()];
			memcpy(name, fullpath.c_str(), fullpath.length());
			response->source_name = name;
			response->source_name_length = fullpath.length();
			response->content = contentSource;
			response->content_length = contentSize;
			response->user_data = nullptr;
		}
		return response;
	}
	inline void ReleaseInclude(shaderc_include_result* data) override {
		delete data->source_name;
		delete data->content;
		if (data->user_data) delete data->user_data;
		delete data;
	}
};

struct AssemblyOutputInfo {
	fs::path mFilename;
	string mEntryPoint;
	set<string> mKeywords;
};
struct CompilerContext {
	fs::path mFilename;
	uint32_t mLineNumber;
	vector<string>::iterator mCurrentWord;
	vector<string>::iterator mLineEnd;

	set<string> mInlineUniformBlocks;
	vector<AssemblyOutputInfo> mAssemblyOutputs;

	set<string> mMacros;
	string mOptimizationLevel;

	std::vector<Shader::Variant*> mResult;
};

#define DIRECTIVE_ERR(...) { fprintf_color(ConsoleColorBits::eRed, stderr, "%s(%u): Error: '%s': ", ctx.mFilename.c_str(), ctx.mLineNumber, directiveName.c_str()); fprintf_color(ConsoleColorBits::eRed, stderr, __VA_ARGS__); return false; }

bool DirectiveAssemblyOutput(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected a filename\n");
	string filename = *ctx.mCurrentWord;
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected an entry point\n");
	ctx.mAssemblyOutputs.push_back({});
	AssemblyOutputInfo& a = ctx.mAssemblyOutputs.back();
	a.mEntryPoint = *ctx.mCurrentWord;
	a.mFilename = filename;
	while (++ctx.mCurrentWord != ctx.mLineEnd) a.mKeywords.insert(*ctx.mCurrentWord);
	return true;
}
bool DirectiveBlend(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected an attachment index\n");
	uint32_t index = atoi(ctx.mCurrentWord->c_str());
	auto cur = ctx.mCurrentWord;
	for (auto& v : variants) {
		ctx.mCurrentWord = cur;

		if (v->mBlendStates.size() <= index)
			for (uint32_t i = 0; i < 1 + index - v->mBlendStates.size(); i++) {
				vk::PipelineColorBlendAttachmentState defaultBlendState = {};
				defaultBlendState.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
				defaultBlendState.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
				defaultBlendState.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
				defaultBlendState.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
				defaultBlendState.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
				v->mBlendStates.push_back(defaultBlendState);
			}

		vk::PipelineColorBlendAttachmentState& blendState = v->mBlendStates[index];
		if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected a blend op, or 'false'\n");
		if (*ctx.mCurrentWord == "false") {
			blendState.blendEnable = VK_FALSE;
			if (++ctx.mCurrentWord != ctx.mLineEnd) blendState.colorWriteMask = atocolormask(*ctx.mCurrentWord);
			continue;
		}
		blendState.blendEnable = VK_TRUE;
		blendState.colorBlendOp = blendState.alphaBlendOp = atoblendop(*ctx.mCurrentWord);
		if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected a source blend mode\n");
		blendState.srcColorBlendFactor = atoblendfactor(*ctx.mCurrentWord);
		if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected a destination blend mode\n");
		blendState.dstColorBlendFactor = atoblendfactor(*ctx.mCurrentWord);
		if (++ctx.mCurrentWord == ctx.mLineEnd) continue;
		blendState.alphaBlendOp = atoblendop(*ctx.mCurrentWord);
		if (++ctx.mCurrentWord == ctx.mLineEnd) continue;
		blendState.srcAlphaBlendFactor = atoblendfactor(*ctx.mCurrentWord);
		if (++ctx.mCurrentWord == ctx.mLineEnd) continue;
		blendState.dstAlphaBlendFactor = atoblendfactor(*ctx.mCurrentWord);
		if (++ctx.mCurrentWord == ctx.mLineEnd) continue;
		blendState.colorWriteMask = atocolormask(*ctx.mCurrentWord);
	}
	return true;
}
bool DirectiveCull(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected one of 'front' 'back' 'false'\n");
	if (*ctx.mCurrentWord == "front") 			for (auto& v : variants) v->mCullMode = vk::CullModeFlagBits::eFront;
	else if (*ctx.mCurrentWord == "back") 	for (auto& v : variants) v->mCullMode = vk::CullModeFlagBits::eBack;
	else if (*ctx.mCurrentWord == "false") 	for (auto& v : variants) v->mCullMode = vk::CullModeFlagBits::eNone;
	else DIRECTIVE_ERR("expected one of 'front' 'back' 'false'\n");
	return true;
}
bool DirectiveDepthOp(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected a depth op\n");
	for (auto& v : variants) v->mDepthStencilState.depthCompareOp = atocmp(*ctx.mCurrentWord);
	return true;
}
bool DirectiveFill(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected one of 'solid' 'line' 'point'\n");
	if (*ctx.mCurrentWord == "solid") 		 	for (auto& v : variants) v->mPolygonMode = vk::PolygonMode::eFill;
	else if (*ctx.mCurrentWord == "line")  	for (auto& v : variants) v->mPolygonMode = vk::PolygonMode::eLine;
	else if (*ctx.mCurrentWord == "point") 	for (auto& v : variants) v->mPolygonMode = vk::PolygonMode::ePoint;
	else DIRECTIVE_ERR("expected one of 'solid' 'line' 'point'\n");
	return true;
}
bool DirectiveInlineUniformBlock(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected a descriptor name\n");
	ctx.mInlineUniformBlocks.insert(*ctx.mCurrentWord);
	return true;
}
bool DirectiveKernel(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected an entry point\n");
	Shader::Variant* v = ctx.mResult.emplace_back(new Shader::Variant());
	v->mShaderPass = "";
	v->mModules.emplace_back(nullptr, {}, vk::ShaderStageFlagBits::eCompute, *ctx.mCurrentWord, {});
	return true;
}
bool DirectiveMultiCompile(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	size_t kwc = ctx.mResult.size();
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected one or more keywords\n");
	// iterate all the keywords added by this multi_compile
	while (ctx.mCurrentWord != ctx.mLineEnd) {
		if (kwc == 0)
			// duplicate all variants, add this keyword to each
			for (uint32_t i = 0; i < variants.size(); i++)
				ctx.mResult.emplace_back(new Shader::Variant(*variants[i]))->mKeywords.insert(*ctx.mCurrentWord);
		else
			// duplicate all existing variants, add this keyword to each
			for (uint32_t i = 0; i < kwc; i++)
				ctx.mResult.emplace_back(new Shader::Variant(*ctx.mResult[i]))->mKeywords.insert(*ctx.mCurrentWord);
		++ctx.mCurrentWord;
	}
	return true;
}
bool DirectiveOptimization(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected an an optimization level\n");
	ctx.mOptimizationLevel = *ctx.mCurrentWord;
	return true;
}
bool DirectivePass(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected a pass name\n");
	string shaderPass = *ctx.mCurrentWord;
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected a vertex shader entry point\n");
	string vs = *ctx.mCurrentWord;
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected a fragment shader entry point\n");
	string fs = *ctx.mCurrentWord;

	ctx.mResult.push_back({});
	auto& v = ctx.mResult.back();
	v.mShaderPass = shaderPass;
	v.mModules.push_back({ nullptr, {}, vk::ShaderStageFlagBits::eVertex, vs, {} });
	v.mModules.push_back({ nullptr, {}, vk::ShaderStageFlagBits::eFragment, fs, {} });
	return true;
}
bool DirectiveRenderQueue(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected an integer\n");
	for (auto& v : variants) v->mRenderQueue = atoi(ctx.mCurrentWord->c_str());
	return true;
}
bool DirectiveSampleShading(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd || (*ctx.mCurrentWord != "true" && *ctx.mCurrentWord != "false")) DIRECTIVE_ERR("expected 'true' or 'false'\n");
	bool s = *ctx.mCurrentWord == "true";
	for (auto& v : variants) v->mSampleShading = s;
	return true;
}
bool DirectiveStaticSampler(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected an sampler name\n");
	string name = *ctx.mCurrentWord;

	vk::SamplerCreateInfo samplerInfo = {};
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;
	samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 2;
	samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = vk::CompareOp::eAlways;
	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
	samplerInfo.minLod = 0;
	samplerInfo.maxLod = 12;
	samplerInfo.mipLodBias = 0;

	while (++ctx.mCurrentWord != ctx.mLineEnd) {
		size_t eq = ctx.mCurrentWord->find('=');
		if (eq == string::npos) DIRECTIVE_ERR("expected arguments in the form '<arg>=<value>'\n");
		string id = ctx.mCurrentWord->substr(0, eq);
		string val = ctx.mCurrentWord->substr(eq + 1);

		if      (id == "magFilter")		  samplerInfo.magFilter = atofilter(val);
		else if (id == "minFilter")			samplerInfo.minFilter = atofilter(val);
		else if (id == "filter")				samplerInfo.minFilter = samplerInfo.magFilter = atofilter(val);
		else if (id == "compareOp") {
			vk::CompareOp cmp = atocmp(val);
			samplerInfo.compareEnable = VK_TRUE;
			samplerInfo.compareOp = cmp;
		}
		else if (id == "addressModeU")	samplerInfo.addressModeU = atoaddressmode(val);
		else if (id == "addressModeV")	samplerInfo.addressModeV = atoaddressmode(val);
		else if (id == "addressModeW")	samplerInfo.addressModeW = atoaddressmode(val);
		else if (id == "addressMode")	 	samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = atoaddressmode(val);
		else if (id == "maxAnisotropy") {
			float aniso = (float)atof(val.c_str());
			samplerInfo.anisotropyEnable = aniso <= 0 ? VK_FALSE : VK_TRUE;
			samplerInfo.maxAnisotropy = aniso;
		}
		else if (id == "borderColor")				samplerInfo.borderColor = atobordercolor(val);
		else if (id == "unnormalizedCoordinates")	samplerInfo.unnormalizedCoordinates = val == "true" ? VK_TRUE : VK_FALSE;
		else if (id == "mipmapMode") samplerInfo.mipmapMode = atomipmapmode(val);
		else if (id == "minLod") samplerInfo.minLod = (float)atof(val.c_str());
		else if (id == "maxLod") samplerInfo.maxLod = (float)atof(val.c_str());
		else if (id == "mipLodBias") samplerInfo.mipLodBias = (float)atof(val.c_str());
		else DIRECTIVE_ERR("unknown argument %s (expected one of '[min,mag]Filter' 'addressMode[U,V,W]' 'borderColor' 'compareOp' 'maxAnisotropy' 'unnormalizedCoordinates')\n", id.c_str());
	}

	for (auto& v : variants) v->mImmutableSamplers[name] = samplerInfo;
	return true;
}
bool DirectiveZTest(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected one of 'true' 'false'\n");
	if (*ctx.mCurrentWord == "true") for (auto& v : variants) v->mDepthStencilState.depthTestEnable = VK_TRUE;
	else if (*ctx.mCurrentWord == "false") for (auto& v : variants) v->mDepthStencilState.depthTestEnable = VK_FALSE;
	else DIRECTIVE_ERR("expected one of 'true' 'false'\n");
	return true;
}
bool DirectiveZWrite(CompilerContext& ctx, const string& directiveName, const vector<Shader::Variant*>& variants) {
	if (++ctx.mCurrentWord == ctx.mLineEnd) DIRECTIVE_ERR("expected one of 'true' 'false'\n");
	if (*ctx.mCurrentWord == "true") for (auto& v : variants) v->mDepthStencilState.depthWriteEnable = VK_TRUE;
	else if (*ctx.mCurrentWord == "false") for (auto& v : variants) v->mDepthStencilState.depthWriteEnable = VK_FALSE;
	else DIRECTIVE_ERR("expected one of 'true' 'false'\n");
	return true;
}

map<string, bool(*)(CompilerContext&, const string&, const vector<Shader::Variant*>&)> gCompilerDirectives {
	{ "assembly_output", DirectiveAssemblyOutput },
	{ "blend", DirectiveBlend },
	{ "cull", DirectiveCull },
	{ "depth_op", DirectiveDepthOp },
	{ "fill", DirectiveFill },
	{ "inline_uniform_block", DirectiveInlineUniformBlock },
	{ "kernel", DirectiveKernel },
	{ "multi_compile", DirectiveMultiCompile },
	{ "optimization", DirectiveOptimization },
	{ "pass", DirectivePass },
	{ "render_queue", DirectiveRenderQueue },
	{ "sample_shading", DirectiveSampleShading },
	{ "static_sampler", DirectiveStaticSampler },
	{ "ztest", DirectiveZTest },
	{ "zwrite", DirectiveZWrite }
};

int ParseCompilerDirectives(CompilerContext& ctx, const fs::path& filename, const string& source, const set<string>& includePaths, uint32_t includeDepth = 0, stack<pair<string, set<string>>>& selectorStack = stack<pair<string, set<string>>>()) {
	uint32_t lineNumber = 0;

	string line;
	istringstream srcstream(source);
	while (getline(srcstream, line)) {
		lineNumber++;
		istringstream linestream(line);
		vector<string> words{ istream_iterator<string>{ linestream }, istream_iterator<string>{} };

		if (words.empty()) continue;

		auto cur = words.begin();

		if ((*cur)[0] != '#') continue;

		if (*cur == "#pragma") {
			if (++cur == words.end() || gCompilerDirectives.count(*cur) == 0) { continue; }

			auto prevFilename = ctx.mFilename;

			ctx.mFilename = filename;
			ctx.mLineNumber = lineNumber;
			ctx.mCurrentWord = ++words.begin();
			ctx.mLineEnd = words.end();

			vector<Shader::Variant*> variants;
			for (const auto& [kw, v] : ctx.mResult) {
				bool exclude = false;
				if (!selectorStack.empty())
					for (const string& kw : selectorStack.top().second)
						if ((kw[0] == '!' && v.mKeywords.count(kw.substr(1))) || (kw[0] != '!' && !v.mKeywords.count(kw))) { exclude = true; break; }
				if (!exclude) variants.push_back(&v);
			}
			ctx.mFilename = prevFilename;
			if (!gCompilerDirectives.at(*cur)(ctx, *cur, variants)) {
				fprintf_color(ConsoleColorBits::eRed, stderr, "%s(%u): Error: Unknown directive '%s'\n", ctx.mFilename.c_str(), ctx.mLineNumber, cur->c_str());
				return 1;
			}

		} else if (*cur == "#include") {
			if (++cur == words.end() || cur->length() <= 2) continue;
			
			shaderc_include_type includeType = shaderc_include_type_relative;
			string includePath = cur->substr(1, cur->length() - 2);
			if ((*cur)[0] == '<') includeType = shaderc_include_type_standard;
			
			Includer includer(includePaths);
			shaderc_include_result* includeResult = includer.GetInclude(includePath.c_str(), includeType, filename.string().c_str(), includeDepth);
			if (includeResult->content_length)
				ParseCompilerDirectives(ctx, includeResult->source_name, includeResult->content, includePaths, includeDepth + 1, selectorStack);
			includer.ReleaseInclude(includeResult);
			
		} else if (*cur == "#ifdef") {
			if (++cur == words.end()) continue;
			set<string> top = selectorStack.empty() ? set<string>() : selectorStack.top().second;
			top.insert(*cur);
			selectorStack.push({ "!" + *cur, top});
		} else if (*cur == "#ifndef") {
			if (++cur == words.end()) continue;
			set<string> top = selectorStack.empty() ? set<string>() : selectorStack.top().second;
			top.insert("!" + *cur);
			selectorStack.push({ *cur, top });
		} else if (*cur == "#if") {
			set<string> top = selectorStack.empty() ? set<string>() : selectorStack.top().second;
			
			// TODO: defined() etc

			selectorStack.push({ "", top });
		} else if (*cur == "#elif" || *cur == "#else" || *cur == "#endif") {
			string kw = selectorStack.top().first;
			selectorStack.pop();
			if (*cur == "#elif" || *cur == "#else") {
				auto next = selectorStack.empty() ? set<string>() : selectorStack.top().second;
				if (!kw.empty()) next.insert(kw);

				// defined() etc

				selectorStack.push({ "", next });
			}
		}
	}

	return 0;
}
int SpirvReflection(const CompilerContext& ctx, Shader::Variant& destVariant, SpirvModule& spirv) {
	spirv_cross::Compiler compiler(spirv.mSpirvBinary.data(), spirv.mSpirvBinary.size());
	spirv_cross::ShaderResources resources = compiler.get_shader_resources();

	map<string, VertexAttributeType> attributeMap {
		{ "POSITION", 	VertexAttributeType::ePosition },
		{ "NORMAL", 		VertexAttributeType::eNormal },
		{ "TANGENT", 		VertexAttributeType::eTangent },
		{ "BITANGENT", 	VertexAttributeType::eBitangent },
		{ "TEXCOORD", 	VertexAttributeType::eTexcoord },
		{ "COLOR", 			VertexAttributeType::eColor },
		{ "PSIZE", 			VertexAttributeType::ePointSize },
		{ "POINTSIZE", 	VertexAttributeType::ePointSize }
	};
	
	unordered_map<spirv_cross::SPIRType::BaseType, vector<vk::Format>> formatMap {
		{ spirv_cross::SPIRType::SByte, 	{ vk::Format::eR8Snorm, 	vk::Format::eR8G8Snorm, 	 vk::Format::eR8G8B8Snorm, 			vk::Format::eR8G8B8A8Snorm } },
		{ spirv_cross::SPIRType::UByte, 	{ vk::Format::eR8Unorm, 	vk::Format::eR8G8Unorm, 	 vk::Format::eR8G8B8Unorm, 			vk::Format::eR8G8B8A8Unorm } },
		{ spirv_cross::SPIRType::Short, 	{ vk::Format::eR16Sint, 	vk::Format::eR16G16Sint, 	 vk::Format::eR16G16B16Sint, 		vk::Format::eR16G16B16A16Sint } },
		{ spirv_cross::SPIRType::UShort, 	{ vk::Format::eR16Uint, 	vk::Format::eR16G16Uint, 	 vk::Format::eR16G16B16Uint, 		vk::Format::eR16G16B16A16Uint } },
		{ spirv_cross::SPIRType::Int, 		{ vk::Format::eR32Sint, 	vk::Format::eR32G32Sint, 	 vk::Format::eR32G32B32Sint, 		vk::Format::eR32G32B32A32Sint } },
		{ spirv_cross::SPIRType::UInt, 		{ vk::Format::eR32Uint, 	vk::Format::eR32G32Uint, 	 vk::Format::eR32G32B32Uint, 		vk::Format::eR32G32B32A32Uint } },
		{ spirv_cross::SPIRType::Int64, 	{ vk::Format::eR64Sint, 	vk::Format::eR64G64Sint, 	 vk::Format::eR64G64B64Sint, 		vk::Format::eR64G64B64A64Sint } },
		{ spirv_cross::SPIRType::UInt64, 	{ vk::Format::eR32Uint, 	vk::Format::eR32G32Uint, 	 vk::Format::eR32G32B32Uint, 		vk::Format::eR32G32B32A32Uint } },
		{ spirv_cross::SPIRType::Half, 		{ vk::Format::eR16Sfloat, vk::Format::eR16G16Sfloat, vk::Format::eR16G16B16Sfloat,  vk::Format::eR16G16B16A16Sfloat } },
		{ spirv_cross::SPIRType::Float, 	{ vk::Format::eR32Sfloat, vk::Format::eR32G32Sfloat, vk::Format::eR32G32B32Sfloat,  vk::Format::eR32G32B32A32Sfloat } },
		{ spirv_cross::SPIRType::Double, 	{ vk::Format::eR64Sfloat, vk::Format::eR64G64Sfloat, vk::Format::eR64G64B64Sfloat,  vk::Format::eR64G64B64A64Sfloat } }
	};
	
	auto RegisterResource = [&](const spirv_cross::Resource& resource, vk::DescriptorType type) {
		auto& binding = destVariant.mDescriptorSetBindings[resource.name];
		binding.mSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
		binding.mBinding.stageFlags |= spirv.mStage;
		binding.mBinding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
		if (ctx.mInlineUniformBlocks.count(resource.name)) {
			binding.mBinding.descriptorType = vk::DescriptorType::eInlineUniformBlockEXT;
			binding.mBinding.descriptorCount = (uint32_t)compiler.get_declared_struct_size(compiler.get_type(resource.type_id));
		} else {
		binding.mBinding.descriptorCount = compiler.get_type(resource.type_id).array.empty() ? 1 : compiler.get_type(resource.type_id).array[0];
		binding.mBinding.descriptorType = type;
		}
	};
	
	for (const auto& r : resources.stage_inputs) {
		auto& input = spirv.mInputs[r.name];
		auto& type = compiler.get_type(r.base_type_id);

		input.mLocation = compiler.get_decoration(r.id, spv::DecorationLocation);
		input.mFormat = formatMap.at(type.basetype)[type.vecsize-1];
		input.mTypeIndex = 0;

		string semantic = compiler.get_decoration_string(r.id, spv::DecorationHlslSemanticGOOGLE);
		if (semantic.empty()) continue;

		size_t semanticIndex = semantic.find_first_of("0123456789");
		string semanticName = semantic;
		if (semanticIndex != string::npos) {
			input.mTypeIndex = atoi(semantic.c_str() + semanticIndex);
			semanticName = semantic.substr(0, semanticIndex);
		}
		if (semanticName.back() == '_') semanticName = semanticName.substr(0, semanticName.length() - 1);
		input.mType = attributeMap.at(semanticName);
	}
	for (const auto& r : resources.separate_images) 	RegisterResource(r, compiler.get_type(r.type_id).image.dim == spv::DimBuffer ? vk::DescriptorType::eUniformTexelBuffer : vk::DescriptorType::eSampledImage);
	for (const auto& r : resources.storage_images) 		RegisterResource(r, compiler.get_type(r.type_id).image.dim == spv::DimBuffer ? vk::DescriptorType::eStorageTexelBuffer : vk::DescriptorType::eStorageImage);
	for (const auto& r : resources.sampled_images) 		RegisterResource(r, vk::DescriptorType::eCombinedImageSampler);
	for (const auto& r : resources.storage_buffers) 	RegisterResource(r, vk::DescriptorType::eStorageBuffer);
	for (const auto& r : resources.separate_samplers) RegisterResource(r, vk::DescriptorType::eSampler);
	for (const auto& r : resources.uniform_buffers) 	RegisterResource(r, vk::DescriptorType::eUniformBuffer);
	for (const auto& r : resources.subpass_inputs) 		RegisterResource(r, vk::DescriptorType::eInputAttachment);
	for (const auto& r : resources.acceleration_structures) 		RegisterResource(r, vk::DescriptorType::eAccelerationStructureKHR);
	for (const auto& r : resources.push_constant_buffers) {
		spirv_cross::SPIRType type = compiler.get_type(r.base_type_id);
		for (uint32_t i = 0; i < type.member_types.size(); i++) {
			spirv_cross::SPIRType mtype = compiler.get_type(type.member_types[i]);
			string name = compiler.get_member_name(r.base_type_id, i);
			
			destVariant.mPushConstants[name].stageFlags |= spirv.mStage;
			destVariant.mPushConstants[name].offset = compiler.type_struct_member_offset(type, i);

			switch (mtype.basetype) {
			case spirv_cross::SPIRType::Boolean:
			case spirv_cross::SPIRType::SByte:
			case spirv_cross::SPIRType::UByte:
				destVariant.mPushConstants[name].size = 1;
				break;
			case spirv_cross::SPIRType::Short:
			case spirv_cross::SPIRType::UShort:
			case spirv_cross::SPIRType::Half:
				destVariant.mPushConstants[name].size = 2;
				break;
			case spirv_cross::SPIRType::Int:
			case spirv_cross::SPIRType::UInt:
			case spirv_cross::SPIRType::Float:
				destVariant.mPushConstants[name].size = 4;
				break;
			case spirv_cross::SPIRType::Int64:
			case spirv_cross::SPIRType::UInt64:
			case spirv_cross::SPIRType::Double:
				destVariant.mPushConstants[name].size = 8;
				break;
			case spirv_cross::SPIRType::Struct:
				destVariant.mPushConstants[name].size = (uint32_t)compiler.get_declared_struct_size(mtype);
				break;
			case spirv_cross::SPIRType::Unknown:
			case spirv_cross::SPIRType::Void:
			case spirv_cross::SPIRType::AtomicCounter:
			case spirv_cross::SPIRType::Image:
			case spirv_cross::SPIRType::SampledImage:
			case spirv_cross::SPIRType::Sampler:
			case spirv_cross::SPIRType::AccelerationStructure:
				fprintf(stderr, "Warning: Unknown type for push constant: %s\n", name.c_str());
				destVariant.mPushConstants[name].size = 0;
				break;
			}

			destVariant.mPushConstants[name].size *= mtype.columns * mtype.vecsize;
			for (uint32_t dim : mtype.array) destVariant.mPushConstants[name].size *= dim;
		}
	}

	if (spirv.mStage == vk::ShaderStageFlagBits::eCompute) {
		auto entryPoints = compiler.get_entry_points_and_stages();
		for (const auto& e : entryPoints) {
			auto& ep = compiler.get_entry_point(e.name, e.execution_model);
			destVariant.mWorkgroupSize = { ep.workgroup_size.x, ep.workgroup_size.y, ep.workgroup_size.z };
		}
	}

	return 0;
}

int CompileSpirv(const fs::path& filename, const set<string>& macros, const string& optimizationLevel, const set<string>& includePaths, SpirvModule& dest) {
	string source;
	ReadFile(filename, source);

	bool hlsl = filename.extension() == ".hlsl";
	// TODO: Try to compile with DXC
	/*
	if (hlsl) {
		string spvFile = fs::temp_directory_path().string() + "/" + p.filename().stem().string() + ".spv";
		if (fs::exists(spvFile)) fs::remove(spvFile);
		string cmd = "dxc -spirv ";
		switch (dest.mStage) {
			case vk::ShaderStageFlagBits::eCompute:
				cmd += "-T cs_6_6 ";
				break;
			case vk::ShaderStageFlagBits::eVertex:
				cmd += "-T vs_6_6 ";
				break;
			case vk::ShaderStageFlagBits::eFragment:
				cmd += "-T ps_6_6 ";
				break;
			default:
				return 1;
		}
		cmd += "-E " + dest.mEntryPoint + " ";
		cmd += filename + " -Fo " + spvFile;
		cmd += "-nologo ";
		if (optimizationLevel == "zero") cmd += "-O0 ";

		for (const string& i : includePaths) cmd += "-I \"" + i + "\" ";
		for (const string& m : macros) cmd += "-D" + m + " ";

		if (int ret = system(cmd.c_str())) return ret;
		if (fs::exists(spvFile)) {
			fs::remove(spvFile);

			vector<uint8_t> spv;
			ReadFile(spvFile, spv);

			dest.mSpirvBinary.resize(spv.size() / sizeof(uint32_t));
			memcpy(dest.mSpirvBinary.data(), spv.data(), spv.size());

			return 0;
		}
	}
	*/

	// Compile with shaderc

	CompileOptions options;
	options.SetHlslFunctionality1(true);
	options.SetOptimizationLevel(shaderc_optimization_level_zero);
	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
	options.SetTargetSpirv(shaderc_spirv_version_1_3);
	options.SetAutoBindUniforms(false);
	options.SetGenerateDebugInfo();
	options.SetSourceLanguage(hlsl ? shaderc_source_language_hlsl : shaderc_source_language_glsl);
	options.SetIncluder(make_unique<Includer>(includePaths));
	for (auto& m : macros) options.AddMacroDefinition(m);
	options.SetOptimizationLevel(shaderc_optimization_level_zero);
	if (optimizationLevel == "performance") options.SetOptimizationLevel(shaderc_optimization_level_performance);
	else if (optimizationLevel == "size") options.SetOptimizationLevel(shaderc_optimization_level_size);
	Compiler compiler;
	SpvCompilationResult result = compiler.CompileGlslToSpv(source, vk_to_shaderc(dest.mStage), filename.string().c_str(), dest.mEntryPoint.c_str(), options);
	string msg = result.GetErrorMessage();
	if (msg.size()) {
		stringstream ss(msg);
		string line;
		while (getline(ss, line)) {
			if (const char* error = strstr(line.c_str(), "error: "))
				fprintf_color(ConsoleColorBits::eRed, stderr, "%s\n", error + 7);
			else
				fprintf_color(ConsoleColorBits::eRed, stderr, "%s\n", line.c_str());
		}
	}
	dest.mSpirvBinary.clear();
	for (const uint32_t& c : result) dest.mSpirvBinary.push_back(c);
	return result.GetCompilationStatus() == shaderc_compilation_status_success ? 0 : 1;
}
Shader CompileShader(const string& filename, const set<string>& includePaths) {
	Shader destShader;
	
	CompilerContext context = {};
	context.mFilename = filename;

	string source;
	if (!ReadFile(filename, source)) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "%s: Error: Failed to open file for reading\n", filename.c_str());
		return destShader;
	}
	if (int result = ParseCompilerDirectives(context, filename, source, includePaths)) return destShader;

	shaderc_compilation_status status = shaderc_compilation_status_success;

	for (auto& variant : destShader.mVariants) {
		set<string> macros = context.mMacros;
		for (const string& kw : variant.mKeywords) macros.insert(kw);
		for (uint32_t i = 0; i < variant.mModules.size(); i++) {
			
			set<string> m = macros;
			m.insert("ENTRYP_" + variant.mModules[i].mEntryPoint);
			if (int result = CompileSpirv(context.mFilename, m, context.mOptimizationLevel, includePaths, variant.mModules[i])) return destShader;
			if (int result = SpirvReflection(context, variant, variant.mModules[i])) return destShader;
			
			// do assembly output
			for (const auto& assemblyOutput : context.mAssemblyOutputs)
				if (variant.mModules[i].mEntryPoint == assemblyOutput.mEntryPoint) {
					bool f = true;
					for (const string& kw : assemblyOutput.mKeywords) if (!variant.mKeywords.count(kw)) { f = false; break; }
					for (const string& kw : variant.mKeywords) if (!assemblyOutput.mKeywords.count(kw)) { f = false; break; }
					if (!f) continue;
					/*
					// TODO: disassemble without spirv-tools
					spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_2);
					string assembly;
					if (tools.Disassemble(variant.mModules[i].mSpirvBinary, &assembly)) {
						ofstream out(assemblyOutput.mFilename);
						out << assembly;
						printf("Wrote assembly file: %s\n", assemblyOutput.mFilename.string().c_str());
					}
					*/
				}

		}
	}
	
	return destShader;
}

int main(int argc, char* argv[]) {
	const char* inputFile;
	const char* outputFile;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <shader source> <output file> <optional include paths>\n", argv[0]);
		return EXIT_FAILURE;
	} else {
		inputFile = argv[1];
		outputFile = argv[2];
	}

	set<string> globalIncludes;
	for (int i = 3; i < argc; i++) globalIncludes.emplace(argv[i]);

	CompileShader(inputFile, globalIncludes).Write(outputFile);

	return EXIT_SUCCESS;
}