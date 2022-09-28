#ifndef PATH_H
#define PATH_H

#include "intersection.hlsli"
#include "../environment.h"
#include "light.hlsli"

Real mis(const Real a) {
	return pow2(a);
}
Real mis(const Real a, const Real b) {
	if (!gUseMIS) return 0.5;
	const Real a2 = mis(a);
	return a2 / (a2 + mis(b));
}
Real path_weight(const uint view_length, const uint light_length) {
	const uint path_length = view_length + light_length;
	if (path_length <= 2) return 1; // light is directly visible
	// count the number of ways to generate a path with path_length vertices
	// E E ... E E   regular path tracing
	uint n = 1;
	// E E ... E L   regular path tracing, next event estimation
	if (gConnectToLights) n++;
	// E L ... L L   light tracing view connection
	if (gConnectToViews && path_length <= gMaxPathVertices+1) n++;
	// E E ... L L   bdpt connection to light subpath
	if (gConnectToLightPaths) n += min(gMaxPathVertices, path_length-2);
	return 1.f / n;
}
// computes dE (or dL) for a vertex. pdfA_rev is the reverse pdfA (towards the previous vertex), prev_pdfA_fwd is the forward pdfA at the previous vertex
Real connection_dVC(const Real dVC, const Real pdfA_rev, const Real prev_pdfA_fwd, const bool specular) {
	// dE_s = ( N_{s-1,k} + P(s-1 <- s  ) * dE_{s-1} ) / P(s-1 -> s)
	//		= ( N_{s-1,k} + pdfA_rev * prev_dE ) / prev_pdfA_fwd

	// dL_s = ( N_{s,k} + P(s -> s+1) * dL_{s+1} ) / P(s <- s+1)
	//		= ( N_{s,k} + pdfA_rev * prev_dL ) / prev_pdfA_fwd
	return ((specular ? 0 : 1) + dVC * mis(pdfA_rev)) / mis(prev_pdfA_fwd);
}

Spectrum load_light_sample(const uint2 pixel_coord) {
	const uint idx = pixel_coord.y * gOutputExtent.x + pixel_coord.x;
	uint4 v = gLightTraceSamples.Load<uint4>(16*idx);
	// handle overflow
	if (v.w & BIT(0)) v.r = 0xFFFFFFFF;
	if (v.w & BIT(1)) v.g = 0xFFFFFFFF;
	if (v.w & BIT(2)) v.b = 0xFFFFFFFF;
	return v.rgb / (Real)gLightTraceQuantization;
}
void accumulate_light_contribution(const uint output_index, const Spectrum c) {
	const uint3 ci = max(0,c) * gLightTraceQuantization;
	if (all(ci == 0)) return;
	const uint addr = 16*output_index;
	uint3 ci_p;
	gLightTraceSamples.InterlockedAdd(addr + 0 , ci[0], ci_p[0]);
	gLightTraceSamples.InterlockedAdd(addr + 4 , ci[1], ci_p[1]);
	gLightTraceSamples.InterlockedAdd(addr + 8 , ci[2], ci_p[2]);
	const bool3 overflow = ci > (0xFFFFFFFF - ci_p);
	if (any(overflow)) {
		const uint overflow_mask = (overflow[0] ? BIT(0) : 0) | (overflow[1] ? BIT(1) : 0) | (overflow[2] ? BIT(2) : 0);
		gLightTraceSamples.InterlockedOr(addr + 12, overflow_mask);
	}
}

uint light_vertex_index(const uint path_index, const uint diffuse_vertices) { return gOutputExtent.x*gOutputExtent.y*(diffuse_vertices-1) + path_index; }
uint shadow_ray_index  (const uint path_index, const uint diffuse_vertices) { return gOutputExtent.x*gOutputExtent.y*(diffuse_vertices-1) + path_index; }

Real shading_normal_correction(const Real ndotin, const Real ndotout, const Real ngdotin, const Real ngdotout, const Real ngdotns, const bool adjoint) {
	// light leak fix
	if (sign(ngdotout * ngdotin) != sign(ndotin * ndotout))
		return 0;

	Real G = 1;

	if (gShadingNormalFix) {
		// http://www.aconty.com/pdf/bump-terminator-nvidia2019.pdf
		//const Real cos_d = min(abs(ngdotns), 1);
		//const Real cos_d2 = pow2(cos_d);
		//const Real tan2_d = (1 - cos_d2) / cos_d2;
		//const Real alpha2 = saturate(0.125 * tan2_d);
		//const Real cos_i = max(abs(adjoint ? ngdotin : ngdotout), 1e -6);
		//const Real cos_i2 = pow2(cos_i);
		//const Real tan2_i = (1 - cos_i2) / cos_i2;
		//G = 2 / (1 + sqrt(1 + alpha2 * tan2_i));

		// https://media.disneyanimation.com/technology/publications/2019/TamingtheShadowTerminator.pdf
		G = min(1, abs(adjoint ? ngdotin / (ndotin * ngdotns) : ngdotout / (ndotout * ngdotns)));
		G = -pow3(G) + pow2(G) + G;
	}

	if (adjoint) {
		const Real num = ngdotout * ndotin;
		const Real denom = ndotout * ngdotin;
		if (abs(denom) > 1e-5)
			G *= abs(num / denom);
	}

	return G;
}

MaterialEvalRecord eval_bsdf(PathVertex v, const Vector3 dir_out, const bool adjoint, out Real ngdotout) {
	MaterialEvalRecord eval;
	eval.f = 0;
	if (gHasMedia && v.is_medium()) {
		Medium m;
		m.load(v.material_address);
		if (m.is_specular()) return eval;
		m.eval(eval, v.local_dir_in(), dir_out);
		ngdotout = 1;
	} else {
		Material m;
		m.load(v.material_address, v.uv, 0, v.packed_shading_normal, v.packed_tangent, v.flip_bitangent());
		if (m.is_specular()) return eval;
		const Vector3 local_dir_in = v.local_dir_in();
		const Vector3 local_dir_out = normalize(v.to_local(dir_out));
		m.eval(eval, local_dir_in, local_dir_out, adjoint);
		if (eval.pdf_fwd < 1e-6) { eval.f = 0; return eval; }

		const Vector3 ng = v.geometry_normal();
		ngdotout = dot(ng, dir_out);
		eval.f *= shading_normal_correction(local_dir_in.z, local_dir_out.z, dot(ng, normalize(v.to_world(local_dir_in))), ngdotout, dot(ng, v.shading_normal()), adjoint);
	}
	return eval;
}
Spectrum eval_bsdf_Le(PathVertex v) {
	if (v.is_background()) {
		if (!gHasEnvironment) return 0;
		Environment env;
		env.load(gEnvironmentMaterialAddress);
		return env.eval(v.local_dir_in());
	}
	if (gHasMedia && v.is_medium()) {
		Medium m;
		m.load(v.material_address);
		return m.Le();
	} else {
		Material m;
		m.load(v.material_address, v.uv, 0, v.packed_shading_normal, v.packed_tangent, v.flip_bitangent());
		return m.Le();
	}
}
PresampledLightPoint sample_Le(inout rng_state_t _rng, const Vector3 ref_pos, out Vector3 to_light, out Real dist, out Real G) {
	float4 rnd = float4(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));
	LightSampleRecord ls;
	sample_point_on_light(ls, rnd, ref_pos);

	PresampledLightPoint r;
	r.position = ls.position;
	r.packed_geometry_normal = pack_normal_octahedron(ls.normal);
	r.Le = ls.radiance;
	r.pdfA = ls.pdf;

	to_light = ls.to_light;
	dist = ls.dist;

	if (ls.is_environment()) {
		G = 1;
	} else {
		G = abs(dot(ls.to_light, ls.normal)) / pow2(ls.dist);
		if (!ls.pdf_area_measure)
			r.pdfA = pdfWtoA(r.pdfA, G);
	}

	return r;
}

struct DirectLightSample {
	PresampledLightPoint p;
	Vector3 ray_origin, ray_direction;
	Real ray_distance;
	Real G;
	Real emission_pdfA;
	Vector3 local_to_light;
	Real ngdotout, ngdotns;

	static Real reservoir_bsdf_mis() {
		return 0.5;
	}


	__init(const IntersectionVertex _isect, inout rng_state_t _rng) {
		p = sample_Le(_rng, _isect.sd.position, ray_direction, ray_distance, G);
		setup(_isect);
	}

	__init(const IntersectionVertex _isect, const PresampledLightPoint ls) {
		p = ls;

		if (p.pdfA < 0) { // environment map sample
			p.pdfA = -p.pdfA;
			ray_direction = p.position;
			ray_distance = POS_INFINITY;
			G = 1;
		} else {
			ray_direction = p.position - _isect.sd.position;
			const Real dist2 = len_sqr(ray_direction);
			ray_distance = sqrt(dist2);
			ray_direction /= ray_distance;
			G = abs(dot(ray_direction, p.geometry_normal()))/dist2;
		}

		setup(_isect);
	}

	SLANG_MUTATING
	void setup(const IntersectionVertex _isect) {
		ray_origin = _isect.sd.position;
		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = ray_direction;
			ngdotout = 1;
			ngdotns = 1;
			emission_pdfA = 0;
		} else {
			local_to_light = normalize(_isect.sd.to_local(ray_direction));
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			ngdotout = dot(geometry_normal, ray_direction);
			ray_origin = ray_offset(ray_origin, ngdotout > 0 ? geometry_normal : -geometry_normal);
			ngdotns = dot(geometry_normal, _isect.sd.shading_normal());
			ray_distance = visibility_distance_epsilon(ray_distance);
			emission_pdfA = pdfWtoA(cosine_hemisphere_pdfW(ngdotout), ngdotout/pow2(ray_distance));
		}
	}
};

// after initialization, simply call next_vertex() until _beta is 0
// for view paths: radiance is accumulated directly into gRadiance[pixel_coord]
struct PathIntegrator {
	uint2 pixel_coord;
	uint path_index;

	uint diffuse_vertices;
	uint path_length;
	rng_state_t _rng;
	Spectrum _beta;
	Real eta_scale;

	Spectrum path_contrib;
	Real path_pdf; // area measure
	Real path_pdf_rev; // area measure
	Real bsdf_pdf; // solid angle measure
	Real dVC; // bdpt mis quantity. dVC for the previous vertex is computed in sample_direction()
	bool prev_specular;

	// the ray that was traced to get here
	// also the ray traced by trace()
	// computed in sample_next_direction()
	Vector3 origin, direction;
	Real prev_cos_out; // abs(dot(direction, prev_geometry_normal))

	// current intersection, computed in trace()
	IntersectionVertex _isect;
	Vector3 local_position;
	uint _medium;
	Vector3 local_dir_in; // _isect.sd.to_local(-direction)
	Real ngdotin; // dot(-direction, _isect.sd.geometry_normal())
	Real G; // abs(ngdotin) / dist2(_isect.sd.position, origin)
	Real T_nee_pdf;


	////////////////////////////////////////////
	// State

	__init(const uint2 pixel, const uint idx) {
		pixel_coord = pixel;
		path_index = idx;

		diffuse_vertices = 0;
		path_length = 1;
		eta_scale = 1;

		prev_specular = false;

		static const uint rngs_per_ray = gHasMedia ? (1 + 2*gMaxNullCollisions) : 0;
		_rng = rng_init(pixel_coord, (gTraceLight ? 0xFFFFFF : 0) + (0xFFF + rngs_per_ray*4)*(path_length-1));
		if (gCoherentRNG) _rng = WaveReadLaneFirst(_rng);
	}


	////////////////////////////////////////////
	// NEE

	SLANG_MUTATING
	void connect_light(BSDF m) {
		DirectLightSample c;
		if (gPresampleLights) {
			// uniformly sample from the tile
			const uint tile_offset = ((path_index/gLightPresampleTileSize) % gLightPresampleTileCount)*gLightPresampleTileSize;
			uint ti = rng_next_uint(_rng) % gLightPresampleTileSize;
			if (gCoherentSampling)
				ti = (WaveReadLaneFirst(ti) + WaveGetLaneIndex()) % gLightPresampleTileSize;
			c = DirectLightSample(_isect, gPresampledLights[tile_offset + ti]);
		} else
			c = DirectLightSample(_isect, _rng);
		if (all(c.p.Le <= 0) && c.p.pdfA < 1e-6) return;

		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, c.local_to_light, false);
		Real pdfA_fwd = pdfWtoA(_eval.pdf_fwd, c.G);
		if (pdfA_fwd < 1e-6) return;

		if (!gDeferShadowRays) {
			trace_visibility_ray(_rng, c.ray_origin, c.ray_direction, c.ray_distance, _medium, c.p.Le, pdfA_fwd, c.p.pdfA);
			if (all(c.p.Le <= 0)) return;
		}

		if (!gHasMedia || _isect.sd.shape_area > 0)
			c.G *= shading_normal_correction(local_dir_in.z, c.local_to_light.z, ngdotin, c.ngdotout, c.ngdotns, false);

		const Spectrum contrib = c.p.Le * _eval.f * c.G / c.p.pdfA;
		if (all(contrib <= 0)) return;

		// compute path weight
		Real weight = 1;
		if (gConnectToLightPaths || gConnectToViews) {
			// BDPT MIS
			if (gUseMIS) {
				const Real dL_1 = 1/c.p.pdfA;
				const Real dL = connection_dVC(dL_1, c.emission_pdfA, c.p.pdfA, false);
				const Real G_rev = prev_cos_out/len_sqr(origin - _isect.sd.position);
				const Real dE = connection_dVC(dVC, pdfWtoA(_eval.pdf_rev, G_rev), pdfWtoA(bsdf_pdf, G), prev_specular);
				weight = 1 / (1 + dE * mis(c.emission_pdfA) + dL * mis(pdfA_fwd));
			} else
				weight = path_weight(path_length, 1);
		} else if (gSampleBSDFs)
			weight = mis(c.p.pdfA, pdfA_fwd);

		if (gDeferShadowRays) {
			ShadowRayData rd;
			rd.contribution = _beta * contrib * weight;
			rd.rng_offset = _rng.w;
			rd.ray_origin = c.ray_origin;
			rd.medium = _medium;
			rd.ray_direction = c.ray_direction;
			rd.ray_distance = c.ray_distance;
			gShadowRays[shadow_ray_index(path_index, diffuse_vertices)] = rd;
		} else
			gRadiance[pixel_coord].rgb += _beta * contrib * weight;

		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 1 && path_length == gPushConstants.gDebugViewPathLength)
			gDebugImage[pixel_coord].rgb += _beta * contrib;
	}
	SLANG_MUTATING
	void connect_light_reservoir(BSDF m) {
		DirectLightSample c;
		Reservoir r;
		Real r_target_pdf;
		r.init();

		// ris pass
		{
			const uint tile_offset = ((path_index/gLightPresampleTileSize) % gLightPresampleTileCount)*gLightPresampleTileSize;
			uint ti = rng_next_uint(_rng);
			if (gCoherentSampling)
				ti = WaveReadLaneFirst(ti) + WaveGetLaneIndex();

			for (uint i = 0; i < gReservoirM; i++) {
				DirectLightSample c_i;

				if (gPresampleLights) {
					if (!gCoherentSampling) ti = rng_next_uint(_rng);
					c_i = DirectLightSample(_isect, gPresampledLights[tile_offset + ti % gLightPresampleTileSize]);
					if (gCoherentSampling) ti += WaveGetLaneCount();
				} else
					c_i = DirectLightSample(_isect, _rng);

				if (c_i.p.pdfA <= 0 || all(c_i.p.Le <= 0)) continue;

				const Real target_pdf_i = luminance(c_i.p.Le) * c_i.G * abs(c_i.local_to_light.z);
				if (r.update(rng_next_float(_rng), target_pdf_i/c_i.p.pdfA)) {
					r_target_pdf = target_pdf_i;
					c = c_i;
				}
			}
		}

		Vector3 t,b;
		make_orthonormal(_isect.sd.geometry_normal(), t, b);

		#ifndef HASHGRID_RESERVOIR_VERTEX
		const Real cell_size = hashgrid_cell_size(_isect.sd.position);
		if (gUseNEEReservoirReuse && gReservoirSpatialM > 0) {
			const Real phi = rng_next_float(_rng)*2*M_PI;
			const Vector3 jitter = gHashGridJitter ? cell_size*rng_next_float(_rng)*(t*cos(phi) + b*sin(phi)) : 0;
			const uint bucket_index = gPrevNEEHashGrid.lookup(_isect.sd.position + jitter, cell_size);
			if (bucket_index != -1) {
				const uint bucket_start = gPrevNEEHashGrid.mIndices[bucket_index];
				const uint bucket_size = gPrevNEEHashGrid.mCounters[bucket_index];
				uint M = r.M;
				for (uint i = 0; i < gReservoirSpatialM; i++) {
					const uint reservoir_index = bucket_start + rng_next_uint(_rng)%bucket_size;
					const ReservoirData prev_reservoir = gPrevNEEHashGrid.mReservoirs[reservoir_index];
					DirectLightSample c_i = DirectLightSample(_isect, gPrevNEEHashGrid.mReservoirSamples[reservoir_index]);
					if (c_i.p.pdfA <= 0 || all(c_i.p.Le <= 0)) continue;

					M += prev_reservoir.r.M;

					const Real target_pdf_i = luminance(c_i.p.Le) * c_i.G * abs(c_i.local_to_light.z);
					if (r.update(rng_next_float(_rng), target_pdf_i * prev_reservoir.W * prev_reservoir.r.M)) {
						r_target_pdf = target_pdf_i;
						c = c_i;
					}
				}
				r.M = M;
			}
		}
		#endif

		const Real W = r.W(r_target_pdf);

		if (W <= 1e-6 || isnan(W)) return;

		#ifndef HASHGRID_RESERVOIR_VERTEX
		if (gUseNEEReservoirReuse) {
			const Real phi = rng_next_float(_rng)*2*M_PI;
			const Vector3 jitter = gHashGridJitter ? cell_size*rng_next_float(_rng)*(t*cos(phi) + b*sin(phi)) : 0;
			r.M = min(r.M, gReservoirMaxM);
			gNEEHashGrid.append(_isect.sd.position + jitter, cell_size, { r, _isect.sd.packed_geometry_normal, W }, c.p);
		}
		#endif

		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, c.local_to_light, false);

		if (!gHasMedia || _isect.sd.shape_area > 0)
			c.G *= shading_normal_correction(local_dir_in.z, c.local_to_light.z, ngdotin, c.ngdotout, c.ngdotns, false);

		Spectrum contrib = c.p.Le * _eval.f * c.G * W;

		if (all(contrib <= 0) || c.p.pdfA < 1e-6) return;

		// compute path weight
		Real weight = 1;
		if (gConnectToLightPaths || gConnectToViews) {
			// BDPT MIS
			const Real dL_1 = W;
			const Real dL = connection_dVC(dL_1, c.emission_pdfA, 1/W, false);
			const Real G_rev = prev_cos_out/len_sqr(origin - _isect.sd.position);
			const Real dE = connection_dVC(dVC, pdfWtoA(_eval.pdf_rev, G_rev), pdfWtoA(bsdf_pdf, G), prev_specular);
			weight = 1 / (1 + dE * mis(c.emission_pdfA) + dL * mis(pdfWtoA(_eval.pdf_fwd, c.G)));
		} else if (gSampleBSDFs)
			weight = 1 - DirectLightSample::reservoir_bsdf_mis(); // MIS between nee and bsdf sampling

		if (gDeferShadowRays) {
			ShadowRayData rd;
			rd.contribution = _beta * contrib * weight;
			rd.rng_offset = _rng.w;
			rd.ray_origin = c.ray_origin;
			rd.medium = _medium;
			rd.ray_direction = c.ray_direction;
			rd.ray_distance = c.ray_distance;
			gShadowRays[shadow_ray_index(path_index, diffuse_vertices)] = rd;
		} else {
			Real nee_pdf = 1;
			Real dir_pdf = 1;
			trace_visibility_ray(_rng, c.ray_origin, c.ray_direction, c.ray_distance, _medium, contrib, dir_pdf, nee_pdf);
			if (nee_pdf <= 0) return;
			contrib /= nee_pdf;

			if (all(contrib <= 0)) return;

			if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eReservoirWeight)
				gDebugImage[pixel_coord].rgb += W;

			gRadiance[pixel_coord].rgb += _beta * contrib * weight;
			if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 1 && path_length == gPushConstants.gDebugViewPathLength)
				gDebugImage[pixel_coord].rgb += _beta * contrib;
		}
	}


	////////////////////////////////////////////
	// BDPT

	PathVertex vertex() {
		uint flags = 0;
		if (_isect.sd.flags & SHADING_FLAG_FLIP_BITANGENT) flags |= PATH_VERTEX_FLAG_FLIP_BITANGENT;
		if (gHasMedia && _isect.sd.shape_area == 0) flags |= PATH_VERTEX_FLAG_IS_MEDIUM;
		if (_isect.instance_index() != INVALID_INSTANCE) flags |= PATH_VERTEX_FLAG_IS_BACKGROUND;
		if (prev_specular) flags |= PATH_VERTEX_FLAG_IS_PREV_DELTA;

		PathVertex v;
		if (path_length > 1) {
			v.material_address = gEnvironmentMaterialAddress;
			if (_isect.instance_index() != INVALID_INSTANCE)
				v.material_address = gInstances[_isect.instance_index()].material_address();
		} else
			v.material_address = -1;
		v.position = _isect.sd.position;
		v.packed_geometry_normal = _isect.sd.packed_geometry_normal;
		v.packed_local_dir_in = pack_normal_octahedron(local_dir_in);
		v.packed_shading_normal = _isect.sd.packed_shading_normal;
		v.packed_tangent = _isect.sd.packed_tangent;
		v.uv = _isect.sd.uv;
		v.pack_beta(gUseLVCReservoirs ? path_contrib : _beta, path_length, diffuse_vertices, flags);

		v.prev_dVC = dVC;
		v.G_rev = prev_cos_out/len_sqr(origin - _isect.sd.position);
		v.prev_pdfA_fwd = pdfWtoA(bsdf_pdf, G);
		v.path_pdf = path_pdf;

		return v;
	}
	void store_light_vertex() {
		uint idx;
		if (gLightVertexCache) {
			InterlockedAdd(gLightPathVertexCount[0], 1, idx);
			idx = idx % (gLightPathCount*gMaxDiffuseVertices);
		} else
			idx = light_vertex_index(path_index, diffuse_vertices);
		gLightPathVertices[idx] = vertex();
	}

	Real shading_normal_factor(const Real ndotout, const Real ngdotout, const Real ngdotns, const bool adjoint) {
		return shading_normal_correction(local_dir_in.z, ndotout, ngdotin, ngdotout, ngdotns, adjoint);
	}

	SLANG_MUTATING
	void connect_view(const BSDF m) {
		uint view_index = 0;
		if (gViewCount > 1) view_index = min(rng_next_float(_rng)*gViewCount, gViewCount-1);

		float4 screen_pos = gViews[view_index].projection.project_point(gInverseViewTransforms[view_index].transform_point(_isect.sd.position));
		screen_pos.y = -screen_pos.y;
		screen_pos.xyz /= screen_pos.w;
		if (any(abs(screen_pos.xyz) >= 1) || screen_pos.z <= 0) return;
        const float2 uv = screen_pos.xy*.5 + .5;
        const int2 ipos = gViews[view_index].image_min + (gViews[view_index].image_max - gViews[view_index].image_min) * uv;
		const uint output_index = ipos.y * gOutputExtent.x + ipos.x;

		const Vector3 position = Vector3(
			gViewTransforms[view_index].m[0][3],
			gViewTransforms[view_index].m[1][3],
			gViewTransforms[view_index].m[2][3] );
		const Vector3 view_normal = normalize(gViewTransforms[view_index].transform_vector(Vector3(0,0,1)));

		Vector3 to_view = position - _isect.sd.position;
		const Real dist = length(to_view);
		to_view /= dist;

		const Real sensor_cos_theta = abs(dot(to_view, view_normal));

		const Real lens_radius = 0;
		const Real lens_area = lens_radius > 0 ? (M_PI * lens_radius * lens_radius) : 1;
		const Real sensor_importance = 1 / (gViews[view_index].projection.sensor_area * lens_area * pow4(sensor_cos_theta));

		Spectrum contribution = _beta * sensor_importance / pdfAtoW(1/lens_area, sensor_cos_theta / pow2(dist));

		const Real G_rev = abs(prev_cos_out) / len_sqr(origin - _isect.sd.position);
		Real ngdotout;
		Vector3 local_to_view;

		Vector3 ray_origin = _isect.sd.position;
		if (gHasMedia && _isect.sd.shape_area == 0) {
			ngdotout = 1;
			local_to_view = to_view;
		} else {
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			ngdotout = dot(to_view, geometry_normal);
			ray_origin = ray_offset(ray_origin, ngdotout > 0 ? geometry_normal : -geometry_normal);
			local_to_view = normalize(_isect.sd.to_local(to_view));
			contribution *= shading_normal_factor(local_to_view.z, ngdotout, dot(geometry_normal, _isect.sd.shading_normal()), true);
		}

		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, local_to_view, true);
		if (_eval.pdf_fwd < 1e-6) return;

		contribution *= _eval.f;

		if (all(contribution <= 0)) return;

		Real nee_pdf = 1;
		Real dir_pdf = 1;
		trace_visibility_ray(_rng, ray_origin, to_view, dist, _medium, contribution, dir_pdf, nee_pdf);
		if (nee_pdf > 0) contribution /= nee_pdf;

		Real weight;
		if (gUseMIS) {
			const Real p0_fwd = 1;//pdfWtoA(gViews[view_index].sensor_pdfW(sensor_cos_theta), abs(ngdotout)/pow2(dist));
			if (gConnectToLightPaths) {
				// dL_{s+1} = (1 + P(s+1 -> s+2)*dL_{s+2}) / P(s+1 <- s+2)
				// dL_1 = (1 + P(1 -> 2)*dL_2) / P(1 <- 2)
				const Real dL_1 = connection_dVC(dVC, pdfWtoA(_eval.pdf_rev, G_rev), pdfWtoA(bsdf_pdf, G), prev_specular);
				weight = 1 / (1 + dL_1 * mis(p0_fwd));
			} else
				weight = prev_specular ? 1 : mis(path_pdf, p0_fwd * path_pdf_rev * pdfWtoA(_eval.pdf_rev, G_rev));
		} else
			weight = path_weight(1, path_length);

		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eLightTraceContribution)
			weight = 1;

		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugViewPathLength == 1) {
			if (gPushConstants.gDebugLightPathLength == path_length)
				accumulate_light_contribution(output_index, contribution);
		} else
			accumulate_light_contribution(output_index, contribution * weight);
	}
	SLANG_MUTATING
	Spectrum connect_light_vertex(BSDF m, const PathVertex lv, out Real weight, out Vector3 ray_origin, out Vector3 ray_direction, out Real ray_distance) {
		Spectrum contrib = lv.beta();
		if (all(contrib <= 0) || any(isnan(contrib))) return 0; // invalid vertex

		ray_origin = _isect.sd.position;
		ray_direction = lv.position - _isect.sd.position;
		ray_distance = length(ray_direction);
		const Real rcp_dist = 1/ray_distance;
		ray_direction *= rcp_dist;

		const Real rcp_dist2 = pow2(rcp_dist);
		contrib *= rcp_dist2;
		Real connection_G_fwd = rcp_dist2;

		Real dL, pdfA_rev;

		if (!lv.is_medium())
			ray_distance = visibility_distance_epsilon(ray_distance);

		// evaluate BSDF at light path vertex
		Real cos_theta_light;
		MaterialEvalRecord lv_eval = eval_bsdf(lv, -ray_direction, true, cos_theta_light);
		contrib *= lv_eval.f;
		connection_G_fwd *= abs(cos_theta_light);

		// dL_{s+2}
		dL = connection_dVC(lv.prev_dVC, pdfWtoA(lv_eval.pdf_rev, lv.G_rev), lv.prev_pdfA_fwd, lv.is_prev_delta());
		pdfA_rev = lv_eval.pdf_fwd * rcp_dist2;

		if (all(contrib <= 0) || any(isnan(contrib))) return 0;

		Vector3 local_to_light;

		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = ray_direction;
		} else {
			local_to_light = normalize(_isect.sd.to_local(ray_direction));
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			const Real ngdotout = dot(geometry_normal, ray_direction);
			const Real ngdotns = dot(geometry_normal, _isect.sd.shading_normal());
			ray_origin = ray_offset(ray_origin, ngdotout > 0 ? geometry_normal : -geometry_normal);

			pdfA_rev *= abs(ngdotout);
			contrib *= shading_normal_factor(local_to_light.z, ngdotout, ngdotns, false);
		}

		// evaluate bsdf at view path vertex
		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, local_to_light, false);
		if (_eval.pdf_fwd < 1e-6) return 0;
		contrib *= _eval.f;

		if (all(contrib <= 0)) return 0;

		if (gUseMIS) {
			const Real G_rev = prev_cos_out/len_sqr(origin - _isect.sd.position);
			const Real dE = connection_dVC(dVC, pdfWtoA(_eval.pdf_rev, G_rev), pdfWtoA(bsdf_pdf, G), prev_specular);
			weight = 1 / (1 + dE * mis(pdfA_rev) + dL * mis(pdfWtoA(_eval.pdf_fwd, connection_G_fwd)));
		} else
			weight = path_weight(path_length, lv.subpath_length());

		return _beta * contrib;
	}

	SLANG_MUTATING
	void connect_lvc(const BSDF m) {
		// connect eye vertex to random light vertex
		const uint n = min(gLightPathVertexCount[0], gLightPathCount*gMaxDiffuseVertices);

		uint li = rng_next_uint(_rng);
		if (gCoherentSampling) li = WaveReadLaneFirst(li) + WaveGetLaneIndex();
		PathVertex lv = gLightPathVertices[li % n];

		Spectrum contrib = 0;
		Real weight = 1;
		Vector3 ray_origin, ray_direction;
		Real ray_distance;

		if (gUseLVCReservoirs) {
			Reservoir r;
			Real r_target_pdf;
			r.init();

			// RIS pass
			for (int i = 0; i < gReservoirM; i++) {
				const PathVertex lv_i = gLightPathVertices[(gCoherentSampling ? (li + (1 + i)*WaveGetLaneCount()) : rng_next_uint(_rng)) % n];
				if (lv_i.subpath_length() + path_length > gMaxPathVertices || lv_i.diffuse_vertices() + diffuse_vertices > gMaxDiffuseVertices || all(lv_i.beta() <= 0)) continue;

				Vector3 ray_origin_i, ray_direction_i;
				Real ray_distance_i;
				Real weight_i;
				const Spectrum contrib_i = connect_light_vertex(m, lv_i, weight_i, ray_origin_i, ray_direction_i, ray_distance_i);
				const Real target_pdf_i = luminance(contrib_i);
				if (r.update(rng_next_float(_rng), target_pdf_i/lv_i.path_pdf)) {
					contrib = contrib_i;
					weight = weight_i;
					ray_origin = ray_origin_i;
					ray_direction = ray_direction_i;
					ray_distance = ray_distance_i;
					r_target_pdf = target_pdf_i;
					lv = lv_i;
				}
			}

			Real W = r.W(r_target_pdf);

			#ifdef HASHGRID_RESERVOIR_VERTEX
			if (gUseLVCReservoirReuse) {
				Vector3 t,b;
				make_orthonormal(_isect.sd.geometry_normal(), t, b);
				const Real cell_size = hashgrid_cell_size(_isect.sd.position);
				if (gReservoirSpatialM > 0) {
					const Real phi = rng_next_float(_rng)*2*M_PI;
					const Vector3 jitter = gHashGridJitter ? cell_size*rng_next_float(_rng)*(t*cos(phi) + b*sin(phi)) : 0;
					const uint bucket_index = gPrevLVCHashGrid.lookup(_isect.sd.position + jitter, cell_size);
					if (bucket_index != -1) {
						const uint bucket_start = gPrevLVCHashGrid.mIndices[bucket_index];
						const uint bucket_size = gPrevLVCHashGrid.mCounters[bucket_index];
						uint M = r.M;
						for (uint i = 0; i < gReservoirSpatialM; i++) {
							const uint reservoir_index = bucket_start + rng_next_uint(_rng)%bucket_size;
							const ReservoirData prev_reservoir = gPrevLVCHashGrid.mReservoirs[reservoir_index];
							const PathVertex lv_i = gPrevLVCHashGrid.mReservoirSamples[reservoir_index];
							if (lv_i.subpath_length() + path_length > gMaxPathVertices || lv_i.diffuse_vertices() + diffuse_vertices > gMaxDiffuseVertices || all(lv_i.beta() <= 0)) continue;

							M += prev_reservoir.r.M;

							Vector3 ray_origin_i, ray_direction_i;
							Real ray_distance_i;
							Real weight_i;
							const Spectrum contrib_i = connect_light_vertex(m, lv_i, weight_i, ray_origin_i, ray_direction_i, ray_distance_i);
							const Real target_pdf_i = luminance(contrib_i);
							if (r.update(rng_next_float(_rng), target_pdf_i/lv_i.path_pdf)) {
								contrib = contrib_i;
								weight = weight_i;
								ray_origin = ray_origin_i;
								ray_direction = ray_direction_i;
								ray_distance = ray_distance_i;
								r_target_pdf = target_pdf_i;
								lv = lv_i;
							}
						}
						r.M = M;
					}
				}

				W = r.W(r_target_pdf);

				{
					const Real phi = rng_next_float(_rng)*2*M_PI;
					const Vector3 jitter = gHashGridJitter ? cell_size*rng_next_float(_rng)*(t*cos(phi) + b*sin(phi)) : 0;
					r.M = min(r.M, gReservoirMaxM);

					gLVCHashGrid.append(_isect.sd.position + jitter, cell_size, { r, _isect.sd.packed_geometry_normal, W }, lv);
				}
			}
			#endif

			contrib *= r.W(r_target_pdf);

		} else if (lv.subpath_length() + path_length <= gMaxPathVertices && lv.diffuse_vertices() + diffuse_vertices <= gMaxDiffuseVertices && any(lv.beta() > 0))
			contrib = connect_light_vertex(m, lv, weight, ray_origin, ray_direction, ray_distance);

		contrib *= gMaxDiffuseVertices-1;

		if (gDeferShadowRays) {
			ShadowRayData rd;
			rd.contribution = contrib * weight;
			rd.rng_offset = _rng.w;
			rd.ray_origin = ray_origin;
			rd.medium = _medium;
			rd.ray_direction = ray_direction;
			rd.ray_distance = ray_distance;
			gShadowRays[shadow_ray_index(path_index, diffuse_vertices)] = rd;
		} else if (any(contrib > 0) && weight > 0) {
			Real dir_pdf = 1;
			Real nee_pdf = 1;
			trace_visibility_ray(_rng, ray_origin, ray_direction, ray_distance, _medium, contrib, dir_pdf, nee_pdf);
			if (any(contrib > 0) && nee_pdf > 0) {
				contrib /= nee_pdf;
				gRadiance[pixel_coord].rgb += contrib * weight;
				if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && lv.subpath_length() == gPushConstants.gDebugLightPathLength && path_length == gPushConstants.gDebugViewPathLength)
					gDebugImage[pixel_coord].rgb += contrib;
			}
		}
	}
	SLANG_MUTATING
	void connect_light_subpath(const BSDF m) {
		// connect eye vertex to all light subpath vertices
		for (uint i = 1; i < gMaxDiffuseVertices; i++) {
			const PathVertex lv = gLightPathVertices[light_vertex_index(path_index, i)];
			if (lv.subpath_length() + path_length > gMaxPathVertices || lv.diffuse_vertices() + diffuse_vertices > gMaxDiffuseVertices || all(lv.beta() <= 0)) break;

			Vector3 ray_origin, ray_direction;
			Real ray_distance;
			Real weight;
			Spectrum contrib = connect_light_vertex(m, lv, weight, ray_origin, ray_direction, ray_distance);
			if (all(contrib <= 0) || weight <= 0) continue;

			Real dir_pdf = 1;
			Real nee_pdf = 1;
			trace_visibility_ray(_rng, ray_origin, ray_direction, ray_distance, _medium, contrib, dir_pdf, nee_pdf);
			if (all(contrib <= 0) || nee_pdf <= 0) continue;
			contrib /= nee_pdf;

			gRadiance[pixel_coord].rgb += contrib * weight;
			if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && lv.subpath_length() == gPushConstants.gDebugLightPathLength && path_length == gPushConstants.gDebugViewPathLength)
				gDebugImage[pixel_coord].rgb += contrib;
		}
	}


	////////////////////////////////////////////
	// PT

	SLANG_MUTATING
	bool russian_roulette() {
		Real p = luminance(_beta) / eta_scale * 0.95;
		if (gCoherentRR) p = WaveActiveMax(p); // WaveActiveSum(p) / WaveActiveCountBits(1);

		if (p >= 1) return true;

		bool v = rng_next_float(_rng) > p;
		if (gCoherentRR) v = WaveReadLaneFirst(v);

		if (v)
			return false;

		_beta /= p;
		path_pdf *= p;

		return true;
	}

	void eval_emission(const Spectrum Le) {
		if (all(Le <= 0)) return;
		// evaluate path contribution
		Vector3 contrib = _beta * Le;
		Real cos_theta_light;
		if (_isect.instance_index() == INVALID_INSTANCE || _isect.sd.shape_area == 0) {
			// background
			cos_theta_light = 1;
		} else {
			// emissive surface
			cos_theta_light = -dot(_isect.sd.geometry_normal(), direction);
			if (cos_theta_light < 0) return;
		}

		// compute emission pdf
		bool area_measure;
		Real light_pdfA = point_on_light_pdf(_isect, area_measure);
		if (!area_measure) light_pdfA = pdfWtoA(light_pdfA, G);

		if (!gDeferShadowRays) light_pdfA *= T_nee_pdf;

		// compute path weight
		Real weight = 1;
		if (path_length > 2) {
			if (gConnectToLightPaths || gConnectToViews) {
				if (gUseMIS) {
					const Real p_rev_k = pdfWtoA(cosine_hemisphere_pdfW(abs(cos_theta_light)), abs(prev_cos_out)/len_sqr(origin - _isect.sd.position));
					if (gConnectToLightPaths) {
						const Real dE_k = connection_dVC(dVC, p_rev_k, pdfWtoA(bsdf_pdf, G), prev_specular);
						weight = 1 / (1 + dE_k * mis(light_pdfA));
					} else {
						weight = prev_specular ? 0 : mis(path_pdf, path_pdf_rev*p_rev_k*light_pdfA);
					}
				} else
					weight = path_weight(path_length, 0);
			} else if (gConnectToLights) {
				if (gUseNEEReservoirs)
					weight = DirectLightSample::reservoir_bsdf_mis();
				else
					weight = mis(pdfWtoA(bsdf_pdf, G), light_pdfA);
			}
		}

		gRadiance[pixel_coord].rgb += contrib * weight;

		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eViewTraceContribution)
			gDebugImage[pixel_coord].rgb += contrib;
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 0 && path_length == gPushConstants.gDebugViewPathLength)
			gDebugImage[pixel_coord].rgb += contrib;
	}

	// sample next direction, compute _beta *= f/pdfW, update dE/dL, update origin and direction
	SLANG_MUTATING
	bool sample_direction(BSDF m) {
		const Vector3 rnd = Vector3(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));
		MaterialSampleRecord _material_sample;
		path_contrib *= m.sample(_material_sample, rnd, local_dir_in, _beta, gTraceLight);

		if (_material_sample.pdf_fwd < 1e-6) {
			_beta = 0;
			return false;
		}

		if (_material_sample.eta != 0)
			eta_scale /= pow2(_material_sample.eta);

		if (gUseRayCones) {
			if (_material_sample.eta != 0)
				gRayDifferentials[path_index].refract(_isect.sd.mean_curvature, _material_sample.roughness, _material_sample.eta);
			else
				gRayDifferentials[path_index].reflect(_isect.sd.mean_curvature, _material_sample.roughness);
		}

		// MIS quantities
		const Real G_rev = prev_cos_out/len_sqr(origin - _isect.sd.position);
		if (gTraceLight || path_length > 2)
			path_pdf_rev *= pdfWtoA(_material_sample.pdf_rev, G_rev);
		dVC = connection_dVC(dVC, pdfWtoA(_material_sample.pdf_rev, G_rev), pdfWtoA(bsdf_pdf, G), m.is_specular());
		bsdf_pdf = _material_sample.pdf_fwd;
		prev_specular = m.is_specular();

		// update origin, direction, compute prev_cos_out, apply shading normal correction

		if (gHasMedia && _isect.sd.shape_area == 0) {
			origin = _isect.sd.position;
			prev_cos_out = 1;
		} else {
			const Real ndotout = _material_sample.dir_out.z;

			_material_sample.dir_out = normalize(_isect.sd.to_world(_material_sample.dir_out));

			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			const Real ngdotout = dot(geometry_normal, _material_sample.dir_out);
			origin = ray_offset(_isect.sd.position, ngdotout > 0 ? geometry_normal : -geometry_normal);

			_beta *= shading_normal_factor(ndotout, ngdotout, dot(geometry_normal, _isect.sd.shading_normal()), gTraceLight);

			prev_cos_out = ngdotout;

			if (all(_beta <= 0))
				return false;
		}

		direction = _material_sample.dir_out;

		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eDirOut) gDebugImage[pixel_coord] = float4(direction*.5+.5, 1);
		return true;
	}

	SLANG_MUTATING
	bool next_vertex(BSDF m) {
		// emission from vertex 2 is evaluated in sample_visibility
		if (!gTraceLight && path_length > 2)
			eval_emission(m.Le());

		if (!m.can_eval() || path_length >= gMaxPathVertices)
			return false;

		if (!m.is_specular()) {
			diffuse_vertices++;
			if (diffuse_vertices > gMaxDiffuseVertices)
				return false;

			if (gTraceLight && gConnectToLightPaths && path_length+2 <= gMaxPathVertices && diffuse_vertices < gMaxDiffuseVertices)
				store_light_vertex();

			if (gTraceLight) {
				if (gConnectToViews)
					connect_view(m);
			} else {
				if (path_length >= gMinPathVertices)
					if (!russian_roulette()) return false;

				if (gConnectToLights) {
					if (gUseNEEReservoirs)
						connect_light_reservoir(m);
					else
						connect_light(m);
				}

				if (gConnectToLightPaths) {
					if (gLightVertexCache)
						connect_lvc(m);
					else
						connect_light_subpath(m);
				}
			}
		}

		if (gSampleBSDFs || gTraceLight)
			return sample_direction(m);
		else
			return false;
	}

	// expects _medium, origin and direction to bet set
	// increments path_length, sets G and T_nee_pdf.
	SLANG_MUTATING
	void trace() {
		Real T_dir_pdf = 1;
		T_nee_pdf = 1;
		if (gUsePerformanceCounters) InterlockedAdd(gRayCount[1], 1);
		trace_ray(_rng, origin, direction, _medium, _beta, T_dir_pdf, T_nee_pdf, _isect, local_position);
		if (T_dir_pdf <= 0 || all(_beta <= 0)) { _beta = 0; return; }
		_beta /= T_dir_pdf;

		if (!gDeferShadowRays) bsdf_pdf *= T_dir_pdf;
		path_pdf *= T_dir_pdf;

		path_length++;

		// handle miss
		if (_isect.instance_index() == INVALID_INSTANCE) {
			G = 1;
			ngdotin = 1;
			if (gUseRayCones)
				_isect.sd.uv_screen_size *= gRayDifferentials[path_index].radius;
			return;
		}

		const Real dist2 = len_sqr(_isect.sd.position - origin);

		// update ray differential
		if (gUseRayCones) {
			RayDifferential ray_differential = gRayDifferentials[path_index];
			ray_differential.transfer(sqrt(dist2));
			gRayDifferentials[path_index] = ray_differential;
			_isect.sd.uv_screen_size *= ray_differential.radius;
		}

		G = 1/dist2;

		if (gHasMedia && _isect.sd.shape_area == 0) {
			ngdotin = 1;
		} else {
			ngdotin = -dot(direction, _isect.sd.geometry_normal());
			G *= abs(ngdotin);
		}

		path_pdf *= pdfWtoA(bsdf_pdf, G);
		path_contrib *= G;
	}

	// sample nee/bdpt connections and next direction. sets local_dir_in
	SLANG_MUTATING
	void next_vertex() {
		if (_isect.instance_index() == INVALID_INSTANCE) {
			// background
			if (gHasEnvironment) {
				Environment env;
				env.load(gEnvironmentMaterialAddress);
				eval_emission(env.eval(direction));
			}
			_beta = 0;
			return;
		}

		const uint material_address = gInstances[_isect.instance_index()].material_address();

		if (gHasMedia && _isect.sd.shape_area == 0) {
			Medium m;
			m.load(material_address);
			local_dir_in = -direction;
			if (!next_vertex(m)) { _beta = 0; return; }
		} else {
			Material m;
			m.load(material_address, _isect.sd);
			local_dir_in = normalize(_isect.sd.to_local(-direction));
			if (!next_vertex(m)) { _beta = 0; return; }
		}

		trace();
	}
};

#endif