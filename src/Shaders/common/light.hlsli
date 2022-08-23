#ifndef LIGHT_H
#define LIGHT_H

#include "../common.h"

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

	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
	inline bool is_environment() { return instance_index() == INVALID_INSTANCE; }
};

inline uint sample_light(out float pdf, const float rnd) {
	int li;
	if (gSampleLightPower) {
		li = dist1d_sample(gDistributions, gLightDistributionCDF, gLightCount, rnd);
		pdf = dist1d_pdf(gDistributions, gLightDistributionPDF, li);
	} else {
		// pick random light
		li = rnd*(gLightCount*.9999);
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
	} else if (gHasEmissives) {
		const uint light_instance_index = sample_light(ls.pdf, gHasEnvironment ? (rnd.w - gEnvironmentSampleProbability) / (1 - gEnvironmentSampleProbability) : rnd.w);
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

					// Compute theta and phi values for sample in cone
					const float sinThetaMax = r / dist;
					const float sinThetaMax2 = sinThetaMax * sinThetaMax;
					const float invSinThetaMax = 1 / sinThetaMax;
					const float cosThetaMax = sqrt(max(0, 1 - sinThetaMax2));

					ls.pdf /= 2 * M_PI * (1 - cosThetaMax);
					ls.pdf_area_measure = false;

					float cosTheta  = (cosThetaMax - 1) * rnd.x + 1;
					float sinTheta2 = 1 - cosTheta * cosTheta;

					if (sinThetaMax2 < 0.00068523 /* sin^2(1.5 deg) */) {
						/* Fall back to a Taylor series expansion for small angles, where
						the standard approach suffers from severe cancellation errors */
						sinTheta2 = sinThetaMax2 * rnd.x;
						cosTheta = sqrt(1 - sinTheta2);
					}

					// Compute angle alpha from center of sphere to sampled point on surface
					const float cosAlpha = sinTheta2 * invSinThetaMax + cosTheta * sqrt(max(0, 1 - sinTheta2 * invSinThetaMax * invSinThetaMax));
					const float sinAlpha = sqrt(max(0, 1 - cosAlpha*cosAlpha));
					const float phi = rnd.y * 2 * M_PI;

					float3 T, B;
					make_orthonormal(to_center, T, B);

					ls.normal = -(T * sinAlpha * cos(phi) + B * sinAlpha * sin(phi) + to_center * cosAlpha);
					ls.position = center + r * ls.normal;
					ls.to_light = ls.position - ref_pos;
					ls.dist = length(ls.to_light);
					ls.to_light /= ls.dist;

					const float3 local_normal = gInstanceInverseTransforms[light_instance_index].transform_vector(ls.normal);
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
				make_triangle_shading_data(sd, instance, gInstanceTransforms[light_instance_index], prim_index, bary);
				uv = sd.uv;

				ls.position = sd.position;
				ls.normal = sd.geometry_normal();
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
			m.load(ls.material_address, uv, 0);
			ls.radiance = m.emission;
		}
	}
}

inline void point_on_light_pdf(inout float pdf, out bool pdf_area_measure, const IntersectionVertex _isect) {
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