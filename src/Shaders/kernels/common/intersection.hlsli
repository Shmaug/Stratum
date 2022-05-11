#ifndef INTERSECTION_H
#define INTERSECTION_H

#include "../../shading_data.h"
#include "../../materials/medium.hlsli"

struct IntersectionVertex {
	ShadingData sd;
	uint instance_primitive_index;
	inline uint instance_index()  { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
	SLANG_MUTATING
	inline uint set_instance_index(const uint v)  { return BF_SET(instance_primitive_index, v, 0, 16); }
	SLANG_MUTATING
	inline uint set_primitive_index(const uint v) { return BF_SET(instance_primitive_index, v, 16, 16); }
};

#ifdef GROUPSHARED_ISECT
// 12 floats
groupshared IntersectionVertex s_isect[GROUPSIZE_X*GROUPSIZE_Y];
#define _isect s_isect[group_thread_index]
#define DECLARE_ISECT
#define DECLARE_ISECT_ARG const uint group_thread_index
#define PASS_ISECT_ARG group_thread_index
#else
#define DECLARE_ISECT IntersectionVertex _isect;
#define DECLARE_ISECT_ARG inout IntersectionVertex _isect
#define PASS_ISECT_ARG _isect
#endif

float trace_ray(const float3 origin, const float3 direction, const float t_max, DECLARE_ISECT_ARG, const bool accept_first = false) {
	if (gCountRays) InterlockedAdd(gRayCount[0], 1);

	RayQuery<RAY_FLAG_NONE> rayQuery;
	RayDesc rayDesc;
	rayDesc.Origin = origin;
	rayDesc.Direction = direction;
	rayDesc.TMin = 0;
	rayDesc.TMax = t_max;
	rayQuery.TraceRayInline(gScene, accept_first ? RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH : RAY_FLAG_NONE, ~0, rayDesc);
	while (rayQuery.Proceed()) {
		const uint hit_instance = rayQuery.CandidateInstanceID();
		switch (rayQuery.CandidateType()) {
			case CANDIDATE_PROCEDURAL_PRIMITIVE: {
				const InstanceData instance = gInstances[hit_instance];
				switch (instance.type()) {
					case INSTANCE_TYPE_SPHERE: {
						const float2 st = ray_sphere(rayQuery.CandidateObjectRayOrigin(), rayQuery.CandidateObjectRayDirection(), 0, instance.radius());
						if (st.x < st.y) {
							const float t = st.x > rayQuery.RayTMin() ? st.x : st.y;
							if (t < rayQuery.CommittedRayT() && t > rayQuery.RayTMin())
								rayQuery.CommitProceduralPrimitiveHit(t);
						}
						break;
					}

					case INSTANCE_TYPE_VOLUME: {
						pnanovdb_buf_t buf = gVolumes[instance.volume_index()];
						pnanovdb_grid_handle_t grid_handle = {0};
						pnanovdb_root_handle_t root = pnanovdb_tree_get_root(buf, pnanovdb_grid_get_tree(buf, grid_handle));
						const float3 origin    = pnanovdb_grid_world_to_indexf(buf, grid_handle, rayQuery.CandidateObjectRayOrigin());
						const float3 direction = pnanovdb_grid_world_to_index_dirf(buf, grid_handle, rayQuery.CandidateObjectRayDirection());
						const pnanovdb_coord_t bbox_min = pnanovdb_root_get_bbox_min(buf, root);
						const pnanovdb_coord_t bbox_max = pnanovdb_root_get_bbox_max(buf, root) + 1;
						const float3 t0 = (bbox_min - origin) / direction;
						const float3 t1 = (bbox_max - origin) / direction;
						const float3 tmin = min(t0, t1);
						const float3 tmax = max(t0, t1);
						const float2 st = float2(max3(tmin), min3(tmax));
						const float t = st.x > rayQuery.RayTMin() ? st.x : st.y;
						if (t < rayQuery.CommittedRayT() && t > rayQuery.RayTMin()) {
							float3 tt0 = t == t0;
							float3 vol_normal = (t == t1) - tt0;
							vol_normal = normalize(gInstanceTransforms[hit_instance].transform_vector(pnanovdb_grid_index_to_world_dirf(buf, grid_handle, vol_normal)));
							_isect.sd.packed_geometry_normal = _isect.sd.packed_shading_normal = pack_normal_octahedron(vol_normal);
							rayQuery.CommitProceduralPrimitiveHit(t);
						}
						break;
					}

				}
			}
			case CANDIDATE_NON_OPAQUE_TRIANGLE: {
				//uint instance_primitive_index = 0;
				//BF_SET(instance_primitive_index, rayQuery.CandidateInstanceID(), 0, 16);
				//BF_SET(instance_primitive_index, rayQuery.CandidatePrimitiveIndex(), 16, 16);
				//PathVertex _isect;
				//make_vertex(instance_primitive_index, ray.origin + ray.direction*rayQuery.CandidateTriangleRayT(), rayQuery.CandidateTriangleBarycentrics(), ray, _isect);
				// TODO: test alpha
				rayQuery.CommitNonOpaqueTriangleHit();
				break;
			}
		}
	}

	if (rayQuery.CommittedStatus() != COMMITTED_NOTHING) {
		// hit an instance
		_isect.set_instance_index(rayQuery.CommittedInstanceID());
		const InstanceData instance = gInstances[rayQuery.CommittedInstanceID()];
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			_isect.set_primitive_index(INVALID_PRIMITIVE);
			const float3 local_pos = rayQuery.CommittedObjectRayOrigin() + rayQuery.CommittedObjectRayDirection()*rayQuery.CommittedRayT();
			switch (instance.type()) {
				case INSTANCE_TYPE_SPHERE:
					make_sphere_shading_data(_isect.sd, instance, gInstanceTransforms[rayQuery.CommittedInstanceID()], local_pos);
					break;
				case INSTANCE_TYPE_VOLUME:
					make_volume_shading_data(_isect.sd, instance, gInstanceTransforms[rayQuery.CommittedInstanceID()], local_pos);
					// shading_normal and geometry_normal are set in the rayQuery loop above
					break;
			}
		} else { // COMMITTED_TRIANGLE_HIT
			// triangle
			_isect.set_primitive_index(rayQuery.CommittedPrimitiveIndex());
			make_triangle_shading_data_from_barycentrics(_isect.sd, instance, gInstanceTransforms[rayQuery.CommittedInstanceID()], rayQuery.CommittedPrimitiveIndex(), rayQuery.CommittedTriangleBarycentrics());
		}
		_isect.sd.flags = 0;
		if (dot(direction, _isect.sd.geometry_normal()) < 0)
			_isect.sd.flags |= SHADING_FLAG_FRONT_FACE;
		return rayQuery.CommittedRayT();
	} else {
		// background
		_isect.set_instance_index(INVALID_INSTANCE);
		_isect.set_primitive_index(INVALID_PRIMITIVE);
		_isect.sd.shape_area = 0;
		return t_max;
	}
}
void trace_shadow_ray(inout rng_state_t rng_state, float3 origin, const float3 direction, float t_max, uint cur_medium, inout Spectrum beta, inout float T_dir_pdf, inout float T_nee_pdf) {
	Medium m;
	if (gHasMedia && cur_medium != INVALID_INSTANCE) m.load(gInstances[cur_medium].material_address());
	while (t_max > 1e-6f) {
		IntersectionVertex shadow_isect;
		const float dt = trace_ray(origin, direction, t_max, shadow_isect, !gHasMedia);
		if (!isinf(t_max)) t_max -= dt;

		if (gHasMedia) {
			if (shadow_isect.instance_index() == INVALID_INSTANCE) break;
			if (gInstances[shadow_isect.instance_index()].type() != INSTANCE_TYPE_VOLUME) {
				// hit a surface
				beta = 0;
				T_dir_pdf = 0;
				T_nee_pdf = 0;
				break;
			}
			if (cur_medium != INVALID_INSTANCE) {
				// interact with medium
				const TransformData inv_transform = gInstanceInverseTransforms[cur_medium];
				Spectrum dir_pdf = 1;
				Spectrum nee_pdf = 1;
				m.delta_track(rng_state, inv_transform.transform_point(origin), inv_transform.transform_vector(direction), dt, beta, dir_pdf, nee_pdf, false);
				T_dir_pdf *= average(dir_pdf);
				T_nee_pdf *= average(nee_pdf);
			}

			if (shadow_isect.sd.flags & SHADING_FLAG_FRONT_FACE) {
				// entering medium
				cur_medium = shadow_isect.instance_index();
				m.load(gInstances[shadow_isect.instance_index()].material_address());
				origin = ray_offset(shadow_isect.sd.position, -shadow_isect.sd.geometry_normal());
			} else {
				// leaving medium
				cur_medium = INVALID_INSTANCE;
				origin = ray_offset(shadow_isect.sd.position, shadow_isect.sd.geometry_normal());
			}
		} else {
			if (shadow_isect.instance_index() != INVALID_INSTANCE) {
				// hit a surface
				beta = 0;
				T_dir_pdf = 0;
				T_nee_pdf = 0;
			}
			break; // early exit; dont need to do delta tracking if no volumes are present
		}
	}
}
void trace_ray(inout rng_state_t rng_state, float3 origin, const float3 direction, inout uint cur_medium, inout Spectrum beta, inout float T_dir_pdf, inout float T_nee_pdf, DECLARE_ISECT_ARG) {
	Medium m;
	if (gHasMedia && cur_medium != INVALID_INSTANCE) m.load(gInstances[cur_medium].material_address());
	for (uint steps = 0; steps < 64; steps++) {
		const float dt = trace_ray(origin, direction, POS_INFINITY, _isect);

		if (!gHasMedia) break;

		if (gHasMedia && cur_medium != INVALID_INSTANCE) {
			// interact with medium
			const TransformData inv_transform = gInstanceInverseTransforms[cur_medium];
			const Vector3 m_origin = inv_transform.transform_point(origin);
			const Vector3 m_direction = inv_transform.transform_vector(direction);
			Spectrum dir_pdf = 1;
			Spectrum nee_pdf = 1;
			const float3 scatter_p = m.delta_track(rng_state, m_origin, m_direction, dt, beta, dir_pdf, nee_pdf, true);
			T_dir_pdf *= average(dir_pdf);
			T_nee_pdf *= average(nee_pdf);
			if (all(isfinite(scatter_p))) {
				_isect.set_instance_index(cur_medium);
				_isect.set_primitive_index(INVALID_PRIMITIVE);
				_isect.sd.position = scatter_p;
				_isect.sd.shape_area = 0;
				break;
			}
		}

		if (_isect.instance_index() == INVALID_INSTANCE) break;

		const InstanceData instance = gInstances[_isect.instance_index()];
		if (instance.type() != INSTANCE_TYPE_VOLUME) break;

		if (_isect.sd.flags & SHADING_FLAG_FRONT_FACE) {
			// entering volume
			cur_medium = _isect.instance_index();
			m.load(instance.material_address());
			origin = ray_offset(_isect.sd.position, -_isect.sd.geometry_normal());
		} else {
			// leaving volume
			cur_medium = INVALID_INSTANCE;
			origin = ray_offset(_isect.sd.position, _isect.sd.geometry_normal());
		}
	}
}

#endif