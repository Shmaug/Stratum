#ifndef PATH_VERTEX_H
#define PATH_VERTEX_H

#include "image_value.hlsli"

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
	
	inline ShadingFrame shading_frame() {
		ShadingFrame frame;
		frame.n = shading_normal;
		frame.t = tangent.xyz;
    frame.b = normalize(cross(frame.n, frame.t))*tangent.w;
		return frame;
	}

	inline float4 sample_image(Texture2D<float4> img) {
		float w,h;
		img.GetDimensions(w,h);
		return img.SampleLevel(gSampler, uv, (gPushConstants.gSamplingFlags & SAMPLE_FLAG_RAY_CONE_LOD) ? log2(max(uv_screen_size*min(w,h), 1e-8f)) : 0);
	}
	
	inline float  eval(const ImageValue1 v) {
		if (!v.has_image()) return v.value;
		return v.value * sample_image(v.image())[v.channel()];
	}
	inline float2 eval(const ImageValue2 v) {
		if (!v.has_image()) return v.value;
		const float4 s = sample_image(v.image());
		return v.value * float2(s[v.channels()[0]], s[v.channels()[1]]);
	}
	inline float3 eval(const ImageValue3 v) {
		if (!v.has_image()) return v.value;
		const float4 s = sample_image(v.image());
		return v.value * float3(s[v.channels()[0]], s[v.channels()[1]], s[v.channels()[2]]);
	}
	inline float4 eval(const ImageValue4 v) {
		if (!v.has_image()) return v.value;
		return v.value * sample_image(v.image());
	}
};
inline PathVertexGeometry instance_triangle_geometry(const InstanceData instance, const uint3 tri, const float2 bary) {
	const PackedVertexData v0 = gVertices[tri.x];
	const PackedVertexData v1 = gVertices[tri.y];
	const PackedVertexData v2 = gVertices[tri.z];

	const float3 v1v0 = v1.position - v0.position;
	const float3 v2v0 = v2.position - v0.position;

	PathVertexGeometry r;
	r.position = instance.transform.transform_point(v0.position + v1v0*bary.x + v2v0*bary.y);

	const float3 dPds = instance.transform.transform_vector(-v2v0);
	const float3 dPdt = instance.transform.transform_vector(v1.position - v2.position);
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
		r.shading_normal = normalize(instance.transform.transform_vector(r.shading_normal));
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
	return r;
}
inline PathVertexGeometry instance_sphere_geometry(const InstanceData instance, const float3 local_position) {
	PathVertexGeometry r;
	r.position = instance.transform.transform_point(local_position);
	r.geometry_normal = r.shading_normal = normalize(instance.transform.transform_vector(local_position));
	r.uv = cartesian_to_spherical_uv(normalize(local_position));

	const float3 dpdu = instance.transform.transform_vector(float3(-sin(r.uv[0]) * sin(r.uv[1]), 0            , cos(r.uv[0]) * sin(r.uv[1])));
	const float3 dpdv = instance.transform.transform_vector(float3( cos(r.uv[0]) * cos(r.uv[1]), -sin(r.uv[1]), sin(r.uv[0]) * cos(r.uv[1])));
	r.tangent = float4(dpdu - r.geometry_normal*dot(r.geometry_normal, dpdu), 1);
	r.shape_area = 4*M_PI*instance.radius()*instance.radius();
	r.mean_curvature = 1/instance.radius();
	r.inv_uv_size = (length(dpdu) + length(dpdv)) / 2;
	return r;
}
inline PathVertexGeometry instance_volume_geometry(const InstanceData instance, const float3 local_position) {
	PathVertexGeometry r;
	r.position = instance.transform.transform_point(local_position);
	r.shape_area = 0;
	r.geometry_normal = r.shading_normal = 0;
	r.mean_curvature = 0;
	r.tangent = 0;
	r.uv = 0;
	r.inv_uv_size = 0;
	r.uv_screen_size = 0;
	return r;
}
inline PathVertexGeometry instance_geometry(const uint instance_primitive_index, const float3 position, const float2 bary) {
	if (BF_GET(instance_primitive_index, 0, 16) == INVALID_INSTANCE) {
		PathVertexGeometry r;
		r.position = position;
		r.shape_area = 0;
		r.geometry_normal = 0;
		r.mean_curvature = 0;
		r.shading_normal = 0;
		r.tangent = 0;
		r.uv = cartesian_to_spherical_uv(position);
		r.inv_uv_size = 1;
		return r;
	} else {
		const InstanceData instance = gInstances[BF_GET(instance_primitive_index, 0, 16)];
		const float3 local_pos = instance.inv_transform.transform_point(position);
		switch (instance.type()) {
		default:
		case INSTANCE_TYPE_SPHERE:
			return instance_sphere_geometry(instance, local_pos);
		case INSTANCE_TYPE_TRIANGLES:
			return instance_triangle_geometry(instance, load_tri(gIndices, instance, BF_GET(instance_primitive_index, 16, 16)), bary);
		case INSTANCE_TYPE_VOLUME:
			return instance_volume_geometry(instance, local_pos);
		}
	}
}

struct PathVertex {
	uint instance_primitive_index;
	uint material_address;
	float ray_radius;
	PathVertexGeometry g;
	
	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
};
inline void make_vertex(out PathVertex r, const uint instance_primitive_index, const float3 position, const float2 bary, const RayDifferential ray) {
	r.instance_primitive_index = instance_primitive_index;
	r.g = instance_geometry(instance_primitive_index, position, bary);
	if (r.instance_index() == INVALID_INSTANCE) {
		r.material_address = gPushConstants.gEnvironmentMaterialAddress;
		r.ray_radius = ray.radius;
	} else {
		r.material_address = gInstances[r.instance_index()].material_address();
		r.ray_radius = ray.differential_transfer(length(r.g.position - ray.origin));
	}
	r.g.uv_screen_size = r.ray_radius / r.g.inv_uv_size;
}

#endif