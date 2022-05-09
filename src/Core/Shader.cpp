#include "Shader.hpp"

#include <json.hpp>
#include <slang.h>

using namespace stm;

uint32_t spirv_type_size(const nlohmann::json& j, const string& tname) {
	static const unordered_map<string, size_t> gSpirvTypeSizeMap {
		{ "bool",   sizeof(int32_t) },
		{ "int",    sizeof(int32_t) },
		{ "uint",   sizeof(uint32_t) },
		{ "float",  sizeof(float) },
		{ "double", sizeof(double) },
		{ "ivec2",  sizeof(int32_t)*2 },
		{ "ivec3",  sizeof(int32_t)*3 },
		{ "ivec4",  sizeof(int32_t)*4 },
		{ "uvec2",  sizeof(uint32_t)*2 },
		{ "uvec3",  sizeof(uint32_t)*3 },
		{ "uvec4",  sizeof(uint32_t)*4 },
		{ "vec2",   sizeof(float)*2 },
		{ "vec3",   sizeof(float)*3 },
		{ "vec4",   sizeof(float)*4 },
		{ "dvec2",  sizeof(double)*2 },
		{ "dvec3",  sizeof(double)*3 },
		{ "dvec4",  sizeof(double)*4 },
		{ "mat2",  sizeof(float)*2*2 },
		{ "mat3",  sizeof(float)*3*3 },
		{ "mat3x4",  sizeof(float)*3*4 },
		{ "mat4x3",  sizeof(float)*4*3 },
		{ "mat4",  sizeof(float)*4*4 },
	};
	if (auto it = gSpirvTypeSizeMap.find(tname); it != gSpirvTypeSizeMap.end())
		return (uint32_t)it->second;
	else {
		uint32_t mo = 0;
		string mt;
		for (const auto& a : j["types"][tname]["members"])
			if (a["offset"] >= mo) {
				mo = a["offset"];
				mt = a["type"];
			}
		return mo + spirv_type_size(j, mt);
	}
}
auto spirv_array_size(const nlohmann::json& j, const nlohmann::json& v) {
	vector<variant<uint32_t,string>> dst;
	if (!v.contains("array")) return dst;
	for (uint32_t i = 0; i < v["array"].size(); i++) {
		if (v["array_size_is_literal"][i])
			dst.push_back(v["array"][i].get<uint32_t>());
		else
			for (uint32_t k = 0; k < j["specialization_constants"].size(); k++)
				if (j["specialization_constants"][k]["variable_id"] == v["array"][i]) {
					dst.push_back(j["specialization_constants"][k]["name"].get<string>());
					break;
				}
	}
	return dst;
}
pair<VertexArrayObject::AttributeType, uint32_t> attribute_type(string name) {
	static const unordered_map<string, VertexArrayObject::AttributeType> gAttributeTypeMap {
		{ "position", VertexArrayObject::AttributeType::ePosition },
		{ "normal", VertexArrayObject::AttributeType::eNormal },
		{ "tangent", VertexArrayObject::AttributeType::eTangent },
		{ "binormal", VertexArrayObject::AttributeType::eBinormal },
		{ "joints", VertexArrayObject::AttributeType::eBlendIndex },
		{ "blendindices", VertexArrayObject::AttributeType::eBlendIndex },
		{ "weights", VertexArrayObject::AttributeType::eBlendWeight },
		{ "blendweight", VertexArrayObject::AttributeType::eBlendWeight },
		{ "color", VertexArrayObject::AttributeType::eColor },
		{ "sv_target", VertexArrayObject::AttributeType::eColor },
		{ "pointsize", VertexArrayObject::AttributeType::ePointSize },
		{ "texcoord", VertexArrayObject::AttributeType::eTexcoord },
	};
	for (char& c : name) c = tolower(c);
	for (const auto& [id,attribType] : gAttributeTypeMap)
		if (size_t pos = name.find(id); pos != string::npos)
			if (pos+id.size() < name.length() && isdigit(name[pos+id.size()]))
				return make_pair(attribType,  stoi(name.substr(pos+id.size())));
			else
				return make_pair(attribType,  0u);
	throw invalid_argument("Failed to parse attribute type");
}

vector<uint32_t> Shader::slang_compile(const unordered_map<string, variant<uint32_t,string>>& defines, bool reflect) {
	if (reflect) {
		SlangSession* session = spCreateSession();
		SlangCompileRequest* request = spCreateCompileRequest(session);

		vector<const char*> args;
		for (const string& arg : mCompileArgs) args.emplace_back(arg.c_str());
		if (SLANG_FAILED(spProcessCommandLineArguments(request, args.data(), args.size())))
			cout << "Failed to process compile arguments" << endl;

		int targetIndex = spAddCodeGenTarget(request, SLANG_SPIRV);
		spAddPreprocessorDefine(request, "__SLANG__", "");
		spAddPreprocessorDefine(request, "__HLSL__", "");
		vector<string> uintDefs;
		for (const auto&[n,d] : defines)
			if (d.index() == 0)
				spAddPreprocessorDefine(request, n.c_str(), uintDefs.emplace_back(to_string(std::get<uint32_t>(d))).c_str());
			else
				spAddPreprocessorDefine(request, n.c_str(), std::get<string>(d).c_str());
		spAddSearchPath(request, "../../src/Shaders");
		spAddSearchPath(request, "../../src/extern");
		int translationUnitIndex = spAddTranslationUnit(request, SLANG_SOURCE_LANGUAGE_SLANG, "");
		spAddTranslationUnitSourceFile(request, translationUnitIndex, mShaderFile.string().c_str());
		int entryPointIndex = spAddEntryPoint(request, translationUnitIndex, mEntryPoint.c_str(), SLANG_STAGE_NONE);
		spSetTargetProfile(request, targetIndex, spFindProfile(session, "sm_6_6"));
		spSetTargetFloatingPointMode(request, targetIndex, SLANG_FLOATING_POINT_MODE_FAST);

		SlangResult r = spCompile(request);
		cout << spGetDiagnosticOutput(request);
		if (SLANG_FAILED(r)) throw runtime_error(spGetDiagnosticOutput(request));

		slang::ShaderReflection* shaderReflection = slang::ShaderReflection::get(request);

		static const unordered_map<SlangTypeKind, const char*> type_kind_name_map = {
			{ SLANG_TYPE_KIND_NONE, "None" },
			{ SLANG_TYPE_KIND_STRUCT, "Struct" },
			{ SLANG_TYPE_KIND_ARRAY, "Array" },
			{ SLANG_TYPE_KIND_MATRIX, "Matrix" },
			{ SLANG_TYPE_KIND_VECTOR, "Vector" },
			{ SLANG_TYPE_KIND_SCALAR, "Scalar" },
			{ SLANG_TYPE_KIND_CONSTANT_BUFFER, "ConstantBuffer" },
			{ SLANG_TYPE_KIND_RESOURCE, "Resource" },
			{ SLANG_TYPE_KIND_SAMPLER_STATE, "SamplerState" },
			{ SLANG_TYPE_KIND_TEXTURE_BUFFER, "TextureBuffer" },
			{ SLANG_TYPE_KIND_SHADER_STORAGE_BUFFER, "ShaderStorageBuffer" },
			{ SLANG_TYPE_KIND_PARAMETER_BLOCK, "ParameterBlock" },
			{ SLANG_TYPE_KIND_GENERIC_TYPE_PARAMETER, "GenericTypeParameter" },
			{ SLANG_TYPE_KIND_INTERFACE, "Interface" },
			{ SLANG_TYPE_KIND_OUTPUT_STREAM, "OutputStream" },
			{ SLANG_TYPE_KIND_SPECIALIZED, "Specialized" },
			{ SLANG_TYPE_KIND_FEEDBACK, "Feedback" }
		};
		static const unordered_map<SlangBindingType, const char*> binding_type_name_map = {
			{ SLANG_BINDING_TYPE_UNKNOWN, "Unknown" },
			{ SLANG_BINDING_TYPE_SAMPLER, "Sampler" },
			{ SLANG_BINDING_TYPE_TEXTURE, "Texture" },
			{ SLANG_BINDING_TYPE_CONSTANT_BUFFER, "ConstantBuffer" },
			{ SLANG_BINDING_TYPE_PARAMETER_BLOCK, "ParameterBlock" },
			{ SLANG_BINDING_TYPE_TYPED_BUFFER, "TypedBuffer" },
			{ SLANG_BINDING_TYPE_RAW_BUFFER, "RawBuffer" },
			{ SLANG_BINDING_TYPE_COMBINED_TEXTURE_SAMPLER, "CombinedTextureSampler" },
			{ SLANG_BINDING_TYPE_INPUT_RENDER_TARGET, "InputRenderTarget" },
			{ SLANG_BINDING_TYPE_INLINE_UNIFORM_DATA, "InlineUniformData" },
			{ SLANG_BINDING_TYPE_RAY_TRACTING_ACCELERATION_STRUCTURE, "RayTracingAccelerationStructure" },
			{ SLANG_BINDING_TYPE_VARYING_INPUT, "VaryingInput" },
			{ SLANG_BINDING_TYPE_VARYING_OUTPUT, "VaryingOutput" },
			{ SLANG_BINDING_TYPE_EXISTENTIAL_VALUE, "ExistentialValue" },
			{ SLANG_BINDING_TYPE_PUSH_CONSTANT, "PushConstant" },
			{ SLANG_BINDING_TYPE_MUTABLE_FLAG, "MutableFlag" },
			{ SLANG_BINDING_TYPE_MUTABLE_TETURE, "MutableTexture" },
			{ SLANG_BINDING_TYPE_MUTABLE_TYPED_BUFFER, "MutableTypedBuffer" },
			{ SLANG_BINDING_TYPE_MUTABLE_RAW_BUFFER, "MutableRawBuffer" },
			{ SLANG_BINDING_TYPE_BASE_MASK, "BaseMask" },
			{ SLANG_BINDING_TYPE_EXT_MASK, "ExtMask" },
		};
		static const unordered_map<SlangParameterCategory, const char*> category_name_map = {
			{ SLANG_PARAMETER_CATEGORY_NONE, "None" },
			{ SLANG_PARAMETER_CATEGORY_MIXED, "Mixed" },
			{ SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER, "ConstantBuffer" },
			{ SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE, "ShaderResource" },
			{ SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS, "UnorderedAccess" },
			{ SLANG_PARAMETER_CATEGORY_VARYING_INPUT, "VaryingInput" },
			{ SLANG_PARAMETER_CATEGORY_VARYING_OUTPUT, "VaryingOutput" },
			{ SLANG_PARAMETER_CATEGORY_SAMPLER_STATE, "SamplerState" },
			{ SLANG_PARAMETER_CATEGORY_UNIFORM, "Uniform" },
			{ SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT, "DescriptorTableSlot" },
			{ SLANG_PARAMETER_CATEGORY_SPECIALIZATION_CONSTANT, "SpecializationConstant" },
			{ SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER, "PushConstantBuffer" },
			{ SLANG_PARAMETER_CATEGORY_REGISTER_SPACE, "RegisterSpace" },
			{ SLANG_PARAMETER_CATEGORY_GENERIC, "GenericResource" },
			{ SLANG_PARAMETER_CATEGORY_RAY_PAYLOAD, "RayPayload" },
			{ SLANG_PARAMETER_CATEGORY_HIT_ATTRIBUTES, "HitAttributes" },
			{ SLANG_PARAMETER_CATEGORY_CALLABLE_PAYLOAD, "CallablePayload" },
			{ SLANG_PARAMETER_CATEGORY_SHADER_RECORD, "ShaderRecord" },
			{ SLANG_PARAMETER_CATEGORY_EXISTENTIAL_TYPE_PARAM, "ExistentialTypeParam" },
			{ SLANG_PARAMETER_CATEGORY_EXISTENTIAL_OBJECT_PARAM, "ExistentialObjectParam" },
		};
		static const unordered_map<SlangScalarType, size_t> scalar_size_map = {
			{ SLANG_SCALAR_TYPE_NONE, 0 },
			{ SLANG_SCALAR_TYPE_VOID, sizeof(uint32_t) },
			{ SLANG_SCALAR_TYPE_BOOL, sizeof(uint32_t) },
			{ SLANG_SCALAR_TYPE_INT32, sizeof(int32_t) },
			{ SLANG_SCALAR_TYPE_UINT32, sizeof(uint32_t) },
			{ SLANG_SCALAR_TYPE_INT64, sizeof(int64_t) },
			{ SLANG_SCALAR_TYPE_UINT64, sizeof(uint64_t) },
			{ SLANG_SCALAR_TYPE_FLOAT16, sizeof(uint16_t) },
			{ SLANG_SCALAR_TYPE_FLOAT32, sizeof(uint32_t) },
			{ SLANG_SCALAR_TYPE_FLOAT64, sizeof(uint64_t) },
			{ SLANG_SCALAR_TYPE_INT8, sizeof(int8_t) },
			{ SLANG_SCALAR_TYPE_UINT8, sizeof(uint8_t) },
			{ SLANG_SCALAR_TYPE_INT16, sizeof(int16_t) },
			{ SLANG_SCALAR_TYPE_UINT16, sizeof(uint16_t) }
		};

		static const unordered_map<SlangStage, vk::ShaderStageFlagBits> stage_map = {
        	{ SLANG_STAGE_VERTEX, vk::ShaderStageFlagBits::eVertex },
        	{ SLANG_STAGE_HULL, vk::ShaderStageFlagBits::eTessellationControl },
        	{ SLANG_STAGE_DOMAIN, vk::ShaderStageFlagBits::eTessellationEvaluation },
        	{ SLANG_STAGE_GEOMETRY, vk::ShaderStageFlagBits::eGeometry },
        	{ SLANG_STAGE_FRAGMENT, vk::ShaderStageFlagBits::eFragment },
        	{ SLANG_STAGE_COMPUTE, vk::ShaderStageFlagBits::eCompute },
        	{ SLANG_STAGE_RAY_GENERATION, vk::ShaderStageFlagBits::eRaygenKHR },
        	{ SLANG_STAGE_INTERSECTION, vk::ShaderStageFlagBits::eIntersectionKHR },
        	{ SLANG_STAGE_ANY_HIT, vk::ShaderStageFlagBits::eAnyHitKHR },
        	{ SLANG_STAGE_CLOSEST_HIT, vk::ShaderStageFlagBits::eClosestHitKHR },
        	{ SLANG_STAGE_MISS, vk::ShaderStageFlagBits::eMissKHR },
        	{ SLANG_STAGE_CALLABLE, vk::ShaderStageFlagBits::eCallableKHR },
        	{ SLANG_STAGE_MESH, vk::ShaderStageFlagBits::eMeshNV },
		};
		mStage = stage_map.at(shaderReflection->getEntryPointByIndex(0)->getStage());
		if (mStage == vk::ShaderStageFlagBits::eCompute) {
			SlangUInt sz[3];
			shaderReflection->getEntryPointByIndex(0)->getComputeThreadGroupSize(3, &sz[0]);
			mWorkgroupSize.width  = (uint32_t)sz[0];
			mWorkgroupSize.height = (uint32_t)sz[1];
			mWorkgroupSize.depth  = (uint32_t)sz[2];
		}

		for(uint32_t parameter_index = 0; parameter_index < shaderReflection->getParameterCount(); parameter_index++) {
			slang::VariableLayoutReflection* parameter = shaderReflection->getParameterByIndex(parameter_index);
			slang::ParameterCategory category = parameter->getCategory();
			slang::TypeReflection* type = parameter->getType();
			slang::TypeLayoutReflection* typeLayout = parameter->getTypeLayout();

			if (category == slang::ParameterCategory::DescriptorTableSlot) {
				static const unordered_map<SlangBindingType, vk::DescriptorType> descriptor_type_map = {
  					{ SLANG_BINDING_TYPE_SAMPLER, vk::DescriptorType::eSampler },
  					{ SLANG_BINDING_TYPE_TEXTURE, vk::DescriptorType::eSampledImage},
  					{ SLANG_BINDING_TYPE_CONSTANT_BUFFER, vk::DescriptorType::eUniformBuffer },
  					{ SLANG_BINDING_TYPE_TYPED_BUFFER, vk::DescriptorType::eUniformTexelBuffer },
  					{ SLANG_BINDING_TYPE_RAW_BUFFER, vk::DescriptorType::eStorageBuffer },
  					{ SLANG_BINDING_TYPE_COMBINED_TEXTURE_SAMPLER, vk::DescriptorType::eCombinedImageSampler },
  					{ SLANG_BINDING_TYPE_INPUT_RENDER_TARGET, vk::DescriptorType::eInputAttachment },
  					{ SLANG_BINDING_TYPE_INLINE_UNIFORM_DATA, vk::DescriptorType::eInlineUniformBlock },
  					{ SLANG_BINDING_TYPE_RAY_TRACTING_ACCELERATION_STRUCTURE, vk::DescriptorType::eAccelerationStructureKHR },
  					{ SLANG_BINDING_TYPE_MUTABLE_TETURE, vk::DescriptorType::eStorageImage },
  					{ SLANG_BINDING_TYPE_MUTABLE_TYPED_BUFFER, vk::DescriptorType::eStorageTexelBuffer },
  					{ SLANG_BINDING_TYPE_MUTABLE_RAW_BUFFER, vk::DescriptorType::eStorageBuffer },
				};
				const vk::DescriptorType descriptor_type = descriptor_type_map.at((SlangBindingType)typeLayout->getBindingRangeType(0));

				vector<variant<uint32_t,string>> array_size;
				if (typeLayout->getKind() == slang::TypeReflection::Kind::Array)
					array_size.emplace_back((uint32_t)typeLayout->getTotalArrayElementCount());

				const uint32_t input_attachment_index = 0; // TODO

				mDescriptorMap.emplace(parameter->getName(), DescriptorBinding(parameter->getBindingSpace(), parameter->getBindingIndex(), descriptor_type, array_size, input_attachment_index));

			} else if (category == slang::ParameterCategory::PushConstantBuffer) {
				// TODO: get offset from slang; currently assumes tightly packed scalar push constants
				size_t offset = 0;
				for (uint32_t i = 0; i < type->getElementType()->getFieldCount(); i++) {
					slang::VariableReflection* field = type->getElementType()->getFieldByIndex(i);
					slang::TypeLayoutReflection* fieldTypeLayout = typeLayout->getElementTypeLayout()->getFieldByIndex(i)->getTypeLayout();
					slang::TypeReflection* fieldType = field->getType();
					if (fieldType->getScalarType() == slang::TypeReflection::ScalarType::None) {
						cerr << "Warning: Non-scalar push constants unsupported" << endl;
						continue;
					}
					auto& dst = mPushConstants[field->getName()];
					dst.mOffset = (uint32_t)offset;
					dst.mArrayStride = fieldTypeLayout->getStride();
					if (fieldType->getTotalArrayElementCount())
						dst.mArraySize.emplace_back((uint32_t)fieldType->getTotalArrayElementCount());
					dst.mTypeSize = scalar_size_map.at((SlangScalarType)fieldType->getScalarType());
					offset += max<size_t>(fieldType->getTotalArrayElementCount(), 1) * fieldTypeLayout->getStride();
				}
			} else
				cerr << "Warning: Unsupported resource category: " << category_name_map.at((SlangParameterCategory)category) << endl;
		}

		spDestroyCompileRequest(request);
		spDestroySession(session);
	}

	const fs::path spvpath = (fs::temp_directory_path().string() / mShaderFile.stem()).string() + "_" + mEntryPoint + ".spv";

	string compile_cmd = "..\\..\\extern\\slang\\bin\\windows-x64\\release\\slangc.exe " + mShaderFile.string() + " -entry " + mEntryPoint;
	compile_cmd += " -profile sm_6_6 -lang slang -target spirv -o " + spvpath.string();
	for (const string& arg : mCompileArgs)
		compile_cmd += " " + arg;
	compile_cmd += " -D__HLSL__ -D__SLANG__";
	for (const auto&[n,d] : defines)
		if (d.index() == 0)
			compile_cmd += " -D" + n + "=" + to_string(std::get<uint32_t>(d));
		else
			compile_cmd += " -D" + n + "=" + std::get<string>(d);
	system(compile_cmd.c_str());

	return read_file<vector<uint32_t>>(spvpath);
}

void Shader::load_reflection(const fs::path& json_path) {
	std::ifstream s(json_path);
	if (!s.is_open()) throw runtime_error("Could not open " + json_path.string());

	nlohmann::json j;
	s >> j;

	static unordered_map<string, vk::ShaderStageFlagBits> gStageMap {
		{ "vertex", vk::ShaderStageFlagBits::eVertex },
		{ "vert", vk::ShaderStageFlagBits::eVertex },
		{ "tessellationcontrol", vk::ShaderStageFlagBits::eTessellationControl },
		{ "tesc", vk::ShaderStageFlagBits::eTessellationControl },
		{ "tessellationevaluation", vk::ShaderStageFlagBits::eTessellationEvaluation },
		{ "tese", vk::ShaderStageFlagBits::eTessellationEvaluation },
		{ "geometry", vk::ShaderStageFlagBits::eGeometry },
		{ "geom", vk::ShaderStageFlagBits::eGeometry },
		{ "fragment", vk::ShaderStageFlagBits::eFragment },
		{ "frag", vk::ShaderStageFlagBits::eFragment },
		{ "compute", vk::ShaderStageFlagBits::eCompute },
		{ "comp", vk::ShaderStageFlagBits::eCompute },
		{ "raygen", vk::ShaderStageFlagBits::eRaygenKHR },
		{ "rgen", vk::ShaderStageFlagBits::eRaygenKHR },
		{ "anyhit", vk::ShaderStageFlagBits::eAnyHitKHR },
		{ "rahit", vk::ShaderStageFlagBits::eAnyHitKHR },
		{ "closesthit", vk::ShaderStageFlagBits::eClosestHitKHR },
		{ "rchit", vk::ShaderStageFlagBits::eClosestHitKHR },
		{ "miss", vk::ShaderStageFlagBits::eMissKHR },
		{ "rmiss", vk::ShaderStageFlagBits::eMissKHR },
		{ "intersection", vk::ShaderStageFlagBits::eIntersectionKHR },
		{ "rint", vk::ShaderStageFlagBits::eIntersectionKHR },
		{ "callable", vk::ShaderStageFlagBits::eCallableKHR },
		{ "task", vk::ShaderStageFlagBits::eTaskNV },
		{ "mesh", vk::ShaderStageFlagBits::eMeshNV }
	};

	mEntryPoint = j["entryPoints"][0]["name"];
	mStage = gStageMap.at(j["entryPoints"][0]["mode"]);
	if (j["entryPoints"][0]["workgroup_size"].is_array()) {
		mWorkgroupSize.width = j["entryPoints"][0]["workgroup_size"][0];
		mWorkgroupSize.height = j["entryPoints"][0]["workgroup_size"][1];
		mWorkgroupSize.depth = j["entryPoints"][0]["workgroup_size"][2];
	} else
		mWorkgroupSize = vk::Extent3D(0, 0, 0);

	for (const auto& v : j["specialization_constants"]) {
		uint32_t defaultValue = 0;
		if (v["type"] == "int")
			defaultValue = v["default_value"].get<int32_t>();
		else if (v["type"] == "uint")
			defaultValue = v["default_value"].get<uint32_t>();
		else if (v["type"] == "float") {
			float f = v["default_value"].get<float>();
			defaultValue = *reinterpret_cast<uint32_t*>(&f);
		} else if (v["type"] == "bool")
			defaultValue = v["default_value"].get<bool>();
		mSpecializationConstants.emplace(v["name"], make_pair(v["id"], defaultValue));
	}

	for (const auto& v : j["push_constants"])
		for (const auto& c : j["types"][v["type"].get<string>()]["members"]) {
			auto& dst = mPushConstants[c["name"]];
			dst.mOffset = c["offset"];
			dst.mTypeSize = spirv_type_size(j, c["type"]);
			if (c.contains("array")) {
				dst.mArrayStride = c["array_stride"];
				dst.mArraySize = spirv_array_size(j, c);
			}
		}

	static const unordered_map<string, vk::Format> gSpirvTypeMap {
		{ "int",   vk::Format::eR32Sint },
		{ "ivec2", vk::Format::eR32G32Sint },
		{ "ivec3", vk::Format::eR32G32B32Sint },
		{ "ivec4", vk::Format::eR32G32B32A32Sint },
		{ "uint",  vk::Format::eR32Uint },
		{ "uvec2", vk::Format::eR32G32Uint },
		{ "uvec3", vk::Format::eR32G32B32Uint },
		{ "uvec4", vk::Format::eR32G32B32A32Uint },
		{ "float", vk::Format::eR32Sfloat },
		{ "vec2",  vk::Format::eR32G32Sfloat },
		{ "vec3",  vk::Format::eR32G32B32Sfloat },
		{ "vec4",  vk::Format::eR32G32B32A32Sfloat  }
	};
	for (const auto& v : j["inputs"]) {
		auto& dst = mStageInputs[v["name"]];
		dst.mLocation = v["location"];
		dst.mFormat = gSpirvTypeMap.at(v["type"]);
		tie(dst.mAttributeType,dst.mTypeIndex) = attribute_type(v["name"]);
	}
	for (const auto& v : j["outputs"]) {
		auto& dst = mStageOutputs[v["name"]];
		dst.mLocation = v["location"];
		dst.mFormat = gSpirvTypeMap.at(v["type"]);
		tie(dst.mAttributeType,dst.mTypeIndex) = attribute_type(v["name"]);
	}

	for (const auto& v : j["subpass_inputs"])
		mDescriptorMap.emplace(v["name"], DescriptorBinding(v["set"], v["binding"], vk::DescriptorType::eInputAttachment, spirv_array_size(j, v), v["input_attachment_index"]));
	for (const auto& v : j["textures"])
		mDescriptorMap.emplace(v["name"], DescriptorBinding(v["set"], v["binding"], vk::DescriptorType::eCombinedImageSampler, spirv_array_size(j, v)));
	for (const auto& v : j["images"])
		mDescriptorMap.emplace(v["name"], DescriptorBinding(v["set"], v["binding"], vk::DescriptorType::eStorageImage, spirv_array_size(j, v))); // TODO: texelbuffer
	for (const auto& v : j["separate_images"])
		mDescriptorMap.emplace(v["name"], DescriptorBinding(v["set"], v["binding"], vk::DescriptorType::eSampledImage, spirv_array_size(j, v)));
	for (const auto& v : j["separate_samplers"])
		mDescriptorMap.emplace(v["name"], DescriptorBinding(v["set"], v["binding"], vk::DescriptorType::eSampler, spirv_array_size(j, v)));
	for (const auto& v : j["ubos"]) {
		string n = v["name"];
		auto t = j["types"][v["type"].get<string>()];
		if (t["name"] == n && n.starts_with("type.")) n = n.substr(5);
		mDescriptorMap.emplace(n, DescriptorBinding(v["set"], v["binding"], vk::DescriptorType::eUniformBuffer, spirv_array_size(j, v)));
	}
	for (const auto& v : j["ssbos"])
		mDescriptorMap.emplace(v["name"], DescriptorBinding(v["set"], v["binding"], vk::DescriptorType::eStorageBuffer, spirv_array_size(j, v)));
	for (const auto& v : j["acceleration_structures"])
		mDescriptorMap.emplace(v["name"], DescriptorBinding(v["set"], v["binding"], vk::DescriptorType::eAccelerationStructureKHR, spirv_array_size(j, v)));

}

Shader::Shader(Device& device, const fs::path& filename, const string& entrypoint, const vector<string>& args) :
	DeviceResource(device, filename.stem().string()), mShaderFile(filename), mEntryPoint(entrypoint), mCompileArgs(args) {

	if (!fs::exists(filename)) throw runtime_error(filename.string() + " does not exist");
	vector<uint32_t> spv;
	if (filename.extension() == ".spv") {
		// load spirv binary
		spv = read_file<vector<uint32_t>>(mShaderFile);
		cout << "Loaded " << mShaderFile << endl;
		load_reflection(fs::path(mShaderFile).replace_extension("json"));
		mCompileSpecializations = false;
	} else if (filename.extension() == ".slang" || filename.extension() == ".hlsl" || filename.extension() == ".glsl") {
		// Compile source with slang
		spv = slang_compile({}, true);
		cout << "Compiled " << mShaderFile << ": " << mEntryPoint << " (" << spv.size()*sizeof(uint32_t) << " bytes)" << endl;
		mCompileSpecializations = true;
	}
	mShaderModules.emplace("", device->createShaderModule(vk::ShaderModuleCreateInfo({}, spv)));
}

const vk::ShaderModule& Shader::get(const unordered_map<string, variant<uint32_t,string>>& defines) {
	if (defines.empty())
		return mShaderModules.at("");

	string define_args = "";
	for (const auto&[n,d] : defines)
		if (d.index() == 0)
			define_args += " -D" + n + "=" + to_string(std::get<uint32_t>(d));
		else
			define_args += " -D" + n + "=" + std::get<string>(d);

	if (auto it = mShaderModules.find(define_args); it != mShaderModules.end())
		return it->second;

	const vector<uint32_t> spv = slang_compile(defines);
	cout << "Compiled " << mShaderFile << ": " << mEntryPoint << define_args << endl;
	return mShaderModules.emplace(define_args, mDevice->createShaderModule(vk::ShaderModuleCreateInfo({}, spv))).first->second;
}
