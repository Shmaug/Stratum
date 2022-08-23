#ifndef PATH_H
#define PATH_H

#include "rng.hlsli"
#include "intersection.hlsli"
#include "../materials/environment.h"
#include "light.hlsli"

Real path_weight(const uint view_length, const uint light_length) {
	const uint path_length = view_length + light_length;
	if (path_length <= 2) return 1; // light is directly visible
	// count the number of ways to generate a path with path_length vertices
	uint n = 0;
	// E E ... E E   regular path tracing
	n++;
	// E E ... E N   regular path tracing, next event estimation
	if (gUseNEE) n++;
	// E E ... L L   bdpt connection to light subpath
	if (gConnectToLightPaths) n += min(gMaxLightPathVertices, path_length-2);
	// V L ... L L   light tracing view connection
	if (gConnectToViews && path_length <= gMaxLightPathVertices+1) n++;
	return 1.f / n;
}

#define N_kk gMaxLightPathVertices
#define N_sk(view_length) 1

// connects a view path to a vertex in its associated light path
struct LightPathConnection {
	Spectrum f;
	Vector3 local_to_light;
	Real dL;
	Real pdfA_rev; // P(s <- s+1)
	Real G_fwd; // G(s -> s+1)

	SLANG_MUTATING
	bool connect(inout rng_state_t _rng, const uint li, const bool emissive_vertex, const IntersectionVertex _isect, const uint cur_medium) {
		const LightPathVertex lv = gLightPathVertices[li];
		f = lv.beta();
		if (all(f <= 0)) return false; // invalid vertex

		Vector3 to_light = lv.position - _isect.sd.position;
		Real dist = length(to_light);
		const Real rcp_dist = 1/dist;
		to_light *= rcp_dist;

		const Real rcp_dist2 = pow2(rcp_dist);
		f *= rcp_dist2;
		G_fwd = rcp_dist2;

		// evaluate BSDF at light vertex

		if (emissive_vertex) { // dont evaluate bsdf at emissive vertex
			const Real cos_theta_light = max(0, -dot(lv.geometry_normal(), to_light));
			f *= cos_theta_light; // cosine term from light surface
			G_fwd *= cos_theta_light;
			if (gUseMIS) {
				dL = gLightPathVertices1[li].dL_prev;
				pdfA_rev = cosine_hemisphere_pdfW(cos_theta_light);
			}
		} else {
			MaterialEvalRecord _eval;
			if (lv.is_medium()) {
				Medium m;
				m.load(lv.material_address());
				m.eval(_eval, lv.local_dir_in(), -to_light);
			} else {
				Material m;
				m.load(lv.material_address(), lv.uv, 0);
				const Vector3 local_dir_in = lv.local_dir_in();
				const Vector3 local_dir_out = normalize(lv.to_local(-to_light));
				m.eval(_eval, local_dir_in, local_dir_out, true);
				if (_eval.pdf_fwd < 1e-6) { f = 0; return true; }
				_eval.f *= abs(local_dir_out.z);

				const Vector3 local_geometry_normal = lv.to_local(lv.geometry_normal());
				G_fwd *= abs(dot(local_geometry_normal, local_dir_out));

				// shading normal correction
				const Real num = dot(local_geometry_normal, local_dir_out) * local_dir_in.z;
				const Real denom = local_dir_out.z * dot(local_geometry_normal, local_dir_in);
				if (abs(denom) > 1e-5) _eval.f *= abs(num / denom);
			}
			f *= _eval.f;

			if (gUseMIS) {
				const LightPathVertex1 lv1 = gLightPathVertices1[li];
				// dL_{s+2}
				dL = (N_sk(s+1) + pdfWtoA(_eval.pdf_rev, lv1.G_rev) * lv1.dL_prev) / lv1.pdfA_fwd_prev;
				pdfA_rev = _eval.pdf_fwd;
			}
		}

		if (all(f <= 0)) return true; // vertex not visible, but light subpath still valid

		Vector3 origin = _isect.sd.position;
		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = to_light;
		} else {
			local_to_light = normalize(_isect.sd.to_local(to_light));
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			const Real cos_theta_view = dot(geometry_normal, to_light);
			origin = ray_offset(origin, cos_theta_view > 0 ? geometry_normal : -geometry_normal);
			if (gUseMIS) pdfA_rev = pdfWtoA(pdfA_rev, abs(cos_theta_view)*rcp_dist2);
		}

		Real T_dir_pdf = 1;
		Real T_nee_pdf = 1;
		trace_visibility_ray(_rng, origin, to_light, dist, cur_medium, f, T_dir_pdf, T_nee_pdf);
		if (T_nee_pdf > 0)
			f /= T_nee_pdf;

		return true;
	}
};

// connects a light path to a random view
struct ViewConnection {
	static Spectrum load_sample(const uint2 pixel_coord) {
		const uint idx = pixel_coord.y * gOutputExtent.x + pixel_coord.x;
		uint4 v = gLightTraceSamples.Load<uint4>(16*idx);
		if (v.w & BIT(0)) v.r = 0xFFFFFFFF;
		if (v.w & BIT(1)) v.g = 0xFFFFFFFF;
		if (v.w & BIT(2)) v.b = 0xFFFFFFFF;
		return v.rgb / (Real)gLightTraceQuantization;
	}
	static void accumulate_contribution(const uint output_index, const Spectrum c) {
		const uint3 ci = max(0,c) * gLightTraceQuantization;
		if (all(ci == 0)) return;
		const uint addr = 16*output_index;
		uint3 ci_p;
		gLightTraceSamples.InterlockedAdd(addr + 0 , ci[0], ci_p[0]);
		gLightTraceSamples.InterlockedAdd(addr + 4 , ci[1], ci_p[1]);
		gLightTraceSamples.InterlockedAdd(addr + 8 , ci[2], ci_p[2]);
		const bool3 overflow = ci > (0xFFFFFFFF - ci_p);
		if (any(overflow))
			gLightTraceSamples.InterlockedOr(addr + 12, (overflow[0] ? BIT(0) : 0) |
														(overflow[1] ? BIT(1) : 0) |
														(overflow[2] ? BIT(2) : 0) );
	}

	SLANG_MUTATING
	static void sample(const BSDF m, inout uint4 _rng, const uint cur_medium, const IntersectionVertex _isect, const Spectrum beta, const uint path_length, const Vector3 local_dir_in, const Real ngdotin, const Real dL_2, const Real bsdf_pdfA_prev, const Real G_rev) {
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
		const Real sensor_pdfW = pdfAtoW(1/lens_area, sensor_cos_theta / pow2(dist));

		Spectrum contribution = beta * sensor_importance / sensor_pdfW;

		Real ngdotout;
		Vector3 local_to_view;

		Vector3 origin = _isect.sd.position;
		if (gHasMedia && _isect.sd.shape_area == 0) {
			ngdotout = 1;
			local_to_view = to_view;
		} else {
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			ngdotout = dot(to_view, geometry_normal);
			origin = ray_offset(origin, ngdotout > 0 ? geometry_normal : -geometry_normal);
			local_to_view = normalize(_isect.sd.to_local(to_view));

			// shading normal correction
			if (ngdotout != 0) {
				const Real num = ngdotout * local_dir_in.z;
				const Real denom = local_to_view.z * ngdotin;
				if (abs(denom) > 1e-5) contribution *= abs(num / denom);
			}
		}

		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, local_to_view, true);
		if (_eval.pdf_fwd < 1e-6) return;

		contribution *= _eval.f * abs(local_to_view.z);

		if (all(contribution <= 0)) return;

		Real T_nee_pdf = 1;
		Real T_dir_pdf = 1;
		trace_visibility_ray(_rng, origin, to_view, dist, cur_medium, contribution, T_dir_pdf, T_nee_pdf);
		if (T_nee_pdf > 0) contribution /= T_nee_pdf;

		Real weight;
		if (gUseMIS) {
			const Real dL_1 = (N_sk(1) + pdfWtoA(_eval.pdf_rev, G_rev)*dL_2) / bsdf_pdfA_prev;
			const Real sensor_pdf_fwd = 1 / (gViews[view_index].projection.sensor_area * lens_area * pow3(sensor_cos_theta));
			weight = 1 / (N_sk(0) + dL_1*pdfWtoA(sensor_pdf_fwd, ngdotout/pow2(dist)));
		} else
			weight = path_weight(1, path_length);

		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugViewPathLength == 1) {
			if (gPushConstants.gDebugLightPathLength == path_length)
				accumulate_contribution(output_index, contribution);
		} else
			accumulate_contribution(output_index, contribution * weight);
	}
};

struct NEE {
	Spectrum Le;
	Vector3 local_to_light;
	Real pdfA;
	Real G;
	Real T_dir_pdf, T_nee_pdf;
	Vector3 ray_origin, ray_direction;
	Real dist;

	static Real mis(const Real a, const Real b) {
		if (!gUseMIS) return 0.5;
		const Real a2 = pow2(a);
		return a2 / (a2 + pow2(b));
	}
	static Real reservoir_bsdf_mis() {
		return 0.5;
	}

	// load shading data and Le
	SLANG_MUTATING
	void sample(inout rng_state_t _rng, const IntersectionVertex _isect) {
		float4 rnd = float4(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));
		LightSampleRecord ls;
		sample_point_on_light(ls, rnd, _isect.sd.position);
		if (ls.pdf <= 0 || all(ls.radiance <= 0)) { Le = 0; return; }

		dist = ls.dist;
		ray_direction = ls.to_light;
		Le = ls.radiance;

		if (ls.is_environment())
			G = 1;
		else {
			const Real cos_theta = -dot(ls.to_light, ls.normal);
			if (cos_theta < 1e-4) { Le = 0; return; }
			G = abs(cos_theta) / pow2(ls.dist);
		}
		pdfA = ls.pdf_area_measure ? ls.pdf : pdfWtoA(ls.pdf, G);

		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = ray_direction;
			ray_origin = _isect.sd.position;
		} else {
			local_to_light = normalize(_isect.sd.to_local(ray_direction));
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			ray_origin = ray_offset(_isect.sd.position, dot(geometry_normal, ray_direction) > 0 ? geometry_normal : -geometry_normal);
		}
		T_dir_pdf = 1;
		T_nee_pdf = 1;
	}

	SLANG_MUTATING
	void load_presample(const uint index, const IntersectionVertex _isect) {
		const PresampledLightPoint ls = gPresampledLights[index];
		Le = ls.Le;
		pdfA = ls.pdfA;

		if (pdfA < 0) { // environment map sample
			pdfA = -pdfA;
			ray_direction = ls.position;
			dist = POS_INFINITY;
			G = 1;
		} else {
			ray_direction = ls.position - _isect.sd.position;
			const Real dist2 = dot(ray_direction, ray_direction);
			dist = sqrt(dist2);
			ray_direction /= dist;

			const Real cos_theta = -dot(ray_direction, ls.geometry_normal());
			if (cos_theta < 1e-4) { Le = 0; return; }
			G = abs(cos_theta)/dist2;
		}

		if (gHasMedia && _isect.sd.shape_area <= 0) {
			local_to_light = ray_direction;
			ray_origin = _isect.sd.position;
		} else {
			local_to_light = normalize(_isect.sd.to_local(ray_direction));
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			ray_origin = ray_offset(_isect.sd.position, dot(geometry_normal, ray_direction) > 0 ? geometry_normal : -geometry_normal);
		}
		T_dir_pdf = 1;
		T_nee_pdf = 1;
	}

	SLANG_MUTATING
	void eval_visibility(inout rng_state_t _rng, const uint cur_medium) {
		if (any(Le > 0)) {
			trace_visibility_ray(_rng, ray_origin, ray_direction, dist, cur_medium, Le, T_dir_pdf, T_nee_pdf);
			if (T_nee_pdf > 0) Le /= T_nee_pdf;
		}
	}

	// use if eval is called before eval_visibility
	SLANG_MUTATING
	void eval_visibility(inout rng_state_t _rng, const uint cur_medium, inout Spectrum contrib) {
		trace_visibility_ray(_rng, ray_origin, ray_direction, dist, cur_medium, contrib, T_dir_pdf, T_nee_pdf);
		if (T_nee_pdf > 0) contrib /= T_nee_pdf;
	}

	// returns full contribution (C*G*f/pdf) and pdf used for MIS
	Spectrum eval(const BSDF m, const Vector3 local_dir_in, out Real bsdf_pdf) {
		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, local_to_light, false);
		bsdf_pdf = _eval.pdf_fwd;
		if (bsdf_pdf < 1e-6) return 0;
		return Le * _eval.f * G * abs(local_to_light.z);
	}
};

// after initialization, simply call next_vertex() until _beta is 0
// for view paths: radiance is accumulated directly into gRadiance[pixel_coord]
struct PathIntegrator {
	uint2 pixel_coord;
	uint path_index;

	uint path_length;
	rng_state_t _rng;
	Spectrum _beta;

	Real bsdf_pdf; // solid angle measure
	Real d; // dE for view paths, dL for light paths. updated in sample_direction()

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
	Real G; // abs(dot(direction, _isect.sd.geometry_normal())) / dist2(_isect.sd.position, origin)
	Real T_nee_pdf;

	static const uint gMaxVertices = gTraceLight ? gMaxLightPathVertices : gMaxPathVertices;

	static const uint light_vertex_index(const uint path_index, const uint path_length) { return gOutputExtent.x*gOutputExtent.y*(path_length-1) + path_index; }
	static const uint nee_vertex_index  (const uint path_index, const uint path_length) { return gOutputExtent.x*gOutputExtent.y*(path_length-2)*(gMaxPathVertices-2) + path_index; }

	SLANG_MUTATING
	void init_rng() {
		static const uint rngs_per_ray = gHasMedia ? (1 + 2*gMaxNullCollisions) : 0;
		_rng = rng_init(pixel_coord, (gTraceLight ? 0xFFFFFF : 0) + (0xFFF + rngs_per_ray*4)*(path_length-1));
		if (gCoherentRNG) _rng = WaveReadLaneFirst(_rng);
	}

	void store_state() {
		if (all(_beta <= 0) || any(isnan(_beta))) {
			gPathStates[path_index].beta = 0;
			return;
		}

		PathState ps;
		ps.p.local_position = local_position;
		ps.p.instance_primitive_index = _isect.instance_primitive_index;
		ps.origin = origin;
		ps.pack_path_length_medium(path_length, _medium);
		ps.beta = _beta;
		if (gUseMIS) ps.prev_cos_out = prev_cos_out;
		ps.dir_in = direction;
		ps.bsdf_pdf = bsdf_pdf;
		gPathStates[path_index] = ps;

		PathState1 ps1;
		ps1.d = d;
		gPathStates1[path_index] = ps1;
	}
	// load state, update ray differential, compute G and local_dir_in
	SLANG_MUTATING
	void load_state(const uint _path_index, const uint2 _pixel_coord) {
		path_index = _path_index;
		pixel_coord = _pixel_coord;
		const PathState ps = gPathStates[path_index];
		origin = ps.origin;
		path_length = ps.path_length();
		_medium = ps.medium();
		_beta = ps.beta;
		bsdf_pdf = ps.bsdf_pdf;
		if (gUseMIS) prev_cos_out = ps.prev_cos_out;
		direction = ps.dir_in;
		d = gPathStates1[path_index].d;
		local_position = ps.p.local_position;
		_isect.instance_primitive_index = ps.p.instance_primitive_index;
		make_shading_data(_isect.sd, ps.p.instance_index(), ps.p.primitive_index(), local_position);
		_isect.shape_pdf = shape_pdf(origin, _isect.sd.shape_area, ps.p, _isect.shape_pdf_area_measure);

		T_nee_pdf = 1;

		// handle miss
		if (_isect.instance_index() == INVALID_INSTANCE) {
			G = 1;
			local_dir_in = direction;
			// update ray differential
			if (gSpecializationFlags & BDPT_FLAG_RAY_CONES)
				_isect.sd.uv_screen_size *= gRayDifferentials[path_index].radius;
			return;
		}

		const Vector3 dp = _isect.sd.position - origin;
		const Real dist2 = dot(dp, dp);
		G = 1/dist2;

		// update ray differential
		if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
			RayDifferential ray_differential = gRayDifferentials[path_index];
			ray_differential.transfer(sqrt(dist2));
			_isect.sd.uv_screen_size *= ray_differential.radius;
			gRayDifferentials[path_index] = ray_differential;
		}

		_isect.sd.flags = 0;

		if (!gHasMedia || _isect.sd.shape_area > 0) {
			local_dir_in = normalize(_isect.sd.to_local(-direction));
			const Real cos_theta = dot(direction, _isect.sd.geometry_normal());
			if (cos_theta > 0) _isect.sd.flags |= SHADING_FLAG_FRONT_FACE;
			G *= abs(cos_theta);
		} else
			local_dir_in = direction;
	}

	void store_light_vertex() {
		uint flags = 0;
		if (gHasMedia && _isect.sd.shape_area == 0) flags |= PATH_VERTEX_FLAG_IS_MEDIUM;
		if (_isect.sd.flags & SHADING_FLAG_FLIP_BITANGENT) flags |= PATH_VERTEX_FLAG_FLIP_BITANGENT;
		if (path_length > 1) {
			const uint material_address = gInstances[_isect.instance_index()].material_address();
			BF_SET(flags, material_address, 4, 28);
		} else
			BF_SET(flags, -1, 4, 28);

		LightPathVertex lv;
		lv.position = _isect.sd.position;
		lv.packed_geometry_normal = _isect.sd.packed_geometry_normal;
		lv.material_address_flags = flags;
		lv.packed_local_dir_in = pack_normal_octahedron(local_dir_in);
		lv.packed_shading_normal = _isect.sd.packed_shading_normal;
		lv.packed_tangent = _isect.sd.packed_tangent;
		lv.uv = _isect.sd.uv;
		lv.pack_beta(_beta);
		const uint i = light_vertex_index(path_index, path_length);
		gLightPathVertices[i] = lv;

		if (gUseMIS) {
			LightPathVertex1 lv1;
			lv1.dL_prev = d;
			const Vector3 dp = origin - _isect.sd.position;
			lv1.G_rev = prev_cos_out/dot(dp,dp);
			lv1.pdfA_fwd_prev = pdfWtoA(bsdf_pdf, G);
			gLightPathVertices1[i] = lv1;
		}
	}

	void eval_emission(BSDF m) {
		// evaluate path contribution
		Real cos_theta_light;
		Vector3 contrib;
		if (_isect.instance_index() == INVALID_INSTANCE) {
			// background
			if (!gHasEnvironment) return;
			Environment env;
			env.load(gEnvironmentMaterialAddress);
			contrib = _beta * env.eval(direction);
			cos_theta_light = 1;
		} else {
			// emissive surface
			cos_theta_light = -dot(_isect.sd.geometry_normal(), direction);
			if (cos_theta_light < 0) return;
			const Spectrum Le = m.Le();
			if (all(Le <= 0)) return;
			contrib = _beta * Le;
		}

		// compute emission pdf
		Real light_pdfA = T_nee_pdf;
		bool area_measure;
		point_on_light_pdf(light_pdfA, area_measure, _isect);
		if (!area_measure) light_pdfA = pdfWtoA(light_pdfA, G);

		// compute path weight
		Real weight = 1;
		if (path_length > 2) {
			if (gConnectToLightPaths || gConnectToViews) {
				if (gUseMIS) {
					const Vector3 dp = origin - _isect.sd.position;
					const Real p_rev_k = pdfWtoA(cosine_hemisphere_pdfW(abs(cos_theta_light)), abs(prev_cos_out)/dot(dp, dp));
					const Real dE_k = (N_sk(path_length-1) + p_rev_k*d) / pdfWtoA(bsdf_pdf, G);
					weight = 1 / (N_kk + dE_k * light_pdfA);
				} else
					weight = path_weight(path_length, 0);
			} else if (gUseNEE) {
				if (gReservoirNEE)
					weight = NEE::reservoir_bsdf_mis();
				else
					weight = NEE::mis(pdfWtoA(bsdf_pdf, G), light_pdfA);
			}
		}

		gRadiance[pixel_coord].rgb += contrib * weight;

		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eViewTraceContribution)
			gDebugImage[pixel_coord].rgb += contrib * weight;
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 0 && path_length == gPushConstants.gDebugViewPathLength)
			gDebugImage[pixel_coord].rgb += contrib;
	}

	SLANG_MUTATING
	void sample_nee(BSDF m) {
		NEE c;
		if (gPresampleLights) {
			// uniformly sample from the tile
			const uint tile_offset = ((path_index/gLightPresampleTileSize) % gLightPresampleTileCount)*gLightPresampleTileSize;
			const uint ti = rng_next_uint(_rng) % gLightPresampleTileSize;
			c.load_presample(tile_offset + ti, _isect);
		} else
			c.sample(_rng, _isect);

		if (!gDeferNEERays)
			c.eval_visibility(_rng, _medium);

		if (any(c.Le > 0) && c.pdfA > 1e-6) {
			Real nee_bsdf_pdf;
			Spectrum contrib = _beta * c.eval(m, local_dir_in, nee_bsdf_pdf) / c.pdfA;

			// mis between nee and bsdf sampling
			const Real weight = gSampleBSDFs ? NEE::mis(c.pdfA, pdfWtoA(c.T_dir_pdf*nee_bsdf_pdf, c.G)) : 1;

			if (gDeferNEERays) {
				NEERayData rd;
				rd.contribution = contrib * weight;
				rd.rng_offset = _rng.w;
				rd.ray_origin = c.ray_origin;
				rd.medium = _medium;
				rd.ray_direction = c.ray_direction;
				rd.dist = c.dist;
				gNEERays[nee_vertex_index(path_index, path_length)] = rd;
			} else
				gRadiance[pixel_coord].rgb += contrib * weight;

			if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 1 && path_length == gPushConstants.gDebugViewPathLength) {
				if (gDeferNEERays) c.eval_visibility(_rng, _medium, contrib);
				gDebugImage[pixel_coord].rgb += contrib;
			}
		}
	}
	SLANG_MUTATING
	void sample_prev_reservoir(BSDF m, inout Reservoir r, const uint reservoir_index, const float2 coord, inout Spectrum contrib, inout Real r_target_pdf, inout uint M, inout Real sum_src_pdf) {
		if (any(coord < 0) || any(coord >= gOutputExtent) || any(coord != coord)) return;
		const uint prev_index_1d = ((int)coord.y)*gOutputExtent.x + (int)coord.x;

		const VisibilityInfo prev_vis = gPrevVisibility[prev_index_1d];
		if (dot(_isect.sd.shading_normal(), prev_vis.normal()) < cos(degrees(5))) return;
		const Reservoir prev = gPrevReservoirs[prev_index_1d];
		if (prev.M == 0) return;

		M += prev.M;
		sum_src_pdf += prev.src_pdf;

		if (prev.W <= 0) return;

		const rng_state_t prev_s = gPrevReservoirSamples[prev_index_1d];
		rng_state_t tmp_rng = prev_s;
		NEE prev_c;
		prev_c.sample(tmp_rng, _isect);

		if (any(prev_c.Le > 0) && gReservoirUnbiasedReuse) {
			prev_c.eval_visibility(_rng, _medium);
			//if (any(prev_c.Le > 0)) Z += prev.M;
		}

		if (any(prev_c.Le > 0)) {
			Real _bsdf_pdf;
			const Spectrum prev_contrib = prev_c.eval(m, local_dir_in, _bsdf_pdf);
			const Real prev_target_pdf = luminance(prev_contrib);
			if (prev_target_pdf > 0) {
				if (r.update(rng_next_float(_rng), prev_target_pdf * prev.W * prev.M)) {
					r.src_pdf = prev.src_pdf;
					r_target_pdf = prev_target_pdf;
					gReservoirSamples[reservoir_index] = prev_s;
					contrib = prev_contrib;
				}
			}
		}
	}
	SLANG_MUTATING
	void sample_reservoir_nee(BSDF m) {
		Spectrum contrib;
		Reservoir r;
		Real r_target_pdf = 0;
		r.init();

		const uint reservoir_index = pixel_coord.y*gOutputExtent.x + pixel_coord.x;

		// initial ris
		{
			const uint tile_offset = ((path_index/gLightPresampleTileSize) % gLightPresampleTileCount)*gLightPresampleTileSize;

			NEE c;
			for (uint j = 0; j < gNEEReservoirM; j++) {
				NEE _c;
				rng_state_t _s;

				if (gPresampleLights) {
					// uniformly sample from the tile
					const uint ti = rng_next_uint(_rng) % gLightPresampleTileSize;
					_s = rng_init(-1, tile_offset + ti);
					_c.load_presample(tile_offset + ti, _isect);
				} else {
					_s = _rng;
					_c.sample(_rng, _isect);
				}

				if (_c.pdfA <= 0 || all(_c.Le <= 0)) { r.M++; continue; }

				// evaluate the material
				Real _bsdf_pdf;
				const Spectrum _contrib = _c.eval(m, local_dir_in, _bsdf_pdf);
				const Real target_pdf = luminance(_contrib);
				if (r.update(rng_next_float(_rng), target_pdf/_c.pdfA)) {
					if (path_length == 2 && (gReservoirTemporalReuse || gReservoirSpatialReuse)) gReservoirSamples[reservoir_index] = _s;
					r.src_pdf = _c.pdfA;
					r_target_pdf = target_pdf;
					c = _c;
					contrib = _contrib;
				}
			}

			if (r_target_pdf > 0 && r.M > 0)
				r.W = r.total_weight / (r.M * r_target_pdf);

			if (r.W > 0) c.eval_visibility(_rng, _medium, contrib);

				// zero weight from shadowed sample
			if (all(contrib <= 0) || any(contrib != contrib)) r.W = 0;
		}

		if (path_length == 2 && (gReservoirTemporalReuse || gReservoirSpatialReuse)) {
			const float2 prev_pixel_coord = gPrevUVs[pixel_coord] * gOutputExtent;

			if (gReservoirTemporalReuse && all((int2)prev_pixel_coord >= 0) && all((int2)prev_pixel_coord < gOutputExtent)) {
				uint M = r.M;
				Real sum_src_pdf = r.src_pdf;
				sample_prev_reservoir(m, r, reservoir_index, prev_pixel_coord, contrib, r_target_pdf, M, sum_src_pdf);
				r.W = r.total_weight / (M * r_target_pdf);
				r.M = M;
			}

			if (gReservoirSpatialReuse) {
				uint M = r.M;
				Real sum_src_pdf = r.src_pdf;
				for (uint i = 0; i < gNEEReservoirSpatialSamples; i++) {
					const Real theta = 2 * M_PI * rng_next_float(_rng);
					const float2 coord = prev_pixel_coord + float2(cos(theta), sin(theta)) * sqrt(rng_next_float(_rng)) * gNEEReservoirSpatialRadius;
					sample_prev_reservoir(m, r, reservoir_index, coord, contrib, r_target_pdf, M, sum_src_pdf);
				}
				r.W = r.total_weight / (M * r_target_pdf);
				r.M = M;
			}

			r.M = min(r.M, gReservoirMaxM);
			gReservoirs[reservoir_index] = r;
		}

		if (r.W > 1e-6) {
			contrib *= r.W;
			if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eReservoirWeight)
				gDebugImage[pixel_coord].rgb += r.W;

			// average nee and bsdf sampling
			Real weight = gSampleBSDFs ? (1 - NEE::reservoir_bsdf_mis()) : 1;

			gRadiance[pixel_coord].rgb += _beta * contrib * weight;
			if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 1 && path_length == gPushConstants.gDebugViewPathLength)
				gDebugImage[pixel_coord].rgb += _beta * contrib;
		}
	}

	// light subpath connections and light tracing connection strategies
	SLANG_MUTATING
	void bdpt_connect(BSDF m) {
		if (gTraceLight && gConnectToViews) {
			if (path_length+1 <= gMaxPathVertices) {
				// connect light path to eye
				const Vector3 dp = origin - _isect.sd.position;
				const Real G_rev = abs(prev_cos_out) / dot(dp,dp);
				const Real ngdotin = -dot(_isect.sd.geometry_normal(), direction);
				ViewConnection::sample(m, _rng, _medium, _isect, _beta, path_length, local_dir_in, ngdotin, d, pdfWtoA(bsdf_pdf, G), G_rev);
			}
		} else if (!gTraceLight && gConnectToLightPaths) {

			// TODO: choose a random light sub-path with probability proportional to its number of vertices, then choose a uniformly random vertex on the path

			// connect eye path to light subpaths
			for (uint light_length = 1; light_length <= gMaxLightPathVertices && light_length + path_length <= gMaxPathVertices; light_length++) {
				LightPathConnection c;
				if (!c.connect(_rng, light_vertex_index(path_index, light_length), light_length == 1, _isect, _medium)) break; // break on invalid vertex (end of light subpath)
				if (all(c.f <= 0)) continue;

				MaterialEvalRecord _eval;
				m.eval(_eval, local_dir_in, c.local_to_light, false);
				if (_eval.pdf_fwd < 1e-6) continue;
				_eval.f *= abs(c.local_to_light.z);

				// c.f includes 1/dist2 and cosine term from the light path vertex
				const Spectrum contrib = _beta * _eval.f * c.f;

				Real weight;
				if (gUseMIS) {
					const Vector3 dp = origin - _isect.sd.position;
					const Real G_rev = prev_cos_out/dot(dp,dp);
					const Real dE = (N_sk(path_length-1) + d * pdfWtoA(_eval.pdf_rev, G_rev)) / pdfWtoA(bsdf_pdf, G);
					weight = 1 / (N_sk(path_length) + dE * c.pdfA_rev + c.dL * pdfWtoA(_eval.pdf_fwd, c.G_fwd));
				} else
					weight = path_weight(path_length, light_length);

				if (weight > 0 && any(contrib > 0)) {
					gRadiance[pixel_coord].rgb += contrib * weight;
					if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && light_length == gPushConstants.gDebugLightPathLength && path_length == gPushConstants.gDebugViewPathLength)
						gDebugImage[pixel_coord].rgb += contrib;
				}
			}
		}
	}

	// sample next direction, compute _beta *= f/pdfW, update dE/dL, update origin and direction
	SLANG_MUTATING
	bool sample_direction(BSDF m) {
		const Vector3 rnd = Vector3(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));
		MaterialSampleRecord _material_sample;
		m.sample(_material_sample, rnd, local_dir_in, _beta, gTraceLight);

		if (_material_sample.pdf_fwd < 1e-6) {
			_beta = 0;
			return false;
		}

		if (gUseMIS) {
			// dE_s = ( N_{s-1,k} + P(s-1 <- s  ) * dE_{s-1} ) / P(s-1 -> s)
			//		= ( N_{s-1,k} + pdf_rev * prev_dE ) / prev_pdf_fwd

			// dL_s = ( N_{s,k} + P(s -> s+1) * dL_{s+1} ) / P(s <- s+1)
			//		= ( N_{s,k} + pdf_rev * prev_dL ) / prev_pdf_fwd
			const Vector3 dp = origin - _isect.sd.position;
			const Real G_rev = prev_cos_out/dot(dp,dp);
			d = (N_sk(gTraceLight ? gMaxPathVertices-path_length : (path_length-1)) + d*pdfWtoA(_material_sample.pdf_rev, G_rev)) / pdfWtoA(bsdf_pdf, G);
			bsdf_pdf = _material_sample.pdf_fwd;
		}

		// update origin, compute prev_cos_out, correct shading normal

		if (!gHasMedia || _isect.sd.shape_area > 0) {
			const Real ndotout = _material_sample.dir_out.z;

			_beta *= abs(ndotout);

			_material_sample.dir_out = normalize(_isect.sd.to_world(_material_sample.dir_out));

			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			const Real ngdotout = dot(geometry_normal, _material_sample.dir_out);
			origin = ray_offset(_isect.sd.position, ngdotout > 0 ? geometry_normal : -geometry_normal);

			if (gUseMIS) prev_cos_out = ngdotout;

			// shading normal correction
			if (gTraceLight) {
				const Real num = ngdotout * local_dir_in.z;
				const Real denom = ndotout * dot(geometry_normal, -direction);
				if (abs(denom) > 1e-5) _beta *= abs(num / denom);
			}
		} else {
			origin = _isect.sd.position;
			if (gUseMIS) prev_cos_out = 1;
		}

		direction = _material_sample.dir_out;

		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eDirOut) gDebugImage[pixel_coord] = float4(direction*.5+.5, 1);
		return true;
	}

	SLANG_MUTATING
	bool russian_roulette() {
		if (gCoherentRR) {
			if (WaveActiveAllEqual(path_length >= gMinPathVertices)) {
				Real p = luminance(_beta) * 0.95;
				//p = WaveActiveMax(p);
				p = WaveActiveSum(p) / WaveActiveCountBits(1);
				bool v;
				if (WaveIsFirstLane())
					v = rng_next_float(_rng) > p;
				v = WaveReadLaneFirst(v);
				if (v)
					return false;
				else
					_beta /= p;
			}
		} else if (path_length >= gMinPathVertices) {
			const Real p = luminance(_beta) * 0.95;
			if (p < 1) {
				if (rng_next_float(_rng) > p)
					return false;
				else
					_beta /= p;
			}
		}
		return true;
	}

	SLANG_MUTATING
	bool next_vertex(BSDF m) {
		// emission from vertex 2 is accumulated in sample_visibility
		if (path_length > 2 && !gTraceLight)
			eval_emission(m);

		if (_isect.instance_index() == INVALID_INSTANCE || !m.can_eval())
			return false;

		if (!gTraceLight) {
			if (!russian_roulette()) return false;

			if (gUseNEE && path_length < gMaxPathVertices) {
				if (gReservoirNEE)
					sample_reservoir_nee(m);
				else
					sample_nee(m);
			}
		}

		bdpt_connect(m);

		if (gSampleBSDFs && path_length < gMaxVertices)
			return sample_direction(m);
		else
			return false;
	}

	// expects _medium, origin and direction to bet set
	// increments path_length, sets local_dir_in, G and T_nee_pdf.
	SLANG_MUTATING
	void trace() {
		Real T_dir_pdf = 1;
		T_nee_pdf = 1;
		trace_ray(_rng, origin, direction, _medium, _beta, T_dir_pdf, T_nee_pdf, _isect, local_position);
		if (T_dir_pdf <= 0 || all(_beta <= 0)) { _beta = 0; return; }
		_beta /= T_dir_pdf;

		if (gUseMIS) bsdf_pdf *= T_dir_pdf;

		path_length++;

		// handle miss
		if (_isect.instance_index() == INVALID_INSTANCE) {
			G = 1;
			local_dir_in = direction;
			if (gSpecializationFlags & BDPT_FLAG_RAY_CONES)
				_isect.sd.uv_screen_size *= gRayDifferentials[path_index].radius;
			return;
		}

		const Vector3 dp = _isect.sd.position - origin;
		const Real dist2 = dot(dp, dp);

		// update ray differential
		if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
			RayDifferential ray_differential = gRayDifferentials[path_index];
			ray_differential.transfer(sqrt(dist2));
			_isect.sd.uv_screen_size *= ray_differential.radius;
			gRayDifferentials[path_index] = ray_differential;
		}

		G = 1/dist2;

		if (!gHasMedia || _isect.sd.shape_area > 0) {
			local_dir_in = normalize(_isect.sd.to_local(-direction));
			G *= abs(dot(direction, _isect.sd.geometry_normal()));
		} else
			local_dir_in = direction;
	}

	// calls sample_directions() and trace()
	SLANG_MUTATING
	void next_vertex() {
		if (gTraceLight && gConnectToLightPaths)
			store_light_vertex();

		if (gKernelIterationCount > 0) init_rng();

		// sample nee/bdpt connections, sample next direction

		const uint material_address = gInstances[_isect.instance_index()].material_address();

		if (gHasMedia && _isect.sd.shape_area == 0) {
			Medium m;
			m.load(material_address);
			if (!next_vertex(m)) { _beta = 0; return; }
		} else {
			Material m;
			m.load(material_address, _isect.sd.uv, _isect.sd.uv_screen_size);
			if (!next_vertex(m)) { _beta = 0; return; }
		}

		trace();
	}
};

#endif