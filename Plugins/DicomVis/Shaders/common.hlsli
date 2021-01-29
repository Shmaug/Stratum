#ifndef COMMON_H
#define COMMON_H

struct VolumeData {
	float4 Rotation;
	float4 InvRotation;
	float3 Scale;
	float Density;
	float3 InvScale;
	uint Mask;
	float3 Position;
	uint FrameIndex;
	float2 RemapRange;
	float2 HueRange;
	uint3 Resolution;
	uint pad;
};

#ifndef __cplusplus

[[vk::constant_id(0)]] gMaskColored = false; // Is the mask colored?
[[vk::constant_id(1)]] gSingleChannel = true;
[[vk::constant_id(2)]] gBakedGradient = false;

//#pragma inline_uniform_block Volume
[[vk::binding(3, 0)]] ConstantBuffer<VolumeData> gVolume : register(b0);

#ifndef M_PI
#define M_PI (3.1415926535897932)
#endif
#ifndef M_1_PI
#define M_1_PI (1.0 / M_PI)
#endif

#ifdef SINGLE_CHANNEL
RWTexture3D<float> RWVolume : register(t0);
#else
RWTexture3D<float4> RWVolume : register(t0);
#endif
RWTexture3D<float4> Gradient : register(u2);
RWTexture3D<uint> RawMask : register(t1);

float3 HuetoRGB(float hue) {
	return saturate(float3(abs(hue * 6 - 3) - 1, 2 - abs(hue * 6 - 2), 2 - abs(hue * 6 - 4)));
}
float3 HSVtoRGB(float3 hsv) {
	float3 rgb = HuetoRGB(hsv.x);
	return ((rgb - 1) * hsv.y + 1) * hsv.z;
}
float3 RGBtoHCV(float3 rgb) {
	// Based on work by Sam Hocevar and Emil Persson
	float4 P = (rgb.g < rgb.b) ? float4(rgb.bg, -1.0, 2.0 / 3.0) : float4(rgb.gb, 0.0, -1.0 / 3.0);
	float4 Q = (rgb.r < P.x) ? float4(P.xyw, rgb.r) : float4(rgb.r, P.yzx);
	float C = Q.x - min(Q.w, Q.y);
	float H = abs((Q.w - Q.y) / (6 * C + 1e-6) + Q.z);
	return float3(H, C, Q.x);
}
float3 RGBtoHSV(float3 rgb) {
	float3 hcv = RGBtoHCV(rgb);
	return float3(hcv.x, hcv.y / (hcv.z + 1e-6), hcv.z);
}
float ChromaKey(float3 hsv, float3 key) {
	float3 d = abs(hsv - key) / float3(0.1, 0.5, 0.5);
	return saturate(length(d) - 1);
}

float4 SampleColor(uint3 index){
	float4 c = RWVolume[index];

	#ifndef BAKED
	// non-baked volume, do processing

	#ifdef COLORIZE
	c.rgb = HSVtoRGB(float3(HueRange.x + c.a * (HueRange.y - HueRange.x), .5, 1));

	#elif defined(NON_BAKED_RGBA)
	// chroma-key blue out (for visible human dataset)
	float3 hsv = RGBtoHSV(c.rgb);
	c.a *= ChromaKey(hsv, RGBtoHSV(float3(0.07059, 0.07843, 0.10589))) * saturate(4 * hsv.z);
	#endif

	c.a *= saturate((c.a - RemapRange.x) / (RemapRange.y - RemapRange.x));
	#endif

	if (gMaskColored) {
		static const float3 maskColors[8] = {
			float3(1.0, 0.1, 0.1),
			float3(0.1, 1.0, 0.1),
			float3(0.1, 0.1, 1.0),

			float3(1.0, 1.0, 0.0),
			float3(1.0, 0.1, 1.0),
			float3(0.1, 1.0, 1.0),

			float3(1.0, 0.5, 0.1),
			float3(1.0, 0.1, 0.5),
		};
		uint value = RawMask[index] & MaskValue;
		if (value) c.rgb = maskColors[firstbitlow(value)];
	}

	return c;
}
float3 SampleGradient(uint3 index) {
	if (gBakedGradient)
		return Gradient[index].xyz;
	else
		return float3(
			SampleColor(index + int3(1, 0, 0)).a - SampleColor(index + int3(-1, 0, 0)).a,
			SampleColor(index + int3(0, 1, 0)).a - SampleColor(index + int3(0, -1, 0)).a,
			SampleColor(index + int3(0, 0, 1)).a - SampleColor(index + int3(0, 0, -1)).a);
}

#endif
#endif