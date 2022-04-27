#include "Shader.hpp"

#include <json.hpp>

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

Shader::Shader(Device& device, const fs::path& spv) : DeviceResource(device, spv.stem().string()) {
	fs::path json_path = fs::path(spv).replace_extension("json");
	std::ifstream s(json_path);
	if (!s.is_open()) throw runtime_error("Could not open " + json_path.string());

	auto shader = read_file<vector<uint32_t>>(spv);
	mShader = device->createShaderModule(vk::ShaderModuleCreateInfo({}, shader));

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
	for (const auto& v : j["images"]) {

		mDescriptorMap.emplace(v["name"], DescriptorBinding(v["set"], v["binding"], vk::DescriptorType::eStorageImage, spirv_array_size(j, v))); // TODO: texelbuffer
	}
	for (const auto& v : j["separate_images"]) {

		mDescriptorMap.emplace(v["name"], DescriptorBinding(v["set"], v["binding"], vk::DescriptorType::eSampledImage, spirv_array_size(j, v)));
	}
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

	cout << "Loaded " << spv << endl;
}