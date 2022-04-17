#ifndef LIGHT_H
#define LIGHT_H

#include "common.hlsli"

struct LightSampleRecord {
	float3 position;
	uint instance_primitive_index;
	float3 radiance;
	float pdfA;
	float3 normal;
	float dist;
	float3 to_light;

	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
};

inline float3 light_emission(const uint light_instance_primitive, const float3 dir_out, const ShadingData light_vertex) {
	if (BF_GET(light_instance_primitive, 0, 16) == INVALID_INSTANCE) {
		if (!gSampleEnvironment) return 0;
		return eval_material_emission(load_material<Environment>(gPushConstants.gEnvironmentMaterialAddress+4, light_vertex), dir_out, light_vertex).f;
	} else {
		if (!gSampleEmissive) return 0;
		return eval_material_emission(load_material<Emissive>(gInstances[BF_GET(light_instance_primitive, 0, 16)].material_address() + 4, light_vertex), dir_out, light_vertex).f;
	}
}

inline void sample_light_or_environment(out LightSampleRecord ls, const float4 rnd, const float3 ref_pos) {
	if (gSampleEnvironment && (!gSampleEmissive || rnd.w <= gPushConstants.gEnvironmentSampleProbability)) {
		// sample environment
		ShadingData sd;
		ls.radiance = load_material<Environment>(gPushConstants.gEnvironmentMaterialAddress+4, sd).sample(rnd.xy, ls.to_light, ls.pdfA);
		if (gSampleEmissive) ls.pdfA *= gPushConstants.gEnvironmentSampleProbability;
		ls.position = ls.normal = ls.to_light;
		BF_SET(ls.instance_primitive_index, INVALID_INSTANCE, 0, 16);
		BF_SET(ls.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
		ls.dist = 1.#INF;
	} else if (gSampleEmissive) {
		// pick random light
		const uint light_instance_index = gLightInstances[min(uint(rnd.z * gPushConstants.gLightCount), gPushConstants.gLightCount-1)];
		BF_SET(ls.instance_primitive_index, light_instance_index, 0, 16);

		ShadingData sd;
		const ImageValue3 emissive = load_material<Emissive>(gInstances[ls.instance_index()].material_address() + 4, sd).emission;
		ls.radiance = emissive.value;
			
		ls.pdfA = 1 / (float)gPushConstants.gLightCount;
		if (gSampleEnvironment) ls.pdfA *= 1 - gPushConstants.gEnvironmentSampleProbability;

		switch (gInstances[light_instance_index].type()) {
			case INSTANCE_TYPE_SPHERE: {
				BF_SET(ls.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
				if (gUniformSphereSampling) {
					const float z = 1 - 2 * rnd.x;
					const float r_ = sqrt(max(0, 1 - z * z));
					const float phi = 2 * M_PI * rnd.y;
					const float radius = gInstances[light_instance_index].radius();
					const float3 local_normal = float3(r_ * cos(phi), z, r_ * sin(phi));
					ls.position = gInstances[light_instance_index].transform.transform_point(radius * local_normal);
					ls.normal = normalize(gInstances[light_instance_index].transform.transform_vector(local_normal));
					ls.to_light = ls.position - ref_pos;
					ls.dist = length(ls.to_light);
					ls.to_light /= ls.dist;
					ls.pdfA /= 4*M_PI*radius*radius;
				} else {
					const float3 center = gInstances[light_instance_index].transform.transform_point(0);
					float3 to_center = center - ref_pos;
					const float dist = length(to_center);
					const float rcp_dist = 1/dist;
					to_center *= rcp_dist;

					const float sinThetaMax2 = pow2(gInstances[light_instance_index].radius() * rcp_dist);
					const float cosThetaMax = sqrt(max(0, 1 - sinThetaMax2));

					const float cosTheta = (1 - rnd.x) + rnd.x * cosThetaMax;
					const float sinTheta = sqrt(max(0, 1 - cosTheta * cosTheta));
					const float phi = rnd.y * 2 * M_PI;
					
					float3 T, B;
					make_orthonormal(to_center, T, B);

					ls.to_light = T*sinTheta*cos(phi) + B*sinTheta*sin(phi) + to_center*cosTheta;
					ls.dist = ray_sphere(ref_pos, ls.to_light, center, gInstances[light_instance_index].radius()).x;
					ls.position = ref_pos + ls.to_light*ls.dist;
					ls.normal = normalize(ls.position - center);
					ls.pdfA *= pdfWtoA(1/(2 * M_PI * (1 - cosThetaMax)), abs(dot(ls.to_light, ls.normal)) / pow2(ls.dist));
				}
				
				break;
			}
			case INSTANCE_TYPE_TRIANGLES: {
				const float u = gSampleEnvironment ? (rnd.w - gPushConstants.gEnvironmentSampleProbability) / (1 - gPushConstants.gEnvironmentSampleProbability) : rnd.w;
				const uint prim_index = min(u*gInstances[light_instance_index].prim_count(), gInstances[light_instance_index].prim_count() - 1);
				BF_SET(ls.instance_primitive_index, prim_index, 16, 16);
				const float a = sqrt(rnd.x);
				
				ShadingData sd;
				make_triangle_shading_data_from_barycentrics(sd, light_instance_index, ls.primitive_index(), float2(1 - a, a*rnd.y));
				
				if (emissive.has_image()) ls.radiance *= sample_image(emissive.image(), sd.uv, sd.uv_screen_size).rgb;

				ls.position = sd.position;
				ls.normal = sd.shading_normal();
				ls.to_light = sd.position - ref_pos;
				ls.dist = length(ls.to_light);
				ls.to_light /= ls.dist;
				ls.pdfA /= sd.shape_area * gInstances[light_instance_index].prim_count();
				break;
			}
		}
	}
}

inline void light_sample_pdf(const uint light_instance, const ShadingData light_vertex, const float3 ref_pos, out float3 dir, out float dist, out float pdfA, out float G) {
	if (light_instance == INVALID_INSTANCE) {
		if (gSampleEnvironment) {
			dir = light_vertex.position;
			dist = 1.#INF;
			pdfA = load_material<Environment>(gPushConstants.gEnvironmentMaterialAddress + 4, light_vertex).eval_pdf(light_vertex.position);
			if (gSampleEmissive) pdfA *= gPushConstants.gEnvironmentSampleProbability;
			G = 1;
		} else {
			pdfA = 0;
			G = 0;
		}
	} else {
		if (gSampleEmissive)  {
			dir = ref_pos - light_vertex.position;
			dist = length(dir);
			dir /= dist;
			G = abs(dot(dir, light_vertex.geometry_normal())) / pow2(dist);
			pdfA = 1 / (float)gPushConstants.gLightCount;
			if (gSampleEnvironment) pdfA *= 1 - gPushConstants.gEnvironmentSampleProbability;
			switch (gInstances[light_instance].type()) {
				case INSTANCE_TYPE_SPHERE: {
					if (gUniformSphereSampling) {
						pdfA /= light_vertex.shape_area;
					} else {
						const float3 to_center = gInstances[light_instance].transform.transform_point(0) - ref_pos;
						const float pdfW = 1 / (2 * M_PI * (1 - sqrt(max(0, 1 - pow2(gInstances[light_instance].radius()) / dot(to_center, to_center)))));
						pdfA *= pdfWtoA(pdfW, G);
					}
					break;
				}
				case INSTANCE_TYPE_TRIANGLES: {
					pdfA /= light_vertex.shape_area * gInstances[light_instance].prim_count();
					break;
				}
			}
		} else {
			G = 1;
			pdfA = 0;
		}
	}
}

inline float mis_heuristic(const float a, const float b) {
	const float a2 = a * a;
	return a2 / (b*b + a2);
}

#endif