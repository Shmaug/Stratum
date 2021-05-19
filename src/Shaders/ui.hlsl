#pragma compile vertex vs_ui fragment fs_ui

SamplerState gFontsSampler : register(s0);
Texture2D<float4> gFontsTexture : register(t1); 

struct PushConstants {
	float2 Scale;
	float2 Translate;
};
[[vk::push_constant]] const PushConstants gPushConstants = {float2(1.f, 1.f), float2(0,0)}; 

struct v2f {
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
}; 
 
v2f vs_ui(uint instance : SV_InstanceID, float2 aPos : POSITION, float2 aUV : TEXCOORD0, float4 aColor : COLOR0) {
	v2f o; 
	o.color = aColor;
	o.uv = aUV;
	o.position = float4(aPos * gPushConstants.Scale + gPushConstants.Translate, 0.f, 1.f); 
	return o;
}

float4 fs_ui(v2f i, uint instance : SV_InstanceID) : SV_Target {
	//float4 color = /*(float4(1.f, 1.f, 1.f, 1.f) - i.color + float4(0.f, 0.f, 0.f, 1.f)) * */gFontsTexture.Sample(gFontsSampler, i.uv);// float4(1.f, 1.f, 1.f, 1.f); 
	float4 color = i.color * gFontsTexture.Sample(gFontsSampler, i.uv);
	return color;
} 