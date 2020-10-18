#ifndef UTIL_H
#define UTIL_H

float4 ComputeScreenPos(float4 pos) {
	return float4((pos.xy * float2(1, sign(STRATUM_MATRIX_P[1][1])) + pos.w) * .5, pos.zw);
}

float LinearDepth01(float screenPos_z) {
	return screenPos_z / STRATUM_MATRIX_P[2][2] / (STRATUM_CAMERA_FAR - STRATUM_CAMERA_NEAR);
}

float ApplyCameraTranslation(float4x4 m) {
	m[0][3] += -STRATUM_CAMERA_POSITION.x * m[3][3];
	m[1][3] += -STRATUM_CAMERA_POSITION.y * m[3][3];
	m[2][3] += -STRATUM_CAMERA_POSITION.z * m[3][3];
	return m;
}

#endif