#ifndef LIGHT_H
#define LIGHT_H

#include "../../common.h"

struct LightSampleRecord {
	float3 to_light;
	float dist;

	float3 radiance;
	float pdf;
	bool pdf_area_measure;

	float3 position;
	uint instance_primitive_index;
	float3 normal;

	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
	inline bool is_environment() { return instance_index() == INVALID_INSTANCE; }
};

inline void sample_light(inout LightSampleRecord ls, const float4 rnd, const float3 ref_pos) {
	if (gHasEnvironment && (!gHasEmissives || rnd.w <= gEnvironmentSampleProbability)) {
		// sample environment
		Environment env;
		env.load(gEnvironmentMaterialAddress);
		ls.radiance = env.sample(rnd.xy, ls.to_light, ls.pdf);
		if (gHasEmissives) ls.pdf *= gEnvironmentSampleProbability;
		BF_SET(ls.instance_primitive_index, INVALID_INSTANCE, 0, 16);
		ls.dist = POS_INFINITY;
		ls.pdf_area_measure = false;
	} else if (gHasEmissives) {
		// pick random light
		int li;
		if (gSampleLightPower) {
			li = dist1d_sample(gDistributions, gLightDistributionCDF, gLightCount, rnd.z);
			ls.pdf = dist1d_pdf(gDistributions, gLightDistributionPDF, li);
		} else {
			li = min(rnd.z*gLightCount, gLightCount-1);
			ls.pdf = 1/(float)gLightCount;
		}
		const uint light_instance_index = gLightInstances[li];
		BF_SET(ls.instance_primitive_index, light_instance_index, 0, 16);

		if (gHasEnvironment) ls.pdf *= 1 - gEnvironmentSampleProbability;

		const InstanceData instance = gInstances[light_instance_index];
		switch (instance.type()) {
			case INSTANCE_TYPE_SPHERE: {
				BF_SET(ls.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
				float2 uv;
				if (gUniformSphereSampling) {
					const float z = 1 - 2 * rnd.x;
					const float r_ = sqrt(max(0, 1 - z * z));
					const float phi = 2 * M_PI * rnd.y;
					const float radius = instance.radius();
					ls.pdf /= 4*M_PI*radius*radius;
					ls.pdf_area_measure = true;
					const float3 local_normal = float3(r_ * cos(phi), z, r_ * sin(phi));
					uv = cartesian_to_spherical_uv(local_normal);
					ls.position = gInstanceTransforms[light_instance_index].transform_point(radius * local_normal);
					ls.normal = normalize(gInstanceTransforms[light_instance_index].transform_vector(local_normal));
					ls.to_light = ls.position - ref_pos;
					ls.dist = length(ls.to_light);
					ls.to_light /= ls.dist;
				} else {
					const float3 center = gInstanceTransforms[light_instance_index].transform_point(0);
					float3 to_center = center - ref_pos;
					const float dist = length(to_center);
					const float rcp_dist = 1/dist;
					to_center *= rcp_dist;

					const float sinThetaMax2 = pow2(instance.radius() * rcp_dist);
					const float cosThetaMax = sqrt(max(0, 1 - sinThetaMax2));

					const float cosTheta = (1 - rnd.x) + rnd.x * cosThetaMax;
					const float sinTheta = sqrt(max(0, 1 - cosTheta * cosTheta));
					const float phi = rnd.y * 2 * M_PI;

					float3 T, B;
					make_orthonormal(to_center, T, B);

					ls.to_light = T*sinTheta*cos(phi) + B*sinTheta*sin(phi) + to_center*cosTheta;
					ls.dist = ray_sphere(ref_pos, ls.to_light, center, instance.radius()).x;
					ls.position = ref_pos + ls.to_light*ls.dist;
					ls.normal = normalize(ls.position - center);
					ls.pdf /= 2 * M_PI * (1 - cosThetaMax);
					ls.pdf_area_measure = false;
					uv = cartesian_to_spherical_uv(gInstanceInverseTransforms[light_instance_index].transform_vector(ls.normal));
				}

				Material m;
				m.load_and_sample(gInstances[ls.instance_index()].material_address(), uv, 0);
				ls.radiance = m.emission;

				break;
			}
			case INSTANCE_TYPE_TRIANGLES: {
				const float u = gHasEnvironment ? (rnd.w - gEnvironmentSampleProbability) / (1 - gEnvironmentSampleProbability) : rnd.w;
				const uint prim_index = min(u*instance.prim_count(), instance.prim_count() - 1);
				BF_SET(ls.instance_primitive_index, prim_index, 16, 16);
				const float a = sqrt(rnd.x);

				ShadingData sd;
				make_triangle_shading_data_from_barycentrics(sd, instance, gInstanceTransforms[light_instance_index], ls.primitive_index(), float2(1 - a, a*rnd.y));

				Material m;
				m.load_and_sample(gInstances[ls.instance_index()].material_address(), sd.uv, 0);
				ls.radiance = m.emission;

				ls.position = sd.position;
				ls.normal = sd.shading_normal();
				ls.to_light = sd.position - ref_pos;
				ls.dist = length(ls.to_light);
				ls.to_light /= ls.dist;
				ls.pdf /= sd.shape_area * instance.prim_count();
				ls.pdf_area_measure = true;
				break;
			}
		}
	}
}

#endif