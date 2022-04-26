#include "../Scene.hpp"
#include <Core/PipelineState.hpp>

#include <extern/pugixml.hpp>
#include <regex>

using namespace stm::hlsl;
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
			throw runtime_error(string("Invalid SRGB value: ") + value);
		}
		srgb[0] = ((encoded & 0xFF0000) >> 16) / 255.0f;
		srgb[1] = ((encoded & 0x00FF00) >> 8) / 255.0f;
		srgb[2] = (encoded & 0x0000FF) / 255.0f;
	} else {
		throw runtime_error(string("Unknown SRGB format: ") + value);
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

Matrix4f parse_matrix4x4(const string& value) {
	vector<string> list = split_string(value, regex("(,| )+"));
	if (list.size() != 16)
		throw runtime_error("parse_matrix4x4 failed");

	Matrix4f m;
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

ImageValue3 parse_spectrum_texture(pugi::xml_node node, const map<string /* name id */, Image::View>& texture_map) {
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
			throw runtime_error(string("Texture not found. ID = ") + ref_id);
		}
		return make_image_value3(t_it->second);
	} else {
		throw runtime_error(string("Unknown spectrum texture type:") + type);
		return make_image_value3({}, float3::Zero());
	}
}

ImageValue1 parse_float_texture(pugi::xml_node node, const map<string /* name id */, Image::View>& texture_map) {
	string type = node.name();
	if (type == "ref") {
		// referencing a texture
		string ref_id = node.attribute("id").value();
		auto t_it = texture_map.find(ref_id);
		if (t_it == texture_map.end()) {
			throw runtime_error(string("Texture not found. ID = ") + ref_id);
		}
		return make_image_value1(t_it->second);
	} else if (type == "float") {
		return make_image_value1({}, stof(node.attribute("value").value()));
	} else {
		throw runtime_error(string("Unknown float texture type:") + type);
		return make_image_value1({}, 0);
	}
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
		throw runtime_error(string("Unknown color type:") + type);
		return float3::Zero();
	}
}

Image::View alpha_to_roughness(Node& n, CommandBuffer& commandBuffer, const Image::View& alpha) {

	Image::View roughness = make_shared<Image>(commandBuffer.mDevice, "roughness", alpha.extent(), alpha.image()->format(), alpha.image()->layer_count(), alpha.image()->level_count(), alpha.image()->sample_count(), vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage);

	const ShaderDatabase& shaders = *n.node_graph().find_components<ShaderDatabase>().front();
	auto p = make_shared<ComputePipelineState>("material_convert_alpha_to_roughness", shaders.at("material_convert_alpha_to_roughness"));

	p->descriptor("gInput")  = image_descriptor(alpha, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
	p->descriptor("gRoughness") = image_descriptor(roughness, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
	commandBuffer.bind_pipeline(p->get_pipeline());
	p->bind_descriptor_sets(commandBuffer);
	p->push_constants(commandBuffer);
	commandBuffer.dispatch_over(alpha.extent());
	roughness.image()->generate_mip_maps(commandBuffer);
	return roughness;
}

tuple<string /* ID */, component_ptr<Material>> parse_bsdf(Node& dst, CommandBuffer& commandBuffer, pugi::xml_node node, const map<string /* name id */, Image::View>& texture_map) {
	string type = node.attribute("type").value();
	string id;
	if (!node.attribute("id").empty()) {
		id = node.attribute("id").value();
	}
	if (type == "diffuse") {
		Lambertian l;
		l.reflectance = make_image_value3({}, float3::Constant(0.5f));
		for (auto child : node.children()) {
			string name = child.attribute("name").value();
			if (name == "reflectance")
				l.reflectance = parse_spectrum_texture(child, texture_map);
		}
		return make_tuple(id,  dst.make_component<Material>(l));
	} else if (type == "roughplastic" || type == "plastic") {
		RoughPlastic r;
		r.diffuse_reflectance  = make_image_value3({}, float3::Constant(0.5f));
		r.specular_reflectance = make_image_value3({}, float3::Ones());
		r.roughness = make_image_value1({}, 0.1f);
		if (type == "plastic") {
			// Approximate plastic materials with very small roughness
			r.roughness = make_image_value1({}, 0.01f);
		}
		float intIOR = 1.49;
		float extIOR = 1.000277;
		for (auto child : node.children()) {
			string name = child.attribute("name").value();
			if (name == "diffuseReflectance") {
				r.diffuse_reflectance = parse_spectrum_texture(child, texture_map);
			} else if (name == "specularReflectance") {
				r.specular_reflectance = parse_spectrum_texture(child, texture_map);
			} else if (name == "alpha") {
				// Alpha requires special treatment since we need to convert
				// the values to roughness
				string type = child.name();
				if (type == "ref") {
					// referencing a texture
					string ref_id = child.attribute("id").value();
					auto t_it = texture_map.find(ref_id);
					if (t_it == texture_map.end()) {
						throw runtime_error(string("Texture not found. ID = ") + ref_id);
					}
					r.roughness = make_image_value1(alpha_to_roughness(dst, commandBuffer, t_it->second));
				} else if (type == "float") {
					float alpha = stof(child.attribute("value").value());
					r.roughness = make_image_value1({}, sqrt(alpha));
				} else {
					throw runtime_error(string("Unknown float texture type:") + type);
				}
			} else if (name == "roughness") {
				r.roughness = parse_float_texture(child, texture_map);
			} else if (name == "intIOR") {
				intIOR = stof(child.attribute("value").value());
			} else if (name == "extIOR") {
				extIOR = stof(child.attribute("value").value());
			}
		}
		r.eta = intIOR / extIOR;
		return make_tuple(id, dst.make_component<Material>(r));
	} else if (type == "roughdielectric" || type == "dielectric") {
		RoughDielectric r;
		r.specular_reflectance = make_image_value3({}, float3::Ones());
		r.specular_transmittance = make_image_value3({}, float3::Ones());
		r.roughness = make_image_value1({}, 0.1f);
		if (type == "dielectric") {
			// Approximate plastic materials with very small roughness
			r.roughness = make_image_value1({}, 0.01f);
		}
		float intIOR = 1.5046;
		float extIOR = 1.000277;
		for (auto child : node.children()) {
			string name = child.attribute("name").value();
			if (name == "specularReflectance") {
				r.specular_reflectance = parse_spectrum_texture(child, texture_map);
			} else if (name == "specularTransmittance") {
				r.specular_transmittance = parse_spectrum_texture(child, texture_map);
			} else if (name == "alpha") {
				string type = child.name();
				if (type == "ref") {
					// referencing a texture
					string ref_id = child.attribute("id").value();
					auto t_it = texture_map.find(ref_id);
					if (t_it == texture_map.end())
						throw runtime_error(string("Texture not found. ID = ") + ref_id);
					r.roughness = make_image_value1(alpha_to_roughness(dst, commandBuffer, t_it->second));
				} else if (type == "float") {
					const float alpha = stof(child.attribute("value").value());
					r.roughness = make_image_value1({}, sqrt(alpha));
				} else {
					throw runtime_error(string("Unknown float texture type:") + type);
				}
			} else if (name == "roughness") {
				r.roughness = parse_float_texture(child, texture_map);
			} else if (name == "intIOR") {
				intIOR = stof(child.attribute("value").value());
			} else if (name == "extIOR") {
				extIOR = stof(child.attribute("value").value());
			}
		}
		r.eta = intIOR / extIOR;
		return make_tuple(id, dst.make_component<Material>(r));
	}/* else if (type == "disneydiffuse") {
			ImageValue3 base_color = make_image_value3(0.5f);
			ImageValue1 roughness = make_image_value1(float(0.5));
			ImageValue1 subsurface = make_image_value1(float(0));
			for (auto child : node.children()) {
					string name = child.attribute("name").value();
					if (name == "baseColor") {
							base_color = parse_spectrum_texture(child, texture_map);
					} else if (name == "roughness") {
							roughness = parse_float_texture(child, texture_map);
					} else if (name == "subsurface") {
							subsurface = parse_float_texture(child, texture_map);
					}
			}
			return make_tuple(id, DisneyDiffuse{
					base_color, roughness, subsurface});
	} else if (type == "disneymetal") {
			ImageValue3 base_color = make_image_value3(0.5f);
			ImageValue1 roughness = make_image_value1(0.5f);
			ImageValue1 anisotropic = make_image_value1(0.0f);
			for (auto child : node.children()) {
					string name = child.attribute("name").value();
					if (name == "baseColor") {
							base_color = parse_spectrum_texture(child, texture_map);
					} else if (name == "roughness") {
							roughness = parse_float_texture(child, texture_map);
					} else if (name == "anisotropic") {
							anisotropic = parse_float_texture(child, texture_map);
					}
			}
			return make_tuple(id, DisneyMetal{base_color, roughness, anisotropic});
	} else if (type == "disneyglass") {
			ImageValue3 base_color = make_image_value3(0.5f);
			ImageValue1 roughness = make_image_value1(0.5f);
			ImageValue1 anisotropic = make_image_value1(0f);
			float eta = float(1.5);
			for (auto child : node.children()) {
					string name = child.attribute("name").value();
					if (name == "baseColor") {
							base_color = parse_spectrum_texture(child, texture_map);
					} else if (name == "roughness") {
							roughness = parse_float_texture(child, texture_map);
					} else if (name == "anisotropic") {
							anisotropic = parse_float_texture(child, texture_map);
					} else if (name == "eta") {
							eta = stof(child.attribute("value").value());
					}
			}
			return make_tuple(id, DisneyGlass{base_color, roughness, anisotropic, eta});
	} else if (type == "disneyclearcoat") {
			ImageValue1 clearcoat_gloss = make_image_value1(1);
			for (auto child : node.children()) {
					string name = child.attribute("name").value();
					if (name == "clearcoatGloss") {
							clearcoat_gloss = parse_float_texture(child, texture_map);
					}
			}
			return make_tuple(id, DisneyClearcoat{clearcoat_gloss});
	} else if (type == "disneysheen") {
			ImageValue3 base_color = make_image_value3(0.5f);
			ImageValue1 sheen_tint = make_image_value1(0.5f);
			for (auto child : node.children()) {
					string name = child.attribute("name").value();
					if (name == "baseColor") {
							base_color = parse_spectrum_texture(child, texture_map);
					} else if (name == "sheenTint") {
							sheen_tint = parse_float_texture(child, texture_map);
					}
			}
			return make_tuple(id, DisneySheen{base_color, sheen_tint});
	} else if (type == "disneybsdf") {
			ImageValue3 base_color = make_image_value3(0.5f);
			ImageValue1 specular_transmission = make_image_value1(0);
			ImageValue1 metallic = make_image_value1(0);
			ImageValue1 subsurface = make_image_value1(0);
			ImageValue1 specular = make_image_value1(0.5);
			ImageValue1 roughness = make_image_value1(0.5);
			ImageValue1 specular_tint = make_image_value1(0);
			ImageValue1 anisotropic = make_image_value1(0);
			ImageValue1 sheen = make_image_value1(0);
			ImageValue1 sheen_tint = make_image_value1(0.5);
			ImageValue1 clearcoat = make_image_value1(0);
			ImageValue1 clearcoat_gloss = make_image_value1(1);
			float eta = float(1.5);
			for (auto child : node.children()) {
					string name = child.attribute("name").value();
					if (name == "baseColor") {
							base_color = parse_spectrum_texture(child, texture_map);
					} else if (name == "specularTransmission") {
							specular_transmission = parse_float_texture(child, texture_map);
					} else if (name == "metallic") {
							metallic = parse_float_texture(child, texture_map);
					} else if (name == "subsurface") {
							subsurface = parse_float_texture(child, texture_map);
					} else if (name == "specular") {
							specular = parse_float_texture(child, texture_map);
					} else if (name == "roughness") {
							roughness = parse_float_texture(child, texture_map);
					} else if (name == "specularTint") {
							specular_tint = parse_float_texture(child, texture_map);
					} else if (name == "anisotropic") {
							anisotropic = parse_float_texture(child, texture_map);
					} else if (name == "sheen") {
							sheen = parse_float_texture(child, texture_map);
					} else if (name == "sheenTint") {
							sheen_tint = parse_float_texture(child, texture_map);
					} else if (name == "clearcoat") {
							clearcoat = parse_float_texture(child, texture_map);
					} else if (name == "clearcoatGloss") {
							clearcoat_gloss = parse_float_texture(child, texture_map);
					} else if (name == "eta") {
							eta = stof(child.attribute("value").value());
					}
			}
			return make_tuple(id, DisneyBSDF{base_color,
																						specular_transmission,
																						metallic,
																						subsurface,
																						specular,
																						roughness,
																						specular_tint,
																						anisotropic,
																						sheen,
																						sheen_tint,
																						clearcoat,
																						clearcoat_gloss,
																						eta});
	}*/ else {
		throw runtime_error(string("Unknown BSDF: ") + type);
	}
	return {};
}

void parse_shape(CommandBuffer& commandBuffer, Node& dst, pugi::xml_node node,
	map<string /* name id */, component_ptr<Material>>& material_map,
	const map<string /* name id */, Image::View>& texture_map) {
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
			if (it == material_map.end()) throw runtime_error(string("Material reference ") + id.value() + string(" not found."));
			if (!material)
				material = it->second;
		} else if (name == "bsdf") {
			string material_name;
			component_ptr<Material> m;
			tie(material_name, m) = parse_bsdf(dst.make_child("BSDF"), commandBuffer, child, texture_map);
			if (!material_name.empty())
				material_map[material_name] = m;
			if (!material)
				material = m;
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
			Emissive e;
			e.emission = make_image_value3({}, radiance);
			if (material)
				*material = e;
			else
				material = dst.make_component<Material>(e);
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
		dst.make_component<MeshPrimitive>(material, dst.make_component<Mesh>(load_obj(commandBuffer, filename)));
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
	} else {
		throw runtime_error(string("Unknown shape:") + type);
	}
}

Image::View parse_texture(CommandBuffer& commandBuffer, pugi::xml_node node) {
	string type = node.attribute("type").value();
	if (type == "bitmap") {
		fs::path filename;
		float uscale = 1;
		float vscale = 1;
		float uoffset = 0;
		float voffset = 0;
		for (auto child : node.children()) {
			string name = child.attribute("name").value();
			if (name == "filename") {
				filename = child.attribute("value").value();
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
		ImageData pixels = load_image_data(commandBuffer.mDevice, filename, 0, 4);
		return make_shared<Image>(commandBuffer, filename.stem().string(), pixels);
	} else if (type == "checkerboard") {
		float3 color0 = float3::Constant(0.4f);
		float3 color1 = float3::Constant(0.2f);
		float uscale = 1;
		float vscale = 1;
		float uoffset = 0;
		float voffset = 0;
		for (auto child : node.children()) {
			string name = child.attribute("name").value();
			if (name == "color0") {
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
		ImageData pixels;
		pixels.extent = vk::Extent3D(8, 8, 1);
		pixels.pixels = Buffer::TexelView{
			make_shared<Buffer>(commandBuffer.mDevice, "checkerboard pixels", pixels.extent.width*pixels.extent.height*4, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU),
			vk::Format::eR8G8B8A8Unorm };
		for (uint32_t y = 0; y < pixels.extent.height; y++)
			for (uint32_t x = 0; x < pixels.extent.width; x++) {
				const bool f = (uint32_t(x*uscale/4)%2) ^ (uint32_t(y*vscale/4)%2);
				const float3 c = f ? color0 : color1;
				const size_t addr = 4*(y*pixels.extent.width + x);
				pixels.pixels[addr+0] = (byte)(c[0] * 0xFF);
				pixels.pixels[addr+1] = (byte)(c[1] * 0xFF);
				pixels.pixels[addr+2] = (byte)(c[2] * 0xFF);
				pixels.pixels[addr+3] = (byte)(0xFF);
			}
		return make_shared<Image>(commandBuffer, "checkerboard", pixels);
	}
	throw runtime_error(string("Unknown texture type: ") + type);
	return {};
}

void parse_scene(Node& root, CommandBuffer& commandBuffer, pugi::xml_node node) {
	map<string /* name id */, component_ptr<Material>> material_map;
	map<string /* name id */, Image::View> texture_map;
	int envmap_light_id = -1;
	for (auto child : node.children()) {
		string name = child.name();
		if (name == "bsdf") {
			string material_name;
			component_ptr<Material> m;
			Node& n = root.make_child("BSDF");
			tie(material_name, m) = parse_bsdf(n, commandBuffer, child, texture_map);
			if (!material_name.empty())
				material_map[material_name] = m;
		} else if (name == "shape") {
			parse_shape(commandBuffer,
				root.make_child("shape"),
				child,
				material_map,
				texture_map);
		} else if (name == "texture") {
			string id = child.attribute("id").value();
			if (texture_map.find(id) != texture_map.end()) {
				throw runtime_error(string("Duplicated texture ID:") + id);
			}
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
					n.make_component<Material>(e);
				} else {
					throw runtime_error("Filename unspecified for envmap.");
				}
			} else {
				throw runtime_error(string("Unknown emitter type:") + type);
			}
		}
	}
}

void load_mitsuba(Node& root, CommandBuffer& commandBuffer, const fs::path& filename) {
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