#ifndef IMAGE_VALUE_H
#define IMAGE_VALUE_H

#include "common.hlsli"

#ifdef __cplusplus

struct ByteAppendBuffer {
  vector<uint32_t> data;
  
  inline uint Load(uint32_t address) {
    return data[address/4];
  }

	template<typename T, int N>
  inline void AppendN(const MatrixType<T,N,1>& x) {
		for (uint i = 0; i < N; i++)
			data.emplace_back(asuint(x[i]));
  }

  inline void Append(const uint32_t x) {
    data.emplace_back(x);
  }
  inline void Appendf(const float x) {
    data.emplace_back(asuint(x));
  }
};

struct ResourcePool {
	unordered_map<Image::View, uint32_t> images;
	unordered_map<Buffer::View<byte>, uint32_t> volume_data_map;
	unordered_map<Buffer::View<float>, uint32_t> distribution_data_map;
	uint32_t distribution_data_size;

	inline uint32_t get_index(const Image::View& image) {
		if (!image) return ~0u;
		const Image::View tmp = Image::View(image.image(), image.subresource_range());
		auto it = images.find(tmp);
		return (it == images.end()) ? images.emplace(tmp, (uint32_t)images.size()).first->second : it->second;
	}
	inline uint32_t get_index(const Buffer::View<byte>& buf) {
		if (!buf) return ~0u;
		auto it = volume_data_map.find(buf);
		return (it == volume_data_map.end()) ? volume_data_map.emplace(buf, (uint32_t)volume_data_map.size()).first->second : it->second;
	}
	inline uint32_t get_index(const Buffer::View<float>& data) {
		if (!data) return ~0u;
		auto it = distribution_data_map.find(data);
		if (it == distribution_data_map.end()) {
			const uint32_t r = distribution_data_size;
			distribution_data_map.emplace(data, r);
			distribution_data_size += data.size();
			return r;
		} else
			return it->second;
	}
};

inline uint4 channel_mapping_swizzle(vk::ComponentMapping m) {
	uint4 c;
	c[0] = (m.r < vk::ComponentSwizzle::eR) ? 0 : (uint32_t)m.r - (uint32_t)vk::ComponentSwizzle::eR;
	c[1] = (m.g < vk::ComponentSwizzle::eR) ? 1 : (uint32_t)m.g - (uint32_t)vk::ComponentSwizzle::eR;
	c[2] = (m.b < vk::ComponentSwizzle::eR) ? 2 : (uint32_t)m.b - (uint32_t)vk::ComponentSwizzle::eR;
	c[3] = (m.a < vk::ComponentSwizzle::eR) ? 3 : (uint32_t)m.a - (uint32_t)vk::ComponentSwizzle::eR;
	return c;
}

#endif // __cplusplus

struct ImageValue1 {
	#ifdef __HLSL_VERSION
	min16float value;
	uint image_index_and_channel;
	inline uint image_index() { return BF_GET(image_index_and_channel, 0, 30); }
	inline uint channel() { return BF_GET(image_index_and_channel, 30, 2); }
	inline bool has_image() { return image_index() < gImageCount; }
	inline Texture2D<float4> image() { return gImages[NonUniformResourceIndex(image_index())]; }
	#endif
	
	#ifdef __cplusplus
	float value;
	Image::View image;
	inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
		const uint image_index = resources.get_index(image);
		const uint32_t channel = channel_mapping_swizzle(image.components())[0];
		uint image_index_and_channel = 0;
		BF_SET(image_index_and_channel, image_index, 0, 30);
		BF_SET(image_index_and_channel, channel, 30, 2);
		bytes.Append(image_index_and_channel);
		bytes.Appendf(value);
	}
	#endif
};
struct ImageValue2 {
	#ifdef __HLSL_VERSION
	min16float2 value;
	uint image_index_and_channel;
	inline bool has_image() { return BF_GET(image_index_and_channel, 0, 30) < gImageCount; }
	inline Texture2D<float4> image() { return gImages[NonUniformResourceIndex(BF_GET(image_index_and_channel, 0, 30))]; }
	inline uint channel() { return BF_GET(image_index_and_channel, 30, 2); }
	#endif

	#ifdef __cplusplus
	float2 value;
	Image::View image;
	inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
		const uint image_index = resources.get_index(image);
		const uint32_t channel = channel_mapping_swizzle(image.components())[0];
		uint image_index_and_channel = 0;
		BF_SET(image_index_and_channel, image_index, 0, 30);
		BF_SET(image_index_and_channel, channel, 30, 2);
		bytes.Append(image_index_and_channel);
		bytes.AppendN(value);
	}
	#endif
};
struct ImageValue3 {
	#ifdef __HLSL_VERSION
	min16float3 value;
	uint image_index;
	inline bool has_image() { return image_index < gImageCount; }
	inline Texture2D<float4> image() { return gImages[NonUniformResourceIndex(image_index)]; }
	#endif

	#ifdef __cplusplus
	Image::View image;
	float3 value;
	inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
		bytes.Append(resources.get_index(image));
		bytes.AppendN(value);
	}
	#endif
};
struct ImageValue4 {
	#ifdef __HLSL_VERSION
	min16float4 value;
	uint image_index;
	inline bool has_image() { return image_index < gImageCount; }
	inline Texture2D<float4> image() { return gImages[NonUniformResourceIndex(image_index)]; }
	#endif

	#ifdef __cplusplus
	Image::View image;
	float4 value;
	inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
		bytes.Append(resources.get_index(image));
		bytes.AppendN(value);
	}
	#endif
};

#ifdef __cplusplus

inline ImageValue1 make_image_value1(const Image::View& img, const float& v = 1) {
	ImageValue1 r;
	r.value = v;
	r.image = img;
	return r;
}
inline ImageValue2 make_image_value2(const Image::View& img, const float2& v = float2::Ones()) {
	ImageValue2 r;
	r.value = v;
	r.image = img;
	return r;
}
inline ImageValue3 make_image_value3(const Image::View& img, const float3& v = float3::Ones()) {
	ImageValue3 r;
	r.value = v;
	r.image = img;
	return r;
}
inline ImageValue4 make_image_value4(const Image::View& img, const float4& v = float4::Ones()) {
	ImageValue4 r;
	r.value = v;
	r.image = img;
	return r;
}

inline void image_value_field(const char* label, ImageValue1& v) {
	ImGui::DragFloat(label, &v.value, .01f);
	if (v.image) {
		const uint32_t w = ImGui::GetWindowSize().x;
		ImGui::Image(&v.image, ImVec2(w, w*(float)v.image.extent().height/(float)v.image.extent().width));
	}
}
inline void image_value_field(const char* label, ImageValue2& v) {
	ImGui::DragFloat2(label, v.value.data(), .01f);
	if (v.image) {
		const uint32_t w = ImGui::GetWindowSize().x;
		ImGui::Image(&v.image, ImVec2(w, w*(float)v.image.extent().height/(float)v.image.extent().width));
	}
}
inline void image_value_field(const char* label, ImageValue3& v) {
	ImGui::ColorEdit3(label, v.value.data(), ImGuiColorEditFlags_Float);
	if (v.image) {
		const uint32_t w = ImGui::GetWindowSize().x;
		ImGui::Image(&v.image, ImVec2(w, w*(float)v.image.extent().height/(float)v.image.extent().width));
	}
}
inline void image_value_field(const char* label, ImageValue4& v) {
	ImGui::ColorEdit4(label, v.value.data(), ImGuiColorEditFlags_Float);
	if (v.image) {
		const uint32_t w = ImGui::GetWindowSize().x;
		ImGui::Image(&v.image, ImVec2(w, w*(float)v.image.extent().height/(float)v.image.extent().width));
	}
}
#endif // __cplusplus

#ifdef __HLSL_VERSION

inline ImageValue1 load_image_value1(inout uint address) {
	ImageValue1 r;
	r.image_index_and_channel = gMaterialData.Load(address); address += 4;
	r.value = (min16float)gMaterialData.Load<float>(address); address += 4;
	return r;
}
inline ImageValue2 load_image_value2(inout uint address) {
	ImageValue2 r;
	r.image_index_and_channel = gMaterialData.Load(address); address += 4;
	r.value = (min16float2)gMaterialData.Load<float2>(address); address += 8;
	return r;
}
inline ImageValue3 load_image_value3(inout uint address) {
	ImageValue3 r;
	r.image_index = gMaterialData.Load(address); address += 4;
	r.value = (min16float3)gMaterialData.Load<float3>(address); address += 12;
	return r;
}
inline ImageValue4 load_image_value4(inout uint address) {
	ImageValue4 r;
	r.image_index = gMaterialData.Load(address); address += 4;
	r.value = (min16float4)gMaterialData.Load<float4>(address); address += 16;
	return r;
}

inline float4 sample_image(Texture2D<float4> img, const float2 uv, const float uv_screen_size) {
	float w,h;
	img.GetDimensions(w,h);
	float lod = 0;
	if (gUseRayCones && uv_screen_size > 0)
		lod = log2(max(uv_screen_size*min(w,h), 1e-8f));
	return img.SampleLevel(gSampler, uv, lod);
}
inline float  sample_image(const ImageValue1 img, const ShadingData shading_data) {
	if (!img.has_image()) return img.value;
	return img.value * sample_image(img.image(), shading_data.uv, shading_data.uv_screen_size)[img.channel()];
}
inline float2 sample_image(const ImageValue2 img, const ShadingData shading_data) {
	if (!img.has_image()) return img.value;
	return img.value * sample_image(img.image(), shading_data.uv, shading_data.uv_screen_size).rg;
}
inline float3 sample_image(const ImageValue3 img, const ShadingData shading_data) {
	if (!img.has_image()) return img.value;
	return img.value * sample_image(img.image(), shading_data.uv, shading_data.uv_screen_size).rgb;
}
inline float4 sample_image(const ImageValue4 img, const ShadingData shading_data) {
	if (!img.has_image()) return img.value;
	return img.value * sample_image(img.image(), shading_data.uv, shading_data.uv_screen_size);
}

#endif // __HLSL_VERSION

#endif