#ifndef PATH_VERTEX_H
#define PATH_VERTEX_H

#include "scene.hlsli"

struct ShadingFrame {
	float3 t, b, n;
	inline void flip() {
		t = -t;
		b = -b;
		n = -n;
	}
	inline float3 to_world(const float3 v_l) { return v_l[0]*t + v_l[1]*b + v_l[2]*n; }
	inline float3 to_local(const float3 v_w) { return float3(dot(v_w, t), dot(v_w, b), dot(v_w, n)); }
};

struct PathVertexGeometry {
	float3 position;
	float shape_area;
  	float3 geometry_normal;
	float mean_curvature;
  	float3 shading_normal;
	uint front_face;
  	float4 tangent;
	float2 uv;
	float inv_uv_size;
	float uv_screen_size;
	
#ifdef __HLSL_VERSION
	inline ShadingFrame shading_frame() {
		ShadingFrame frame;
		frame.n = shading_normal;
		frame.t = tangent.xyz;
    	frame.b = normalize(cross(frame.n, frame.t))*tangent.w;
		return frame;
	}
#endif
};

struct PathState {
	uint4 rng_state;
	float3 throughput;
	float eta_scale;
	float3 ray_origin;
	uint radius_spread;
	uint instance_primitive_index;
	uint vol_index;
	float pdfA;
	uint pad1;

#ifdef __HLSL_VERSION
	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
	inline min16float radius() { return (min16float)f16tof32(radius_spread); }
	inline min16float spread() { return (min16float)f16tof32(radius_spread>>16); }
#endif
};

struct DirectLightSample {
	float3 radiance;
	float G;
	float3 to_light;
	uint pdfs;

#ifdef __HLSL_VERSION
	inline float pdfA() { return f16tof32(pdfs); }
	inline float T_dir_pdf() { return f16tof32(pdfs>>16); }
#endif
};

struct LightPathVertex {
	float3 throughput;
	float pdfA;
	uint instance_primitive_index;
	uint vertex;
	uint pad[2];
};

struct MaterialEvalRecord {
	float3 f;
	float pdfW;
};
struct MaterialSampleRecord {
	MaterialEvalRecord eval;
	float3 dir_out;
	// The index of refraction ratio. Set to 0 if it's not a transmission event.
	uint eta_roughness;
#ifdef __HLSL_VERSION
	inline float eta() { return f16tof32(eta_roughness); }
	inline float roughness() { return f16tof32(eta_roughness >> 16); }
#endif
};

struct PathTracePushConstants {
	uint gViewCount;
	uint gLightCount;
	uint gEnvironmentMaterialAddress;
	float gEnvironmentSampleProbability;	
	uint gRandomSeed;
	uint gReservoirSamples;
	uint gMaxNullCollisions;
	uint gMaxDepth;
	uint gMinDepth;
};

#ifdef __HLSL_VERSION
#ifdef PT_DESCRIPTOR_SET_0
inline void instance_triangle_geometry(out PathVertexGeometry r, const uint instance_index, const uint3 tri, const float2 bary) {
	const PackedVertexData v0 = gVertices[tri.x];
	const PackedVertexData v1 = gVertices[tri.y];
	const PackedVertexData v2 = gVertices[tri.z];

	const float3 v1v0 = v1.position - v0.position;
	const float3 v2v0 = v2.position - v0.position;

	r.position = gInstances[instance_index].transform.transform_point(v0.position + v1v0*bary.x + v2v0*bary.y);

	const float3 dPds = gInstances[instance_index].transform.transform_vector(-v2v0);
	const float3 dPdt = gInstances[instance_index].transform.transform_vector(v1.position - v2.position);
	const float3 ng = cross(dPds, dPdt);
	const float area2 = length(ng);
	r.geometry_normal = ng/area2;
	r.shape_area = area2/2;

	r.uv = v0.uv() + (v1.uv() - v0.uv())*bary.x + (v2.uv() - v0.uv())*bary.y;

	// [du/ds, du/dt]
	// [dv/ds, dv/dt]
	const float2 duvds = v2.uv() - v0.uv();
	const float2 duvdt = v2.uv() - v1.uv();
	// The inverse of this matrix is
	// (1/det) [ dv/dt, -du/dt]
	//         [-dv/ds,  du/ds]
	// where det = duds * dvdt - dudt * dvds
	const float det  = duvds[0] * duvdt[1] - duvdt[0] * duvds[1];
	const float inv_det  = 1/det;
	const float dsdu =  duvdt[1] * inv_det;
	const float dtdu = -duvds[1] * inv_det;
	const float dsdv =  duvdt[0] * inv_det;
	const float dtdv = -duvds[0] * inv_det;
	float3 dPdu,dPdv;
	if (det != 0) {
		// Now we just need to do the matrix multiplication
		dPdu = -(dPds * dsdu + dPdt * dtdu);
		dPdv = -(dPds * dsdv + dPdt * dtdv);
		r.inv_uv_size = max(length(dPdu), length(dPdv));
	} else
		make_orthonormal(r.geometry_normal, dPdu, dPdv);

	r.shading_normal = v0.normal + (v1.normal - v0.normal)*bary.x + (v2.normal - v0.normal)*bary.y;
	if (all(r.shading_normal.xyz == 0) || any(isnan(r.shading_normal))) {
		r.shading_normal = r.geometry_normal;
		r.tangent = float4(dPdu, 1);
		r.mean_curvature = 0;
	} else {
		r.shading_normal = normalize(gInstances[instance_index].transform.transform_vector(r.shading_normal));
		if (dot(r.shading_normal, r.geometry_normal) < 0) r.geometry_normal = -r.geometry_normal;
		r.tangent = float4(normalize(dPdu - r.shading_normal*dot(r.shading_normal, dPdu)), 1);
		const float3 dNds = v2.normal - v0.normal;
		const float3 dNdt = v2.normal - v1.normal;
		const float3 dNdu = dNds * dsdu + dNdt * dtdu;
		const float3 dNdv = dNds * dsdv + dNdt * dtdv;
		const float3 bitangent = normalize(cross(r.shading_normal, r.tangent.xyz));
		r.mean_curvature = (dot(dNdu, r.tangent.xyz) + dot(dNdv, bitangent)) / 2;
	}
		
	//if (dot(bitangent, dPdv) < 0) r.tangent.xyz = -r.tangent.xyz;
}
inline void instance_sphere_geometry(out PathVertexGeometry r, const uint instance_index, const float3 local_position) {
	r.position = gInstances[instance_index].transform.transform_point(local_position);
	r.geometry_normal = r.shading_normal = normalize(gInstances[instance_index].transform.transform_vector(local_position));
	const float radius = gInstances[instance_index].radius();
	r.shape_area = 4*M_PI*radius*radius;
	r.mean_curvature = 1/radius;
	r.uv = cartesian_to_spherical_uv(normalize(local_position));
	const float3 dpdu = gInstances[instance_index].transform.transform_vector(float3(-sin(r.uv[0]) * sin(r.uv[1]), 0            , cos(r.uv[0]) * sin(r.uv[1])));
	const float3 dpdv = gInstances[instance_index].transform.transform_vector(float3( cos(r.uv[0]) * cos(r.uv[1]), -sin(r.uv[1]), sin(r.uv[0]) * cos(r.uv[1])));
	r.tangent = float4(dpdu - r.geometry_normal*dot(r.geometry_normal, dpdu), 1);
	r.inv_uv_size = (length(dpdu) + length(dpdv)) / 2;
}
inline void instance_volume_geometry(out PathVertexGeometry r, const uint instance_index, const float3 local_position) {
	r.position = gInstances[instance_index].transform.transform_point(local_position);
	r.shape_area = 0;
	r.geometry_normal = r.shading_normal = 0;
	r.mean_curvature = 0;
	r.tangent = 0;
	r.uv = 0;
	r.inv_uv_size = 0;
	r.uv_screen_size = 0;
}
inline void instance_geometry(out PathVertexGeometry r, const uint instance_primitive_index, const float3 position, const float2 bary) {
	if (BF_GET(instance_primitive_index, 0, 16) == INVALID_INSTANCE) {
		r.position = position;
		r.shape_area = 0;
		r.geometry_normal = 0;
		r.mean_curvature = 0;
		r.shading_normal = 0;
		r.tangent = 0;
		r.uv = cartesian_to_spherical_uv(position);
		r.inv_uv_size = 1;
	} else {
		switch (gInstances[BF_GET(instance_primitive_index, 0, 16)].type()) {
		default:
		case INSTANCE_TYPE_SPHERE:
			instance_sphere_geometry(r, BF_GET(instance_primitive_index, 0, 16), position);
			break;
		case INSTANCE_TYPE_VOLUME:
			r.position = position;
			r.shape_area = 0;
			r.geometry_normal = r.shading_normal = 0;
			r.mean_curvature = 0;
			r.tangent = 0;
			r.uv = 0;
			r.inv_uv_size = 0;
			r.uv_screen_size = 0;
			break;
		case INSTANCE_TYPE_TRIANGLES:
			instance_triangle_geometry(r, BF_GET(instance_primitive_index, 0, 16), load_tri(gIndices, gInstances[BF_GET(instance_primitive_index, 0, 16)], BF_GET(instance_primitive_index, 16, 16)), bary);
			break;
		}
	}
}
#endif
#endif

#endif