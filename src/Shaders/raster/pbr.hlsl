#pragma pass forward/depth vsmain fsdepth
#pragma pass forward/opaque vsmain fsmain

#pragma render_queue 1000
#pragma blend 0 false

#pragma multi_compile ALPHA_CLIP

#pragma static_sampler Sampler
#pragma inline_uniform_block Materials

#include <shadercompat.h>

#if defined(TEXTURED_COLORONLY)
#define NEED_TEXCOORD
#elif defined(TEXTURED)
#define NEED_TEXCOORD
#define NEED_TANGENT
#endif

#ifdef ALPHA_CLIP
#define NEED_TEXCOORD
#endif

#define TEXTURE_COUNT 64

[[vk::binding(BINDING_START + 0, PER_MATERIAL)]] Texture2D<float4> Textures[TEXTURE_COUNT] : register(t6);
[[vk::binding(BINDING_START + 1, PER_MATERIAL)]] SamplerState Sampler : register(s0);

[[vk::push_constant]] cbuffer PushConstants : register(b2) {
	STM_PUSH_CONSTANTS
	float4 BaseColor;
	float3 Emission;
	float Metallic;
	float Roughness;
	float BumpStrength;
	uint BaseColorTextureIndex;
	uint RoughnessTextureIndex;
	uint NormalTextureIndex;
	float4 TextureST;
};

#include <util.hlsli>
#include <shadow.hlsli>
#include <brdf.hlsli>

struct v2f {
	float4 position : SV_Position;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float2 texcoord : TEXCOORD0;
	float4 worldPos : TEXCOORD1;
	float4 screenPos : TEXCOORD2;
};

v2f vsmain(
	[[vk::location(0)]] float3 vertex : POSITION,
	[[vk::location(1)]] float3 normal : NORMAL,
	[[vk::location(2)]] float4 tangent : TANGENT,
	[[vk::location(3)]] float2 texcoord : TEXCOORD0,
	uint instance : SV_InstanceID ) {
	v2f o;
	
	float4 worldPos = mul(ApplyCameraTranslation(Instances[instance].ObjectToWorld), float4(vertex, 1.0));

	o.position = mul(STRATUM_MATRIX_VP, worldPos);
	o.normal = mul(float4(normal, 1), Instances[instance].WorldToObject).xyz;
	o.tangent = mul(tangent, Instances[instance].WorldToObject).xyz * tangent.w;
	o.texcoord = texcoord * TextureST.xy + TextureST.zw;
	o.worldPos = float4(worldPos.xyz, o.position.z);
	o.screenPos = ComputeScreenPos(o.position);

	return o;
}

float4 fsmain(v2f i) : SV_Target0 {
	float3 view = normalize(-i.worldPos.xyz);

	float4 col = BaseColor;
	float metallic = Metallic;
	float roughness = Roughness;
	float occlusion = 1.0;
	float3 normal = normalize(i.normal) * (dot(i.normal, view) > 0 ? 1 : -1);

	if (BaseColorTextureIndex < TEXTURE_COUNT) col *= Textures[BaseColorTextureIndex].Sample(Sampler, i.texcoord);
	if (RoughnessTextureIndex < TEXTURE_COUNT) roughness *= Textures[RoughnessTextureIndex].Sample(Sampler, i.texcoord).r;
	if (NormalTextureIndex < TEXTURE_COUNT) {
		float4 bump = Textures[NormalTextureIndex].Sample(Sampler, i.texcoord);
		bump.xyz = bump.xyz * 2 - 1;
		float3 tangent = normalize(i.tangent);
		float3 bitangent = normalize(cross(normal, tangent));
		bump.xy *= BumpStrength;
		normal = normalize(tangent * bump.x + bitangent * bump.y + normal * bump.z);
	}

	MaterialInfo material;
	material.diffuse = DiffuseAndSpecularFromMetallic(col.rgb, metallic, material.specular, material.oneMinusReflectivity);
	material.perceptualRoughness = Roughness;
	material.roughness = max(.002, material.perceptualRoughness * material.perceptualRoughness);
	material.occlusion = occlusion;
	material.emission = Emission;
	return float4(ShadeSurface(material, i.worldPos.xyz, normal, view, i.worldPos.w, i.screenPos.xy / i.screenPos.w), 1);
}

float fsdepth(float4 position : SV_Position, in float2 texcoord : TEXCOORD2) : SV_Target0 {
	#ifdef ALPHA_CLIP
	clip((Textures[BaseColorTextureIndex].Sample(Sampler, texcoord) * BaseColor).a - .75);
	#endif
	return position.z;
}