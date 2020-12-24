#ifdef WITH_DXC
#include <wrl.h>
#include <dxc/dxcapi.h>
#endif

#include <spirv_cross/spirv_cross.hpp>
#include <shaderc/shaderc.hpp>

#include "ShaderCompiler.hpp"

using namespace stm;
using namespace shaderc;

class Includer : public CompileOptions::IncluderInterface {
private:
	set<fs::path> mIncludePaths;
	map<string, string> mSources;

public:
	Includer(const set<fs::path>& paths) : mIncludePaths(paths) {}

	shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type, const char* requesting_source, size_t include_depth) override {
		fs::path fullpath;
		if (type == shaderc_include_type_relative)
			fullpath = fs::absolute(requesting_source).parent_path() / requested_source;
		else
			for (const auto& p : mIncludePaths)
				if (fs::exists(p / requested_source))
					fullpath = p / requested_source;

		if (mSources.count(fullpath.string()) == 0) mSources.emplace(fullpath.string(), ReadFile(fullpath));
		auto it = mSources.find(fullpath.string());

		shaderc_include_result* response = new shaderc_include_result();
		if (it == mSources.end() || it->second.empty()) {
			char* err = new char[1024];
			sprintf(err, "%s: Failed to evaluate %s include", requesting_source, type == shaderc_include_type_relative ? "relative" : "standard");
			response->source_name = "\0";
			response->source_name_length = 0;
			response->content = err;
			response->content_length = strlen(response->content);
			response->user_data = err;
		} else {
			response->source_name = it->first.c_str();
			response->source_name_length = it->first.length();
			response->content = it->second.data();
			response->content_length = it->second.length();
			response->user_data = nullptr;
		}
		return response;
	}
	void ReleaseInclude(shaderc_include_result* data) override {
		if (data->user_data) delete data->user_data;
		mSources.erase(data->source_name);
		delete data;
	}
};

static const unordered_map<spirv_cross::SPIRType::BaseType, vector<vk::Format>> gFormatMap {
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
static const unordered_map<vk::ShaderStageFlagBits, shaderc_shader_kind> gShaderStageMap {
	{ vk::ShaderStageFlagBits::eVertex, shaderc_vertex_shader },
	{ vk::ShaderStageFlagBits::eFragment, shaderc_fragment_shader },
	{ vk::ShaderStageFlagBits::eGeometry, shaderc_geometry_shader },
	{ vk::ShaderStageFlagBits::eTessellationControl, shaderc_tess_control_shader },
	{ vk::ShaderStageFlagBits::eTessellationEvaluation, shaderc_tess_evaluation_shader },
	{ vk::ShaderStageFlagBits::eCompute, shaderc_compute_shader }
};

size_t SizeOf(const spirv_cross::SPIRType& type, const spirv_cross::Compiler& compiler) {
	size_t sz = 0;
	switch (type.basetype) {
	case spirv_cross::SPIRType::Boolean:
	case spirv_cross::SPIRType::SByte:
	case spirv_cross::SPIRType::UByte:
		sz = 1;
		break;
	case spirv_cross::SPIRType::Short:
	case spirv_cross::SPIRType::UShort:
	case spirv_cross::SPIRType::Half:
		sz = 2;
		break;
	case spirv_cross::SPIRType::Int:
	case spirv_cross::SPIRType::UInt:
	case spirv_cross::SPIRType::Float:
		sz = 4;
		break;
	case spirv_cross::SPIRType::Int64:
	case spirv_cross::SPIRType::UInt64:
	case spirv_cross::SPIRType::Double:
		sz = 8;
		break;
	case spirv_cross::SPIRType::Struct:
		sz = (uint32_t)compiler.get_declared_struct_size(type);
		break;
	case spirv_cross::SPIRType::Unknown:
	case spirv_cross::SPIRType::Void:
	case spirv_cross::SPIRType::AtomicCounter:
	case spirv_cross::SPIRType::Image:
	case spirv_cross::SPIRType::SampledImage:
	case spirv_cross::SPIRType::Sampler:
	case spirv_cross::SPIRType::AccelerationStructure:
		throw logic_error("Unknown type id " + to_string((size_t)type.basetype));
	}

	sz *= type.columns * type.vecsize;
	for (uint32_t dim : type.array) sz *= dim;
	return sz;
}

void ShaderCompiler::DirectiveCompile(SpirvModuleGroup& modules, word_iterator arg, const word_iterator& argEnd) {
	static const map<string, vk::ShaderStageFlagBits> stageMap {
    { "vertex", vk::ShaderStageFlagBits::eVertex },
    { "tess_control", vk::ShaderStageFlagBits::eTessellationControl },
    { "tess_evaluation", vk::ShaderStageFlagBits::eTessellationEvaluation },
    { "geometry", vk::ShaderStageFlagBits::eGeometry },
    { "fragment", vk::ShaderStageFlagBits::eFragment }, { "pixel", vk::ShaderStageFlagBits::eFragment },
    { "compute", vk::ShaderStageFlagBits::eCompute }, { "kernel", vk::ShaderStageFlagBits::eCompute },
    { "raygen_khr", vk::ShaderStageFlagBits::eRaygenKHR },
    { "any_hit_khr", vk::ShaderStageFlagBits::eAnyHitKHR },
    { "closest_hit_khr", vk::ShaderStageFlagBits::eClosestHitKHR },
    { "miss_khr", vk::ShaderStageFlagBits::eMissKHR },
    { "intersection_khr", vk::ShaderStageFlagBits::eIntersectionKHR },
    { "callable_khr", vk::ShaderStageFlagBits::eCallableKHR },
    { "task_nv", vk::ShaderStageFlagBits::eTaskNV },
    { "mesh_nv", vk::ShaderStageFlagBits::eMeshNV },
    { "any_hit_nv", vk::ShaderStageFlagBits::eAnyHitNV },
    { "callable_nv", vk::ShaderStageFlagBits::eCallableNV },
    { "closest_hit_nv", vk::ShaderStageFlagBits::eClosestHitNV },
    { "intersection_nv", vk::ShaderStageFlagBits::eIntersectionNV },
    { "miss_nv", vk::ShaderStageFlagBits::eMissNV },
    { "raygen_nv", vk::ShaderStageFlagBits::eRaygenNV }
	};
	while (++arg != argEnd) {
		SpirvModule m;
		if (!stageMap.count(*arg)) throw logic_error("unknown shader stage: " + *arg);
		m.mStage = stageMap.at(*arg);
		if (++arg == argEnd) throw logic_error("expected an entry point");
		m.mEntryPoint = *arg;
		modules.mModules.push_back(m);
	}
}

SpirvModuleGroup ShaderCompiler::ParseCompilerDirectives(const fs::path& filename, const string& source, uint32_t includeDepth) {
	uint32_t lineNumber = 0;

	SpirvModuleGroup modules;

	string line;
	istringstream srcstream(source);
	while (getline(srcstream, line)) {
		lineNumber++;
		istringstream linestream(line);
		vector<string> words{ istream_iterator<string>(linestream), istream_iterator<string>{} };

		if (words.size() < 2 || words[0][0] != '#') continue;

		if (words[0] == "#pragma") {
			string directive = words[1];
			if (!gCompilerDirectives.count(directive))
				throw logic_error(filename.string() + "(" + to_string(lineNumber) + "): Error: Unknown directive '" + words[1] + "'");
			try {
				(this->*gCompilerDirectives.at(directive))(modules, words.begin() + 1, words.end());
			} catch (logic_error e) {
				throw logic_error(filename.string() + "(" + to_string(lineNumber) + "): " + directive + " Error: " + e.what());
			}
		} else if (words[0] == "#include") {
			if (words[1].length() <= 2) continue;
			
			shaderc_include_type includeType = shaderc_include_type_relative;
			string includePath = words[1].substr(1, words[1].length() - 2);
			if (words[1][0] == '<') includeType = shaderc_include_type_standard;
			
			Includer includer(mIncludePaths);
			shaderc_include_result* includeResult = includer.GetInclude(includePath.c_str(), includeType, filename.string().c_str(), includeDepth);
			if (includeResult->content_length)
				ParseCompilerDirectives(includeResult->source_name, includeResult->content, includeDepth + 1);
			includer.ReleaseInclude(includeResult);
		}
	}

	return modules;
}
vector<uint32_t> ShaderCompiler::CompileSpirv(const fs::path& filename, const string& entryPoint, vk::ShaderStageFlagBits stage, const set<string>& defines) {
	bool hlsl = filename.extension() == ".hlsl";
	#if WITH_DXC
	if (hlsl) {
		using namespace Microsoft::WRL;
		ComPtr<IDxcLibrary> library;
		ComPtr<IDxcCompiler> compiler;
		DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));
		DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

		ComPtr<IDxcBlobEncoding> sourceBlob;
		uint32_t codePage = CP_UTF8;
		library->CreateBlobFromFile(filename.c_str(), &codePage, &sourceBlob);
		ComPtr<IDxcIncludeHandler> includeHandler;
		library->CreateIncludeHandler(&includeHandler);

		vector<const wchar_t*> args {
			L"-spirv",
			L"-fspv-target-env=vulkan1.2",
			L"-fspv-reflect",
			L"-Zpc", // column-major matrix packing
			L"-Zi",  // debug information
		};

		vector<wstring> wincludes;
		for (const auto& p : mIncludePaths) wincludes.push_back(L"-I " + p.wstring());
		for (const auto& inc : wincludes) args.push_back(inc.c_str());

		wstring entryP = s2ws(entryPoint);

		wstring targetProfile;
		switch (stage) {
			case vk::ShaderStageFlagBits::eCompute:
				targetProfile = L"cs_6_6";
				break;
			case vk::ShaderStageFlagBits::eVertex:
				targetProfile = L"vs_6_6";
				break;
			case vk::ShaderStageFlagBits::eFragment:
				targetProfile = L"ps_6_6";
				break;
		}
		
		vector<wstring> wdefines;
		wdefines.reserve(defines.size()*2);
		vector<DxcDefine> dxcDefines;
		for (const auto& s : defines) {
			auto eq = s.find('=');
			wdefines.push_back(s2ws(s.substr(0,eq)));
			if (eq == string::npos) wdefines.push_back(wstring());
			else 										wdefines.push_back(s2ws(s.substr(eq+1)));
			dxcDefines.push_back({ (wdefines.end()-2)->c_str(), (wdefines.end()-1)->c_str() });
		}

		ComPtr<IDxcOperationResult> result;
		compiler->Compile(sourceBlob.Get(), filename.c_str(), entryP.c_str(), targetProfile.c_str(), args.data(), (uint32_t)args.size(), dxcDefines.data(), (uint32_t)dxcDefines.size(), includeHandler.Get(), &result);

		HRESULT status;
		result->GetStatus(&status);
		if (status != S_OK) {
			ComPtr<IDxcBlobEncoding> msg;
			result->GetErrorBuffer(msg.GetAddressOf());
			throw logic_error((char*)msg->GetBufferPointer());
		}

		ComPtr<IDxcBlob> spirvBlob;
		DXC_OUT_KIND outputKind = (DXC_OUT_KIND)result->GetResult(&spirvBlob);
		vector<uint32_t> r(spirvBlob->GetBufferSize()/sizeof(uint32_t));
		memcpy(r.data(), spirvBlob->GetBufferPointer(), spirvBlob->GetBufferSize());
		return r;
	}
	#endif

	string source = ReadFile(filename);

	// Compile with shaderc
	CompileOptions options;
	if (hlsl) {
		options.SetHlslFunctionality1(true);
		options.SetHlslIoMapping(true);
		options.SetSourceLanguage(shaderc_source_language_hlsl);
	} else
		options.SetSourceLanguage(shaderc_source_language_glsl);
	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
	options.SetTargetSpirv(shaderc_spirv_version_1_3);
	options.SetOptimizationLevel(shaderc_optimization_level_performance);
	options.SetAutoMapLocations(true);
	options.SetGenerateDebugInfo();
	options.SetIncluder(make_unique<Includer>(mIncludePaths));
	for (auto& m : defines) options.AddMacroDefinition(m);
	Compiler compiler;
	SpvCompilationResult result = compiler.CompileGlslToSpv(source, gShaderStageMap.at(stage), filename.string().c_str(), entryPoint.c_str(), options);
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
	vector<uint32_t> spirv;
	for (const uint32_t& c : result) spirv.push_back(c);
	if (result.GetCompilationStatus() != shaderc_compilation_status_success)
		throw logic_error(result.GetErrorMessage());
	return spirv;
}
void ShaderCompiler::SpirvReflection(SpirvModule& shaderModule) {
	spirv_cross::Compiler compiler(shaderModule.mSpirv.data(), shaderModule.mSpirv.size());
	spirv_cross::ShaderResources resources = compiler.get_shader_resources();

	auto RegisterDescriptorResource = [&](const spirv_cross::Resource& resource, vk::DescriptorType type) {
		auto& spirType = compiler.get_type(resource.type_id);

		auto& binding = shaderModule.mDescriptorBindings[resource.name];
		binding.mSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
		binding.mBinding.stageFlags |= shaderModule.mStage;
		binding.mBinding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
		binding.mBinding.descriptorCount = spirType.array.empty() ? 1 : spirType.array[0];
		binding.mBinding.descriptorType = type;
	};
	
	for (const auto& r : resources.separate_images) 	RegisterDescriptorResource(r, compiler.get_type(r.type_id).image.dim == spv::DimBuffer ? vk::DescriptorType::eUniformTexelBuffer : vk::DescriptorType::eSampledImage);
	for (const auto& r : resources.storage_images) 		RegisterDescriptorResource(r, compiler.get_type(r.type_id).image.dim == spv::DimBuffer ? vk::DescriptorType::eStorageTexelBuffer : vk::DescriptorType::eStorageImage);
	for (const auto& r : resources.sampled_images) 		RegisterDescriptorResource(r, vk::DescriptorType::eCombinedImageSampler);
	for (const auto& r : resources.storage_buffers) 	RegisterDescriptorResource(r, vk::DescriptorType::eStorageBuffer);
	for (const auto& r : resources.separate_samplers) RegisterDescriptorResource(r, vk::DescriptorType::eSampler);
	for (const auto& r : resources.uniform_buffers) 	RegisterDescriptorResource(r, vk::DescriptorType::eUniformBuffer);
	for (const auto& r : resources.subpass_inputs) 		RegisterDescriptorResource(r, vk::DescriptorType::eInputAttachment);
	for (const auto& r : resources.acceleration_structures) 		RegisterDescriptorResource(r, vk::DescriptorType::eAccelerationStructureKHR);
	
	uint32_t other_idx = 0;
	for (const auto& r : resources.stage_inputs) {
		auto& var = shaderModule.mStageInputs[r.name];
		auto& type = compiler.get_type(r.base_type_id);

		var.mLocation = compiler.get_decoration(r.id, spv::DecorationLocation);
		var.mFormat = gFormatMap.at(type.basetype)[type.vecsize-1];

		string semantic = compiler.get_decoration_string(r.id, spv::DecorationHlslSemanticGOOGLE);
		size_t l = semantic.find_first_of("0123456789");
		if (l != string::npos)
			var.mTypeIndex = atoi(semantic.c_str() + l);
		else {
			var.mTypeIndex = 0;
			l = semantic.length();
		}
		if (l > 0 && semantic[l-1] == '_') l--;
		transform(semantic.begin(), semantic.begin() + l, semantic.begin(), tolower);
		if (gAttributeMap.count(semantic))
			var.mType = gAttributeMap.at(semantic);
		else
			var.mType = VertexAttributeType::eOther;
	}
	for (const auto& r : resources.stage_outputs) {
		auto& var = shaderModule.mStageOutputs[r.name];
		auto& type = compiler.get_type(r.base_type_id);

		var.mLocation = compiler.get_decoration(r.id, spv::DecorationLocation);
		var.mFormat = gFormatMap.at(type.basetype)[type.vecsize-1];

		string semantic = compiler.get_decoration_string(r.id, spv::DecorationHlslSemanticGOOGLE);
		size_t l = semantic.find_first_of("0123456789");
		if (l != string::npos)
			var.mTypeIndex = atoi(semantic.c_str() + l);
		else {
			var.mTypeIndex = 0;
			l = semantic.length();
		}
		if (l > 0 && semantic[l-1] == '_') l--;
		transform(semantic.begin(), semantic.begin() + l, semantic.begin(), tolower);
		if (gAttributeMap.count(semantic))
			var.mType = gAttributeMap.at(semantic);
		else
			var.mType = VertexAttributeType::eOther;
	}
	for (const auto& e : compiler.get_entry_points_and_stages()) {
		auto& ep = compiler.get_entry_point(e.name, e.execution_model);
		shaderModule.mWorkgroupSize = { ep.workgroup_size.x, ep.workgroup_size.y, ep.workgroup_size.z };
	}
	for (const auto& r : resources.push_constant_buffers) {
		spirv_cross::SPIRType type = compiler.get_type(r.base_type_id);
		for (uint32_t i = 0; i < type.member_types.size(); i++) {
			auto& pushConstant = shaderModule.mPushConstants[compiler.get_member_name(r.base_type_id, i)];
			pushConstant.stageFlags = shaderModule.mStage;
			pushConstant.offset = compiler.type_struct_member_offset(type, i);
			pushConstant.size = (uint32_t)SizeOf(compiler.get_type(type.member_types[i]), compiler);
		}
	}
	for (const auto& c : compiler.get_specialization_constants()) {
		auto specConstant = shaderModule.mSpecializationMap[compiler.get_name(c.id)];
		specConstant.constantID = c.constant_id;
		specConstant.size = SizeOf(compiler.get_type(compiler.get_constant(c.id).constant_type), compiler);
	}
}

int main(int argc, char* argv[]) {
	if (argc < 2) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Usage: %s <shader source> <output file> <optional include paths>\n", argv[0]);
		return EXIT_FAILURE;
	}

	const char* inputFile = argv[1];
	const char* outputFile = argv[2];
	set<fs::path> globalIncludes;
	for (int i = 3; i < argc; i++) globalIncludes.emplace(argv[i]);

	//try {
		ShaderCompiler compiler(globalIncludes);
		SpirvModuleGroup result = compiler(inputFile);
		binary_stream stream(outputFile);
		stream << result;
	//} catch (logic_error e) {
	//	fprintf_color(ConsoleColorBits::eRed, stderr, "Error: %s\n\tWhile compiling %s\n", e.what(), inputFile);
	//	throw;
	//}

	return EXIT_SUCCESS;
}