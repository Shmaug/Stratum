#ifndef UTIL_H
#define UTIL_H

float4 ComputeScreenPos(float4 pos) {
	return float4((pos.xy * float2(1, sign(STRATUM_MATRIX_P[1][1])) + pos.w) * .5, pos.zw);
}

float LinearDepth01(float screenPos_z) {
	return screenPos_z / STRATUM_MATRIX_P[2][2] / (STRATUM_CAMERA_FAR - STRATUM_CAMERA_NEAR);
}

#endif