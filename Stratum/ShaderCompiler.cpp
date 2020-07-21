#include <chrono>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "ShaderCompiler.hpp"
#include <shaderc/shaderc.hpp>
#include <../spirv_cross.hpp>

using namespace std;
using namespace shaderc;

CompileOptions options;

class Includer : public CompileOptions::IncluderInterface {
public:
	inline Includer(const string& globalPath) : mIncludePath(globalPath) {}

	inline virtual shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type, const char* requesting_source, size_t include_depth) override {
		fs::path folder;
		
		if (type == shaderc_include_type_relative)
			folder = fs::absolute(requesting_source).parent_path();
		else
			folder = mIncludePath;

		string fullpath = folder.string() + "/" + requested_source;

		shaderc_include_result* response = new shaderc_include_result();
		string& data = mFiles[fullpath];
		if (data.empty() && !ReadFile(fullpath, data)) {
			char* err = new char[128];
			sprintf(err, "Failed to read include file: %s while compiling %s\n", fullpath.c_str(), requesting_source);

			response->source_name = "";
			response->source_name_length = 0;
			response->content = err;
			response->content_length = strlen(response->content);;
			response->user_data = err;
		} else {
			mFullPaths.emplace(fullpath, fullpath);
			const string& name = mFullPaths.at(fullpath);

			response->source_name = name.data();
			response->source_name_length = strlen(response->source_name);
			response->content = data.data();
			response->content_length = strlen(response->content);
			response->user_data = nullptr;
		}

		return response;
	}
	inline void ReleaseInclude(shaderc_include_result* data) override {
		if (data->user_data) delete[] (char*)data->user_data;
		delete data;
	}

private:
	string mIncludePath;

	unordered_map<string, string> mFiles;
	unordered_map<string, string> mFullPaths;
};

bool CompileAssembly(Compiler* compiler, const CompileOptions& options, const string& source, const string& filename,
	shaderc_shader_kind stage, const string& entryPoint, const string& destFile) {
	
	AssemblyCompilationResult result = compiler->CompileGlslToSpvAssembly(source, stage, filename.c_str(), entryPoint.c_str(), options);
	string msg = result.GetErrorMessage();
	if (msg.size()) {
		stringstream ss(msg);
		string line;
		while (getline(ss, line)) {
			if (const char* error = strstr(line.c_str(), "error: "))
				fprintf_color(COLOR_RED, stderr, "%s\n", error + 7);
			else
				fprintf_color(COLOR_RED, stderr, "%s\n", line.c_str());
		}
	}

	if (result.GetCompilationStatus() != shaderc_compilation_status_success) return false;

	fs::path dstPath(destFile);
	if (dstPath.is_relative()) {
		fs::path srcPath(filename);
		dstPath = fs::path(srcPath.parent_path().string() + "/" + destFile);
		fprintf(stdout, "Outputting SPIR-V assembly file: %s\n", dstPath.string().c_str());
	}

	ofstream file(dstPath.c_str(), ios::out);
	for (auto d = result.cbegin(); d != result.cend(); d++)
		file << d << "\n";

	return true;
}

bool CompileStage(Compiler* compiler, const CompileOptions& options, const string& source, const string& filename,
	shaderc_shader_kind stage, const string& entryPoint, CompiledVariant& dest, CompiledShader& destShader) {
	
	SpvCompilationResult result = compiler->CompileGlslToSpv(source, stage, filename.c_str(), entryPoint.c_str(), options);
	string msg = result.GetErrorMessage();

	if (msg.size()) {
		stringstream ss(msg);
		string line;
		while (getline(ss, line)) {
			if (const char* error = strstr(line.c_str(), "error: "))
				fprintf_color(COLOR_RED, stderr, "%s\n", error + 7);
			else
				fprintf_color(COLOR_RED, stderr, "%s\n", line.c_str());
		}
	}

	if (result.GetCompilationStatus() != shaderc_compilation_status_success) return false;

	// store SPIRV module
	destShader.mModules.push_back({});
	SpirvModule& m = destShader.mModules.back();
	for (auto d = result.cbegin(); d != result.cend(); d++)
		m.mSpirv.push_back(*d);

	// assign SPIRV module
	VkSampleCountFlags vkstage;
	switch (stage) {
	case shaderc_vertex_shader:
		dest.mModules[0] = (uint32_t)destShader.mModules.size() - 1;
		vkstage = VK_SHADER_STAGE_VERTEX_BIT;
		break;
	case shaderc_fragment_shader:
		dest.mModules[1] = (uint32_t)destShader.mModules.size() - 1;
		vkstage = VK_SHADER_STAGE_FRAGMENT_BIT;
		break;
	case shaderc_compute_shader:
		dest.mModules[0] = (uint32_t)destShader.mModules.size() - 1;
		vkstage = VK_SHADER_STAGE_COMPUTE_BIT;
		break;
	}

	spirv_cross::Compiler comp(m.mSpirv.data(), m.mSpirv.size());
	spirv_cross::ShaderResources res = comp.get_shader_resources();
	
	#pragma region register resource bindings
	auto registerResource = [&](const spirv_cross::Resource& res, VkDescriptorType type) {
		auto& binding = dest.mDescriptorBindings[res.name];

		binding.first = comp.get_decoration(res.id, spv::DecorationDescriptorSet);

		binding.second.stageFlags |= vkstage;
		binding.second.binding = comp.get_decoration(res.id, spv::DecorationBinding);
		binding.second.descriptorCount = 1;
		binding.second.descriptorType = type;
	};

	for (const auto& r : res.sampled_images)
		registerResource(r, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	for (const auto& r : res.separate_images)
		if (comp.get_type(r.type_id).image.dim == spv::DimBuffer)
			registerResource(r, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
		else
			registerResource(r, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	for (const auto& r : res.storage_images)
		if (comp.get_type(r.type_id).image.dim == spv::DimBuffer)
			registerResource(r, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
		else
			registerResource(r, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	for (const auto& r : res.storage_buffers)
		registerResource(r, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	for (const auto& r : res.separate_samplers)
		registerResource(r, VK_DESCRIPTOR_TYPE_SAMPLER);
	for (const auto& r : res.uniform_buffers)
		registerResource(r, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

	for (const auto& r : res.push_constant_buffers) {
		uint32_t index = 0;

		const auto& type = comp.get_type(r.base_type_id);

		if (type.basetype == spirv_cross::SPIRType::Struct) {
			for (uint32_t i = 0; i < type.member_types.size(); i++) {
				const auto& mtype = comp.get_type(type.member_types[i]);

				const string name = comp.get_member_name(r.base_type_id, index);
				
				VkPushConstantRange range = {};
				range.stageFlags = vkstage == VK_SHADER_STAGE_COMPUTE_BIT ? VK_SHADER_STAGE_COMPUTE_BIT : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
				range.offset = comp.type_struct_member_offset(type, index);

				switch (mtype.basetype) {
				case spirv_cross::SPIRType::Boolean:
				case spirv_cross::SPIRType::SByte:
				case spirv_cross::SPIRType::UByte:
					range.size = 1;
					break;
				case spirv_cross::SPIRType::Short:
				case spirv_cross::SPIRType::UShort:
				case spirv_cross::SPIRType::Half:
					range.size = 2;
					break;
				case spirv_cross::SPIRType::Int:
				case spirv_cross::SPIRType::UInt:
				case spirv_cross::SPIRType::Float:
					range.size = 4;
					break;
				case spirv_cross::SPIRType::Int64:
				case spirv_cross::SPIRType::UInt64:
				case spirv_cross::SPIRType::Double:
					range.size = 8;
					break;
				case spirv_cross::SPIRType::Struct:
					range.size = (uint32_t)comp.get_declared_struct_size(mtype);
					break;
				case spirv_cross::SPIRType::Unknown:
				case spirv_cross::SPIRType::Void:
				case spirv_cross::SPIRType::AtomicCounter:
				case spirv_cross::SPIRType::Image:
				case spirv_cross::SPIRType::SampledImage:
				case spirv_cross::SPIRType::Sampler:
				case spirv_cross::SPIRType::AccelerationStructureNV:
					fprintf(stderr, "Warning: Unknown type for push constant: %s\n", name.c_str());
					range.size = 0;
					break;
				}

				range.size *= mtype.columns * mtype.vecsize;

				vector<pair<string, VkPushConstantRange>> ranges;
				ranges.push_back(make_pair(name, range));

				for (uint32_t dim : mtype.array) {
					for (auto& r : ranges)
						r.second.size *= dim;
					// TODO: support individual element ranges
					//uint32_t sz = ranges.size();
					//for (uint32_t j = 0; j < sz; j++)
					//	for (uint32_t c = 0; c < dim; c++)
					//		ranges.push_back(make_pair(ranges[j].first + "[" + to_string(c) + "]", range));
				}

				for (auto& r : ranges)
					dest.mPushConstants[r.first] = r.second;

				index++;
			}
		} else
			fprintf(stderr, "Warning: Push constant data is not a struct! Reflection will not work.\n");
	}
	#pragma endregion
	
	if (vkstage == VK_SHADER_STAGE_COMPUTE_BIT) {
		auto entryPoints = comp.get_entry_points_and_stages();
		for (const auto& e : entryPoints) {
			if (e.name == entryPoint) {
				auto& ep = comp.get_entry_point(e.name, e.execution_model);
				dest.mWorkgroupSize[0] = ep.workgroup_size.x;
				dest.mWorkgroupSize[1] = ep.workgroup_size.y;
				dest.mWorkgroupSize[2] = ep.workgroup_size.z;
			}
		}
	} else
		dest.mWorkgroupSize = 0;
	
	return true;
}

CompiledShader* Compile(shaderc::Compiler* compiler, const string& filename) {
	string source;
	if (!ReadFile(filename, source)) {
		fprintf_color(COLOR_RED, stderr, "Failed to read %s!\n", filename.c_str());
		return nullptr;
	}

	unordered_map<PassType, pair<string, string>> passes; // pass -> (vertex, fragment)
	vector<string> kernels;

	std::vector<std::pair<std::string, uint32_t>> arrays; // name, size
	std::vector<std::pair<std::string, VkSamplerCreateInfo>> staticSamplers;
	
	struct AssemblyOutput {
		string destFile;
		string entryPoint;
		shaderc_shader_kind stage;
		vector<string> keywords;
	};
	std::vector<AssemblyOutput> assemblyOutputs; // filename, entryPoint, compilerArgs

	CompiledShader* result = new CompiledShader();
	result->mRenderQueue = 1000;
	result->mColorMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	result->mCullMode = VK_CULL_MODE_BACK_BIT;
	result->mFillMode = VK_POLYGON_MODE_FILL;
	result->mBlendMode = BLEND_MODE_OPAQUE;
	result->mDepthStencilState = {};
	result->mDepthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	result->mDepthStencilState.depthTestEnable = VK_TRUE;
	result->mDepthStencilState.depthWriteEnable = VK_TRUE;
	result->mDepthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	result->mDepthStencilState.front = result->mDepthStencilState.back;
	result->mDepthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;

	vector<set<string>> variants{ {} };

	istringstream srcstream(source);

	uint32_t lineNumber = 0;

	#define RET_ERR(...) { fprintf_color(COLOR_RED, stderr, "%s(%u): ", filename.c_str(), lineNumber); fprintf_color(COLOR_RED, stderr, __VA_ARGS__);  return nullptr; }

	string line;
	while (getline(srcstream, line)) {
		lineNumber++;
		istringstream linestream(line);
		vector<string> words{ istream_iterator<string>{linestream}, istream_iterator<string>{} };
		
		size_t kwc = variants.size();

		for (auto it = words.begin(); it != words.end(); ++it) {
			if (*it == "#pragma") {
				if (++it == words.end()) RET_ERR("Error: 'pragma': expected compiler directive\n");
				if (*it == "multi_compile") {
					if (++it == words.end()) RET_ERR("Error: 'multi_compile': expected one or more keywords\n");
					// iterate all the keywords added by this multi_compile
					while (it != words.end()) {
						// duplicate all existing variants, add this keyword to each
						for (uint32_t i = 0; i < kwc; i++) {
							variants.push_back(variants[i]);
							variants.back().insert(*it);
						}
						++it;
					}
				
				} else if (*it == "vertex") {
					if (++it == words.end()) RET_ERR("Error: 'vertex': expected an entry point\n");
					string ep = *it;
					PassType pass = PASS_MAIN;
					if (++it != words.end()) pass = atopass(*it);
					if (passes.count(pass) && passes[pass].first != "") RET_ERR("Error: 'vertex': Cannot have more than oney vertex shader for pass %s!\n", it->c_str());
					passes[pass].first = ep;

				} else if (*it == "fragment") {
					if (++it == words.end()) RET_ERR("Error: 'fragment': expected an entry point\n");
					string ep = *it;
					PassType pass = PASS_MAIN;
					if (++it != words.end()) pass = atopass(*it);
					if (passes.count(pass) && passes[pass].second != "") RET_ERR("Error: 'fragment': Cannot have more than one fragment shader for pass %s\n", it->c_str());
					passes[pass].second = ep;

				} else if (*it == "kernel") {
					if (++it == words.end()) RET_ERR("Error: 'kernel': expected an entry point\n");
					kernels.push_back(*it);

				} else if (*it == "output_assembly") {
					if (++it == words.end()) RET_ERR("Error: 'output_assembly': expected a filename\n");
					string filename = *it;
					if (++it == words.end()) RET_ERR("Error: 'output_assembly': expected a stage identifier\n");
					shaderc_shader_kind stage;
					if (*it == "kernel") stage = shaderc_compute_shader;
					else if (*it == "fragment") stage = shaderc_fragment_shader;
					else if (*it == "vertex") stage = shaderc_vertex_shader;
					else RET_ERR("Error: 'output_assembly': unknown stage identifer %s (expected one of: 'vertex' 'fragment' 'kernel')\n");
					if (++it == words.end()) RET_ERR("Error: 'output_assembly': expected an entry point\n");
					assemblyOutputs.push_back({});
					AssemblyOutput& a = assemblyOutputs.back();
					a.entryPoint = *it;
					a.destFile = filename;
					a.stage = stage;
					while (++it != words.end()) a.keywords.push_back(*it);
				
				} else if (*it == "render_queue"){
					if (++it == words.end()) RET_ERR("Error: 'render_queue': expected an integer\n");
					result->mRenderQueue = atoi(it->c_str());

				} else if (*it == "color_mask") {
					if (++it == words.end()) RET_ERR("Error: 'color_mask': expected a concatenation of 'r' 'g' 'b' 'a'\n");
					result->mColorMask = atomask(*it);

				} else if (*it == "zwrite") {
					if (++it == words.end()) RET_ERR("Error: 'zwrite': expected one of 'true' 'false'\n");
					if (*it == "true") result->mDepthStencilState.depthWriteEnable = VK_TRUE;
					else if (*it == "false") result->mDepthStencilState.depthWriteEnable = VK_FALSE;
					else RET_ERR("Error: 'zwrite' expected one of 'true' 'false'\n");

				} else if (*it == "ztest") {
					if (++it == words.end()) RET_ERR("Error: 'ztest': expected one of 'true' 'false'\n");
					if (*it == "true")
						result->mDepthStencilState.depthTestEnable = VK_TRUE;
					else if (*it == "false")
						result->mDepthStencilState.depthTestEnable = VK_FALSE;
					else RET_ERR("Error: 'ztest': expected one of 'true' 'false'\n");

				} else if (*it == "depth_op") {
					if (++it == words.end()) RET_ERR("Error: 'depth_op': expected a depth op\n");
					result->mDepthStencilState.depthCompareOp = atocmp(*it);

				} else if (*it == "cull") {
					if (++it == words.end()) RET_ERR("Error: 'cull': expected one of 'front' 'back' 'false'\n");
					if (*it == "front") 			result->mCullMode = VK_CULL_MODE_FRONT_BIT;
					else if (*it == "back") 	result->mCullMode = VK_CULL_MODE_BACK_BIT;
					else if (*it == "false") 	result->mCullMode = VK_CULL_MODE_NONE;
					else RET_ERR("Error: 'cull': expected one of 'front' 'back' 'false'\n");

				} else if (*it == "fill") {
					if (++it == words.end()) RET_ERR("Error: 'fill': expected one of 'solid' 'line' 'point'\n");
					if (*it == "solid") 		 	result->mFillMode = VK_POLYGON_MODE_FILL;
					else if (*it == "line")  	result->mFillMode = VK_POLYGON_MODE_LINE;
					else if (*it == "point") 	result->mFillMode = VK_POLYGON_MODE_POINT;
					else RET_ERR("Error: 'fill': expected one of 'solid' 'line' 'point'\n");

				} else if (*it == "blend") {
					if (++it == words.end()) RET_ERR("Error: 'blend': expected one of 'opaque' 'alpha' 'add' 'multiply'\n");
					if (*it == "opaque")		    result->mBlendMode = BLEND_MODE_OPAQUE;
					else if (*it == "alpha")	  result->mBlendMode = BLEND_MODE_ALPHA;
					else if (*it == "add")		  result->mBlendMode = BLEND_MODE_ADDITIVE;
					else if (*it == "multiply")	result->mBlendMode = BLEND_MODE_MULTIPLY;
					else RET_ERR("Error: 'blend': expected one of 'opaque' 'alpha' 'add' 'multiply'\n");

				} else if (*it == "static_sampler") {
					if (++it == words.end()) RET_ERR("Error: 'static_sampler': expected an sampler name\n");
					string name = *it;

					VkSamplerCreateInfo samplerInfo = {};
					samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
					samplerInfo.magFilter = VK_FILTER_LINEAR;
					samplerInfo.minFilter = VK_FILTER_LINEAR;
					samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
					samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
					samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
					samplerInfo.anisotropyEnable = VK_TRUE;
					samplerInfo.maxAnisotropy = 2;
					samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
					samplerInfo.unnormalizedCoordinates = VK_FALSE;
					samplerInfo.compareEnable = VK_FALSE;
					samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
					samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
					samplerInfo.minLod = 0;
					samplerInfo.maxLod = 12;
					samplerInfo.mipLodBias = 0;

					while (++it != words.end()) {
						size_t eq = it->find('=');
						if (eq == string::npos) RET_ERR("Error: 'static_sampler': expected arguments in the form '<arg>=<value>'\n");
						string id = it->substr(0, eq);
						string val = it->substr(eq + 1);
						if      (id == "magFilter")		  samplerInfo.magFilter = atofilter(val);
						else if (id == "minFilter")			samplerInfo.minFilter = atofilter(val);
						else if (id == "filter")				samplerInfo.minFilter = samplerInfo.magFilter = atofilter(val);
						else if (id == "compareOp") {
							VkCompareOp cmp = atocmp(val);
							samplerInfo.compareEnable = cmp == VK_COMPARE_OP_MAX_ENUM ? VK_FALSE : VK_TRUE;
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
						else RET_ERR("Error: 'static_sampler': Unknown argument %s (expected one of '[min,mag]Filter' 'addressMode[U,V,W]' 'borderColor' 'compareOp' 'maxAnisotropy' 'unnormalizedCoordinates')\n", id.c_str());
					}

					staticSamplers.push_back(make_pair(name, samplerInfo));

				} else if (*it == "array") {
					if (++it == words.end()) RET_ERR("Error: 'array': expected a descriptor name\n");
					string name = *it;
					if (++it == words.end()) RET_ERR("Error: 'array': expected an integer\n");
					arrays.push_back(make_pair(name, (uint32_t)atoi(it->c_str())));
				}
				break;
			}
		}
	}

	for (const auto& variant : variants) {
		auto variantOptions = options;

		vector<string> keywords;
		for (const auto& kw : variant) {
			if (kw.empty()) continue;
			keywords.push_back(kw);
			variantOptions.AddMacroDefinition(kw);
		}

		/// applies array and static_sampler pragmas
		auto UpdateBindings = [&](CompiledVariant& input) {
			for (auto b = input.mDescriptorBindings.begin(); b != input.mDescriptorBindings.end(); b++) {
				for (const auto& s : arrays)
					if (s.first == b->first) {
						b->second.second.descriptorCount = s.second;
						break;
					}
				for (const auto& s : staticSamplers)
					if (s.first == b->first) {
						input.mStaticSamplers.emplace(s.first, s.second);
						//input.mDescriptorBindings.erase(b);
						break;
					}
			}
		};

		if (kernels.size()) {
			for (const auto& k : kernels) {
				auto kernelOptions = variantOptions;
				kernelOptions.AddMacroDefinition("KERNEL_" + k);
				kernelOptions.AddMacroDefinition("SHADER_STAGE_COMPUTE");

				// compile all kernels for this variant
				CompiledVariant v = {};
				v.mPass = (PassType)0;
				v.mKeywords = keywords;
				v.mEntryPoints[0] = k;
				if (!CompileStage(compiler, kernelOptions, source, filename, shaderc_compute_shader, k, v, *result)) return nullptr;
				UpdateBindings(v);
				result->mVariants.push_back(v);
			}
		} else {
			unordered_map<string, uint32_t> compiled;

			for (auto& stagep : passes) {
				auto vsOptions = variantOptions;
				auto fsOptions = variantOptions;
				vsOptions.AddMacroDefinition("SHADER_STAGE_VERTEX");
				fsOptions.AddMacroDefinition("SHADER_STAGE_FRAGMENT");
				// compile all passes for this variant
				switch (stagep.first) {
				case PASS_MAIN:
					vsOptions.AddMacroDefinition("PASS_MAIN");
					fsOptions.AddMacroDefinition("PASS_MAIN");
					break;
				case PASS_DEPTH:
					vsOptions.AddMacroDefinition("PASS_DEPTH");
					fsOptions.AddMacroDefinition("PASS_DEPTH");
					break;
				}

				string vs = stagep.second.first;
				string fs = stagep.second.second;

				if (vs == "" && passes.count(PASS_MAIN)) vs = passes.at(PASS_MAIN).first;
				if (vs == "") {
					fprintf_color(COLOR_RED, stderr, "No vertex shader entry point found for fragment shader entry point: \n", fs.c_str());
					return nullptr;
				}
				if (fs == "" && passes.count(PASS_MAIN)) fs = passes.at(PASS_MAIN).second;
				if (fs == "") {
					fprintf_color(COLOR_RED, stderr, "No fragment shader entry point found for vertex shader entry point: \n", vs.c_str());
					return nullptr;
				}

				CompiledVariant v = {};
				v.mKeywords = keywords;
				v.mPass = stagep.first;
				v.mEntryPoints[0] = vs;
				v.mEntryPoints[1] = fs;

				// Don't recompile shared stages
				//if (compiled.count(vs)) {
				//	v.mModules[0] = compiled.at(vs);
				//} else {
					if (!CompileStage(compiler, vsOptions, source, filename, shaderc_vertex_shader, vs, v, *result)) return nullptr;
				//	compiled.emplace(vs, v.mModules[0]);
				//}
				
				//if (compiled.count(fs)) {
				//	v.mModules[1] = compiled.at(fs);
				//} else {
					if (!CompileStage(compiler, fsOptions, source, filename, shaderc_fragment_shader, fs, v, *result)) return nullptr;
				//	compiled.emplace(fs, v.mModules[1]);
				//}

				UpdateBindings(v);

				result->mVariants.push_back(v);
			}
		}
	}

	
	for (const auto& assemblyOutput : assemblyOutputs) {
		auto opts = options;

		for (const auto& kw : assemblyOutput.keywords) {
			if (kw.empty()) continue;
			opts.AddMacroDefinition(kw);
		}

		if (!CompileAssembly(compiler, opts, source, filename, assemblyOutput.stage, assemblyOutput.entryPoint, assemblyOutput.destFile)) return nullptr;		
	}

	return result;
}

int main(int argc, char* argv[]) {
	const char* inputFile;
	const char* outputFile;
	const char* include;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <input> <output> <global include path>\n", argv[0]);
		return EXIT_FAILURE;
	} else {
		inputFile = argv[1];
		outputFile = argv[2];
		include = argv[3];
	}

	printf("Compiling %s\n", inputFile);
	
	if (fs::path(inputFile).extension().string() == ".hlsl")
		options.SetSourceLanguage(shaderc_source_language_hlsl);
	else
		options.SetSourceLanguage(shaderc_source_language_glsl);
	
	options.SetIncluder(make_unique<Includer>(include));
	options.SetOptimizationLevel(shaderc_optimization_level_zero);
	options.SetAutoBindUniforms(false);
	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);

	Compiler* compiler = new Compiler();
	CompiledShader* shader = Compile(compiler, inputFile);
	
	if (!shader) return EXIT_FAILURE;

	// write shader
	ofstream output(outputFile, ios::binary);
	shader->Write(output);
	output.close();

	delete shader;

	delete compiler;

	return EXIT_SUCCESS;
}