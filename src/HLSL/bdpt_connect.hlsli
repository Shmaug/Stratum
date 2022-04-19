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
        const float sensor_importance = 1 / (gViews[i].projection.sensor_area * lens_area * pow3(sensor_cos_theta));
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
void connect_to_light_paths(const Material material, inout ray_query_t rayQuery, const uint index_1d, const uint2 index_2d, const float3 dir_in, const uint eye_depth) {
    uint2 extent;
    gRadiance.GetDimensions(extent.x,extent.y);

    if (gPushConstants.gMaxLightDepth < 2) return;

    float3 last_pos = gPathVertices[index_1d].shading_data.position;
    for (uint light_depth = 1; light_depth < gPushConstants.gMaxLightDepth-1; light_depth++) {
        const uint light_vertex_index = extent.x*extent.y*light_depth + index_1d;
        if (all(gPathVertices[light_vertex_index].beta <= 0)) break;

        const uint material_address = instance_material_address(gPathVertices[light_vertex_index].instance_index());
        if (material_address == INVALID_MATERIAL) break;

        const float3 light_vertex_pos = gPathVertices[light_vertex_index].shading_data.position;
        float3 dir_out = light_vertex_pos - gPathState.vertex.shading_data.position;
        const float dist = length(dir_out);
        dir_out /= dist;

        const float3 light_dir_in = normalize(last_pos - light_vertex_pos);
        last_pos = light_vertex_pos;

        float dir_pdf = 1;
        float nee_pdf = 1;
        float3 C = 1;
        path_eval_transmittance(rayQuery, index_1d, dir_out, dist, C, dir_pdf, nee_pdf);
        C /= nee_pdf;
        
        // light_vertex not mutually visible
        if (all(C <= 0)) continue;

        C /= pow2(dist); // geometry term; both cosine terms handled inside eval_material (from light and view)
        
        // reflect off current vertex
        const MaterialEvalRecord f = eval_material(material, dir_in, dir_out, gPathState.vertex.shading_data, false);
        C *= gPathState.vertex.beta * f.f;

        // reflect off stored vertex
        MaterialEvalRecord light_f;
		const uint type = gMaterialData.Load(material_address);
		switch (type) {
		#define CASE_FN(T) case e##T: {\
            const T m = load_material<T>(material_address + 4, gPathVertices[light_vertex_index].shading_data); \
            light_f = eval_material(material, light_dir_in, -dir_out, gPathVertices[light_vertex_index].shading_data, true); \
            break; }
		FOR_EACH_BSDF_TYPE(CASE_FN);
		#undef CASE_FN
        }
        C *= light_f.f * gPathVertices[light_vertex_index].beta;

        gFilterImages[0][index_2d] += float4(C, 1);
    }
}

#endif