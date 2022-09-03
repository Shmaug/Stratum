#include "rng.hlsli"
#include "intersection.hlsli"
#include "../environment.h"

bool IsZero(const float3 aVec) { return all(aVec <= 0) || any(aVec != aVec); }
float Mis(const float aPdf) { return aPdf; }
float PdfWtoA(const float aPdfW, const float aCosTheta, const float aDist) { return aPdfW * abs(aCosTheta) / pow2(aDist); }

bool Occluded(const float3 aOrigin, const float3 aDirection, const float aDist) {
	float3 localHitPos;
	IntersectionVertex isect;
	const float t = trace_ray(aOrigin, aDirection, aDist, isect, localHitPos, true);
	return t < aDist;
}

//////////////////////////////////////////////////////////////////////////
// Sampling functions (from SmallVCM)

float2 SampleConcentricDisc(const float2 aSamples) {
    float phi, r;

    float a = 2*aSamples.x - 1;   /* (a,b) is now on [-1,1]^2 */
    float b = 2*aSamples.y - 1;

    if(a > -b) {     /* region 1 or 2 */
        if(a > b) {  /* region 1, also |a| > |b| */
            r = a;
            phi = (M_PI/4.f) * (b/a);
        } else {     /* region 2, also |b| > |a| */
            r = b;
            phi = (M_PI/4.f) * (2.f - (a/b));
        }
    } else {         /* region 3 or 4 */
        if(a < b) {  /* region 3, also |a| >= |b|, a != 0 */
            r = -a;
            phi = (M_PI/4.f) * (4.f + (b/a));
        } else {     /* region 4, |b| >= |a|, but a==0 and b==0 could occur. */
            r = -b;
            if (b != 0)
                phi = (M_PI/4.f) * (6.f - (a/b));
            else
                phi = 0;
        }
    }

    return r * float2(cos(phi), sin(phi));
}
float ConcentricDiscPdfA() {
    return 1 / M_PI;
}
float3 SampleUniformSphereW(const float2 aSamples, out float oPdfSA) {
    const float term1 = 2 * M_PI * aSamples.x;
    const float term2 = 2 * sqrt(aSamples.y - aSamples.y * aSamples.y);

    const float3 ret = float3(
        cos(term1) * term2,
        sin(term1) * term2,
        1 - 2 * aSamples.y);

	oPdfSA = 0.25f / M_PI; // 1 / (4 * M_PI);
    return ret;
}
float UniformSpherePdfW() {
    return 0.25f / M_PI; // 1 / (4 * M_PI);
}
float3 SampleCosHemisphereW(const float2 aSamples, out float oPdfW) {
    const float term1 = 2.f * M_PI * aSamples.x;
    const float term2 = sqrt(1.f - aSamples.y);

    const float3 ret = float3(
        cos(term1) * term2,
        sin(term1) * term2,
        sqrt(aSamples.y));

	oPdfW = ret.z / M_PI;

    return ret;
}
float CosHemispherePdfW(const float aCosTheta) {
    return max(0, aCosTheta) / M_PI;
}
// returns barycentric coordinates
float2 SampleUniformTriangle(const float2 aSamples) {
    const float term = sqrt(aSamples.x);
    return float2(1.f - term, aSamples.y * term);
}

//////////////////////////////////////////////////////////////////////////
// Atomic framebuffer functions

void AddLightTraceContribution(const int2 aPixelCoord, const float3 aContrib) {
	const int idx = aPixelCoord.y*gOutputExtent.x + aPixelCoord.x;
	const uint3 ci = max(0,aContrib) * gLightTraceQuantization;
	if (all(ci == 0)) return;
	const uint addr = 16*idx;
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
float3 LoadLightTraceContribution(const int2 aPixelCoord) {
	const int idx = int(aPixelCoord.y)*gOutputExtent.x + int(aPixelCoord.x);
	uint4 v = gLightTraceSamples.Load<uint4>(16*idx);
	// handle overflow
	if (v.w & BIT(0)) v.r = 0xFFFFFFFF;
	if (v.w & BIT(1)) v.g = 0xFFFFFFFF;
	if (v.w & BIT(2)) v.b = 0xFFFFFFFF;
	return v.rgb / (float)gLightTraceQuantization;
}

//////////////////////////////////////////////////////////////////////////
// Structs

struct Rng {
	rng_state_t mState;

	__init(uint2 index, uint offset) {
		mState = rng_init(index, offset);
	}

	SLANG_MUTATING
	float GetFloat() { return rng_next_float(mState); }
	SLANG_MUTATING
	float2 GetVec2f() { return float2(GetFloat(), GetFloat()); }
	SLANG_MUTATING
	float3 GetVec3f() { return float3(GetFloat(), GetFloat(), GetFloat()); }
};

struct AbstractCamera {
	uint mViewIndex;
	float3 mPosition;
	float3 mForward;
	float mImagePlaneDist;

	__init(uint aViewIndex) {
		mViewIndex = aViewIndex;
		const TransformData t = gViewTransforms[mViewIndex];
		mForward = t.transform_vector(float3(0,0,sign(gViews[mViewIndex].projection.near_plane)));
		mPosition = float3(t.m[0][3], t.m[1][3], t.m[2][3]);
		mImagePlaneDist = abs(gViews[mViewIndex].image_max.y - gViews[mViewIndex].image_min.y) * gViews[mViewIndex].projection.scale[1] / 2;
	}

	float3 GenerateRay(const float2 aImagePos, out float oCosTheta) {
		// initialize ray
		const float2 uv = (aImagePos - gViews[mViewIndex].image_min)/gViews[mViewIndex].extent();
		float2 clip_pos = 2*uv - 1;
		clip_pos.y = -clip_pos.y;
		const Vector3 localDirection = normalize(gViews[mViewIndex].projection.back_project(clip_pos));
		oCosTheta = abs(localDirection.z);
		return normalize(gViewTransforms[mViewIndex].transform_vector(localDirection));
	}

	bool WorldToRaster(const float3 aWorldPos, out float2 oRasterCoord) {
		const ViewData view = gViews[mViewIndex];
		float4 clipPos = view.projection.project_point(gInverseViewTransforms[mViewIndex].transform_point(aWorldPos));
		clipPos.y = -clipPos.y;
		clipPos.xyz /= clipPos.w;
		if (any(abs(clipPos.xyz) >= 1) || clipPos.z <= 0) return false;
        const float2 uv = clipPos.xy*.5 + .5;
       	oRasterCoord = view.image_min + (view.image_max - view.image_min) * uv;
		return true;
	}
};

struct AbstractBSDF {
	Material bsdf;
	bool mAdjoint;
	bool mFlipBitangent;
	uint mPackedShadingNormal;
	uint mPackedGeometryNormal;
	uint mPackedTangent;
	float mCosGeometryThetaIn;
	float3 mLocalDirIn;

	__init(const float3 aWorldDirIn, const IntersectionVertex aIsect, const bool aAdjoint) {
		mAdjoint = aAdjoint;
		bsdf.load(gInstances[aIsect.instance_index()].material_address(), aIsect.sd.uv, 0);
		mFlipBitangent = aIsect.sd.flags & SHADING_FLAG_FLIP_BITANGENT;
		mPackedGeometryNormal = aIsect.sd.packed_geometry_normal;
		mPackedShadingNormal = aIsect.sd.packed_shading_normal;
		mPackedTangent = aIsect.sd.packed_tangent;
		mCosGeometryThetaIn = dot(aWorldDirIn, GetGeometryNormal());
		mLocalDirIn = aIsect.sd.to_local(-aWorldDirIn);
	}

	bool IsLight() { return !IsZero(bsdf.Le()); }
	bool IsValid() { return bsdf.can_eval() || any(bsdf.Le() > 0) && abs(mLocalDirIn.z) >= 1e-4; }
#ifdef VCM_LAMBERTIAN
	bool IsDelta() { return false; }
#else
	bool IsDelta() { return bsdf.is_specular(); }
#endif
	float ContinuationProb() { return 1; }
	float CosThetaFix() { return abs(mLocalDirIn.z); }
	float3 WorldDirFix() { return ToWorld(mLocalDirIn); }
	float3 Le() { return bsdf.Le(); }

	float3 GetGeometryNormal()  { return unpack_normal_octahedron(mPackedGeometryNormal); }
	float3 GetShadingNormal()  { return unpack_normal_octahedron(mPackedShadingNormal); }
	float3 GetTangent() { return unpack_normal_octahedron(mPackedTangent); }
	float3 ToWorld(const float3 v) {
		const float3 n = GetShadingNormal();
		const float3 t = GetTangent();
		return v.x*t + v.y*cross(n, t)*(mFlipBitangent ? -1 : 1) + v.z*n;
	}
	float3 ToLocal(const float3 v) {
		const float3 n = GetShadingNormal();
		const float3 t = GetTangent();
		return float3(dot(v, t), dot(v, cross(n, t)*(mFlipBitangent ? -1 : 1)), dot(v, n));
	}

	float CorrectShadingNormal(const float3 aWorldDirOut, const float3 aLocalDirOut) {
		if (mPackedShadingNormal == mPackedGeometryNormal) return 1;
		const float num   = abs(dot(aWorldDirOut, GetGeometryNormal()) * mLocalDirIn.z);
		const float denom = abs(aLocalDirOut.z * mCosGeometryThetaIn);
		return denom <= 1e-5 ? 1 : num / denom;
	}

#ifdef VCM_LAMBERTIAN
	float3 Evaluate(const float3 aWorldDir, out float oCosThetaOut, out float oDirectPdfW, out float oReversePdfW) {
		const float3 localDir = ToLocal(aWorldDir);
		if (localDir.z * mLocalDirIn.z <= 0) {
			return 0;
		} else {
			oCosThetaOut = abs(localDir.z);
			oDirectPdfW = cosine_hemisphere_pdfW(abs(localDir.z));
			oReversePdfW = cosine_hemisphere_pdfW(abs(mLocalDirIn.z));
			float3 f = bsdf.bsdf.base_color() / M_PI;
			if (mAdjoint) f *= CorrectShadingNormal(aWorldDir, localDir);
			return f;
		}
	}
	float3 Sample(const float3 aRndTriplet, out float3 oWorldDir, out float oPdfW, out float oRevPdfW, out float oCosThetaOut, out bool specularEvent) {
		float3 localDir = sample_cos_hemisphere(aRndTriplet.x, aRndTriplet.y);
		if (mLocalDirIn.z < 0) localDir = -localDir;
		oWorldDir = ToWorld(localDir);
		oCosThetaOut = abs(localDir.z);
		oPdfW = cosine_hemisphere_pdfW(abs(localDir.z));
		oRevPdfW = cosine_hemisphere_pdfW(abs(mLocalDirIn.z));
		specularEvent = false;
		float3 f = bsdf.bsdf.base_color() / M_PI;
		if (mAdjoint) f *= CorrectShadingNormal(oWorldDir, localDir);
		return f;
	}
#else
	float3 Evaluate(const float3 oWorldDir, out float oCosThetaOut, out float oDirectPdfW, out float oReversePdfW) {
		MaterialEvalRecord r;
		const float3 localDirOut = ToLocal(oWorldDir);
		bsdf.eval(r, mLocalDirIn, localDirOut, mAdjoint);
		oDirectPdfW = r.pdf_fwd;
		oReversePdfW = r.pdf_rev;
		oCosThetaOut = abs(localDirOut.z);
		if (mAdjoint) r.f *= CorrectShadingNormal(oWorldDir, localDirOut);
        return r.f / oCosThetaOut; // HACK: Stratum multiplies by cosTheta inside BSDF, but SmallVCM doesn't
	}
	float3 Sample(const float3 aRndTriplet, out float3 oWorldDir, out float oPdfW, out float oRevPdfW, out float oCosThetaOut, out bool specularEvent) {
		MaterialSampleRecord r;
		float3 tmpBeta = 1;
		float3 f = bsdf.sample(r, aRndTriplet, mLocalDirIn, tmpBeta, mAdjoint);
		specularEvent = bsdf.is_specular(); // TODO: specularEvent based on actual sampled event
		oPdfW = r.pdf_fwd;
		oRevPdfW = r.pdf_rev;
		oCosThetaOut = abs(r.dir_out.z);
		oWorldDir = ToWorld(r.dir_out);
		if (mAdjoint) f *= CorrectShadingNormal(oWorldDir, r.dir_out);
		return f / oCosThetaOut; // HACK: Stratum multiplies by cosTheta inside BSDF, but SmallVCM doesn't
	}
#endif
};

struct BackgroundLight {
    static float3 Illuminate(
        const float2 aRndTuple,
        out float3   oDirectionToLight,
        out float    oDistance,
        out float    oDirectPdfW,
        out float    oEmissionPdfW,
        out float    oCosAtLight) {
		Environment env;
		env.load(gEnvironmentMaterialAddress);
		const float3 radiance = env.sample(aRndTuple, oDirectionToLight, oDirectPdfW);

        // This stays even with image sampling
        oDistance = 1e36f;
		oEmissionPdfW = oDirectPdfW * ConcentricDiscPdfA() / pow2(gSceneSphere.w);

        oCosAtLight = 1.f;

        return radiance;
    }

    static float3 Emit(
        const float2 aDirRndTuple,
        const float2 aPosRndTuple,
        out float3   oPosition,
        out float3   oDirection,
        out float    oEmissionPdfW,
        out float    oDirectPdfA,
        out float    oCosThetaLight) {
		Environment env;
		env.load(gEnvironmentMaterialAddress);
        float directPdf;
		const float3 radiance = env.sample(aDirRndTuple, oDirection, directPdf);

        // Stays even with image sampling
        const float2 xy = SampleConcentricDisc(aPosRndTuple);

        float3 t, b;
        make_orthonormal(oDirection, t, b);

        oPosition = gSceneSphere.xyz + gSceneSphere.w * (-oDirection + b * xy.x + t * xy.y);

        oEmissionPdfW = directPdf * ConcentricDiscPdfA() / pow2(gSceneSphere.w);

        // For background we lie about Pdf being in area measure
		oDirectPdfA = directPdf;

        // Not used for infinite or delta lights
		oCosThetaLight = 1.f;

        return radiance;
    }

    static float3 GetRadiance(
        const float3 aRayDirection,
        out float    oDirectPdfA,
        out float    oEmissionPdfW) {
		Environment env;
		env.load(gEnvironmentMaterialAddress);
        float directPdf = env.eval_pdf(aRayDirection);
        const float positionPdf = ConcentricDiscPdfA() / pow2(gSceneSphere.w);
        oDirectPdfA   = directPdf;
        oEmissionPdfW = directPdf * positionPdf;
		return env.eval(aRayDirection);
    }
};

struct InstanceLight {
	InstanceData mInstance;
	uint mInstanceIndex;

	__init(const uint aLightID) {
		if (aLightID >= gLightCount) {
			mInstanceIndex = INVALID_INSTANCE;
		} else {
			mInstanceIndex = gLightInstanceMap[aLightID];
			if (mInstanceIndex != INVALID_INSTANCE)
				mInstance = gInstances[mInstanceIndex];
		}
	}

	bool IsDelta() { return false; }
	bool IsFinite() { return mInstanceIndex != INVALID_INSTANCE; }

	float3 SamplePoint(const float3 aRndTuple, out float3 oNormal, out float2 oUV, out float oPdfA) {
		ShadingData sd;
		if (mInstance.type() == INSTANCE_TYPE_TRIANGLES) {
			const float2 bary = SampleUniformTriangle(aRndTuple.xy);
			const uint primitiveIndex = uint(mInstance.prim_count() * aRndTuple.z) % mInstance.prim_count();
			oPdfA = 1 / (float)mInstance.prim_count();
			make_triangle_shading_data(sd, mInstance, gInstanceTransforms[mInstanceIndex], primitiveIndex, bary);
		} else if (mInstance.type() == INSTANCE_TYPE_SPHERE) {
			float tmp;
			const float3 localPosition = mInstance.radius()*SampleUniformSphereW(aRndTuple.xy, tmp);
			make_sphere_shading_data(sd, mInstance, gInstanceTransforms[mInstanceIndex], localPosition);
			oPdfA = 1;
		}
		oPdfA *= 1/sd.shape_area;
		oNormal = sd.geometry_normal();
		oUV = sd.uv;
		return sd.position;
	}

    float3 Illuminate(
        const float3 aReceivingPosition,
        const float3 aRndTuple,
        out float3   oDirectionToLight,
        out float    oDistance,
        out float    oDirectPdfA,
        out float    oDirectPdfW,
        out float    oEmissionPdfW,
        out float    oCosAtLight) {

		if (mInstanceIndex == INVALID_INSTANCE)
			return BackgroundLight::Illuminate(aRndTuple.xy, oDirectionToLight, oDistance, oDirectPdfW, oEmissionPdfW, oCosAtLight);

		float2 uv;
		float3 lightNormal;
		float3 lightPoint = SamplePoint(aRndTuple, lightNormal, uv, oDirectPdfA);

        oDirectionToLight   = lightPoint - aReceivingPosition;
        const float distSqr = dot(oDirectionToLight, oDirectionToLight);
        oDistance           = sqrt(distSqr);
        oDirectionToLight   = oDirectionToLight / oDistance;

        oCosAtLight = dot(lightNormal, -oDirectionToLight);

        // too close to, or under, tangent
        if (oCosAtLight <= 1e-4) return 0;

        oDirectPdfW = oDirectPdfA * distSqr / oCosAtLight;
		oEmissionPdfW = oDirectPdfA * oCosAtLight / M_PI;

		Material bsdf;
		bsdf.load(mInstance.material_address(), uv, 0);
        return bsdf.Le();
    }

    float3 Emit(
        const float2 aDirRndTuple,
        const float3 aPosRndTuple,
        out float3   oPosition,
        out float3   oDirection,
        out float    oEmissionPdfW,
        out float    oDirectPdfA,
        out float    oCosThetaLight) {

		if (mInstanceIndex == INVALID_INSTANCE)
			return BackgroundLight::Emit(aDirRndTuple, aPosRndTuple.xy, oPosition, oDirection, oEmissionPdfW, oDirectPdfA, oCosThetaLight);

		float2 uv;
		float3 lightNormal;
		oPosition = SamplePoint(aPosRndTuple, lightNormal, uv, oDirectPdfA);

        float3 localDirOut = SampleCosHemisphereW(aDirRndTuple, oEmissionPdfW);

        oEmissionPdfW *= oDirectPdfA;

        // cannot really not emit the particle, so just bias it to the correct angle
		//localDirOut.z = max(localDirOut.z, 1e-4);
		if (localDirOut.z < 1e-4) return 0;

		oCosThetaLight = localDirOut.z;

		float3 t,b;
		make_orthonormal(lightNormal, t, b);
        oDirection = normalize(localDirOut.x * t + localDirOut.y * b + localDirOut.z * lightNormal);

		Material bsdf;
		bsdf.load(mInstance.material_address(), uv, 0);
        return bsdf.Le() * localDirOut.z;
    }

	static float3 GetRadiance(
		const float3 aDirection,
		const IntersectionVertex aIsect,
		const AbstractBSDF aBsdf,
		out float oDirectPdfA,
		out float oEmissionPdfW) {
		if (aIsect.instance_index() == INVALID_INSTANCE)
			return BackgroundLight::GetRadiance(aDirection, oDirectPdfA, oEmissionPdfW);

        if (aBsdf.CosThetaFix() <= 0) return 0;

        oDirectPdfA = aIsect.shape_pdf;

		oEmissionPdfW = CosHemispherePdfW(aBsdf.CosThetaFix());
		oEmissionPdfW *= aIsect.shape_pdf;

        return aBsdf.Le();
	}
};
