#ifndef PATH_H
#define PATH_H

#include "rng.hlsli"
#include "intersection.hlsli"
#include "../../materials/environment.h"
#include "light.hlsli"

Real correct_shading_normal(const Real ndotout, const Real ndotin, const Vector3 dir_out, const Vector3 dir_in, const Vector3 geometry_normal) {
	Real num = ndotout * dot(dir_in, geometry_normal);
	Real denom = dot(dir_out, geometry_normal) * ndotin;
	if (denom == 0) return 0;
	return abs(num / denom);
}

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
				m.eval(_eval, lv1.local_dir_in(), lv1.to_local(-to_light), true);
			}
			f *= _eval.f;
		}

		if (all(f <= 0)) return true; // vertex not visible, but still valid

		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = to_light;
		} else {
			local_to_light = _isect.sd.to_local(to_light);
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
			local_to_view = _isect.sd.to_local(to_view);
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
	Real T_dir_pdf;
	Real T_nee_pdf;

	Vector3 ray_origin;
	Vector3 ray_direction;
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

	// load shading data, emission, trace visibility ray
	SLANG_MUTATING
	void load_sample(inout rng_state_t _rng, const uint cur_medium, const IntersectionVertex _isect, const PointSample rs, const bool check_visibility = true) {
		ShadingData sd;
		make_shading_data(sd, rs.instance_index(), rs.primitive_index(), rs.local_position);

		const uint material_address = gInstances[rs.instance_index()].material_address();
		Material m;
		m.load_and_sample(material_address, sd.uv, 0);
		Le = m.emission;

		if (rs.instance_index() == INVALID_INSTANCE) {
			// environment map
			ray_direction = sd.position;
			dist = POS_INFINITY;
			G = 1;
		} else {
			// point on instance
			ray_direction = sd.position - _isect.sd.position;
			dist = length(ray_direction);
			const Real rcp_dist = 1/dist;
			ray_direction *= rcp_dist;
			G = pow2(rcp_dist);

			if (!gHasMedia || sd.shape_area > 0) {
				const Real cos_theta = -dot(ray_direction, sd.geometry_normal());
				if (cos_theta < 1e-4) { Le = 0; return; }
				G *= abs(cos_theta);
			}

			Le *= G;
		}

		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = ray_direction;
			ray_origin = _isect.sd.position;
		} else {
			local_to_light = _isect.sd.to_local(ray_direction);
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			ray_origin = ray_offset(_isect.sd.position, local_to_light.z > 0 ? geometry_normal : -geometry_normal);
		}
	}

	// load shading data, emission, trace visibility ray
	SLANG_MUTATING
	void sample(inout rng_state_t _rng, const IntersectionVertex _isect, out PointSample ps) {
		LightSampleRecord ls;
		sample_point_on_light(ls, float4(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng)), _isect.sd.position);
		ps.local_position = ls.local_position;
		ps.instance_primitive_index = ls.instance_primitive_index;
		if (ls.pdf <= 0 || all(ls.radiance <= 0)) { Le = 0; return; }

		if (ls.is_environment())
			G = 1;
		else {
			const Real cos_theta = -dot(ls.to_light, ls.normal);
			if (cos_theta < 1e-4) { Le = 0; return; }
			G = abs(cos_theta) / pow2(ls.dist);
		}
		Le = G * ls.radiance;
		pdfA = ls.pdf_area_measure ? ls.pdf : pdfWtoA(ls.pdf, G);

		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = ls.to_light;
			ray_origin = _isect.sd.position;
		} else {
			local_to_light = _isect.sd.to_local(ls.to_light);
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			ray_origin = ray_offset(_isect.sd.position, local_to_light.z > 0 ? geometry_normal : -geometry_normal);
		}
		ray_direction = ls.to_light;
		dist = ls.dist;
		T_dir_pdf = 1;
		T_nee_pdf = 1;
	}

	SLANG_MUTATING
	void check_visibility(inout rng_state_t _rng, const uint cur_medium) {
		if (any(Le > 0)) {
			trace_visibility_ray(_rng, ray_origin, ray_direction, dist, cur_medium, Le, T_dir_pdf, T_nee_pdf);
			if (T_nee_pdf > 0) Le /= T_nee_pdf;
		}
	}

	// returns full contribution (C*G*f/pdf) and pdf used for MIS
	Spectrum eval(const BSDF m, const Vector3 local_dir_in, out Real bsdf_pdf) {
		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, local_to_light, false);
		bsdf_pdf = _eval.pdf_fwd;
		return Le * _eval.f;
	}
};

struct PathIntegrator {
	uint2 pixel_coord;
	uint path_index;
	uint path_length;
	Spectrum _beta;
	Real _pdf_fwd;
	Real _pdf_rev;

	rng_state_t _rng;

	Vector3 origin;
	Vector3 direction;

	IntersectionVertex _isect;
	uint _medium;
	Vector3 local_dir_in;
	Real G;
	Real T_nee_pdf;
	MaterialSampleRecord _material_sample;

	SLANG_MUTATING
	void init_rng() {
		_rng = rng_init(pixel_coord, gLightTraceRNGOffset + path_length*gRNGsPerVertex);
	}

	Real path_weight() {
		if (path_length > 2) {
			if (gConnectToLightPaths || gConnectToViews)
				return LightPathConnection::path_weight(path_length);
			if (gUseNEE) {
				if (gReservoirNEE && path_length == 3)
					return 0.5; // average reservoir nee with bsdf sampling
				else
					return NEE::bsdf_mis(_pdf_fwd, T_nee_pdf, _isect, G);
			}
		}
		return 1;
	}

	// expects _medium, origin and direction to bet set
	// sets local_dir_in, G and T_nee_pdf. increments path_length
	// adds radiance contribution from environment
	SLANG_MUTATING
	void trace() {
		_rng = rng_init(pixel_coord, gLightTraceRNGOffset + path_length*gRNGsPerVertex);

		Real T_dir_pdf = 1;
		T_nee_pdf = 1;
		trace_ray(_rng, origin, direction, _medium, _beta, T_dir_pdf, T_nee_pdf, _isect);
		if (T_dir_pdf <= 0 || all(_beta <= 0)) { _beta = 0; return; }
		_beta /= T_dir_pdf;
		_pdf_fwd *= T_dir_pdf;

		path_length++;

		// handle miss
		if (_isect.instance_index() == INVALID_INSTANCE) {
			if (!gTraceLight && gHasEnvironment) {
				Environment env;
				env.load(gEnvironmentMaterialAddress);
				const Real weight = path_weight();
				const Spectrum contrib = _beta * env.eval(direction);
				gRadiance[pixel_coord].rgb += contrib * weight;
				if ((DebugMode)gDebugMode == DebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 0 && path_length == gPushConstants.gDebugViewPathLength)
					gDebugImage[pixel_coord.xy].rgb += contrib;
			}
			_beta = 0;
			return;
		}

		const Vector3 dp = _isect.sd.position - origin;
		const Real dist2 = dot(dp, dp);
		G = 1/dist2;

		// compute ray differential
		if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
			RayDifferential ray_differential = gRayDifferentials[path_index];
			ray_differential.transfer(sqrt(dist2));
			_isect.sd.uv_screen_size *= ray_differential.radius;
			gRayDifferentials[path_index] = ray_differential;
		}

		if (!gHasMedia || _isect.sd.shape_area > 0) {
			local_dir_in = _isect.sd.to_local(-direction);
			G *= abs(dot(direction, _isect.sd.geometry_normal()));
		} else
			local_dir_in = -direction;
	}

	// to be called after trace(). sets _material_sample
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
		gLightPathVertices3[i].pdf_fwd = _pdf_fwd;
		gLightPathVertices3[i].pdf_rev = _pdf_rev;
	}

	// NEE, BDPT, and light tracing connection strategies
	SLANG_MUTATING
	void sample_connections(BSDF m) {
		if (path_length+1 > gMaxPathVertices) return;

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
				// pick a point on a light
				const bool use_reservoir = gReservoirNEE && path_length == 2;
				if (!use_reservoir) {
					PointSample s;
					c.sample(_rng, _isect, s);
					c.Le /= c.pdfA;
					c.check_visibility(_rng, _medium);
				} else {
					Reservoir r;
					PointSample rs;
					Real rs_target_pdf;
					r.init();

					// paths are divided into gReservoirPresampleTileSize tiles
					const uint tile_offset = ((path_index/gReservoirPresampleTileSize) % gReservoirPresampleTileCount)*gReservoirPresampleTileSize;
					for (uint i = 0; i < gNEEReservoirSamples; i++) {
						NEE _c;
						PointSample _s;
						if (gPresampleReservoirNEE) {
							// uniformly sample the tile for this path
							const uint ti = rng_next_uint(_rng) % gReservoirPresampleTileSize;
							const PresampledLightPoint ls = gPresampledLights[tile_offset + ti];
							_s = ls.rs;
							_c.local_to_light = ls.local_to_light;
							_c.Le = ls.contribution();
							_c.pdfA = ls.pdfA();
						} else
							_c.sample(_rng, _isect, _s);

						if (_c.pdfA <= 0) continue;
						if (_s.instance_primitive_index == _isect.instance_primitive_index) continue;

						// evaluate the material
						Real bsdf_pdf;
						const Real target_pdf = luminance(_c.eval(m, local_dir_in, bsdf_pdf));
						if (target_pdf <= 0) continue;

						if (r.update(rng_next_float(_rng), target_pdf/_c.pdfA)) {
							c = _c;
							rs = _s;
							rs_target_pdf = target_pdf;
						}
					}
					if (rs_target_pdf > 0)
						r.W = r.total_weight / (r.M * rs_target_pdf);
					else
						r.W = 0;

					if (r.W > 0) c.check_visibility(_rng, _medium);
					if (all(c.Le <= 0)) r.W = 0;

					// store reservoir for reuse
					gReservoirs[path_index] = r;
					gReservoirSamples[path_index] = rs;

					// reservoir reuse
					if (gReservoirTemporalReuse) {
						const Reservoir prev = gPrevReservoirs[path_index];
						if (prev.W > 0) {
							const PointSample prev_rs = gPrevReservoirSamples[path_index];
							NEE prev_c;
							prev_c.load_sample(_rng, _medium, _isect, prev_rs);

							if (any(prev_c.Le > 0)) {
								// merge with previous reservoir
								Reservoir temporal;
								temporal.init();
								temporal.update(rng_next_float(_rng), r.total_weight);
								Real bsdf_pdf;
								const Real prev_target_pdf = luminance(prev_c.eval(m, local_dir_in, bsdf_pdf));
								if (temporal.update(rng_next_float(_rng), prev_target_pdf * prev.W * prev.M)) {
									c = prev_c;
									rs = prev_rs;
									rs_target_pdf = prev_target_pdf;
								}
								temporal.M = min(gReservoirMaxM, r.M + prev.M);
								temporal.W = temporal.total_weight / (temporal.M * rs_target_pdf);
								r = temporal;
							}
						}
					}

					// apply reservoir weight
					c.Le *= r.W;
					if ((DebugMode)gDebugMode == DebugMode::eReservoirWeight)
						gDebugImage[pixel_coord].rgb += r.W;
				}

				// evaluate material
				if (any(c.Le > 0)) {
					Real bsdf_pdf;
					const Spectrum contrib = c.eval(m, local_dir_in, bsdf_pdf) * _beta;

					// mis between nee and bsdf sampling
					Real weight;
					if (!gSampleBSDFs)
						weight = 1; // only direct light
					else {
						if (use_reservoir)
							weight = 0.5; // average reservoir nee with bsdf sampling
						else
							weight = NEE::mis(c.pdfA, pdfWtoA(c.T_dir_pdf*bsdf_pdf, c.G));
					}

					// bdpt weight overrides nee/bsdf mis weight
					if (gConnectToLightPaths || gConnectToViews) weight = LightPathConnection::path_weight(path_length + 1);

					gRadiance[pixel_coord].rgb += contrib * weight;
					if ((DebugMode)gDebugMode == DebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 1 && path_length == gPushConstants.gDebugViewPathLength)
						gDebugImage[pixel_coord].rgb += contrib;
				}
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

	// sample next direction, compute _beta *= f/pdf
	SLANG_MUTATING
	void sample_next_direction(BSDF m) {
		const Vector3 rnd = Vector3(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));
		m.sample(_material_sample, rnd, local_dir_in, _beta, gTraceLight);

		if (!gHasMedia || _isect.sd.shape_area > 0) {
			const Real ndotout = _material_sample.dir_out.z;

			const Vector3 geometry_normal = _isect.sd.geometry_normal();

			_isect.sd.position = ray_offset(_isect.sd.position, ndotout > 0 ? geometry_normal : -geometry_normal);

			_material_sample.dir_out = _isect.sd.to_world(_material_sample.dir_out);

			if (gTraceLight) _beta *= correct_shading_normal(ndotout, local_dir_in.z, _material_sample.dir_out, -direction, geometry_normal);
		}
	}

	// add radiance contribution from surface, sample NEE/BDPT connections, do russian roullette sample next direction
	SLANG_MUTATING
	void advance(BSDF m) {
		// add emission from surface
		const Spectrum Le = m.Le();
		if (!gTraceLight && local_dir_in.z > 0 && any(Le > 0)) {
			const Real weight = path_weight();
			const Spectrum contrib = _beta * Le;
			gRadiance[pixel_coord].rgb += contrib * weight;
			if ((DebugMode)gDebugMode == DebugMode::ePathLengthContribution && gPushConstants.gDebugLightPathLength == 0 && path_length == gPushConstants.gDebugViewPathLength)
				gDebugImage[pixel_coord.xy].rgb += contrib;
		}

		sample_connections(m);

		// no bounce on final vertex
		if (path_length >= (gTraceLight ? gMaxLightPathVertices : gMaxPathVertices)) { _beta = 0; return; }

		if (!gSampleBSDFs) { _beta = 0; return; }

		// russian roullette
		if (path_length >= gMinPathVertices) {
			const Real p = min(luminance(_beta), 0.95);
			if (rng_next_float(_rng) > p)
				_beta = 0;
			else
				_beta /= p;
		}

		sample_next_direction(m);
	}
};

#endif