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
	uint material_address;

	float3 local_position; // needed for reservoirs

	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
	inline bool is_environment() { return instance_index() == INVALID_INSTANCE; }
};

inline uint sample_light(out float pdf, float rnd) {
	int li;
	if (gSampleLightPower) {
		li = dist1d_sample(gDistributions, gLightDistributionCDF, gLightCount, rnd);
		pdf = dist1d_pdf(gDistributions, gLightDistributionPDF, li);
	} else {
		// pick random light
		li = min(rnd*gLightCount, gLightCount-1);
		pdf = 1/(float)gLightCount;
	}
	return gLightInstances[li];
}

inline void sample_point_on_light(inout LightSampleRecord ls, const float4 rnd, const float3 ref_pos) {
	if (gHasEnvironment && (!gHasEmissives || rnd.w <= gEnvironmentSampleProbability)) {
		// sample environment
		Environment env;
		env.load(gEnvironmentMaterialAddress);
		ls.radiance = env.sample(rnd.xy, ls.to_light, ls.pdf);
		if (gHasEmissives) ls.pdf *= gEnvironmentSampleProbability;
		BF_SET(ls.instance_primitive_index, INVALID_INSTANCE, 0, 16);
		ls.dist = POS_INFINITY;
		ls.pdf_area_measure = false;
		ls.material_address = gEnvironmentMaterialAddress;
		ls.local_position = ls.to_light;
	} else if (gHasEmissives) {
		const float u = gHasEnvironment ? (rnd.w - gEnvironmentSampleProbability) / (1 - gEnvironmentSampleProbability) : rnd.w;
		const uint light_instance_index = sample_light(ls.pdf, u);
		BF_SET(ls.instance_primitive_index, light_instance_index, 0, 16);

		if (gHasEnvironment) ls.pdf *= 1 - gEnvironmentSampleProbability;

		float2 uv;
		const InstanceData instance = gInstances[light_instance_index];
		ls.material_address = instance.material_address();
		switch (instance.type()) {
			case INSTANCE_TYPE_SPHERE: {
				BF_SET(ls.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
				const float r = instance.radius();
				if (gUniformSphereSampling) {
					ls.pdf /= 4*M_PI*r*r;
					ls.pdf_area_measure = true;
					const float z = 1 - 2 * rnd.x;
					const float r_ = sqrt(max(0, 1 - z * z));
					const float phi = 2 * M_PI * rnd.y;
					const float3 local_normal = float3(r_ * cos(phi), z, r_ * sin(phi));
					uv = cartesian_to_spherical_uv(local_normal);
					ls.local_position = local_normal * r;
					ls.position = gInstanceTransforms[light_instance_index].transform_point(r * local_normal);
					uv = cartesian_to_spherical_uv(local_normal);
					ls.normal = normalize(gInstanceTransforms[light_instance_index].transform_vector(local_normal));
					ls.to_light = ls.position - ref_pos;
					ls.dist = length(ls.to_light);
					ls.to_light /= ls.dist;
				} else {
					const float3 center = float3(
						gInstanceTransforms[light_instance_index].m[0][3],
						gInstanceTransforms[light_instance_index].m[1][3],
						gInstanceTransforms[light_instance_index].m[2][3]);
					float3 to_center = center - ref_pos;
					const float dist = length(to_center);
					to_center /= dist;

					// These are not exactly "elevation" and "azimuth": elevation here
					// stands for the extended angle of the cone, and azimuth here stands
					// for the polar coordinate angle on the substended disk.
					// I just don't like the theta/phi naming convention...
					const float sin_elevation_max_sq = r * r / pow2(dist);
					const float cos_elevation_max = sqrt(max(0, 1 - sin_elevation_max_sq));

					ls.pdf /= 2 * M_PI * (1 - cos_elevation_max);
					ls.pdf_area_measure = false;

					// Uniformly interpolate between 1 (angle 0) and max
					const float cos_elevation = (1 - rnd.x) + rnd.x * cos_elevation_max;
					const float sin_elevation = sqrt(max(0, 1 - cos_elevation * cos_elevation));
					const float azimuth = rnd.y * 2 * M_PI;
					const float dc = dist;
					const float ds = dc * cos_elevation - sqrt(max(0, r * r - dc * dc * sin_elevation * sin_elevation));
					const float cos_alpha = (dc * dc + r * r - ds * ds) / (2 * dc * r);
					const float sin_alpha = sqrt(max(0, 1 - cos_alpha * cos_alpha));

					float3 T, B;
					make_orthonormal(to_center, T, B);

					ls.normal = (T * sin_alpha * cos(azimuth) + B * sin_alpha * sin(azimuth) - to_center * cos_alpha);
					ls.position = center + r * ls.normal;
					ls.to_light = ls.position - ref_pos;
					ls.dist = length(ls.to_light);
					ls.to_light /= ls.dist;

					const float3 local_normal = gInstanceInverseTransforms[light_instance_index].transform_vector(ls.normal);
					ls.local_position = local_normal*r;
					uv = cartesian_to_spherical_uv(local_normal);
				}

				break;
			}
			case INSTANCE_TYPE_TRIANGLES: {
				const uint prim_index = min(rnd.z*instance.prim_count(), instance.prim_count() - 1);
				BF_SET(ls.instance_primitive_index, prim_index, 16, 16);
				const float a = sqrt(rnd.x);
				const float2 bary = float2(1 - a, a*rnd.y);

				ShadingData sd;
				make_triangle_shading_data_from_barycentrics(sd, instance, gInstanceTransforms[light_instance_index], prim_index, bary, ls.local_position);
				uv = sd.uv;

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

		if (ls.pdf > 0) {
			Material m;
			m.load_and_sample(ls.material_address, uv, 0);
			ls.radiance = m.emission;
		}
	}
}

inline void point_on_light_pdf(inout float pdf, inout bool pdf_area_measure, const IntersectionVertex _isect) {
	if (_isect.instance_index() == INVALID_INSTANCE) {
		if (!gHasEnvironment) { pdf = 0; return; }
		if (gHasEmissives) pdf *= gEnvironmentSampleProbability;
		Environment env;
		env.load(gEnvironmentMaterialAddress);
		pdf *= env.eval_pdf(_isect.sd.position);
		pdf_area_measure = false;
	} else  {
		if (!gHasEmissives) { pdf = 0; return; }
		if (gHasEnvironment) pdf *= 1 - gEnvironmentSampleProbability;
		if (gSampleLightPower)
			pdf *= dist1d_pdf(gDistributions, gLightDistributionPDF, gInstances[_isect.instance_index()].light_index());
		else
			pdf /= gLightCount;
		pdf *= _isect.shape_pdf;
		pdf_area_measure = _isect.shape_pdf_area_measure;
	}
}

#endif