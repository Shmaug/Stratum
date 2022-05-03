#ifndef INTERSECTION_H
#define INTERSECTION_H

#include <shading_data.h>
#include <materials/medium.h>

struct IntersectionVertex {
	ShadingData sd;
	uint instance_index : 16;
	uint primitive_index : 16;
};
struct TransmittanceEstimate {
	float3 transmittance;
	float dir_pdf;
	float nee_pdf;
};

void trace_ray(inout RayQuery<RAY_FLAG_NONE> rayQuery, const float3 origin, const float3 direction, const float t_max, out IntersectionVertex v) {
	RayDesc rayDesc;
	rayDesc.Origin = origin;
	rayDesc.Direction = direction;
	rayDesc.TMin = 0;
	rayDesc.TMax = t_max;
	rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, rayDesc);
	while (rayQuery.Proceed()) {
		uint hit_instance = rayQuery.CandidateInstanceID();
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
						pnanovdb_root_handle_t root = pnanovdb_tree_get_root(buf, pnanovdb_grid_get_tree(buf, pnanovdb_grid_handle_t(0)));
						const float3 origin    = pnanovdb_grid_world_to_indexf(buf, pnanovdb_grid_handle_t(0), rayQuery.CandidateObjectRayOrigin());
						const float3 direction = pnanovdb_grid_world_to_index_dirf(buf, pnanovdb_grid_handle_t(0), rayQuery.CandidateObjectRayDirection());
						const pnanovdb_coord_t bbox_min = pnanovdb_root_get_bbox_min(buf, root);
						const pnanovdb_coord_t bbox_max = pnanovdb_root_get_bbox_max(buf, root) + 1;
						const float3 t0 = (bbox_min - origin) / direction;
						const float3 t1 = (bbox_max - origin) / direction;
						const float3 tmin = min(t0, t1);
						const float3 tmax = max(t0, t1);
						const float2 st = float2(max3(tmin), min3(tmax));
						const float t = st.x > rayQuery.RayTMin() ? st.x : st.y;
						if (t < rayQuery.CommittedRayT() && t > rayQuery.RayTMin()) {
							float3 vol_normal = -(t == t0) + (t == t1);
							vol_normal = normalize(instance.transform.transform_vector(pnanovdb_grid_index_to_world_dirf(buf, pnanovdb_grid_handle_t(0), vol_normal)));
							v.sd.packed_geometry_normal = v.sd.packed_shading_normal = pack_normal_octahedron(vol_normal);
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
				//PathVertex v;
				//make_vertex(instance_primitive_index, ray.origin + ray.direction*rayQuery.CandidateTriangleRayT(), rayQuery.CandidateTriangleBarycentrics(), ray, v);
				// TODO: test alpha
				rayQuery.CommitNonOpaqueTriangleHit();
				break;
			}
		}
	}

	if (gCountRays) InterlockedAdd(gRayCount[0], 1);

	if (rayQuery.CommittedStatus() != COMMITTED_NOTHING) {
		// hit an instance
		v.instance_index = rayQuery.CommittedInstanceID();
		const InstanceData instance = gInstances[rayQuery.CommittedInstanceID()];
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			v.primitive_index = INVALID_PRIMITIVE;
			const float3 local_pos = rayQuery.CommittedObjectRayOrigin() + rayQuery.CommittedObjectRayDirection()*rayQuery.CommittedRayT();
			switch (instance.type()) {
				case INSTANCE_TYPE_SPHERE:
					make_sphere_shading_data(v.sd, instance, local_pos);
					break;
				case INSTANCE_TYPE_VOLUME:
					make_volume_shading_data(v.sd, instance, local_pos);
					// normal and geometry_normal are set in the rayQuery loop above
					break;
			}
		} else { // COMMITTED_TRIANGLE_HIT
			// triangle
			v.primitive_index = rayQuery.CommittedPrimitiveIndex();
			make_triangle_shading_data_from_barycentrics(v.sd, instance, rayQuery.CommittedPrimitiveIndex(), rayQuery.CommittedTriangleBarycentrics());
		}
		v.sd.flags = 0;
		if (dot(direction, v.sd.geometry_normal()) < 0)
			v.sd.flags |= SHADING_FLAG_FRONT_FACE;
	} else {
		// background
		v.instance_index = INVALID_INSTANCE;
		v.primitive_index = INVALID_PRIMITIVE;
		v.sd.shape_area = 0;
	}
}
void trace_shadow_ray(inout rng_state_t rng_state, float3 origin, const float3 direction, float t_max, uint cur_medium, out TransmittanceEstimate T) {
	RayQuery<RAY_FLAG_NONE> rayQuery;

	T.transmittance = 1;
	T.dir_pdf = 1;
	T.nee_pdf = 1;
	while (t_max > 1e-6f) {
		IntersectionVertex v;
		trace_ray(rayQuery, origin, direction, t_max, v);

		if (!gHasMedia) {
			if (v.instance_index != INVALID_INSTANCE) {
				// hit a surface
				T.transmittance = 0;
				T.dir_pdf = 0;
				T.nee_pdf = 0;
			}
			break;
		}

		const float dt = (v.instance_index == INVALID_INSTANCE) ? t_max : rayQuery.CommittedRayT();

		if (cur_medium != INVALID_INSTANCE) {
			// interact with medium
			Medium m;
			m.load(gInstances[cur_medium].material_address());
			const TransformData inv_transform = gInstanceInverseTransforms[cur_medium];
			const Medium::DeltaTrackResult t = m.delta_track(inv_transform.transform_point(origin), inv_transform.transform_vector(direction), dt, rng_state, false);
			T.transmittance *= t.transmittance;
			T.dir_pdf *= average(t.dir_pdf);
			T.nee_pdf *= average(t.nee_pdf);
		}

		if (v.instance_index == INVALID_INSTANCE) break;

		if (gInstances[v.instance_index].type() == INSTANCE_TYPE_VOLUME) {
			if (v.sd.flags & SHADING_FLAG_FRONT_FACE) {
				// entering medium
				cur_medium = v.instance_index;
				origin = ray_offset(v.sd.position, -v.sd.geometry_normal());
			} else {
				// leaving medium
				cur_medium = INVALID_INSTANCE;
				origin = ray_offset(v.sd.position, v.sd.geometry_normal());
			}
			if (!isinf(t_max))
				t_max -= dt;
			continue;
		} else {
			// hit a surface
			T.transmittance = 0;
			T.dir_pdf = 0;
			T.nee_pdf = 0;
			break;
		}
	}
}
void intersect_scene(inout rng_state_t rng_state, float3 origin, const float3 direction, inout uint cur_medium, out TransmittanceEstimate T, out IntersectionVertex v) {
	RayQuery<RAY_FLAG_NONE> rayQuery;

	T.transmittance = 1;
	T.dir_pdf = 1;
	T.nee_pdf = 1;
	for (uint steps = 0; steps < 64; steps++) {
		trace_ray(rayQuery, origin, direction, 1.#INF, v);

		if (!gHasMedia) break;

		if (cur_medium != INVALID_INSTANCE) {
			// interact with medium
			Medium m;
			m.load(gInstances[cur_medium].material_address());
			const TransformData inv_transform = gInstanceInverseTransforms[cur_medium];
			const Medium::DeltaTrackResult t = m.delta_track(inv_transform.transform_point(origin), inv_transform.transform_vector(direction), rayQuery.CommittedRayT(), rng_state, false);
			T.transmittance *= t.transmittance;
			T.dir_pdf *= average(t.dir_pdf);
			T.nee_pdf *= average(t.nee_pdf);
			if (all(t.scatter_p == t.scatter_p)) {
				v.instance_index = cur_medium;
				v.primitive_index = INVALID_PRIMITIVE;
				v.sd.position = t.scatter_p;
				v.sd.shape_area = 0;
				break;
			}
		}

		if (v.instance_index == INVALID_INSTANCE) break;

		if (gInstances[v.instance_index].type() == INSTANCE_TYPE_VOLUME) {
			if (v.sd.flags & SHADING_FLAG_FRONT_FACE) {
				// entering volume
				cur_medium = v.instance_index;
				origin = ray_offset(v.sd.position, -v.sd.geometry_normal());
			} else {
				// leaving volume
				cur_medium = INVALID_INSTANCE;
				origin = ray_offset(v.sd.position, v.sd.geometry_normal());
			}
		} else
			break;
	}
}

#endif