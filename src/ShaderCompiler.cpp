#ifdef WITH_DXC
#include <wrl.h>
#include <dxc/dxcapi.h>
#endif
#include <spirv_cross/spirv_cross_c.h>
#include <shaderc/shaderc.hpp>

#include "ShaderCompiler.hpp"

using namespace stm;
using namespace shaderc;

class Includer : public CompileOptions::IncluderInterface {
private:
	set<fs::path> mIncludePaths;
	unordered_map<string, string> mSources;

public:
	template<ranges::range R> requires(convertible_to<ranges::range_value_t<R>, fs::path>)
	Includer(const R& includePaths) : mIncludePaths(set<fs::path>(includePaths.begin(), includePaths.end())) {}

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

static const unordered_map<vk::ShaderStageFlagBits, shaderc_shader_kind, hash<vk::ShaderStageFlags>> gShaderStageMap {
	{ vk::ShaderStageFlagBits::eVertex, shaderc_vertex_shader },
	{ vk::ShaderStageFlagBits::eFragment, shaderc_fragment_shader },
	{ vk::ShaderStageFlagBits::eGeometry, shaderc_geometry_shader },
	{ vk::ShaderStageFlagBits::eTessellationControl, shaderc_tess_control_shader },
	{ vk::ShaderStageFlagBits::eTessellationEvaluation, shaderc_tess_evaluation_shader },
	{ vk::ShaderStageFlagBits::eCompute, shaderc_compute_shader }
};

static const unordered_map<spvc_basetype, vector<vk::Format>, hash<size_t>> gFormatMap {
	{ spvc_basetype::SPVC_BASETYPE_INT8, 		{ vk::Format::eR8Snorm, 	vk::Format::eR8G8Snorm, 	 vk::Format::eR8G8B8Snorm, 			vk::Format::eR8G8B8A8Snorm } },
	{ spvc_basetype::SPVC_BASETYPE_UINT8, 	{ vk::Format::eR8Unorm, 	vk::Format::eR8G8Unorm, 	 vk::Format::eR8G8B8Unorm, 			vk::Format::eR8G8B8A8Unorm } },
	{ spvc_basetype::SPVC_BASETYPE_INT16, 	{ vk::Format::eR16Sint, 	vk::Format::eR16G16Sint, 	 vk::Format::eR16G16B16Sint, 		vk::Format::eR16G16B16A16Sint } },
	{ spvc_basetype::SPVC_BASETYPE_UINT16, 	{ vk::Format::eR16Uint, 	vk::Format::eR16G16Uint, 	 vk::Format::eR16G16B16Uint, 		vk::Format::eR16G16B16A16Uint } },
	{ spvc_basetype::SPVC_BASETYPE_INT32, 	{ vk::Format::eR32Sint, 	vk::Format::eR32G32Sint, 	 vk::Format::eR32G32B32Sint, 		vk::Format::eR32G32B32A32Sint } },
	{ spvc_basetype::SPVC_BASETYPE_UINT32, 	{ vk::Format::eR32Uint, 	vk::Format::eR32G32Uint, 	 vk::Format::eR32G32B32Uint, 		vk::Format::eR32G32B32A32Uint } },
	{ spvc_basetype::SPVC_BASETYPE_INT64, 	{ vk::Format::eR64Sint, 	vk::Format::eR64G64Sint, 	 vk::Format::eR64G64B64Sint, 		vk::Format::eR64G64B64A64Sint } },
	{ spvc_basetype::SPVC_BASETYPE_UINT64, 	{ vk::Format::eR32Uint, 	vk::Format::eR32G32Uint, 	 vk::Format::eR32G32B32Uint, 		vk::Format::eR32G32B32A32Uint } },
	{ spvc_basetype::SPVC_BASETYPE_FP16, 		{ vk::Format::eR16Sfloat, vk::Format::eR16G16Sfloat, vk::Format::eR16G16B16Sfloat,  vk::Format::eR16G16B16A16Sfloat } },
	{ spvc_basetype::SPVC_BASETYPE_FP32, 		{ vk::Format::eR32Sfloat, vk::Format::eR32G32Sfloat, vk::Format::eR32G32B32Sfloat,  vk::Format::eR32G32B32A32Sfloat } },
	{ spvc_basetype::SPVC_BASETYPE_FP64, 		{ vk::Format::eR64Sfloat, vk::Format::eR64G64Sfloat, vk::Format::eR64G64B64Sfloat,  vk::Format::eR64G64B64A64Sfloat } }
};
size_t SizeOf(spvc_type type, spvc_compiler compiler) {
	size_t sz = 0;
	switch (spvc_type_get_basetype(type)) {
	case spvc_basetype::SPVC_BASETYPE_BOOLEAN:
	case spvc_basetype::SPVC_BASETYPE_INT8:
	case spvc_basetype::SPVC_BASETYPE_UINT8:
		sz = 1;
		break;
	case spvc_basetype::SPVC_BASETYPE_INT16:
	case spvc_basetype::SPVC_BASETYPE_UINT16:
	case spvc_basetype::SPVC_BASETYPE_FP16:
		sz = 2;
		break;
	case spvc_basetype::SPVC_BASETYPE_INT32:
	case spvc_basetype::SPVC_BASETYPE_UINT32:
	case spvc_basetype::SPVC_BASETYPE_FP32:
		sz = 4;
		break;
	case spvc_basetype::SPVC_BASETYPE_INT64:
	case spvc_basetype::SPVC_BASETYPE_UINT64:
	case spvc_basetype::SPVC_BASETYPE_FP64:
		sz = 8;
		break;
	case spvc_basetype::SPVC_BASETYPE_STRUCT:
		spvc_compiler_get_declared_struct_size(compiler, type, &sz);
		break;
		throw logic_error("Unknown type id " + to_string((size_t)type));
	}
	sz *= spvc_type_get_columns(type) * spvc_type_get_vector_size(type);
	for (uint32_t i = 0; i < spvc_type_get_num_array_dimensions(type); i++)
		sz *= spvc_type_get_array_dimension(type, i);
	return sz;
}

void ShaderCompiler::DirectiveCompile(unordered_map<string, SpirvModule>& modules, word_iterator arg, const word_iterator& argEnd) {
	static const unordered_map<string, vk::ShaderStageFlagBits> stageMap {
    { "vertex", vk::ShaderStageFlagBits::eVertex },
    { "tess_control", vk::ShaderStageFlagBits::eTessellationControl }, { "hull", vk::ShaderStageFlagBits::eTessellationControl }, // TODO: idk if 'tess_control' == 'hull'
    { "tess_evaluation", vk::ShaderStageFlagBits::eTessellationEvaluation }, { "domain", vk::ShaderStageFlagBits::eTessellationEvaluation }, // TODO: idk if 'tess_evaluation' == 'domain'
    { "geometry", vk::ShaderStageFlagBits::eGeometry },
    { "fragment", vk::ShaderStageFlagBits::eFragment }, { "pixel", vk::ShaderStageFlagBits::eFragment },
    { "compute", vk::ShaderStageFlagBits::eCompute }, { "kernel", vk::ShaderStageFlagBits::eCompute },

    { "raygen_khr", vk::ShaderStageFlagBits::eRaygenKHR }, { "raygeneration", vk::ShaderStageFlagBits::eRaygenKHR },
    { "any_hit_khr", vk::ShaderStageFlagBits::eAnyHitKHR }, { "anyhit", vk::ShaderStageFlagBits::eAnyHitKHR },
    { "closest_hit_khr", vk::ShaderStageFlagBits::eClosestHitKHR }, { "closesthit", vk::ShaderStageFlagBits::eClosestHitKHR },
    { "miss_khr", vk::ShaderStageFlagBits::eMissKHR }, { "miss", vk::ShaderStageFlagBits::eMissKHR },
    { "intersection_khr", vk::ShaderStageFlagBits::eIntersectionKHR }, { "intersection", vk::ShaderStageFlagBits::eIntersectionKHR },
    { "callable_khr", vk::ShaderStageFlagBits::eCallableKHR }, { "callable", vk::ShaderStageFlagBits::eCallableKHR },

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
		modules.emplace(m.mEntryPoint, m);
	}
}

unordered_map<string, SpirvModule> ShaderCompiler::ParseCompilerDirectives(const fs::path& filename, const string& source, uint32_t includeDepth) {
	uint32_t lineNumber = 0;

	unordered_map<string, SpirvModule> modules;

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
vector<uint32_t> ShaderCompiler::CompileSpirv(const fs::path& filename, const string& entryPoint, vk::ShaderStageFlagBits stage, const unordered_set<string>& defines) {
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
	spvc_context context;
	spvc_context_create(&context);
	spvc_context_set_error_callback(context, [](void*, const char* msg) { fprintf_color(ConsoleColorBits::eRed, stderr, msg); }, nullptr);

	spvc_parsed_ir ir;
	spvc_context_parse_spirv(context, shaderModule.mSpirv.data(), shaderModule.mSpirv.size(), &ir);

	spvc_resources resources;
	spvc_compiler compiler;
	spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler);
	spvc_variable_id sid;
	spvc_compiler_build_dummy_sampler_for_combined_images(compiler, &sid);
	spvc_compiler_create_shader_resources(compiler, &resources);
	
	size_t count;
	const spvc_specialization_constant* constants;
	spvc_compiler_get_specialization_constants(compiler, &constants, &count);
	for (const auto& c : ranges::subrange(constants, constants+count)) {
		auto specConstant = shaderModule.mSpecializationMap[spvc_compiler_get_name(compiler, c.id)];
		specConstant.constantID = c.constant_id;
		specConstant.size = SizeOf(spvc_compiler_get_type_handle(compiler, spvc_constant_get_type(spvc_compiler_get_constant_handle(compiler, c.id))), compiler);
	}

	const spvc_reflected_resource* list;
	spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_PUSH_CONSTANT, &list, &count);
	for (const auto& c : ranges::subrange(list, list+count)) {
		spvc_type spirType = spvc_compiler_get_type_handle(compiler, c.base_type_id);
		for (uint32_t i = 0; i < spvc_type_get_num_member_types(spirType); i++) {
			auto& pushConstant = shaderModule.mPushConstants[spvc_compiler_get_member_name(compiler, c.base_type_id, i)];
			pushConstant.stageFlags = shaderModule.mStage;
			spvc_compiler_type_struct_member_offset(compiler, spirType, i, &pushConstant.offset);
			size_t c = 0;
			spvc_compiler_get_declared_struct_member_size(compiler, spirType, i, &c);
			if (!c) c = SizeOf(spvc_compiler_get_type_handle(compiler, spvc_type_get_member_type(spirType, i)), compiler);
			pushConstant.size = (uint32_t)c;
		}
	}

	static const unordered_map<spvc_resource_type, vk::DescriptorType> typeMap {
		{ SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, vk::DescriptorType::eSampledImage },
		{ SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, vk::DescriptorType::eSampledImage },
		{ SPVC_RESOURCE_TYPE_STORAGE_IMAGE, vk::DescriptorType::eStorageImage },
		{ SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, vk::DescriptorType::eSampler },
		{ SPVC_RESOURCE_TYPE_SUBPASS_INPUT, vk::DescriptorType::eInputAttachment },
		{ SPVC_RESOURCE_TYPE_STORAGE_BUFFER, vk::DescriptorType::eStorageBuffer },
		{ SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, vk::DescriptorType::eUniformBuffer },
		{ SPVC_RESOURCE_TYPE_ACCELERATION_STRUCTURE, vk::DescriptorType::eAccelerationStructureKHR },
	};
	for (const auto&[spvtype,descriptorType] : typeMap) {
		spvc_resources_get_resource_list_for_type(resources, spvtype, &list, &count);
		for (const auto& r : ranges::subrange(list, list+count)){
			spvc_type spirType = spvc_compiler_get_type_handle(compiler, r.type_id);
			DescriptorBinding& binding = shaderModule.mDescriptorBindings[r.name];
			binding.mDescriptorType = descriptorType;
			binding.mSet = spvc_compiler_get_decoration(compiler, r.id, SpvDecorationDescriptorSet);
			binding.mBinding = spvc_compiler_get_decoration(compiler, r.id, SpvDecorationBinding);
			binding.mStageFlags |= shaderModule.mStage;
			binding.mDescriptorCount = 1;
			uint32_t n = spvc_type_get_num_array_dimensions(spirType);
			for (uint32_t i = 0; i < n; i++)
				binding.mDescriptorCount *= spvc_type_get_array_dimension(spirType, i);
		};
	}


	auto ReflectStageVariables = [&](unordered_map<string, RasterStageVariable>& vars, const spvc_reflected_resource& r) {
		spvc_type spirType = spvc_compiler_get_type_handle(compiler, r.type_id);
		auto& var = vars[r.name];
		var.mLocation = spvc_compiler_get_decoration(compiler, r.id, SpvDecorationLocation);
		var.mFormat = gFormatMap.at(spvc_type_get_basetype(spirType))[spvc_type_get_vector_size(spirType)-1];
		var.mAttributeId = stovertexattribute(spvc_compiler_get_decoration_string(compiler, r.id, SpvDecorationHlslSemanticGOOGLE));
		// ensure unique typeindex
		while (ranges::count(vars | views::values, var.mAttributeId, &RasterStageVariable::mAttributeId) > 1)
			var.mAttributeId.mTypeIndex++;
	};
	
	spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STAGE_INPUT, &list, &count);
	ranges::for_each_n(list, count, bind_front(ReflectStageVariables, ref(shaderModule.mStageInputs)));
	spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STAGE_OUTPUT, &list, &count);
	ranges::for_each_n(list, count, bind_front(ReflectStageVariables, ref(shaderModule.mStageOutputs)));
	
	shaderModule.mWorkgroupSize = vk::Extent3D(
		spvc_compiler_get_execution_mode_argument_by_index(compiler, SpvExecutionMode::SpvExecutionModeLocalSize, 0), 
		spvc_compiler_get_execution_mode_argument_by_index(compiler, SpvExecutionMode::SpvExecutionModeLocalSize, 1), 
		spvc_compiler_get_execution_mode_argument_by_index(compiler, SpvExecutionMode::SpvExecutionModeLocalSize, 2));
	spvc_context_destroy(context);
}

int main(int argc, char* argv[]) {
	if (argc < 2) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Usage: %s <shader source> <output file> <optional include paths>\n", argv[0]);
		return EXIT_FAILURE;
	}

	vector<fs::path> inputs;
	fs::path output = "";
	set<fs::path> includes;

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' || argv[i][0] == '/') {
			switch (argv[i][1]) {
				case 'o':
					if (++i < argc) output = argv[i];
					continue;
				case 'I':
					if (++i < argc) includes.emplace(argv[i]);
					continue;
			}
		} else
			inputs.push_back(argv[i]);
	}

	unordered_map<string, SpirvModule> results;
	ShaderCompiler compiler(includes);
	for (const auto& i : inputs)
		try {
			results.merge(compiler(i));
		} catch (exception e) {
			fprintf_color(ConsoleColorBits::eRed, stderr, "Error: %s\n\tWhile compiling %s\n", e.what(), i.string().c_str());
			throw;
		}

	if (!output.empty()) {
		try {
			byte_stream<ofstream>(output, ios::binary) << results;
			printf(output.string().c_str());
		} catch (exception e) {
			fprintf_color(ConsoleColorBits::eRed, stderr, "Error: %s\n\tWhile writing %s\n", e.what(), output.string().c_str());
			throw;
		}
	}

	return EXIT_SUCCESS;
}