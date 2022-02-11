#ifndef IMAGE_VALUE_H
#define IMAGE_VALUE_H

#include "common.hlsli"

#ifdef __cplusplus

struct ByteAppendBuffer {
  vector<uint32_t> data;
  
  inline uint Load(uint32_t address) {
    return data[address/4];
  }

  inline void Append(const uint32_t x) {
    data.emplace_back(x);
  }
};

struct ImagePool {
	unordered_map<Image::View, uint32_t> images;
	unordered_map<Buffer::View<float>, uint32_t> distribution_data_map;
	uint32_t distribution_data_size;

	inline uint32_t get_index(const Image::View& image) {
		if (!image) return ~0u;
		const Image::View tmp = Image::View(image.image(), image.subresource_range());
		auto it = images.find(tmp);
		return (it == images.end()) ? images.emplace(tmp, (uint32_t)images.size()).first->second : it->second;
	};
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
	};
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
	inline bool has_image() { return BF_GET(image_index_and_channel, 0, 30) < gImageCount; }
	inline Texture2D<float4> image() { return gImages[NonUniformResourceIndex(BF_GET(image_index_and_channel, 0, 30))]; }
	inline uint channel() { return BF_GET(image_index_and_channel, 30, 2); }
	inline float eval(const PathVertexGeometry v) {
		if (has_image())
			return value * image().SampleGrad(gSampler, v.uv, v.d_uv.dx, v.d_uv.dy)[channel()];
		else
			return value;
	}
	inline float eval(const float2 uv) {
		if (has_image())
			return value * image().SampleLevel(gSampler, uv, 0)[channel()];
		else
			return value;
	}
	inline void load(ByteAddressBuffer bytes, inout uint address) {
		image_index_and_channel = bytes.Load(address); address += 4;
		value = (min16float)asfloat(bytes.Load(address)); address += 4;
	}

	#endif
	
	#ifdef __cplusplus

	float value;
	Image::View image;
	inline void store(ByteAppendBuffer& bytes, ImagePool& images) const {
		const uint image_index = images.get_index(image);
		const uint32_t channel = channel_mapping_swizzle(image.components())[0];
		uint image_index_and_channel = 0;
		BF_SET(image_index_and_channel, image_index, 0, 30);
		BF_SET(image_index_and_channel, channel, 30, 2);
		bytes.Append(image_index_and_channel);
		bytes.Append(asuint(value));
	}

	#endif
};
struct ImageValue2 {

	#ifdef __HLSL_VERSION

	min16float2 value;
	uint image_index_and_channels;
	inline bool has_image() { return BF_GET(image_index_and_channels, 0, 28) < gImageCount; }
	inline Texture2D<float4> image() { return gImages[NonUniformResourceIndex(BF_GET(image_index_and_channels, 0, 28))]; }
	inline uint2 channels() { return uint2(BF_GET(image_index_and_channels, 28, 2), BF_GET(image_index_and_channels, 30, 2)); }
	inline float2 eval(const PathVertexGeometry v) {
		if (has_image()) {
			const float4 s = image().SampleGrad(gSampler, v.uv, v.d_uv.dx, v.d_uv.dy);
			const uint2 c = channels();
			return value * float2(s[c[0]], s[c[1]]);
		} else
			return value;
	}
	inline float2 eval(const float2 uv) {
		if (has_image()) {
			const float4 s = image().SampleLevel(gSampler, uv, 0);
			const uint2 c = channels();
			return value * float2(s[c[0]], s[c[1]]);
		} else
			return value;
	}
	inline void load(ByteAddressBuffer bytes, inout uint address) {
		image_index_and_channels = bytes.Load(address); address += 4;
		value = (min16float2)asfloat(bytes.Load2(address)); address += 8;
	}

	#endif

	#ifdef __cplusplus

	float2 value;
	Image::View image;
	inline void store(ByteAppendBuffer& bytes, ImagePool& images) const {
		const uint image_index = images.get_index(image);
		const uint4 c = channel_mapping_swizzle(image.components());
	 	uint image_index_and_channels = 0;
		BF_SET(image_index_and_channels, image_index, 0, 28);
		BF_SET(image_index_and_channels, c[0], 28, 2);
		BF_SET(image_index_and_channels, c[1], 30, 2);
		bytes.Append(image_index_and_channels);
		bytes.Append(asuint(value[0]));
		bytes.Append(asuint(value[1]));
	}

	#endif
};
struct ImageValue3 {

	#ifdef __HLSL_VERSION

	min16float3 value;
	uint image_index_and_channels;
	inline bool has_image() { return BF_GET(image_index_and_channels, 0, 26) < gImageCount; }
	inline Texture2D<float4> image() { return gImages[NonUniformResourceIndex(BF_GET(image_index_and_channels, 0, 26))]; }
	inline uint3 channels() { return uint3(BF_GET(image_index_and_channels, 26, 2), BF_GET(image_index_and_channels, 28, 2), BF_GET(image_index_and_channels, 30, 2)); }
	inline float3 eval(const PathVertexGeometry v) {
		if (has_image()) {
			float4 s = image().SampleGrad(gSampler, v.uv, v.d_uv.dx, v.d_uv.dy);
			const uint3 c = channels();
			return value * float3(s[c[0]], s[c[1]], s[c[2]]);
		} else
			return value;
	}
	inline float3 eval(const float2 uv) {
		if (has_image()) {
			float4 s = image().SampleLevel(gSampler, uv, 0);
			const uint3 c = channels();
			return value * float3(s[c[0]], s[c[1]], s[c[2]]);
		} else
			return value;
	}
	inline void load(ByteAddressBuffer bytes, inout uint address) {
		image_index_and_channels = bytes.Load(address); address += 4;
		value = (min16float3)asfloat(bytes.Load3(address)); address += 12;
	}

	#endif

	#ifdef __cplusplus

	float3 value;
	Image::View image;
	inline void store(ByteAppendBuffer& bytes, ImagePool& images) const {
		const uint image_index = images.get_index(image);
		const uint4 c = channel_mapping_swizzle(image.components());
		uint image_index_and_channels = 0;
		BF_SET(image_index_and_channels, image_index, 0, 26);
		BF_SET(image_index_and_channels, c[0], 26, 2);
		BF_SET(image_index_and_channels, c[1], 28, 2);
		BF_SET(image_index_and_channels, c[2], 30, 2);
		bytes.Append(image_index_and_channels);
		bytes.Append(asuint(value[0]));
		bytes.Append(asuint(value[1]));
		bytes.Append(asuint(value[2]));
	}

	#endif
};
struct ImageValue4 {

	#ifdef __HLSL_VERSION

	min16float4 value;
	uint image_index;
	inline bool has_image() { return image_index < gImageCount; }
	inline Texture2D<float4> image() { return gImages[NonUniformResourceIndex(image_index)]; }
	inline float4 eval(const PathVertexGeometry v) {
		if (has_image())
			return value * image().SampleGrad(gSampler, v.uv, v.d_uv.dx, v.d_uv.dy);
		else
			return value;
	}
	inline float4 eval(const float2 uv) {
		if (has_image())
			return value * image().SampleLevel(gSampler, uv, 0);
		else
			return value;
	}
	inline void load(ByteAddressBuffer bytes, inout uint address) {
		image_index = bytes.Load(address); address += 4;
		value = (min16float4)asfloat(bytes.Load4(address)); address += 16;
	}

	#endif

	#ifdef __cplusplus

	float4 value;
	Image::View image;
	inline void store(ByteAppendBuffer& bytes, ImagePool& images) const {
		bytes.Append(images.get_index(image));
		bytes.Append(asuint(value[0]));
		bytes.Append(asuint(value[1]));
		bytes.Append(asuint(value[2]));
		bytes.Append(asuint(value[3]));
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
	ImGui::DragFloat3(label, v.value.data(), .01f);
	if (v.image) {
		const uint32_t w = ImGui::GetWindowSize().x;
		ImGui::Image(&v.image, ImVec2(w, w*(float)v.image.extent().height/(float)v.image.extent().width));
	}
}
inline void image_value_field(const char* label, ImageValue4& v) {
	ImGui::DragFloat4(label, v.value.data(), .01f);
	if (v.image) {
		const uint32_t w = ImGui::GetWindowSize().x;
		ImGui::Image(&v.image, ImVec2(w, w*(float)v.image.extent().height/(float)v.image.extent().width));
	}
}
#endif

#endif