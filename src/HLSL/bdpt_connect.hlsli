#ifndef BDPT_CONNECT_H
#define BDPT_CONNECT_H

template<typename Material>
void connect_light_path_to_view(const Material material, inout ray_query_t rayQuery, const uint index_1d, const float3 dir_in, const uint depth) {
	for (uint i = 0; i < gPushConstants.gViewCount; i++) {
		float4 screen_pos = gViews[i].projection.project_point(gViews[i].world_to_camera.transform_point(gPathState.vertex.shading_data.position));
		screen_pos.y = -screen_pos.y;
		screen_pos.xyz /= screen_pos.w;
		if (any(abs(screen_pos.xyz) >= 1) || screen_pos.z <= 0) continue;
        const float2 uv = screen_pos.xy*.5 + .5;

        float3 to_view = gViews[i].camera_to_world.transform_point(0) - gPathState.vertex.shading_data.position;
        const float dist = length(to_view);
        to_view /= dist;

        const MaterialEvalRecord f = eval_material(material, dir_in, to_view, gPathState.vertex.shading_data, true);
        if (f.pdf_rev <= 0 || any(f.f != f.f)) continue;
        float3 C = gPathState.vertex.beta * f.f;

        float dir_pdf = 1;
        float nee_pdf = 1;
        path_eval_transmittance(rayQuery, index_1d, to_view, dist, C, dir_pdf, nee_pdf);
        C /= nee_pdf;
        
        // geometry term
        const float sensor_cos_theta = abs(dot(to_view, gViews[i].camera_to_world.transform_vector(float3(0,0,1))));
        const float lens_radius = 0;
        const float lens_area = lens_radius != 0 ? (M_PI * lens_radius * lens_radius) : 1;
        const float sensor_pdf = pow2(dist) / (sensor_cos_theta * lens_area);
        const float sensor_importance = 1 / (gViews[i].projection.sensor_area * lens_area * pow4(sensor_cos_theta));
        C *= sensor_importance;// / sensor_pdf;

        const uint2 extent = gViews[i].image_max - gViews[i].image_min;
        const int2 ipos = gViews[i].image_min + extent * uv;

        if (all(C <= 0)) continue;
        
        uint rw,rh;
        gRadiance.GetDimensions(rw,rh);
        uint value;
        InterlockedCompareExchange(gPathStates[ipos.y*rw + ipos.x].radiance_mutex, 0, 1, value);
        if (value == 0) {
            gFilterImages[0][ipos] += float4(C, 1);
            gPathStates[ipos.y*rw + ipos.x].radiance_mutex = 0;
        }
	}
}

template<typename Material>
void connect_light_paths(const Material material, inout ray_query_t rayQuery, const uint index_1d, const float3 dir_in, const uint light_depth) {
    uint2 extent;
    gRadiance.GetDimensions(extent.x,extent.y);

    //if (gSamplingFlags & SAMPLE_FLAG_CONNECT_TO_VIEWS)
    //    connect_light_path_to_view(material, rayQuery, index_1d, dir_in, light_depth);

    if (gSamplingFlags & SAMPLE_FLAG_DIRECT_ONLY) return;
    if (gPushConstants.gMaxEyeDepth == 0) return;

    const uint2 index_2d = uint2(index_1d%extent.x, index_1d/extent.x);
	const uint view_index = get_view_index(index_2d, gViews, gPushConstants.gViewCount);

    for (uint view_depth = 1; view_depth < gPushConstants.gMaxEyeDepth-1; view_depth++) {
        if (all(gPathVertices[extent.x*extent.y*(view_depth-1) + index_1d].beta <= 0)) break;

        float3 light_to_view = gPathVertices[extent.x*extent.y*(view_depth-1) + index_1d].shading_data.position - gPathState.vertex.shading_data.position;
        const float dist = length(light_to_view);
        light_to_view /= dist;

        const MaterialEvalRecord light_f = eval_material(material, dir_in, light_to_view, gPathState.vertex.shading_data, true);
        float3 C = gPathState.vertex.beta * light_f.f;

        C /= pow2(dist); // geometry term; both cosine terms handled inside eval_material (from light and view)
        
        float dir_pdf = 1;
        float nee_pdf = 1;
        path_eval_transmittance(rayQuery, index_1d, light_to_view, dist, C, dir_pdf, nee_pdf);
        C /= nee_pdf;
        
        if (all(C <= 0)) break;

        float3 view_dir_in;
        if (view_depth == 1)
            view_dir_in = normalize(gPathVertices[extent.x*extent.y*(view_depth-1) + index_1d].shading_data.position - gViews[view_index].camera_to_world.transform_point(0));
        else
            view_dir_in = normalize(gPathVertices[extent.x*extent.y*(view_depth-1) + index_1d].shading_data.position - gPathVertices[extent.x*extent.y*(view_depth-2) + index_1d].shading_data.position);

        const uint material_address = instance_material_address(gPathVertices[extent.x*extent.y*(view_depth-1) + index_1d].instance_index());
        if (material_address == INVALID_MATERIAL) break;

        // compute view_f
        MaterialEvalRecord view_f;
		const uint type = gMaterialData.Load(material_address);
		switch (type) {
		#define CASE_FN(ViewMaterial) case e##ViewMaterial: {\
            const ViewMaterial m = load_material<ViewMaterial>(material_address + 4, gPathVertices[extent.x*extent.y*(view_depth-1) + index_1d].shading_data); \
            view_f = eval_material(material, view_dir_in, -light_to_view, gPathVertices[extent.x*extent.y*(view_depth-1) + index_1d].shading_data, false); \
            break; }
		FOR_EACH_BSDF_TYPE(CASE_FN);
		#undef CASE_FN
        }
        
        C *= view_f.f * gPathVertices[extent.x*extent.y*(view_depth-1) + index_1d].beta;
        gRadiance[index_2d] += float4(C, 1);
    }
}

#endif