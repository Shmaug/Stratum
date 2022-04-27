#ifndef BDPT_CONNECT_H
#define BDPT_CONNECT_H

inline float path_weight(const uint eye_vertices /*t*/, const uint light_vertices /*s*/) {
	if (gPushConstants.gMaxEyeDepth == 0) return 1;
	//if (eye_vertices == 0) {
	//	// standard light tracing
	//	return 1;
	//} else if (light_vertices == 0) {
	//	// standard path tracing
	//	return 1;
	//} else {
		return 1/(float)(eye_vertices + light_vertices - 1);

		//const float w_light = pdf_rev / pdf_fwd * (prev_w_light + 1);
		//const float w_camera = pdf_rev / pdf_fwd * (prev_w_camera + 1);
		//return 1 / (w_light + 1 + w_camera);
	//}
}

template<typename Material>
void connect_light_path_to_view(const Material material, inout ray_query_t rayQuery, const uint index_1d, const float3 dir_in, const uint light_depth) {
	for (uint i = 0; i < gPushConstants.gViewCount; i++) {
		float4 screen_pos = gViews[i].projection.project_point(gViews[i].world_to_camera.transform_point(gPathShadingData.position));
		screen_pos.y = -screen_pos.y;
		screen_pos.xyz /= screen_pos.w;
		if (any(abs(screen_pos.xyz) >= 1) || screen_pos.z <= 0) continue;
        const float2 uv = screen_pos.xy*.5 + .5;

        float3 to_view = gViews[i].camera_to_world.transform_point(0) - gPathShadingData.position;
        const float dist = length(to_view);
        to_view /= dist;

        // reflect off current (eye) vertex
        const MaterialEvalRecord f = eval_material(material, dir_in, to_view, gPathShadingData, true);
        if (f.pdf_rev <= 0 || any(f.f != f.f)) continue;
        float3 C = gPathVertex.beta * f.f;

        // geometry term
        C /= pow2(dist);

        float dir_pdf = 1;
        float nee_pdf = 1;
        path_eval_transmittance(rayQuery, index_1d, to_view, dist, C, dir_pdf, nee_pdf);
        C /= nee_pdf;

        const float sensor_cos_theta = abs(dot(to_view, gViews[i].camera_to_world.transform_vector(float3(0,0,1))));
        const float lens_radius = 0;
        const float lens_area = lens_radius != 0 ? (M_PI * lens_radius * lens_radius) : 1;
        const float sensor_importance = 1 / (gViews[i].projection.sensor_area * lens_area * pow3(sensor_cos_theta));
        C *= sensor_importance;

        C *= path_weight(1, light_depth+1);

        const uint2 extent = gViews[i].image_max - gViews[i].image_min;
        const int2 ipos = gViews[i].image_min + extent * uv;

        if (all(C <= 0) || !all(C == C)) continue;

        uint rw,rh;
        gRadiance.GetDimensions(rw,rh);

        uint value;
        InterlockedCompareExchange(gRadianceMutex[ipos.y*rw + ipos.x], 0, 1, value);
        if (value == 0) {
            gRadiance[ipos].rgb += C;
            gRadianceMutex[ipos.y*rw + ipos.x] = 0;
        }
	}
}

template<typename Material>
float3 connect_to_light_subpath(const Material material, inout ray_query_t rayQuery, const uint index_1d, const float3 dir_in, const uint light_depth) {
	const uint light_vertex_index = gPushConstants.gNumLightPaths*light_depth + index_1d;
	const uint material_address = instance_material_address(gLightPathVertices[light_vertex_index].instance_index());

	float3 dir_out = gLightPathShadingData[light_vertex_index].position - gPathShadingData.position;
	const float dist = length(dir_out);
	dir_out /= dist;

	float dir_pdf = 1;
	float nee_pdf = 1;
	float3 C = 1;
	path_eval_transmittance(rayQuery, index_1d, dir_out, dist, C, dir_pdf, nee_pdf);
	C /= nee_pdf;

	// path vertices not mutually visible
	if (all(C <= 0)) return 0;

	C /= pow2(dist); // geometry term; both cosine terms handled inside eval_material (from light and view)

	// reflect off current (eye path) vertex
	const MaterialEvalRecord f = eval_material(material, dir_in, dir_out, gPathShadingData, false);
	C *= gPathVertex.beta * f.f;

	if (all(C <= 0) || !all(C == C)) return 0;

	C *= gLightPathVertices[light_vertex_index].beta;
	// reflect off stored (light path) vertex
	if (light_depth > 0) {
		const uint prev_light_vertex_index = gPushConstants.gNumLightPaths*(light_depth-1) + index_1d;
		const float3 light_dir_in = normalize(gLightPathShadingData[prev_light_vertex_index].position - gLightPathShadingData[light_vertex_index].position);

		MaterialEvalRecord light_f;
		const uint type = gMaterialData.Load(material_address);
		switch (type) {
		#define CASE_FN(T) case e##T: {\
			const T m = load_material<T>(material_address + 4, gLightPathShadingData[light_vertex_index]); \
			light_f = eval_material(m, light_dir_in, -dir_out, gLightPathShadingData[light_vertex_index], true); \
			break; }
		FOR_EACH_BSDF_TYPE(CASE_FN);
		#undef CASE_FN
		}
		C *= light_f.f;
	}

	if (all(C <= 0) || !all(C == C)) return 0;

	return C;
}

template<typename Material>
void connect_to_light_paths(const Material material, inout ray_query_t rayQuery, const uint index_1d, const uint2 index_2d, const float3 dir_in, const uint eye_depth) {
    const uint n_light = gLightPathVertices[index_1d].path_length;
	if (n_light == 0) return;

	if (gSamplingFlags & SAMPLE_FLAG_RANDOM_LIGHT_PATHS) {
		// pick random path/vertex
		const uint light_path_index = rng_next_uint(index_1d) % gPushConstants.gNumLightPaths;
		const uint light_depth = rng_next_uint(index_1d) % (n_light);

		const uint light_vertex_index = gPushConstants.gNumLightPaths*light_depth + light_path_index;
		if (all(gLightPathVertices[light_vertex_index].beta <= 0)) return;

		const uint material_address = instance_material_address(gLightPathVertices[light_vertex_index].instance_index());
		if (material_address == INVALID_MATERIAL) return;

		const float3 C = connect_to_light_subpath(material, rayQuery, index_1d, dir_in, light_depth);
		gRadiance[index_2d].rgb += C * path_weight(eye_depth+1, light_depth+1) / (float)n_light;

	} else {
		// connect every eye subpath vertex to every light subpath vertex
		for (uint light_depth = 0; light_depth < n_light; light_depth++) {
			const uint light_vertex_index = gPushConstants.gNumLightPaths*light_depth + index_1d;
			if (all(gLightPathVertices[light_vertex_index].beta <= 0)) return;
			const uint material_address = instance_material_address(gLightPathVertices[light_vertex_index].instance_index());
			if (material_address == INVALID_MATERIAL) return;

			const float3 C = connect_to_light_subpath(material, rayQuery, index_1d, dir_in, light_depth);

			gRadiance[index_2d].rgb += C * path_weight(eye_depth+1, light_depth+1);
		}
	}
}

#endif