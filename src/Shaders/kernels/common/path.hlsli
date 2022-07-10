#ifndef PATH_H
#define PATH_H

#include "rng.hlsli"
#include "intersection.hlsli"
#include "../../materials/environment.h"
#include "light.hlsli"

// connects a view path to a vertex in its associated light path
struct LightPathConnection {
	Spectrum f;
	Real T_dir_pdf;
	Vector3 local_to_light;

	static Real path_weight(const uint path_length) {
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

	SLANG_MUTATING
	bool connect(inout uint4 _rng, const uint path_index, const uint light_vertex, const IntersectionVertex _isect, const uint cur_medium) {
		const uint li = path_index*gMaxLightPathVertices + light_vertex-1;

		const LightPathVertex2 lv2 = gLightPathVertices2[li];
		f = lv2.beta();
		if (all(f <= 0)) return false; // invalid vertex

		const LightPathVertex0 lv0 = gLightPathVertices0[li];

		Vector3 origin = _isect.sd.position;
		Vector3 to_light = lv0.position - origin;
		Real dist = length(to_light);
		const Real rcp_dist = 1/dist;
		to_light *= rcp_dist;
		f *= pow2(rcp_dist); // note: cosine terms handled inside material eval

		if (light_vertex == 1) // not reflecting off the first vertex
			f *= max(0,-dot(lv0.geometry_normal(), to_light)); // cosine term from light surface
		else {
			const LightPathVertex1 lv1 = gLightPathVertices1[li];
			MaterialEvalRecord _eval;
			if (lv1.is_medium()) {
				Medium m;
				m.load(lv1.material_address());
				m.eval(_eval, lv1.local_dir_in(), -to_light);
			} else {
				Material m;
				m.load_and_sample(lv1.material_address(), lv2.uv, 0);
				m.eval(_eval, lv1.local_dir_in(), normalize(lv1.to_local(-to_light)), true);
			}
			f *= _eval.f;
		}

		if (all(f <= 0)) return true; // vertex not visible, but still valid

		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = to_light;
		} else {
			local_to_light = normalize(_isect.sd.to_local(to_light));
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			origin = ray_offset(origin, local_to_light.z > 0 ? geometry_normal : -geometry_normal);
			dist -= abs(dist - length(lv0.position - origin));
		}

		T_dir_pdf = 1;
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

	Vector3 local_to_view;
	uint output_index;
	Spectrum contribution;

	static Spectrum load_sample(const uint2 pixel_coord) {
		const uint idx = pixel_coord.y * gOutputExtent.x + pixel_coord.x;
		return gLightTraceSamples.Load<uint3>(16 * idx) / gLightSampleQuantization;
	}
	static void accumulate_contribution(const uint output_index, const Spectrum c) {
		const int3 ci = c * gLightSampleQuantization;
		gLightTraceSamples.InterlockedAdd(16*output_index + 0, ci[0]);
		gLightTraceSamples.InterlockedAdd(16*output_index + 4, ci[1]);
		gLightTraceSamples.InterlockedAdd(16*output_index + 8, ci[2]);
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

		const float lens_radius = 0;
		const float lens_area = lens_radius != 0 ? (M_PI * lens_radius * lens_radius) : 1;
		const float sensor_pdf = pow2(dist) / (sensor_cos_theta * lens_area);
		const float sensor_importance = 1 / (gViews[view_index].projection.sensor_area * lens_area * pow4(sensor_cos_theta));

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
		} else {
			local_to_view = normalize(_isect.sd.to_local(to_view));
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			origin = ray_offset(origin, local_to_view.z > 0 ? geometry_normal : -geometry_normal);
		}

		Real T_nee_pdf = 1;
		Real T_dir_pdf = 1;
		trace_visibility_ray(_rng, origin, to_view, dist, cur_medium, contribution, T_dir_pdf, T_nee_pdf);
		contribution /= T_nee_pdf;
	}

	void eval(const BSDF m, const Vector3 local_dir_in, const uint path_length) {
		if (all(contribution <= 0)) return;
		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, local_to_view, true);
		Spectrum contrib = contribution * _eval.f;
		const Real weight = LightPathConnection::path_weight(path_length + 1);
		if ((DebugMode)gDebugMode == DebugMode::ePathLengthContribution && gPushConstants.gDebugViewPathLength == 1) {
			if (gPushConstants.gDebugLightPathLength == path_length)
				accumulate_contribution(output_index, contrib);
		} else
			accumulate_contribution(output_index, contrib * weight);
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
		if (!gUseNEEMIS) return 0.5;
		const Real a2 = pow2(a);
		return a2 / (a2 + pow2(b));
	}
	static Real bsdf_mis(const Real bsdf_pdf, const Real T_nee_pdf, const IntersectionVertex _isect, const Real G) {
		Real l_pdf = T_nee_pdf;
		bool area_measure;
		point_on_light_pdf(l_pdf, area_measure, _isect);

		if (!area_measure) l_pdf = pdfWtoA(l_pdf, G);

		return mis(pdfWtoA(bsdf_pdf, G), l_pdf);
	}
	static Real reservoir_bsdf_mis(const Real a) {
		if (!gUseNEEMIS) return 0.5;
		return 1 - 1/pow2(1 + abs(a));
	}

	// load shading data and Le
	SLANG_MUTATING
	void sample(inout rng_state_t _rng, const IntersectionVertex _isect) {
		LightSampleRecord ls;
		sample_point_on_light(ls, float4(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng)), _isect.sd.position);
		if (ls.pdf <= 0 || all(ls.radiance <= 0)) { Le = 0; return; }

		dist = ls.dist;
		ray_direction = ls.to_light;

		if (ls.is_environment())
			G = 1;
		else {
			const Real cos_theta = -dot(ls.to_light, ls.normal);
			if (cos_theta < 1e-4) { Le = 0; return; }
			G = abs(cos_theta) / pow2(ls.dist);
		}
		Le = ls.radiance;
		pdfA = ls.pdf_area_measure ? ls.pdf : pdfWtoA(ls.pdf, G);

		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = ls.to_light;
			ray_origin = _isect.sd.position;
		} else {
			local_to_light = normalize(_isect.sd.to_local(ls.to_light));
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			ray_origin = ray_offset(_isect.sd.position, dot(geometry_normal, ls.to_light) > 0 ? geometry_normal : -geometry_normal);
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
	Real bsdf_pdf;

	rng_state_t _rng;

	// the ray that was traced to get here
	// also the ray traced by trace()
	Vector3 origin, direction;
	IntersectionVertex _isect;
	Vector3 local_position;
	uint _medium;
	Vector3 local_dir_in; // _isect.sd.to_local(-direction)
	Real G; // abs(dot(direction, _isect.sd.geometry_normal())) / dist2(_isect.sd.position, origin)
	Real T_nee_pdf;

	SLANG_MUTATING
	void init_rng() {
		_rng = rng_init(pixel_coord, gLightTraceRNGOffset + path_length*gRNGsPerVertex);
	}

	Real path_weight() {
		if (path_length > 2) {
			if (gConnectToLightPaths || gConnectToViews)
				return LightPathConnection::path_weight(path_length);
			if (gUseNEE) {
				if (gReservoirNEE)
					return NEE::reservoir_bsdf_mis(pdfWtoA(bsdf_pdf, G));
				else
					return NEE::bsdf_mis(bsdf_pdf, T_nee_pdf, _isect, G);
			}
		}
		return 1;
	}

	// to be called after trace()
	void store_light_vertex(const uint material_address) {
		uint flags = 0;
		if (gHasMedia && _isect.sd.shape_area == 0) flags |= PATH_VERTEX_FLAG_IS_MEDIUM;
		if (_isect.sd.flags & SHADING_FLAG_FLIP_BITANGENT) flags |= PATH_VERTEX_FLAG_FLIP_BITANGENT;
		BF_SET(flags, material_address, 4, 28);

		const uint i = path_index*gMaxLightPathVertices + path_length-1;
		gLightPathVertices0[i].position = _isect.sd.position;
		gLightPathVertices0[i].packed_geometry_normal = _isect.sd.packed_geometry_normal;
		gLightPathVertices1[i].material_address_flags = flags;
		gLightPathVertices1[i].packed_local_dir_in = pack_normal_octahedron(local_dir_in);
		gLightPathVertices1[i].packed_shading_normal = _isect.sd.packed_shading_normal;
		gLightPathVertices1[i].packed_tangent = _isect.sd.packed_tangent;
		gLightPathVertices2[i].uv = _isect.sd.uv;
		gLightPathVertices2[i].pack_beta(_beta);
		//gLightPathVertices3[i].pdf_fwd = _pdf_fwd;
		//gLightPathVertices3[i].pdf_rev = _pdf_rev;
	}

	void eval_emission(BSDF m) {
		if (!gTraceLight) {
			if (_isect.instance_index() == INVALID_INSTANCE) {
				// background hit
				if (gHasEnvironment) {
					Environment env;
					env.load(gEnvironmentMaterialAddress);
					const Real weight = path_weight();
					const Spectrum contrib = _beta * env.eval(direction);
					gRadiance[pixel_coord].rgb += contrib * weight;
					if ((DebugMode)gDebugMode == DebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 0 && path_length == gPushConstants.gDebugViewPathLength)
						gDebugImage[pixel_coord.xy].rgb += contrib;
				}
			} else if (dot(_isect.sd.geometry_normal(), -direction) > 0) {
				const Spectrum Le = m.Le();
				if (any(Le > 0)) {
					const Real weight = path_weight();
					const Spectrum contrib = _beta * Le;
					gRadiance[pixel_coord].rgb += contrib * weight;
					if ((DebugMode)gDebugMode == DebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 0 && path_length == gPushConstants.gDebugViewPathLength)
						gDebugImage[pixel_coord.xy].rgb += contrib;
				}
			}
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
				v.eval(m, local_dir_in, path_length);
			}
		} else {
			// add nee contribution
			if (gUseNEE) {
				NEE c;
				Spectrum contrib;

				// pick a point on a light
				if (gReservoirNEE) {
					Reservoir r;
					rng_state_t rs;
					Real rs_target_pdf = 0;
					Real bsdf_pdf;
					r.init();

					// paths are divided into gReservoirPresampleTileSize tiles
					const uint tile_offset = ((path_index/gReservoirPresampleTileSize) % gReservoirPresampleTileCount)*gReservoirPresampleTileSize;
					for (uint i = 0; i < gNEEReservoirSamples; i++) {
						NEE _c;
						rng_state_t _s;

						if (gPresampleReservoirNEE) {
							// uniformly sample from the tile
							const uint ti = rng_next_uint(_rng) % gReservoirPresampleTileSize;
							_s = rng_init(-1, tile_offset + ti);
							const PresampledLightPoint ls = gPresampledLights[tile_offset + ti];
							_c.Le = ls.Le;
							_c.pdfA = ls.pdfA;
							_c.ray_direction = ls.position - _isect.sd.position;
							const Real dist2 = dot(_c.ray_direction, _c.ray_direction);
							_c.G = 1/dist2;
							_c.dist = sqrt(dist2);
							_c.ray_direction /= _c.dist;
							if (gHasMedia && _isect.sd.shape_area <= 0) {
								_c.local_to_light = _c.ray_direction;
								_c.ray_origin = _isect.sd.position;
							} else {
								_c.local_to_light = normalize(_isect.sd.to_local(_c.ray_direction));
								const Vector3 geometry_normal = _isect.sd.geometry_normal();
								const Real cos_theta = dot(_c.ray_direction, geometry_normal);
								_c.G *= abs(cos_theta);
								_c.ray_origin = ray_offset(_isect.sd.position, cos_theta > 0 ? geometry_normal : -geometry_normal);
							}
							_c.T_dir_pdf = 1;
							_c.T_nee_pdf = 1;
						} else {
							_s = _rng;
							_c.sample(_rng, _isect);
						}

						if (_c.pdfA <= 0) continue;

						// evaluate the material
						Real _bsdf_pdf;
						const Spectrum _contrib = _c.eval(m, local_dir_in, _bsdf_pdf);
						const Real target_pdf = luminance(_contrib);

						if (r.update(rng_next_float(_rng), target_pdf/_c.pdfA)) {
							bsdf_pdf = pdfWtoA(_bsdf_pdf, _c.G);
							c = _c;
							rs = _s;
							rs_target_pdf = target_pdf;
							contrib = _contrib;
						}
					}
					if (rs_target_pdf > 0)
						r.W = r.total_weight / (r.M * rs_target_pdf);

					if (r.W > 0) c.eval_visibility(_rng, _medium, contrib);
					if (all(contrib <= 0)) { r.total_weight = 0; r.W = 0; }

					if (path_length == 2) {
						// reservoir reuse
						if (gReservoirTemporalReuse) {
							const Reservoir prev = gPrevReservoirs[path_index];
							if (prev.M > 0) {
								const rng_state_t prev_rs = gPrevReservoirSamples[path_index];
								rng_state_t tmp_rng = prev_rs;
								NEE prev_c;
								prev_c.sample(tmp_rng, _isect);
								// merge with previous reservoir
								Real _bsdf_pdf;
								const Spectrum prev_contrib = prev_c.eval(m, local_dir_in, _bsdf_pdf);
								const Real prev_target_pdf = luminance(prev_contrib);
								const uint merged_M = r.M + prev.M;
								if (r.update(rng_next_float(_rng), prev_target_pdf * prev.W * prev.M)) {
									bsdf_pdf = pdfWtoA(_bsdf_pdf, prev_c.G);
									c = prev_c;
									rs = prev_rs;
									rs_target_pdf = prev_target_pdf;
									contrib = prev_contrib;
								}
								r.W = r.total_weight / (merged_M * rs_target_pdf);
								r.M = min(gReservoirMaxM, merged_M);
							}
						}

						// store reservoir for reuse
						gReservoirs[path_index] = r;
						gReservoirSamples[path_index] = rs;
					}

					if (r.W > 1e-6) {
						// average nee and bsdf sampling
						Real weight = gSampleBSDFs ? (1 - NEE::reservoir_bsdf_mis(bsdf_pdf)) : 1;

						// bdpt weight overrides nee/bsdf mis weight
						if (gConnectToLightPaths || gConnectToViews) weight = LightPathConnection::path_weight(path_length + 1);

						contrib *= r.W;

						gRadiance[pixel_coord].rgb += contrib * _beta * weight;

						if ((DebugMode)gDebugMode == DebugMode::eReservoirWeight)
							gDebugImage[pixel_coord].rgb += r.W;
					}
				} else {
					c.sample(_rng, _isect);
					c.eval_visibility(_rng, _medium);
					if (any(c.Le > 0) && c.pdfA > 1e-6) {
						Real bsdf_pdf;
						contrib = c.eval(m, local_dir_in, bsdf_pdf);

						// mis between nee and bsdf sampling
						Real weight = gSampleBSDFs ? NEE::mis(c.pdfA, pdfWtoA(c.T_dir_pdf*bsdf_pdf, c.G)) : 1;

						// bdpt weight overrides nee/bsdf mis weight
						if (gConnectToLightPaths || gConnectToViews) weight = LightPathConnection::path_weight(path_length + 1);

						contrib /= c.pdfA;

						gRadiance[pixel_coord].rgb += contrib * _beta * weight;
					}
				}

				if ((DebugMode)gDebugMode == DebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 1 && path_length == gPushConstants.gDebugViewPathLength)
					gDebugImage[pixel_coord].rgb += contrib * _beta;
			}

			// add bdpt contribution
			if (gConnectToLightPaths) {
				for (uint light_length = 1; light_length <= gMaxLightPathVertices && light_length + path_length <= gMaxPathVertices; light_length++) {
					LightPathConnection c;
					if (!c.connect(_rng, path_index, light_length, _isect, _medium)) break; // break on invalid vertex (at end of path)
					if (all(c.f <= 0)) continue;

					MaterialEvalRecord _eval;
					m.eval(_eval, local_dir_in, c.local_to_light, false);

					// c.f includes 1/dist2, and cosine terms are handled in material evals
					const Spectrum contrib = _beta * _eval.f * c.f;
					const Real weight = LightPathConnection::path_weight(path_length + light_length);

					if (weight > 0 && any(contrib > 0)) {
						gRadiance[pixel_coord].rgb += contrib * weight;
						if ((DebugMode)gDebugMode == DebugMode::ePathLengthContribution && light_length == gPushConstants.gDebugLightPathLength && path_length == gPushConstants.gDebugViewPathLength)
							gDebugImage[pixel_coord].rgb += contrib;
					}
				}
			}
		}
	}

	// sample next direction, compute _beta *= f/pdf, update origin and direction
	SLANG_MUTATING
	void sample_next_direction(BSDF m) {
		const Vector3 rnd = Vector3(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));
		MaterialSampleRecord _material_sample;
		m.sample(_material_sample, rnd, local_dir_in, _beta, gTraceLight);

		bsdf_pdf = _material_sample.pdf_fwd;

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
				const Real num = ndotout * dot(geometry_normal, -direction);
				const Real denom = ngdotout * local_dir_in.z;
				if (abs(denom) > 1e-5) _beta *= abs(num / denom);
			}
		} else
			origin = _isect.sd.position;

		direction = _material_sample.dir_out;

		if ((DebugMode)gDebugMode == DebugMode::eDirOut) gDebugImage[pixel_coord] = float4(direction*.5+.5, 1);
	}

	// add radiance contribution from surface, sample NEE/BDPT connections, do russian roullette sample next direction
	SLANG_MUTATING
	void advance(BSDF m) {
		if (path_length > 2)
			eval_emission(m);

		// no bounce on final vertex
		if (path_length >= (gTraceLight ? gMaxLightPathVertices : gMaxPathVertices)) { _beta = 0; return; }

		sample_connections(m);

		if (!gSampleBSDFs) { _beta = 0; return; }

		// russian roullette
		if (path_length >= gMinPathVertices) {
			const Real p = min(luminance(_beta), 0.95);
			if (rng_next_float(_rng) > p) {
				_beta = 0;
				return;
			} else
				_beta /= p;
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
		bsdf_pdf *= T_dir_pdf;

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

};

#endif