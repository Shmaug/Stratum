#ifndef LIGHT_H
#define LIGHT_H

#include "common.hlsli"

inline void sample_light_or_environment(out LightSampleRecord ls, const float4 rnd, const uint v, const float3 dir_in) {
	ls.pdf = 0;
	if (gSampleBG && (!gSampleLights || rnd.w <= gPushConstants.gEnvironmentSampleProbability)) {
		// sample environment
		const BSDFSampleRecord s = sample_material(load_material<Environment>(gMaterialData, gPushConstants.gEnvironmentMaterialAddress+4), rnd.xyz, dir_in, v, TRANSPORT_TO_LIGHT);
		ls.position_or_bary = s.dir_out;
		BF_SET(ls.instance_primitive_index, INVALID_INSTANCE, 0, 16);
		BF_SET(ls.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
		ls.radiance = s.eval.f;
		ls.to_light = s.dir_out;
		ls.dist = 1.#INF;
		ls.G = 1;
		ls.pdf = s.eval.pdfW;
		ls.pdfA = s.eval.pdfW;
	} else if (gSampleLights) {
		// pick random light
		const uint light_instance_index = gLightInstances[min(uint(rnd.z * gPushConstants.gLightCount), gPushConstants.gLightCount-1)];
		BF_SET(ls.instance_primitive_index, light_instance_index, 0, 16);

		const ImageValue3 emissive = load_material<Emissive>(gMaterialData, gInstances[ls.instance_index()].material_address() + 4).emission;
		ls.radiance = emissive.value;
		
		switch (gInstances[light_instance_index].type()) {
			case INSTANCE_TYPE_SPHERE: {
				BF_SET(ls.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
				const float3 center = gInstances[light_instance_index].transform.transform_point(0);
				float3 to_center = center - gPathVertices[v].position;
				const float dist = length(to_center);
				const float rcp_dist = 1/dist;
				to_center *= rcp_dist;

				const float sinThetaMax2 = pow2(gInstances[light_instance_index].radius()) * rcp_dist * rcp_dist;
				const float cosThetaMax = sqrt(max(0, 1 - sinThetaMax2));

				const float cosTheta = (1 - rnd.x) + rnd.x * cosThetaMax;
				const float sinTheta = sqrt(max(0, 1 - cosTheta * cosTheta));
				const float phi = rnd.y * 2 * M_PI;
				
				float3 T, B;
				make_orthonormal(to_center, T, B);

				ls.to_light = T*sinTheta*cos(phi) + B*sinTheta*sin(phi) + to_center*cosTheta;
				ls.dist = ray_sphere(gPathVertices[v].position, ls.to_light, center, gInstances[light_instance_index].radius()).x;
				ls.position_or_bary = gPathVertices[v].position + ls.to_light*ls.dist;
				
				ls.G = abs(dot(ls.to_light, normalize(ls.position_or_bary - center))) / pow2(ls.dist);
				ls.pdf = 1 / (2 * M_PI * (1 - cosThetaMax));
				ls.pdfA = pdfWtoA(ls.pdf, ls.G);
				break;
			}
			case INSTANCE_TYPE_TRIANGLES: {
				const float u = gSampleBG ? (rnd.w - gPushConstants.gEnvironmentSampleProbability) / (1 - gPushConstants.gEnvironmentSampleProbability) : rnd.w;
				const uint prim_index = min(u*gInstances[light_instance_index].prim_count(), gInstances[light_instance_index].prim_count() - 1);
				BF_SET(ls.instance_primitive_index, prim_index, 16, 16);
				const float a = sqrt(rnd.x);
				ls.position_or_bary = float3(1 - a, a*rnd.y, 0);
				const uint3 tri = load_tri(gIndices, gInstances[light_instance_index], ls.primitive_index());
				PathVertexGeometry g;
				instance_triangle_geometry(g, light_instance_index, tri, ls.position_or_bary.xy);
				
				if (emissive.has_image()) ls.radiance *= sample_image(emissive.image(), g.uv, g.uv_screen_size).rgb;

				ls.to_light = g.position - gPathVertices[v].position;
				ls.dist = length(ls.to_light);
				ls.to_light /= ls.dist;

				ls.G = abs(dot(ls.to_light, g.geometry_normal)) / pow2(ls.dist);
				ls.pdf = 1 / (g.shape_area * (float)gInstances[light_instance_index].prim_count());
				ls.pdfA = ls.pdf;
				break;
			}
		}
		const float inv_light_count = 1 / (float)gPushConstants.gLightCount;
		ls.pdf *= inv_light_count;
		ls.pdfA *= inv_light_count;
	}
	if (gSampleBG && gSampleLights) {
		const float pick_prob = gSampleBG ? gPushConstants.gEnvironmentSampleProbability : 1 - gPushConstants.gEnvironmentSampleProbability;
		ls.pdf *= pick_prob;
		ls.pdfA *= pick_prob;
	}
}

inline void light_sample_pdf(const uint light_instance, const uint light_vertex, const float3 ref_pos, out float pdf, out float pdfA, out float G) {
	if (light_instance == INVALID_INSTANCE) {
		G = 1;
		if (!gSampleBG) {
			pdf = 0;
			pdfA = 0;
		} else {
			pdf = pdfA = load_material<Environment>(gMaterialData, gPushConstants.gEnvironmentMaterialAddress+4).eval_pdf(gPathVertices[light_vertex].position);
			if (gSampleLights) {
				pdf *= gPushConstants.gEnvironmentSampleProbability;
				pdfA *= gPushConstants.gEnvironmentSampleProbability;
			}
		}
	} else {
		if (!gSampleLights)  {
			pdf = 0;
			pdfA = 0;
			G = 1;
		} else {
			float3 dir = ref_pos - gPathVertices[light_vertex].position;
			const float dist = length(dir);
			dir /= dist;
			G = abs(dot(dir, gPathVertices[light_vertex].geometry_normal)) / pow2(dist);
			switch (gInstances[light_instance].type()) {
				case INSTANCE_TYPE_SPHERE: {
					const float3 center = gInstances[light_instance].transform.transform_point(0);
					const float3 to_center = center - ref_pos;
					const float sinThetaMax2 = gInstances[light_instance].radius()*gInstances[light_instance].radius() / dot(to_center, to_center);
					const float cosThetaMax = sqrt(max(0, 1 - sinThetaMax2));
					pdf = 1 / (2 * M_PI * (1 - cosThetaMax));
					pdfA = pdfWtoA(pdf, G);
					break;
				}
				case INSTANCE_TYPE_TRIANGLES: {
					pdf = 1 / ((float)gPathVertices[light_vertex].shape_area * gInstances[light_instance].prim_count());
					pdfA = pdf;
					break;
				}
			}
			const float inv_light_count = 1 / (float)gPushConstants.gLightCount;
			pdf *= inv_light_count;
			pdfA *= inv_light_count;
			if (gSampleBG) {
				pdf *= 1 - gPushConstants.gEnvironmentSampleProbability;
				pdfA *= 1 - gPushConstants.gEnvironmentSampleProbability;
			}
		}
	}
}

inline float mis_heuristic(const float a, const float b) {
	const float a2 = a * a;
	return a2 / (b*b + a2);
}

#endif