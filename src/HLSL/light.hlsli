#ifndef LIGHT_H
#define LIGHT_H

#include "common.hlsli"

template<typename Real>
struct PDFMeasure {
	Real pdf;
	Real G;
	bool is_solid_angle;
	inline float solid_angle() {return is_solid_angle ? pdf : pdf/G; }
	inline float area() {return is_solid_angle ? pdf*G : pdf; }
};
template<typename Real>
inline PDFMeasure<Real> make_area_pdf(const Real pdfA, const Real cos_theta, const Real dist) {
	PDFMeasure<Real> pdf;
	pdf.pdf = pdfA;
	pdf.G = cos_theta / pow2(dist);
	pdf.is_solid_angle = false;
	return pdf;
}
template<typename Real>
inline PDFMeasure<Real> make_solid_angle_pdf(const Real pdfW, const Real cos_theta, const Real dist) {
	PDFMeasure<Real> pdf;
	pdf.pdf = pdfW;
	pdf.G = cos_theta / pow2(dist);
	pdf.is_solid_angle = true;
	return pdf;
}

struct LightSampleRecord {
	float3 position_or_bary;
	uint instance_primitive_index;
	float3 radiance;
	uint material_address;
	float3 to_light;
	float dist;
	PDFMeasure<float> pdf;

	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
};

inline LightSampleRecord sample_light_or_environment(inout rng_t rng, const PathVertexGeometry v, const RayDifferential ray) {
	LightSampleRecord ls;
	ls.pdf.pdf = 0;
	if (gSampleBG && (!gSampleLights || rng.next() <= gPushConstants.gEnvironmentSampleProbability)) {
		// sample environment
		BF_SET(ls.instance_primitive_index, INVALID_INSTANCE, 0, 16);
		BF_SET(ls.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
		ls.material_address = gPushConstants.gEnvironmentMaterialAddress;
		Environment env;
		uint tmp = gPushConstants.gEnvironmentMaterialAddress+4;
		env.load(gMaterialData, tmp);
		const BSDFSampleRecord<float> s = env.sample<float,true>(float3(rng.next(), rng.next(), rng.next()), -ray.direction, v);
		ls.radiance = s.eval.f;
		ls.to_light = s.dir_out;
		ls.dist = 1.#INF;
		ls.pdf = make_solid_angle_pdf<float>(s.eval.pdfW, 1, 1);
		ls.position_or_bary = s.dir_out;
	} else if (gSampleLights) {
		// sample a light
		BF_SET(ls.instance_primitive_index, min(uint(rng.next() * gPushConstants.gLightCount), gPushConstants.gLightCount-1), 0, 16);
		
		PathVertexGeometry g;
		const InstanceData instance = gInstances[gLightInstances[ls.instance_index()]];
		ls.material_address = instance.material_address();
		switch (instance.type()) {
			case INSTANCE_TYPE_SPHERE: {
				BF_SET(ls.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
				const float3 center = instance.transform.transform_point(0);
				float3 to_center = center - v.position;
				const float dist = length(to_center);
				const float rcp_dist = 1/dist;
				to_center *= rcp_dist;

				const float r = instance.radius();
				const float sinThetaMax2 = r*r * rcp_dist * rcp_dist;
				const float cosThetaMax = sqrt(max(0, 1 - sinThetaMax2));

				const float u1 = rng.next();
				const float u2 = rng.next();
				const float cosTheta = (1 - u1) + u1 * cosThetaMax;
				const float sinTheta = sqrt(max(0, 1 - cosTheta * cosTheta));
				const float phi = u2 * 2 * M_PI;
				
				float3 T, B;
				make_orthonormal(to_center, T, B);

				ls.to_light = T*sinTheta*cos(phi) + B*sinTheta*sin(phi) + to_center*cosTheta;
				ls.dist = ray_sphere(v.position, ls.to_light, center, r).x;
				ls.position_or_bary = v.position + ls.to_light*ls.dist;
				
				g = make_sphere_geometry(instance.transform, r, ls.position_or_bary);

				ls.pdf = make_solid_angle_pdf(1 / (2 * M_PI * (1 - cosThetaMax)), abs(dot(ls.to_light, g.geometry_normal)), ls.dist);
				break;
			}
			case INSTANCE_TYPE_TRIANGLES: {
				const uint prim_index = min(rng.next()*instance.prim_count(), instance.prim_count() - 1);
				BF_SET(ls.instance_primitive_index, prim_index, 16, 16);
				const float a = sqrt(rng.next());
				ls.position_or_bary = float3(1 - a, a*rng.next(), 0);
				const uint3 tri = load_tri(gIndices, instance, ls.primitive_index());
				g = make_triangle_geometry(instance.transform, tri, ls.position_or_bary.xy);

				ls.to_light = g.position - v.position;
				ls.dist = length(ls.to_light);
				ls.to_light /= ls.dist;

				ls.pdf = make_area_pdf(1 / (g.shape_area * (float)instance.prim_count()), abs(dot(ls.to_light, g.geometry_normal)), ls.dist);
				break;
			}
		}
		ls.radiance = eval_material_emission<float>(gMaterialData, ls.material_address, g);
		ls.pdf.pdf /= (float)gPushConstants.gLightCount;
	}
	if (gSampleBG && gSampleLights)
		ls.pdf.pdf *= gSampleBG ? gPushConstants.gEnvironmentSampleProbability : 1 - gPushConstants.gEnvironmentSampleProbability;
	return ls;
}

inline PDFMeasure<float> light_sample_pdf(const PathVertex light_vertex, const RayDifferential ray, const float dist) {
	PDFMeasure<float> pdf;

	if (light_vertex.instance_index() == INVALID_INSTANCE) {
		if (!gSampleBG) return make_area_pdf<float>(0, 1, 1);
		Environment env;
		uint tmp = gPushConstants.gEnvironmentMaterialAddress+4;
		env.load(gMaterialData, tmp);
		return make_solid_angle_pdf<float>(env.eval_pdfW(ray.direction)*gPushConstants.gEnvironmentSampleProbability, 1, 1);
	} else {
		if (!gSampleLights) return make_area_pdf<float>(0, 1, 1);
		const InstanceData instance = gInstances[light_vertex.instance_index()];
		switch (instance.type()) {
			case INSTANCE_TYPE_SPHERE: {
				const float3 center = instance.transform.transform_point(0);
				const float3 to_center = center - ray.origin;
				const float sinThetaMax2 = instance.radius()*instance.radius() / dot(to_center, to_center);
				const float cosThetaMax = sqrt(max(0, 1 - sinThetaMax2));
				pdf = make_solid_angle_pdf(1 / (2 * M_PI * (1 - cosThetaMax)), abs(dot(ray.direction, light_vertex.g.geometry_normal)), dist);
				break;
			}
			
			case INSTANCE_TYPE_TRIANGLES: {
				pdf = make_area_pdf(1 / ((float)light_vertex.g.shape_area * instance.prim_count()), abs(dot(ray.direction, light_vertex.g.geometry_normal)), dist);
				break;
			}
		}
		pdf.pdf /= (float)gPushConstants.gLightCount;
		if (gSampleBG) pdf.pdf *= 1 - gPushConstants.gEnvironmentSampleProbability;
		return pdf;
	}
}

inline float mis_heuristic(const float a, const float b) {
	const float a2 = a * a;
	return a2 / (b*b + a2);
}
#endif