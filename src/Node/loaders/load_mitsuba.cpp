#include "../Scene.hpp"
#include <Core/PipelineState.hpp>

#include <extern/pugixml.hpp>
#include <regex>

namespace stm {

vector<string> split_string(const string& str, const regex& delim_regex) {
	sregex_token_iterator first{ begin(str), end(str), delim_regex, -1 }, last;
	vector<string> list{ first, last };
	return list;
}

float3 parse_vector3(const string& value) {
	vector<string> list = split_string(value, regex("(,| )+"));
	float3 v;
	if (list.size() == 1) {
		v[0] = stof(list[0]);
		v[1] = stof(list[0]);
		v[2] = stof(list[0]);
	} else if (list.size() == 3) {
		v[0] = stof(list[0]);
		v[1] = stof(list[1]);
		v[2] = stof(list[2]);
	} else {
		throw runtime_error("parse_vector3 failed");
	}
	return v;
}

float3 parse_srgb(const string& value) {
	float3 srgb;
	if (value.size() == 7 && value[0] == '#') {
		char* end_ptr = NULL;
		// parse hex code (#abcdef)
		int encoded = strtol(value.c_str() + 1, &end_ptr, 16);
		if (*end_ptr != '\0') {
			throw runtime_error("Invalid sRGB value: " + value);
		}
		srgb[0] = ((encoded & 0xFF0000) >> 16) / 255.0f;
		srgb[1] = ((encoded & 0x00FF00) >> 8) / 255.0f;
		srgb[2] = (encoded & 0x0000FF) / 255.0f;
	} else {
		throw runtime_error("Unsupported sRGB format: " + value);
	}
	return srgb;
}

vector<pair<float, float>> parse_spectrum(const string& value) {
	vector<string> list = split_string(value, regex("(,| )+"));
	vector<pair<float, float>> s;
	if (list.size() == 1 && list[0].find(":") == string::npos) {
		// a single uniform value for all wavelength
		s.push_back(make_pair(float(-1), stof(list[0])));
	} else {
		for (auto val_str : list) {
			vector<string> pair = split_string(val_str, regex(":"));
			if (pair.size() < 2) {
				throw runtime_error("parse_spectrum failed");
			}
			s.push_back(make_pair(float(stof(pair[0])), float(stof(pair[1]))));
		}
	}
	return s;
}

Eigen::Matrix4f parse_matrix4x4(const string& value) {
	vector<string> list = split_string(value, regex("(,| )+"));
	if (list.size() != 16)
		throw runtime_error("parse_matrix4x4 failed");

	Eigen::Matrix4f m;
	int k = 0;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			m(i, j) = stof(list[k++]);
	return m;
}

TransformData parse_transform(pugi::xml_node node) {
	TransformData t = make_transform(float3::Zero(), quatf_identity(), float3::Ones());
	for (auto child : node.children()) {
		string name = child.name();
		for (char& c : name) c = tolower(c);
		if (name == "scale") {
			float x = 1;
			float y = 1;
			float z = 1;
			if (!child.attribute("x").empty())
				x = stof(child.attribute("x").value());
			if (!child.attribute("y").empty())
				y = stof(child.attribute("y").value());
			if (!child.attribute("z").empty())
				z = stof(child.attribute("z").value());
			if (!child.attribute("value").empty())
				x = y = z = stof(child.attribute("value").value());
			t = tmul(make_transform(float3::Zero(), quatf_identity(), float3(x, y, z)), t);
		} else if (name == "translate") {
			float x = 0;
			float y = 0;
			float z = 0;
			if (!child.attribute("x").empty())
				x = stof(child.attribute("x").value());
			if (!child.attribute("y").empty())
				y = stof(child.attribute("y").value());
			if (!child.attribute("z").empty())
				z = stof(child.attribute("z").value());
			t = tmul(make_transform(float3(x, y, z), quatf_identity(), float3::Ones()), t);
		} else if (name == "rotate") {
			float x = 0;
			float y = 0;
			float z = 0;
			float angle = 0;
			if (!child.attribute("x").empty())
				x = stof(child.attribute("x").value());
			if (!child.attribute("y").empty())
				y = stof(child.attribute("y").value());
			if (!child.attribute("z").empty())
				z = stof(child.attribute("z").value());
			if (!child.attribute("angle").empty())
				angle = radians(stof(child.attribute("angle").value()));
			t = tmul(make_transform(float3::Zero(), angle_axis(angle, float3(x, y, z)), float3::Ones()), t);
		} else if (name == "lookat") {
			const float3 pos = parse_vector3(child.attribute("origin").value());
			const float3 target = parse_vector3(child.attribute("target").value());
			float3 up = parse_vector3(child.attribute("up").value());

			const float3 fwd = (target - pos).matrix().normalized();
			up = (up - dot(up, fwd) * fwd).matrix().normalized();
			const float3 r = normalize(cross(up, fwd));
			t = tmul(make_transform(pos, make_quatf(r, up, fwd), float3::Ones()), t);
		} else if (name == "matrix") {
			t = tmul(from_float3x4(parse_matrix4x4(string(child.attribute("value").value())).block<3,4>(0,0)), t);
		}
	}
	return t;
}

float3 parse_color(pugi::xml_node node) {
	string type = node.name();
	if (type == "spectrum") {
		vector<pair<float, float>> spec = parse_spectrum(node.attribute("value").value());
		if (spec.size() > 1) {
			float3 xyz = integrate_XYZ(spec);
			return xyz_to_rgb(xyz);
		} else if (spec.size() == 1) {
			return float3::Ones();
		} else {
			return float3::Zero();
		}
	} else if (type == "rgb") {
		return parse_vector3(node.attribute("value").value());
	} else if (type == "srgb") {
		float3 srgb = parse_srgb(node.attribute("value").value());
		return srgb_to_rgb(srgb);
	} else if (type == "float") {
		return float3::Constant(stof(node.attribute("value").value()));
	} else {
		throw runtime_error("Unsupported color type: " + type);
		return float3::Zero();
	}
}

Mesh create_mesh(CommandBuffer& commandBuffer, const vector<float3>& vertices, const vector<float3>& normals, const vector<float2>& uvs, const vector<uint32_t>& indices) {
	float area = 0;
	for (int ii = 0; ii < indices.size(); ii+=3) {
		const float3 v0 = vertices[indices[ii]];
		const float3 v1 = vertices[indices[ii + 1]];
		const float3 v2 = vertices[indices[ii + 2]];
		area += (v2 - v0).matrix().cross((v1 - v0).matrix()).norm();
	}

	vk::BufferUsageFlags bufferUsage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer;
#ifdef VK_KHR_buffer_device_address
	bufferUsage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
	bufferUsage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
#endif

	Buffer::View<float3> positions_tmp = make_shared<Buffer>(commandBuffer.mDevice, "positions_tmp", vertices.size() * sizeof(float3), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	Buffer::View<float3> normals_tmp = make_shared<Buffer>(commandBuffer.mDevice, "normals_tmp", normals.size() * sizeof(float3), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	Buffer::View<float2> texcoords_tmp = make_shared<Buffer>(commandBuffer.mDevice, "texcoords_tmp", uvs.size() * sizeof(float2), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	Buffer::View<uint32_t> indices_tmp = make_shared<Buffer>(commandBuffer.mDevice, "tmp indices", indices.size() * sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memcpy(positions_tmp.data(), vertices.data(), positions_tmp.size_bytes());
	memcpy(normals_tmp.data(), vertices.data(), normals_tmp.size_bytes());
	memcpy(texcoords_tmp.data(), vertices.data(), texcoords_tmp.size_bytes());
	memcpy(indices_tmp.data(), indices.data(), indices_tmp.size_bytes());

	Buffer::View<float3> positions_buf = make_shared<Buffer>(commandBuffer.mDevice, "positions", positions_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	Buffer::View<float3> normals_buf = make_shared<Buffer>(commandBuffer.mDevice, "normals", normals_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	Buffer::View<float2> texcoords_buf = make_shared<Buffer>(commandBuffer.mDevice, "texcoords", texcoords_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	Buffer::View<uint32_t> indices_buf = make_shared<Buffer>(commandBuffer.mDevice, "indices", indices_tmp.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eIndexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	commandBuffer.copy_buffer(positions_tmp, positions_buf);
	commandBuffer.copy_buffer(normals_tmp, normals_buf);
	commandBuffer.copy_buffer(texcoords_tmp, texcoords_buf);
	commandBuffer.copy_buffer(indices_tmp, indices_buf);

	unordered_map<VertexArrayObject::AttributeType, vector<VertexArrayObject::Attribute>> attributes;
	attributes[VertexArrayObject::AttributeType::ePosition].emplace_back(VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float3), vk::Format::eR32G32B32Sfloat, 0, vk::VertexInputRate::eVertex }, positions_buf);
	attributes[VertexArrayObject::AttributeType::eNormal].emplace_back(VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float3), vk::Format::eR32G32B32Sfloat, 0, vk::VertexInputRate::eVertex }, normals_buf);
	attributes[VertexArrayObject::AttributeType::eTexcoord].emplace_back(VertexArrayObject::AttributeDescription{ (uint32_t)sizeof(float2), vk::Format::eR32G32Sfloat, 0, vk::VertexInputRate::eVertex }, texcoords_buf);
	return Mesh(make_shared<VertexArrayObject>(attributes), indices_buf, vk::PrimitiveTopology::eTriangleList, area);
}

Image::View parse_texture(CommandBuffer& commandBuffer, pugi::xml_node node) {
	string type = node.attribute("type").value();
	fs::path filename;
	float3 color0 = float3::Constant(0.4f);
	float3 color1 = float3::Constant(0.2f);
	float uscale = 1;
	float vscale = 1;
	float uoffset = 0;
	float voffset = 0;
	for (auto child : node.children()) {
		string name = child.attribute("name").value();
		if (name == "filename") {
			filename = child.attribute("value").value();
		} else if (name == "color0") {
			color0 = parse_color(child);
		} else if (name == "color1") {
			color1 = parse_color(child);
		} else if (name == "uvscale") {
			uscale = vscale = stof(child.attribute("value").value());
		} else if (name == "uscale") {
			uscale = stof(child.attribute("value").value());
		} else if (name == "vscale") {
			vscale = stof(child.attribute("value").value());
		} else if (name == "uoffset") {
			uoffset = stof(child.attribute("value").value());
		} else if (name == "voffset") {
			voffset = stof(child.attribute("value").value());
		}
	}
	if (type == "bitmap") {
		ImageData pixels = load_image_data(commandBuffer.mDevice, filename, 0, 4);
		return make_shared<Image>(commandBuffer, filename.stem().string(), pixels, 0, vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage);
	} else if (type == "checkerboard") {
		ImageData pixels;
		pixels.extent = vk::Extent3D(512, 512, 1);
		pixels.pixels = Buffer::TexelView{
			make_shared<Buffer>(commandBuffer.mDevice, "checkerboard pixels", pixels.extent.width*pixels.extent.height*4, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU),
			vk::Format::eR8G8B8A8Unorm };
		for (uint32_t y = 0; y < pixels.extent.height; y++)
			for (uint32_t x = 0; x < pixels.extent.width; x++) {
				const float u = uoffset + uscale * (x + 0.5f) / (float)pixels.extent.width;
				const float v = voffset + vscale * (y + 0.5f) / (float)pixels.extent.height;
				const float3 c = fmodf(fmodf(floorf(u/2), 2.f) + fmodf(floorf(v/2), 2.f), 2.f) < 1 ? color0 : color1;
				const size_t addr = 4*(y*pixels.extent.width + x);
				pixels.pixels[addr+0] = (byte)(c[0] * 0xFF);
				pixels.pixels[addr+1] = (byte)(c[1] * 0xFF);
				pixels.pixels[addr+2] = (byte)(c[2] * 0xFF);
				pixels.pixels[addr+3] = (byte)(0xFF);
			}
		return make_shared<Image>(commandBuffer, "checkerboard", pixels, 0, vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage);
	}
	throw runtime_error("Unsupported texture type: " + type + " for " + node.attribute("name").value());
}

ImageValue3 parse_spectrum_texture(CommandBuffer& commandBuffer, pugi::xml_node node, unordered_map<string /* name id */, Image::View>& texture_map) {
	string type = node.name();
	if (type == "spectrum") {
		vector<pair<float, float>> spec =
			parse_spectrum(node.attribute("value").value());
		if (spec.size() > 1) {
			float3 xyz = integrate_XYZ(spec);
			return make_image_value3({}, xyz_to_rgb(xyz));
		} else if (spec.size() == 1) {
			return make_image_value3({}, float3::Ones());
		} else {
			return make_image_value3({}, float3::Zero());
		}
	} else if (type == "rgb") {
		return make_image_value3({}, parse_vector3(node.attribute("value").value()));
	} else if (type == "srgb") {
		float3 srgb = parse_srgb(node.attribute("value").value());
		return make_image_value3({}, srgb_to_rgb(srgb));
	} else if (type == "ref") {
		// referencing a texture
		string ref_id = node.attribute("id").value();
		auto t_it = texture_map.find(ref_id);
		if (t_it == texture_map.end()) {
			throw runtime_error("Texture not found: " + ref_id);
		}
		return make_image_value3(t_it->second);
	} else if (type == "texture") {
		Image::View t = parse_texture(commandBuffer, node);
		if (!node.attribute("id").empty()) {
			string id = node.attribute("id").value();
			if (texture_map.find(id) != texture_map.end()) throw runtime_error("Duplicate texture ID: " + id);
			texture_map.emplace(id, t);
		}
		return make_image_value3(t);
	}

	throw runtime_error("Unsupported spectrum texture type: " + type);
}

ImageValue1 parse_float_texture(CommandBuffer& commandBuffer, pugi::xml_node node, unordered_map<string /* name id */, Image::View>& texture_map) {
	string type = node.name();
	if (type == "ref") {
		// referencing a texture
		string ref_id = node.attribute("id").value();
		auto t_it = texture_map.find(ref_id);
		if (t_it == texture_map.end()) throw runtime_error("Texture not found: " + ref_id);
		return make_image_value1(t_it->second);
	} else if (type == "float") {
		return make_image_value1({}, stof(node.attribute("value").value()));
	} else if (type == "texture") {
		Image::View t = parse_texture(commandBuffer, node);
		if (!node.attribute("id").empty()) {
			string id = node.attribute("id").value();
			if (texture_map.find(id) != texture_map.end()) throw runtime_error("Duplicate texture ID: " + id);
			texture_map.emplace(id, t);
		}
		return make_image_value1(t);
	}

	throw runtime_error("Unsupported float texture type: " + type);
}

component_ptr<Material> parse_bsdf(Node& dst, CommandBuffer& commandBuffer, pugi::xml_node node, unordered_map<string /* name id */, component_ptr<Material>>& material_map, unordered_map<string /* name id */, Image::View>& texture_map) {
	string type = node.attribute("type").value();
	unordered_set<string> ids;
	if (!node.attribute("id").empty()) ids.emplace(node.attribute("id").value());
	while (type == "twosided" || type == "bumpmap" || type == "mask") {
		if (node.child("bsdf").empty()) throw runtime_error(type + " has no child BSDF");
		node = node.child("bsdf");
		type = node.attribute("type").value();
		if (!node.attribute("id").empty()) ids.emplace(node.attribute("id").value());
	}

	string name = !ids.empty() ? *ids.begin() : (type + " BSDF");

	if (type == "diffuse") {
		ImageValue3 diffuse = make_image_value3({}, float3::Constant(0.5f));
		for (auto child : node.children()) {
			string name = child.attribute("name").value();
			if (name == "reflectance")
				diffuse = parse_spectrum_texture(commandBuffer, child, texture_map);
		}
		auto m = dst.make_child(name).make_component<Material>();
		m->diffuse_roughness = make_image_value4(diffuse.image, float4(diffuse.value[0], diffuse.value[1], diffuse.value[2], 1.f));
		m->specular_transmission = make_image_value4({}, float4::Zero());
		m->emission = make_image_value3({}, float3::Zero());
		m->eta = 1.5f;
		for (const string& id : ids)
			if (!id.empty()) material_map[id] = m;
		return m;
	} else if (type == "roughplastic" || type == "plastic" || type == "conductor" || type == "roughconductor") {
		ImageValue3 diffuse  = make_image_value3({}, float3::Constant(0.5f));
		ImageValue3 specular = make_image_value3({}, float3::Ones());
		ImageValue1 roughness = make_image_value1({}, (type == "plastic") ? 0 : 0.1f);

		float intIOR = 1.49;
		float extIOR = 1.000277;
		float eta = intIOR / extIOR;
		for (auto child : node.children()) {
			string name = child.attribute("name").value();
			if (name == "diffuseReflectance") {
				diffuse = parse_spectrum_texture(commandBuffer, child, texture_map);
			} else if (name == "specularReflectance") {
				specular = parse_spectrum_texture(commandBuffer, child, texture_map);
			} else if (name == "alpha") {
				// Alpha requires special treatment since we need to convert
				// the values to roughness
				string type = child.name();
				if (type == "ref") {
					// referencing a texture
					string ref_id = child.attribute("id").value();
					auto t_it = texture_map.find(ref_id);
					if (t_it == texture_map.end()) throw runtime_error("Texture not found: " + ref_id);
					roughness = dst.find_in_ancestor<Scene>()->alpha_to_roughness(commandBuffer, make_image_value1(t_it->second, 1));
				} else if (type == "float") {
					float alpha = stof(child.attribute("value").value());
					roughness.value = sqrt(alpha);
				} else
					throw runtime_error("Unsupported float texture type: " + type);
			} else if (name == "roughness") {
				roughness = parse_float_texture(commandBuffer, child, texture_map);
			} else if (name == "intIOR") {
				intIOR = stof(child.attribute("value").value());
				eta = intIOR / extIOR;
			} else if (name == "extIOR") {
				extIOR = stof(child.attribute("value").value());
				eta = intIOR / extIOR;
			}
		}
		auto m = dst.make_child(name).make_component<Material>(dst.find_in_ancestor<Scene>()->make_diffuse_specular_material(commandBuffer, diffuse, specular, roughness, make_image_value3({},float3::Zero()), eta, make_image_value3({},float3::Zero())));
		for (const string& id : ids)
			if (!id.empty()) material_map[id] = m;
		return m;
	} else if (type == "roughdielectric" || type == "dielectric" || type == "thindielectric") {
		ImageValue3 diffuse = make_image_value3({}, float3::Ones());
		ImageValue3 specular = make_image_value3({}, float3::Ones());
		ImageValue3 transmittance = make_image_value3({}, float3::Ones());
		ImageValue1 roughness = make_image_value1({}, (type == "dielectric") ? 0 : 0.1f);
		float intIOR = 1.5046;
		float extIOR = 1.000277;
		float eta = intIOR / extIOR;
		for (auto child : node.children()) {
			string name = child.attribute("name").value();
			if (name == "specularReflectance") {
				specular = parse_spectrum_texture(commandBuffer, child, texture_map);
			} else if (name == "specularTransmittance") {
				transmittance = parse_spectrum_texture(commandBuffer, child, texture_map);
			} else if (name == "alpha") {
				string type = child.name();
				if (type == "ref") {
					// referencing a texture
					string ref_id = child.attribute("id").value();
					auto t_it = texture_map.find(ref_id);
					if (t_it == texture_map.end()) throw runtime_error("Texture not found: " + ref_id);
					roughness.image = dst.find_in_ancestor<Scene>()->alpha_to_roughness(commandBuffer, make_image_value1(t_it->second)).image;
				} else if (type == "float") {
					roughness.value = sqrt(stof(child.attribute("value").value()));
				} else
					throw runtime_error("Unsupported float texture type: " + type);
			} else if (name == "roughness") {
				roughness = parse_float_texture(commandBuffer, child, texture_map);
			} else if (name == "intIOR") {
				intIOR = stof(child.attribute("value").value());
				eta = intIOR / extIOR;
			} else if (name == "extIOR") {
				extIOR = stof(child.attribute("value").value());
				eta = intIOR / extIOR;
			}
		}
		auto m = dst.make_child(name).make_component<Material>(dst.find_in_ancestor<Scene>()->make_diffuse_specular_material(commandBuffer, diffuse, specular, roughness, transmittance, eta, make_image_value3({},float3::Zero())));
		for (const string& id : ids)
			if (!id.empty()) material_map[id] = m;
		return m;
	}
	string idstr;
	for (const string& id : ids)
		idstr += id + " ";
	throw runtime_error("Unsupported BSDF type: \"" + type + "\" with IDs " + idstr);
}

void parse_shape(CommandBuffer& commandBuffer, Node& dst, pugi::xml_node node, unordered_map<string, component_ptr<Material>>& material_map, unordered_map<string, Image::View>& texture_map, unordered_map<string, component_ptr<Mesh>>& obj_map) {
	component_ptr<Material> material;
	string filename;
	int shape_index = -1;

	for (auto child : node.children()) {
		const string name = child.name();
		if (name == "ref") {
			const string name_value = child.attribute("name").value();
			pugi::xml_attribute id = child.attribute("id");
			if (id.empty()) throw runtime_error("Material reference id not specified.");
			auto it = material_map.find(id.value());
			if (it == material_map.end()) throw runtime_error("Material reference " + string(id.value()) + " not found.");
			if (!material)
				material = it->second;
		} else if (name == "bsdf") {
			optional<ImageValue3> emission;
			if (material) emission = material->emission;
			material = parse_bsdf(dst, commandBuffer, child, material_map, texture_map);
			if (emission) material->emission = *emission;
		} else if (name == "emitter") {
			float3 radiance = float3::Ones();
			for (auto grand_child : child.children()) {
				string name = grand_child.attribute("name").value();
				if (name == "radiance") {
					string rad_type = grand_child.name();
					if (rad_type == "spectrum") {
						vector<pair<float, float>> spec = parse_spectrum(grand_child.attribute("value").value());
						if (spec.size() == 1) {
							// For a light source, the white point is
							// XYZ(0.9505, 1.0, 1.0888) instead
							// or XYZ(1, 1, 1). We need to handle this special case when
							// we don't have the full spectrum data.
							const float3 xyz = float3(0.9505f, 1.0f, 1.0888f);
							radiance = xyz_to_rgb(xyz * spec[0].second);
						} else {
							const float3 xyz = integrate_XYZ(spec);
							radiance = xyz_to_rgb(xyz);
						}
					} else if (rad_type == "rgb") {
						radiance = parse_vector3(grand_child.attribute("value").value());
					} else if (rad_type == "srgb") {
						const string value = grand_child.attribute("value").value();
						const float3 srgb = parse_srgb(value);
						radiance = srgb_to_rgb(srgb);
					}
				}
			}
			if (material)
				material->emission = make_image_value3({}, radiance);
			else {
				Material m;
				m.diffuse_roughness = make_image_value4({}, float4::Zero());
				m.specular_transmission = make_image_value4({}, float4::Zero());
				m.emission = make_image_value3({}, radiance);
				m.eta = 1.5f;
				material = dst.make_component<Material>(m);
			}
		}

		const string name_attrib = child.attribute("name").value();
		if (name == "string" && name_attrib == "filename") {
			filename = child.attribute("value").value();
		} else if (name == "transform" && name_attrib == "toWorld") {
			dst.make_component<TransformData>(parse_transform(child));
		} if (name == "integer" && name_attrib == "shapeIndex") {
			shape_index = stoi(child.attribute("value").value());
		}
	}

	string type = node.attribute("type").value();
	if (type == "obj") {
		component_ptr<Mesh> m;
		if (auto m_it = obj_map.find(filename); m_it == obj_map.end()) {
			m = dst.make_component<Mesh>(load_obj(commandBuffer, filename));
			obj_map.emplace(filename, m);
		} else {
			m = m_it->second;
		}
		dst.make_component<MeshPrimitive>(material, m);
	} else if (type == "serialized") {
		dst.make_component<MeshPrimitive>(material, dst.make_component<Mesh>(load_serialized(commandBuffer, filename, shape_index)));
	} else if (type == "sphere") {
		float3 center{ 0, 0, 0 };
		float radius = 1;
		for (auto child : node.children()) {
			const string name = child.attribute("name").value();
			if (name == "center") {
				center = float3{
						stof(child.attribute("x").value()),
						stof(child.attribute("y").value()),
						stof(child.attribute("z").value()) };
			} else if (name == "radius") {
				radius = stof(child.attribute("value").value());
			}
		}
		if (!center.isZero()) {
			if (auto ptr = dst.find<TransformData>(); ptr)
				*ptr = tmul(*ptr, make_transform(center, quatf_identity(), float3::Ones()));
			else
				dst.make_component<TransformData>(make_transform(center, quatf_identity(), float3::Ones()));
		}
		dst.make_component<SpherePrimitive>(material, radius);
	} else if (type == "rectangle") {
		const vector<float3> vertices = { float3(-1,-1,0), float3(-1,1,0), float3(1,-1,0), float3(1,1,0) };
		const vector<float3> normals = { float3(0,0,1), float3(0,0,1), float3(0,0,1), float3(0,0,1) };
		const vector<float2> uvs = { float2(0,0), float2(0,1), float2(1,0), float2(1,1) };
		const vector<uint32_t> indices = { 0, 1, 2, 1, 3, 2 };
		dst.make_component<MeshPrimitive>(material, dst.make_component<Mesh>(create_mesh(commandBuffer, vertices, normals, uvs, indices)));
	}/* else if (type == "cube") {
		vector<float3> vertices(16);
		vector<float3> normals(16);
		vector<float2> uvs(16);
		vector<uint32_t> indices(36);
		for (uint32_t face = 0; face < 6; face++) {
			const uint32_t i = face*4;
			const int s = face%2 == 0 ? 1 : -1;

			float3 n = float3::Zero();
			n[face/2] = s;

			float3 v0 = n;
			float3 v1 = n;
			float3 v2 = n;
			float3 v3 = n;
			v0[(face/2 + 1) % 3] = -s;
			v0[(face/2 + 2) % 3] = -s;
			v1[(face/2 + 1) % 3] = s;
			v1[(face/2 + 2) % 3] = -s;
			v2[(face/2 + 1) % 3] = -s;
			v2[(face/2 + 2) % 3] = s;
			v3[(face/2 + 1) % 3] = s;
			v3[(face/2 + 2) % 3] = s;

			vertices[i+0] = v0;
			vertices[i+1] = v1;
			vertices[i+2] = v2;
			vertices[i+3] = v3;
			normals[i+0] = n;
			normals[i+1] = n;
			normals[i+2] = n;
			normals[i+3] = n;
			uvs[i+0] = float2(0,0);
			uvs[i+1] = float2(1,0);
			uvs[i+2] = float2(0,1);
			uvs[i+3] = float2(1,1);

			indices[face*6 + 0] = i + 0;
			indices[face*6 + 1] = i + 1;
			indices[face*6 + 2] = i + 2;
			indices[face*6 + 3] = i + 1;
			indices[face*6 + 4] = i + 3;
			indices[face*6 + 5] = i + 2;
		}
		dst.make_component<MeshPrimitive>(material, dst.make_component<Mesh>(create_mesh(commandBuffer, vertices, normals, uvs, indices)));
	}*/
	else throw runtime_error("Unsupported shape: " + type);
}

void parse_scene(Node& root, CommandBuffer& commandBuffer, pugi::xml_node node) {
	unordered_map<string /* name id */, component_ptr<Material>> material_map;
	unordered_map<string /* name id */, Image::View> texture_map;
	unordered_map<string /* filename */, component_ptr<Mesh>> obj_map;
	int envmap_light_id = -1;
	for (auto child : node.children()) {
		string name = child.name();
		if (name == "bsdf") {
			parse_bsdf(root, commandBuffer, child, material_map, texture_map);
		} else if (name == "shape") {
			parse_shape(commandBuffer, root.make_child("shape"), child, material_map, texture_map, obj_map);
		} else if (name == "texture") {
			string id = child.attribute("id").value();
			if (texture_map.find(id) != texture_map.end()) throw runtime_error("Duplicate texture ID: " + id);
			texture_map[id] = parse_texture(commandBuffer, child);
		} else if (name == "emitter") {
			string type = child.attribute("type").value();
			if (type == "envmap") {
				Node& n = root.make_child("shape");
				string filename;
				float scale = 1;
				for (auto grand_child : child.children()) {
					string name = grand_child.attribute("name").value();
					if (name == "filename") {
						filename = grand_child.attribute("value").value();
					} else if (name == "toWorld") {
						n.make_component<TransformData>(parse_transform(grand_child));
					} else if (name == "scale") {
						scale = stof(grand_child.attribute("value").value());
					}
				}
				if (filename.size() > 0) {
					Environment e = load_environment(commandBuffer, filename);
					e.emission.value *= scale;
					n.make_component<Environment>(e);
				} else {
					throw runtime_error("Filename unspecified for envmap.");
				}
			} else {
				throw runtime_error("Unsupported emitter type:" + type);
			}
		}
	}
}

void Scene::load_mitsuba(Node& root, CommandBuffer& commandBuffer, const fs::path& filename) {
	ProfilerRegion ps("load_mitsuba", commandBuffer);

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	if (!result) {
		cerr << "Error description: " << result.description() << endl;
		cerr << "Error offset: " << result.offset << endl;
		throw runtime_error("Parse error");
	}
	// back up the current working directory and switch to the parent folder of the file
	fs::path old_path = fs::current_path();
	fs::current_path(filename.parent_path());

	parse_scene(root, commandBuffer, doc.child("scene"));

	// switch back to the old current working directory
	fs::current_path(old_path);

	cout << "Loaded " << filename << endl;
}

}