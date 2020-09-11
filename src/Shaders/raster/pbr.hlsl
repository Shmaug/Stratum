#pragma pass forward/depth vsmain fsdepth
#pragma pass forward/opaque vsmain fsmain

#pragma render_queue 1000
#pragma blend 0 false

#pragma multi_compile ALPHA_CLIP
#pragma multi_compile TEXTURED TEXTURED_COLORONLY

#pragma static_sampler Sampler

#include <include/shadercompat.h>

#if defined(TEXTURED_COLORONLY)
#define NEED_TEXCOORD
#elif defined(TEXTURED)
#define NEED_TEXCOORD
#define NEED_TANGENT
#endif

#ifdef ALPHA_CLIP
#define NEED_TEXCOORD
#endif

[[vk::binding(BINDING_START + 0, PER_MATERIAL)]] Texture2D<float4> BaseColorTexture		: register(t4);
[[vk::binding(BINDING_START + 1, PER_MATERIAL)]] Texture2D<float4> NormalTexture			: register(t5);
[[vk::binding(BINDING_START + 2, PER_MATERIAL)]] Texture2D<float4> RoughnessTexture		: register(t6);
[[vk::binding(BINDING_START + 7, PER_MATERIAL)]] SamplerState Sampler : register(s0);

[[vk::push_constant]] cbuffer PushConstants : register(b2) {
	STM_PUSH_CONSTANTS
	float4 BaseColor;
	float Metallic;
	float3 Emission;
	float Roughness;
	float4 TextureST;
	float BumpStrength;
};

#include <include/util.hlsli>
#include <include/shadow.hlsli>
#include <include/brdf.hlsli>

struct v2f {
	float4 position : SV_Position;
	float4 worldPos : TEXCOORD0;
	float4 screenPos : TEXCOORD1;
	float3 normal : NORMAL;
	#ifdef NEED_TEXCOORD
	float2 texcoord : TEXCOORD2;
	#endif
	#ifdef NEED_TANGENT
	float3 tangent : TANGENT;
	#endif
};

v2f vsmain(
	[[vk::location(0)]] float3 vertex : POSITION,
	[[vk::location(1)]] float3 normal : NORMAL,
	#ifdef NEED_TANGENT
	[[vk::location(2)]] float4 tangent : TANGENT,
	#endif
	#ifdef NEED_TEXCOORD
	[[vk::location(3)]] float2 texcoord : TEXCOORD0,
	#endif
	uint instance : SV_InstanceID ) {
	v2f o;
	
	float4x4 o2w = Instances[instance].ObjectToWorld;
	o2w[0][3] += -STRATUM_CAMERA_POSITION.x * o2w[3][3];
	o2w[1][3] += -STRATUM_CAMERA_POSITION.y * o2w[3][3];
	o2w[2][3] += -STRATUM_CAMERA_POSITION.z * o2w[3][3];
	float4 worldPos = mul(o2w, float4(vertex, 1.0));

	o.position = mul(STRATUM_MATRIX_VP, worldPos);
	o.worldPos = float4(worldPos.xyz, o.position.z);
	o.screenPos = ComputeScreenPos(o.position);
	o.normal = mul(float4(normal, 1), Instances[instance].WorldToObject).xyz;
	
	#ifdef NEED_TEXCOORD
	o.texcoord = texcoord * TextureST.xy + TextureST.zw;
	#endif
	#ifdef NEED_TANGENT
	o.tangent = mul(tangent, Instances[instance].WorldToObject).xyz * tangent.w;
	#endif

	return o;
}

float4 fsmain(v2f i) : SV_Target0 {
	float3 view = normalize(-i.worldPos.xyz);

	#if defined(TEXTURED) || defined(TEXTURED_COLORONLY)
	float4 col = BaseColorTexture.Sample(Sampler, i.texcoord) * BaseColor;
	#else
	float4 col = BaseColor;
	#endif

	bool ff = dot(i.normal, view) > 0;

	float3 normal = normalize(i.normal) * (ff ? 1 : -1);

	#ifdef TEXTURED
	float4 bump = NormalTexture.Sample(Sampler, i.texcoord);
	bump.xyz = bump.xyz * 2 - 1;
	float3 tangent = normalize(i.tangent);
	float3 bitangent = normalize(cross(normal, tangent));
	bump.xy *= BumpStrength;
	normal = normalize(tangent * bump.x + bitangent * bump.y + normal * bump.z);
	#endif

	float metallic = Metallic;
	float roughness = Roughness;
	float occlusion = 1.0;

	#ifdef TEXTURED
	roughness *= RoughnessTexture.Sample(Sampler, i.texcoord).r;
	#endif

	MaterialInfo material;
	material.diffuse = DiffuseAndSpecularFromMetallic(col.rgb, metallic, material.specular, material.oneMinusReflectivity);
	material.perceptualRoughness = Roughness;
	material.roughness = max(.002, material.perceptualRoughness * material.perceptualRoughness);
	material.occlusion = occlusion;
	material.emission = Emission;

	float3 eval = ShadeSurface(material, i.worldPos.xyz, normal, view, i.worldPos.w, i.screenPos.xy / i.screenPos.w);
	return float4(eval, 1);
}

float fsdepth(float4 position : SV_Position, in float2 texcoord : TEXCOORD2) : SV_Target0 {
	#ifdef ALPHA_CLIP
	clip((BaseColorTexture.Sample(Sampler, texcoord) * BaseColor).a - .75);
	#endif
	return position.z;
}