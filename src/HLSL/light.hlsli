#ifndef LIGHT_H
#define LIGHT_H

#include "common.hlsli"

struct LightSampleRecord {
	float3 position;
	uint instance_primitive_index;
	float3 normal;
	float G;
	float3 to_light;
	float dist;
	float3 radiance;
	float pdfA;
	float2 bary;

	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
};

inline void sample_light_or_environment(out LightSampleRecord ls, const float4 rnd, const float3 ref_pos) {
	ls.pdfA = 0;
	if (gSampleEnvironment && (!gSampleEmissive || rnd.w <= gPushConstants.gEnvironmentSampleProbability)) {
		// sample environment
		const MaterialSampleRecord s = sample_material_emission(load_material<Environment>(gPushConstants.gEnvironmentMaterialAddress+4, 0), rnd.xyz, 0, 0);
		ls.radiance = s.eval.f;
		ls.pdfA = s.eval.pdfW;
		if (gSampleEmissive) ls.pdfA *= gPushConstants.gEnvironmentSampleProbability;
		ls.position = ls.to_light = ls.normal = s.dir_out;
		BF_SET(ls.instance_primitive_index, INVALID_INSTANCE, 0, 16);
		BF_SET(ls.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
		ls.dist = 1.#INF;
		ls.G = 1;
	} else if (gSampleEmissive) {
		// pick random light
		const uint light_instance_index = gLightInstances[min(uint(rnd.z * gPushConstants.gLightCount), gPushConstants.gLightCount-1)];
		BF_SET(ls.instance_primitive_index, light_instance_index, 0, 16);

		const ImageValue3 emissive = load_material<Emissive>(gInstances[ls.instance_index()].material_address() + 4, 0).emission;
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
					ls.bary = float2(rnd.y, rnd.x);
					ls.position = gInstances[light_instance_index].transform.transform_point(radius * local_normal);
					ls.normal = normalize(gInstances[light_instance_index].transform.transform_vector(local_normal));
					ls.to_light = ls.position - ref_pos;
					ls.dist = length(ls.to_light);
					ls.to_light /= ls.dist;
					ls.G = abs(dot(ls.to_light, ls.normal)) / pow2(ls.dist);
					ls.pdfA /= (4*M_PI*radius*radius);
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
					ls.G = abs(dot(ls.to_light, ls.normal)) / pow2(ls.dist);
					ls.pdfA *= pdfWtoA(1/(2 * M_PI * (1 - cosThetaMax)), ls.G);
				}
				
				break;
			}
			case INSTANCE_TYPE_TRIANGLES: {
				const float u = gSampleEnvironment ? (rnd.w - gPushConstants.gEnvironmentSampleProbability) / (1 - gPushConstants.gEnvironmentSampleProbability) : rnd.w;
				const uint prim_index = min(u*gInstances[light_instance_index].prim_count(), gInstances[light_instance_index].prim_count() - 1);
				BF_SET(ls.instance_primitive_index, prim_index, 16, 16);
				const float a = sqrt(rnd.x);
				ls.bary = float2(1 - a, a*rnd.y);
				
				const uint3 tri = load_tri(gIndices, gInstances[light_instance_index], ls.primitive_index());
				PathVertexGeometry g;
				instance_triangle_geometry(g, light_instance_index, tri, ls.bary);
				
				if (emissive.has_image()) ls.radiance *= sample_image(emissive.image(), g.uv, g.uv_screen_size).rgb;

				ls.position = g.position;
				ls.normal = g.geometry_normal;
				ls.to_light = g.position - ref_pos;
				ls.dist = length(ls.to_light);
				ls.to_light /= ls.dist;
				ls.G = abs(dot(ls.to_light, g.geometry_normal)) / pow2(ls.dist);
				ls.pdfA /= g.shape_area * gInstances[light_instance_index].prim_count();
				break;
			}
		}
	}
}

inline float3 light_emission(const uint light_instance_primitive, const PathVertexGeometry light_vertex) {
	if (BF_GET(light_instance_primitive, 0, 16) == INVALID_INSTANCE) {
		if (!gSampleEnvironment) return 0;
		return eval_material_emission(load_material<Environment>(gPushConstants.gEnvironmentMaterialAddress+4, 0), light_vertex.position, 0).f;
	} else {
		if (!gSampleEmissive) return 0;
		const ImageValue3 emissive = load_material<Emissive>(gInstances[BF_GET(light_instance_primitive, 0, 16)].material_address() + 4, 0).emission;
		if (emissive.has_image())
			if (gInstances[BF_GET(light_instance_primitive, 0, 16)].type() == INSTANCE_TYPE_TRIANGLES)
				return emissive.value * sample_image(emissive.image(), light_vertex.uv, light_vertex.uv_screen_size).rgb;
		return emissive.value;
	}
}
 
inline void light_sample_pdf(const uint light_instance, const PathVertexGeometry light_vertex, const float3 ref_pos, out float3 dir, out float dist, out float pdf, out float pdfA, out float G) {
	if (light_instance == INVALID_INSTANCE) {
		G = 1;
		if (gSampleEnvironment) {
			pdf = load_material<Environment>(gPushConstants.gEnvironmentMaterialAddress + 4, 0).eval_pdf(light_vertex.position);
			if (gSampleEmissive) pdf *= gPushConstants.gEnvironmentSampleProbability;
			pdfA = pdf;
		} else {
			pdf = 0;
			pdfA = 0;
		}
	} else {
		if (gSampleEmissive)  {
			dir = ref_pos - light_vertex.position;
			dist = length(dir);
			dir /= dist;
			G = abs(dot(dir, light_vertex.geometry_normal)) / pow2(dist);
			pdf = 1 / (float)gPushConstants.gLightCount;
			if (gSampleEnvironment) pdf *= 1 - gPushConstants.gEnvironmentSampleProbability;
			switch (gInstances[light_instance].type()) {
				case INSTANCE_TYPE_SPHERE: {
					if (gUniformSphereSampling) {
						pdf /= light_vertex.shape_area;
						pdfA = pdf;
					} else {
						const float3 to_center = gInstances[light_instance].transform.transform_point(0) - ref_pos;
						pdf /= 2 * M_PI * (1 - sqrt(max(0, 1 - pow2(gInstances[light_instance].radius()) / dot(to_center, to_center))));
						pdfA = pdfWtoA(pdf, G);
					}
					break;
				}
				case INSTANCE_TYPE_TRIANGLES: {
					pdf /= light_vertex.shape_area * gInstances[light_instance].prim_count();
					pdfA = pdf;
					break;
				}
			}
		} else {
			G = 1;
			pdf = 0;
			pdfA = 0;
		}
	}
}

inline float mis_heuristic(const float a, const float b) {
	const float a2 = a * a;
	return a2 / (b*b + a2);
}

#endif