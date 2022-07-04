#ifndef SHADING_DATA_H
#define SHADING_DATA_H

#include "scene.h"

#define SHADING_FLAG_FRONT_FACE BIT(0)
#define SHADING_FLAG_FLIP_BITANGENT BIT(1)

// 48 bytes
struct ShadingData {
	float3 position;
	uint flags;
	uint packed_geometry_normal;
	uint packed_shading_normal;
	uint packed_tangent;
	float shape_area;
	float2 uv;
	float uv_screen_size;
	float mean_curvature;
#ifdef __HLSL__
	inline float3 geometry_normal() { return unpack_normal_octahedron(packed_geometry_normal); }
	inline float3 shading_normal() { return unpack_normal_octahedron(packed_shading_normal); }
	inline float3 tangent() { return unpack_normal_octahedron(packed_tangent); }

	inline float3 to_world(const float3 v) {
		const float3 n = shading_normal();
		const float3 t = tangent();
		return v.x*t + v.y*cross(n, t)*((flags & SHADING_FLAG_FLIP_BITANGENT) ? -1 : 1) + v.z*n;
	}
	inline float3 to_local(const float3 v) {
		const float3 n = shading_normal();
		const float3 t = tangent();
		return float3(dot(v, t), dot(v, cross(n, t)*((flags & SHADING_FLAG_FLIP_BITANGENT) ? -1 : 1)), dot(v, n));
	}
#endif
};

#ifdef __HLSL__

// assigns everything except r.position
inline void make_triangle_shading_data(inout ShadingData r, const TransformData transform, const float2 bary, const PackedVertexData v0, const PackedVertexData v1, const PackedVertexData v2) {
	r.uv = v0.uv() + (v1.uv() - v0.uv())*bary.x + (v2.uv() - v0.uv())*bary.y;

	const float3 dPds = transform.transform_vector(v0.position - v2.position);
	const float3 dPdt = transform.transform_vector(v1.position - v2.position);
	float3 geometry_normal = cross(dPds, dPdt);
	const float area2 = length(geometry_normal);
	geometry_normal /= area2;
	r.packed_geometry_normal = pack_normal_octahedron(geometry_normal);
	r.shape_area = area2/2;

	// [du/ds, du/dt]
	// [dv/ds, dv/dt]
	const float2 duvds = v2.uv() - v0.uv();
	const float2 duvdt = v2.uv() - v1.uv();
	// The inverse of this matrix is
	// (1/det) [ dv/dt, -du/dt]
	//         [-dv/ds,  du/ds]
	// where det = duds * dvdt - dudt * dvds
	const float det = duvds[0] * duvdt[1] - duvdt[0] * duvds[1];
	const float inv_det = 1/det;
	const float dsdu =  duvdt[1] * inv_det;
	const float dtdu = -duvds[1] * inv_det;
	const float dsdv =  duvdt[0] * inv_det;
	const float dtdv = -duvds[0] * inv_det;
	float3 dPdu,dPdv;
	if (det != 0) {
		// Now we just need to do the matrix multiplication
		dPdu = -(dPds * dsdu + dPdt * dtdu);
		dPdv = -(dPds * dsdv + dPdt * dtdv);
		r.uv_screen_size = 1 / max(length(dPdu), length(dPdv));
	} else {
		make_orthonormal(geometry_normal, dPdu, dPdv);
		r.uv_screen_size = 0;
	}

	float3 shading_normal = v0.normal + (v1.normal - v0.normal)*bary.x + (v2.normal - v0.normal)*bary.y;
	if (all(shading_normal.xyz == 0) || any(isnan(shading_normal))) {
		r.packed_shading_normal = r.packed_geometry_normal;
		r.packed_tangent = pack_normal_octahedron(dPdu);
		r.mean_curvature = 0;
	} else {
		shading_normal = normalize(transform.transform_vector(shading_normal));
		const float3 tangent = normalize(dPdu - shading_normal*dot(shading_normal, dPdu));
		r.packed_shading_normal = pack_normal_octahedron(shading_normal);
		r.packed_tangent = pack_normal_octahedron(tangent);

		// force geometry normal to agree with shading normal
		if (dot(shading_normal, geometry_normal) < 0)
			r.packed_geometry_normal = pack_normal_octahedron(-geometry_normal);

		const float3 dNds = v2.normal - v0.normal;
		const float3 dNdt = v2.normal - v1.normal;
		const float3 dNdu = dNds * dsdu + dNdt * dtdu;
		const float3 dNdv = dNds * dsdv + dNdt * dtdv;
		const float3 bitangent = normalize(cross(shading_normal, tangent));
		r.mean_curvature = (dot(dNdu, tangent) + dot(dNdv, bitangent)) / 2;
	}
}
inline void make_triangle_shading_data_from_barycentrics(out ShadingData r, const InstanceData instance, const TransformData transform, const uint primitive_index, const float2 bary, out float3 local_position) {
	const uint3 tri = load_tri(gIndices, instance, primitive_index);
	const PackedVertexData v0 = gVertices[tri.x];
	const PackedVertexData v1 = gVertices[tri.y];
	const PackedVertexData v2 = gVertices[tri.z];
	const float3 v1v0 = v1.position - v0.position;
	const float3 v2v0 = v2.position - v0.position;
	local_position = v0.position + v1v0*bary.x + v2v0*bary.y;
	r.position = transform.transform_point(local_position);
	make_triangle_shading_data(r, transform, bary, v0, v1, v2);
}
inline void make_triangle_shading_data_from_position(out ShadingData r, const InstanceData instance, const TransformData transform, const uint primitive_index, const float3 local_position) {
	r.position = transform.transform_point(local_position);
	const uint3 tri = load_tri(gIndices, instance, primitive_index);
	const PackedVertexData v0 = gVertices[tri.x];
	const PackedVertexData v1 = gVertices[tri.y];
	const PackedVertexData v2 = gVertices[tri.z];

	const float3 v1v0 = v1.position - v0.position;
	const float3 v2v0 = v2.position - v0.position;
	const float3 p_v0 = local_position - v0.position;
	const float d00 = dot(v1v0, v1v0);
	const float d01 = dot(v1v0, v2v0);
	const float d11 = dot(v2v0, v2v0);
	const float d20 = dot(p_v0, v1v0);
	const float d21 = dot(p_v0, v2v0);
	const float2 bary = float2((d11 * d20 - d01 * d21), (d00 * d21 - d01 * d20)) / (d00 * d11 - d01 * d01);

	r.position = transform.transform_point(local_position);
 	make_triangle_shading_data(r, transform, bary, v0, v1, v2);
}
inline void make_sphere_shading_data(out ShadingData r, const InstanceData instance, const TransformData transform, const float3 local_position) {
	const float3 normal = normalize(transform.transform_vector(local_position));
	r.position = transform.transform_point(local_position);
	r.packed_geometry_normal = r.packed_shading_normal = pack_normal_octahedron(normal);
	const float radius = instance.radius();
	r.shape_area = 4*M_PI*radius*radius;
	r.mean_curvature = 1/radius;
	r.uv = cartesian_to_spherical_uv(normalize(local_position));
	const float3 dpdu = transform.transform_vector(float3(-sin(r.uv[0]) * sin(r.uv[1]), 0            , cos(r.uv[0]) * sin(r.uv[1])));
	const float3 dpdv = transform.transform_vector(float3( cos(r.uv[0]) * cos(r.uv[1]), -sin(r.uv[1]), sin(r.uv[0]) * cos(r.uv[1])));
	r.packed_tangent = pack_normal_octahedron(normalize(dpdu - normal*dot(normal, dpdu)));
	r.uv_screen_size = (length(dpdu) + length(dpdv)) / 2;
}
inline void make_volume_shading_data(out ShadingData r, const InstanceData instance, const TransformData transform, const float3 local_position) {
	r.position = transform.transform_point(local_position);
	r.shape_area = 0;
	r.uv_screen_size = 0;
}

inline void make_shading_data(out ShadingData r, const uint instance_index, const uint primitive_index, const float3 local_position) {
	if (instance_index == INVALID_INSTANCE) {
		r.position = local_position;
		r.shape_area = 0;
		r.uv_screen_size = 0;
	} else {
		const InstanceData instance = gInstances[instance_index];
		switch (instance.type()) {
		default:
		case INSTANCE_TYPE_SPHERE:
			make_sphere_shading_data(r, instance, gInstanceTransforms[instance_index], local_position);
			break;
		case INSTANCE_TYPE_VOLUME:
			make_volume_shading_data(r, instance, gInstanceTransforms[instance_index], local_position);
			break;
		case INSTANCE_TYPE_TRIANGLES:
			make_triangle_shading_data_from_position(r, instance, gInstanceTransforms[instance_index], primitive_index, local_position);
			break;
		}
	}
}

#endif

#endif