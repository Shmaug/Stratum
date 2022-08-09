#ifndef PATH_H
#define PATH_H

#include "rng.hlsli"
#include "intersection.hlsli"
#include "materials/environment.h"
#include "light.hlsli"

static Real path_weight(const uint view_length, const uint light_length) {
	const uint path_length = view_length + light_length;
	if (path_length <= 2)
		return 1; // light is directly visible
	// E E ... E E   regular path tracing
	uint n = 1;
	// E E ... E N   regular path tracing, next event estimation
	if (gUseNEE) n++;
	// E E ... L L   bdpt connection to light vertex N, where N is [1,gMaxLightPathVertices]
	if (gConnectToLightPaths) n += min(gMaxLightPathVertices, path_length-2);
	// V L ... L L   light tracing, view connection
	if (gConnectToViews) n++;
	return 1.f / n;
}

// connects a view path to a vertex in its associated light path
struct LightPathConnection {
	Spectrum f;
	Vector3 local_to_light;

	SLANG_MUTATING
	bool connect(inout rng_state_t _rng, const uint li, const bool eval_bsdf, const IntersectionVertex _isect, const uint cur_medium) {
		const LightPathVertex2 lv2 = gLightPathVertices2[li];
		f = lv2.beta();
		if (all(f <= 0)) return false; // invalid vertex

		const LightPathVertex0 lv0 = gLightPathVertices0[li];

		Vector3 to_light = lv0.position - _isect.sd.position;
		Real dist = length(to_light);
		const Real rcp_dist = 1/dist;
		to_light *= rcp_dist;

		f *= pow2(rcp_dist);

		if (!eval_bsdf) // dont evaluate bsdf for first vertex
			f *= max(0, -dot(lv0.geometry_normal(), to_light)); // cosine term from light surface
		else {
			const LightPathVertex1 lv1 = gLightPathVertices1[li];
			MaterialEvalRecord _eval;
			if (lv1.is_medium()) {
				Medium m;
				m.load(lv1.material_address());
				m.eval(_eval, lv1.local_dir_in(), -to_light);
			} else {
				const Vector3 local_geometry_normal = lv1.to_local(lv0.geometry_normal());
				const Vector3 local_dir_out = normalize(lv1.to_local(-to_light));
				const Vector3 local_dir_in = lv1.local_dir_in();
				Material m;
				m.load_and_sample(lv1.material_address(), lv2.uv, 0);
				m.eval(_eval, local_dir_in, local_dir_out, true);
				_eval.f *= abs(local_dir_out.z);

				// shading normal correction
				const Real num = dot(local_geometry_normal, local_dir_out) * local_dir_in.z;
				const Real denom = local_dir_out.z * dot(local_geometry_normal, local_dir_in);
				if (abs(denom) > 1e-5) _eval.f *= abs(num / denom);
			}
			f *= _eval.f;
		}

		if (all(f <= 0)) return true; // vertex not visible, but still valid

		Vector3 origin = _isect.sd.position;
		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = to_light;
		} else {
			local_to_light = normalize(_isect.sd.to_local(to_light));
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			origin = ray_offset(origin, dot(geometry_normal, to_light) > 0 ? geometry_normal : -geometry_normal);
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
	static const float gLightSampleQuantization = 1024;

	Spectrum contribution;
	uint output_index;
	Vector3 local_geometry_normal;
	Real ngdotout;
	Vector3 local_to_view;

	static Spectrum load_sample(const uint2 pixel_coord) {
		const uint idx = pixel_coord.y * gOutputExtent.x + pixel_coord.x;
		return gLightTraceSamples.Load<uint3>(12 * idx) / gLightSampleQuantization;
	}
	static void accumulate_contribution(const uint output_index, const Spectrum c) {
		const int3 ci = c * gLightSampleQuantization;
		gLightTraceSamples.InterlockedAdd(12*output_index + 0, ci[0]);
		gLightTraceSamples.InterlockedAdd(12*output_index + 4, ci[1]);
		gLightTraceSamples.InterlockedAdd(12*output_index + 8, ci[2]);
	}

	SLANG_MUTATING
	void sample(inout uint4 _rng, const uint cur_medium, const IntersectionVertex _isect, const Spectrum beta) {
		uint view_index = 0;
		if (gViewCount > 1) view_index = min(rng_next_float(_rng)*gViewCount, gViewCount-1);

		const Vector3 position = Vector3(
			gViewTransforms[view_index].m[0][3],
			gViewTransforms[view_index].m[1][3],
			gViewTransforms[view_index].m[2][3] );
		const Vector3 normal = normalize(gViewTransforms[view_index].transform_vector(Vector3(0,0,gViews[view_index].projection.near_plane > 0 ? 1 : -1)));

		Vector3 to_view = position - _isect.sd.position;
		const Real dist = length(to_view);
		to_view /= dist;

		const Real sensor_cos_theta = -dot(to_view, normal);
		if (sensor_cos_theta < 0) { contribution = 0; return; }

		const Real lens_radius = 0;
		const Real lens_area = lens_radius > 0 ? (M_PI * lens_radius * lens_radius) : 1;
		const Real sensor_pdf = pdfAtoW(1/lens_area, sensor_cos_theta / pow2(dist));
		const Real sensor_importance = 1 / (gViews[view_index].projection.sensor_area * lens_area * pow4(sensor_cos_theta));

		contribution = beta * sensor_importance / sensor_pdf;

		float4 screen_pos = gViews[view_index].projection.project_point(gInverseViewTransforms[view_index].transform_point(_isect.sd.position));
		screen_pos.y = -screen_pos.y;
		screen_pos.xyz /= screen_pos.w;
		if (any(abs(screen_pos.xyz) >= 1) || screen_pos.z <= 0) { contribution = 0; return; }
        const float2 uv = screen_pos.xy*.5 + .5;
        const int2 ipos = gViews[view_index].image_min + (gViews[view_index].image_max - gViews[view_index].image_min) * uv;
		output_index = ipos.y * gOutputExtent.x + ipos.x;

		Vector3 origin = _isect.sd.position;
		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_view = to_view;
			local_geometry_normal = 0;
			ngdotout = 0;
		} else {
			local_to_view = normalize(_isect.sd.to_local(to_view));
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			local_geometry_normal = normalize(_isect.sd.to_local(geometry_normal));
			ngdotout = dot(to_view, geometry_normal);
			origin = ray_offset(origin, ngdotout > 0 ? geometry_normal : -geometry_normal);
		}

		Real T_nee_pdf = 1;
		Real T_dir_pdf = 1;
		trace_visibility_ray(_rng, origin, to_view, dist, cur_medium, contribution, T_dir_pdf, T_nee_pdf);
		if (T_nee_pdf > 0) contribution /= T_nee_pdf;
	}

	void eval(const BSDF m, const Vector3 local_dir_in, const uint path_length, const Real ngdotin) {
		if (all(contribution <= 0)) return;

		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, local_to_view, true);

		Spectrum c = contribution * _eval.f * abs(local_to_view.z);

		// shading normal correction
		if (ngdotout != 0) {
			const Real num = ngdotout * local_dir_in.z;
			const Real denom = local_to_view.z * ngdotin;
			if (abs(denom) > 1e-5) c *= abs(num / denom);
		}

		const Real weight = path_weight(1, path_length);
		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugViewPathLength == 1) {
			if (gPushConstants.gDebugLightPathLength == path_length)
				accumulate_contribution(output_index, c);
		} else
			accumulate_contribution(output_index, c * weight);
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
	static Real bsdf_mis(const Real bsdf_pdf, const Real T_nee_pdf, const IntersectionVertex _isect, const Real G) {
		if (!gUseMIS) return 0.5;
		Real l_pdf = T_nee_pdf;
		bool area_measure;
		point_on_light_pdf(l_pdf, area_measure, _isect);

		if (!area_measure) l_pdf = pdfWtoA(l_pdf, G);

		return mis(pdfWtoA(bsdf_pdf, G), l_pdf);
	}
	static Real reservoir_bsdf_mis() {
		return 0.5;
	}

	// load shading data and Le
	SLANG_MUTATING
	void sample(inout rng_state_t _rng, const IntersectionVertex _isect) {
		LightSampleRecord ls;
		sample_point_on_light(ls, float4(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng)), _isect.sd.position);
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
		return Le * _eval.f * G * abs(local_to_light.z);
	}
};

struct PathIntegrator {
	uint2 pixel_coord;
	uint path_index;
	uint path_length;
	Spectrum _beta;

	Real bsdf_pdf; // solid angle measure
	Real d; // dE for view paths, dL for light paths

	rng_state_t _rng;

	// the ray that was traced to get here
	// also the ray traced by trace()
	Vector3 origin, direction;
	Real prev_cos_theta; // at origin, towards current vertex

	IntersectionVertex _isect;
	Vector3 local_position;
	uint _medium;
	Vector3 local_dir_in; // _isect.sd.to_local(-direction)
	Real G; // abs(dot(direction, _isect.sd.geometry_normal())) / dist2(_isect.sd.position, origin)
	Real T_nee_pdf;

	static const uint gVertexRNGOffset = 16 + (gHasMedia ? 2*gMaxNullCollisions : 0);
	static const uint gMaxVertices = gTraceLight ? gMaxLightPathVertices : gMaxPathVertices;

	static const uint light_vertex_index(const uint path_index, const uint path_length) { return path_index*gMaxLightPathVertices + path_length-1; }
	static const uint nee_vertex_index  (const uint path_index, const uint path_length) { return gOutputExtent.x*gOutputExtent.y*(path_length-2)*(gMaxPathVertices-2) + path_index; }

	SLANG_MUTATING
	void init_rng() {
		_rng = rng_init(pixel_coord, (gTraceLight ? 0xFFFFFF : 0) + (path_length-1)*gVertexRNGOffset);
	}

	PathState store_state() {
		PathState ps;
		ps.p.local_position = local_position;
		ps.p.instance_primitive_index = _isect.instance_primitive_index;
		ps.origin = origin;
		ps.pack_path_length_medium(path_length, _medium);
		ps.beta = _beta;
		ps.prev_cos_theta = prev_cos_theta;
		ps.dir_in = direction;
		ps.bsdf_pdf = bsdf_pdf;
		return ps;
	}
	PathState1 store_state1() {
		PathState1 ps;
		ps.d = d;
		return ps;
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

		const uint i = light_vertex_index(path_index, path_length);
		gLightPathVertices0[i].position = _isect.sd.position;
		gLightPathVertices0[i].packed_geometry_normal = _isect.sd.packed_geometry_normal;
		gLightPathVertices1[i].material_address_flags = flags;
		gLightPathVertices1[i].packed_local_dir_in = pack_normal_octahedron(local_dir_in);
		gLightPathVertices1[i].packed_shading_normal = _isect.sd.packed_shading_normal;
		gLightPathVertices1[i].packed_tangent = _isect.sd.packed_tangent;
		gLightPathVertices2[i].uv = _isect.sd.uv;
		gLightPathVertices2[i].pack_beta(_beta);
		gLightPathVertices3[i].d = d;
	}

	void eval_emission(BSDF m) {
		Real weight = 1;
		if (path_length > 2) {
			if (gUseNEE) {
				if (gReservoirNEE)
					weight = NEE::reservoir_bsdf_mis();
				else
					weight = NEE::bsdf_mis(bsdf_pdf, T_nee_pdf, _isect, G);
			}
			if (gConnectToLightPaths || gConnectToViews)
				weight = path_weight(path_length, 0);
		}

		if (_isect.instance_index() == INVALID_INSTANCE) {
			// background hit
			if (gHasEnvironment) {
				Environment env;
				env.load(gEnvironmentMaterialAddress);
				const Spectrum contrib = _beta * env.eval(direction);
				gRadiance[pixel_coord].rgb += contrib * weight;
				if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 0 && path_length == gPushConstants.gDebugViewPathLength)
					gDebugImage[pixel_coord.xy].rgb += contrib;
			}
		} else if (dot(_isect.sd.geometry_normal(), -direction) > 0) {
			const Spectrum Le = m.Le();
			if (any(Le > 0)) {
				const Spectrum contrib = _beta * Le;
				gRadiance[pixel_coord].rgb += contrib * weight;
				if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 0 && path_length == gPushConstants.gDebugViewPathLength)
					gDebugImage[pixel_coord.xy].rgb += contrib;
			}
		}
	}

	SLANG_MUTATING
	void eval_nee(NEE c, Spectrum contrib, const Real weight) {
		if (gDeferNEERays) {
			NEERayData rd;
			rd.contribution = contrib * weight;
			rd.rng_offset = _rng.w;
			rd.ray_origin = c.ray_origin;
			rd.medium = _medium;
			rd.ray_direction = c.ray_direction;
			rd.dist = c.dist;
			gNEERays[nee_vertex_index(path_index, path_length)] = rd;
		} else {
			gRadiance[pixel_coord].rgb += contrib * weight;
		}

		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 1 && path_length == gPushConstants.gDebugViewPathLength) {
			if (gDeferNEERays) c.eval_visibility(_rng, _medium, contrib);
			gDebugImage[pixel_coord].rgb += contrib;
		}
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
			Spectrum contrib = c.eval(m, local_dir_in, nee_bsdf_pdf);

			// mis between nee and bsdf sampling
			Real weight = gSampleBSDFs ? NEE::mis(c.pdfA, pdfWtoA(c.T_dir_pdf*nee_bsdf_pdf, c.G)) : 1;
			// bdpt weight overrides nee/bsdf mis weight
			if (gConnectToLightPaths || gConnectToViews) weight = path_weight(path_length, 1);

			contrib /= c.pdfA;
			eval_nee(c, contrib * _beta, weight);
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
			// bdpt weight overrides nee/bsdf mis weight
			if (gConnectToLightPaths || gConnectToViews) weight = path_weight(path_length, 1);

			gRadiance[pixel_coord].rgb += _beta * contrib * weight;
			if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 1 && path_length == gPushConstants.gDebugViewPathLength)
				gDebugImage[pixel_coord].rgb += _beta * contrib;
		}
	}

	// NEE, BDPT, and light tracing connection strategies
	SLANG_MUTATING
	void sample_connections(BSDF m) {
		if (path_length >= gMaxPathVertices || !m.can_eval()) return;

		if (gTraceLight) {
			// add light trace contribution
			if (gConnectToViews) {
				ViewConnection v;
				v.sample(_rng, _medium, _isect, _beta);
				v.eval(m, local_dir_in, path_length, dot(_isect.sd.geometry_normal(), -direction));
			}
		} else {
			// add nee contribution
			if (gUseNEE) {
				// pick a point on a light
				if (gReservoirNEE)
					sample_reservoir_nee(m);
				else
					sample_nee(m);
			}

			// add bdpt contribution
			if (gConnectToLightPaths) {
				for (uint light_length = 1; light_length <= gMaxLightPathVertices && light_length + path_length <= gMaxPathVertices; light_length++) {
					LightPathConnection c;
					if (!c.connect(_rng, light_vertex_index(path_index, light_length), light_length > 1, _isect, _medium)) break; // break on invalid vertex (at end of path)
					if (all(c.f <= 0)) continue;

					MaterialEvalRecord _eval;
					m.eval(_eval, local_dir_in, c.local_to_light, false);

					// c.f includes 1/dist2 and cosine term from the light path vertex
					const Spectrum contrib = _beta * _eval.f * abs(c.local_to_light.z) * c.f;
					const Real weight = path_weight(path_length, light_length);

					if (weight > 0 && any(contrib > 0)) {
						gRadiance[pixel_coord].rgb += contrib * weight;
						if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && light_length == gPushConstants.gDebugLightPathLength && path_length == gPushConstants.gDebugViewPathLength)
							gDebugImage[pixel_coord].rgb += contrib;
					}
				}
			}
		}
	}

	// sample next direction, compute _beta *= f/pdfW, update origin and direction
	SLANG_MUTATING
	void sample_next_direction(BSDF m) {
		const Vector3 rnd = Vector3(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));
		MaterialSampleRecord _material_sample;
		m.sample(_material_sample, rnd, local_dir_in, _beta, gTraceLight);

		if (gUseMIS) {
			// TODO: light vertex 2 will not have d computed properly, as sample_next_direction is not called on light vertex 1
			// does the computation of d line up with when store_light_vertex is called?
			const Vector3 dp = _isect.sd.position - origin;
			d = (1 + d*pdfWtoA(_material_sample.pdf_rev, prev_cos_theta/dot(dp, dp))) / pdfWtoA(bsdf_pdf, G);
			bsdf_pdf = _material_sample.pdf_fwd;
		}

		if (_material_sample.pdf_fwd < 1e-6) {
			_beta = 0;
			return;
		}

		if (!gHasMedia || _isect.sd.shape_area > 0) {
			const Real ndotout = _material_sample.dir_out.z;

			_beta *= abs(ndotout);

			_material_sample.dir_out = normalize(_isect.sd.to_world(_material_sample.dir_out));

			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			const Real ngdotout = dot(geometry_normal, _material_sample.dir_out);
			origin = ray_offset(_isect.sd.position, ngdotout > 0 ? geometry_normal : -geometry_normal);

			// shading normal correction
			if (gTraceLight) {
				const Real num = ngdotout * local_dir_in.z;
				const Real denom = ndotout * dot(geometry_normal, -direction);
				if (abs(denom) > 1e-5) _beta *= abs(num / denom);
			}

			prev_cos_theta = ngdotout;
		} else {
			origin = _isect.sd.position;
			prev_cos_theta = 1;
		}

		direction = _material_sample.dir_out;

		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eDirOut) gDebugImage[pixel_coord] = float4(direction*.5+.5, 1);
	}

	// add radiance contribution from surface, sample NEE/BDPT connections, terminate with russian roullette, sample next direction
	SLANG_MUTATING
	void sample_directions(BSDF m) {
		if (path_length > 2) {
			if (gTraceLight)
				store_light_vertex();
			else
				eval_emission(m);
		}

		if (path_length >= gMaxVertices || _isect.instance_index() == INVALID_INSTANCE) { _beta = 0; return; }

		sample_connections(m);

		if (!gSampleBSDFs) { _beta = 0; return; }

		if (path_length >= gMinPathVertices) {
			const Real p = max(max(_beta.r, _beta.g), _beta.b) * 0.95;
			if (p < 1) {
				if (rng_next_float(_rng) > p) {
					_beta = 0;
					return;
				} else
					_beta /= p;
			}
		}

		sample_next_direction(m);
	}

	// expects _medium, origin and direction to bet set
	// sets local_dir_in, G and T_nee_pdf. increments path_length
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
		G = 1/dist2;

		// update ray differential
		if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
			RayDifferential ray_differential = gRayDifferentials[path_index];
			ray_differential.transfer(sqrt(dist2));
			_isect.sd.uv_screen_size *= ray_differential.radius;
			gRayDifferentials[path_index] = ray_differential;
		}

		if (!gHasMedia || _isect.sd.shape_area > 0) {
			local_dir_in = normalize(_isect.sd.to_local(-direction));
			G *= abs(dot(direction, _isect.sd.geometry_normal()));
		} else
			local_dir_in = direction;
	}

	// calls sample_directions() and trace()
	SLANG_MUTATING
	void next_vertex() {
		init_rng();

		const uint material_address = gInstances[_isect.instance_index()].material_address();

		// sample light and next direction

		if (gHasMedia && _isect.sd.shape_area == 0) {
			Medium m;
			m.load(material_address);
			sample_directions(m);
		} else {
			Material m;
			m.load_and_sample(material_address, _isect.sd.uv, _isect.sd.uv_screen_size);
			sample_directions(m);
		}

		if (any(_beta > 0))
			trace();
	}
};

#endif