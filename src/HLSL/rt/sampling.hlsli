/*
 * MIT License
 *
 * Copyright(c) 2019-2021 Asif Ali
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this softwareand associated documentation files(the "Software"), to deal
 * the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions :
 *
 * The above copyright notice and this permission notice shall be included all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// https://github.com/knightcrawler25/GLSL-PathTracer/blob/master/src/shaders/common/sampling.glsl

float3 ImportanceSampleGTR1(float a, float r1, float r2) {
    float a2 = a * a;
    float phi = r1 * (2*M_PI);
    float cosTheta = sqrt((1 - pow(a2, 1 - r1)) / (1 - a2));
    float sinTheta = clamp(sqrt(1 - (cosTheta * cosTheta)), 0, 1);
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);

    return float3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}

float3 ImportanceSampleGTR2_aniso(float ax, float ay, float r1, float r2) {
    float phi = r1 * (2*M_PI);
    float sinPhi = ay * sin(phi);
    float cosPhi = ax * cos(phi);
    float tanTheta = sqrt(r2 / (1 - r2));
    return float3(tanTheta * cosPhi, tanTheta * sinPhi, 1);
}

float3 ImportanceSampleGTR2(float a, float r1, float r2) {
    float phi = r1 * (2*M_PI);
    float cosTheta = sqrt((1 - r2) / (1 + (a * a - 1) * r2));
    float sinTheta = clamp(sqrt(1 - (cosTheta * cosTheta)), 0, 1);
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);
    return float3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}

float SchlickFresnel(float u) {
    float m = clamp(1 - u, 0, 1);
    float m2 = m * m;
    return m2 * m2 * m; // pow(m,5)
}

float DielectricFresnel(float cos_theta_i, float eta) {
    float sinThetaTSq = eta * eta * (1.0f - cos_theta_i * cos_theta_i);

    // Total internal reflection
    if (sinThetaTSq > 1)
        return 1;

    float cos_theta_t = sqrt(max(1 - sinThetaTSq, 0));

    float rs = (eta * cos_theta_t - cos_theta_i) / (eta * cos_theta_t + cos_theta_i);
    float rp = (eta * cos_theta_i - cos_theta_t) / (eta * cos_theta_i + cos_theta_t);

    return 0.5f * (rs * rs + rp * rp);
}

float GTR1(float NDotH, float a) {
    if (a >= 1)
        return (1 / M_PI);
    float a2 = a * a;
    float t = 1 + (a2 - 1) * NDotH * NDotH;
    return (a2 - 1) / (M_PI * log(a2) * t);
}

float GTR2(float NDotH, float a) {
    float a2 = a * a;
    float t = 1 + (a2 - 1) * NDotH * NDotH;
    return a2 / (M_PI * t * t);
}

float GTR2_aniso(float NDotH, float HDotX, float HDotY, float ax, float ay) {
    float a = HDotX / ax;
    float b = HDotY / ay;
    float c = a * a + b * b + NDotH * NDotH;
    return 1 / (M_PI * ax * ay * c * c);
}

float SmithG_GGX(float NDotV, float alphaG) {
    float a = alphaG * alphaG;
    float b = NDotV * NDotV;
    return 1 / (NDotV + sqrt(a + b - a * b));
}

float SmithG_GGX_aniso(float NDotV, float VDotX, float VDotY, float ax, float ay) {
    float a = VDotX * ax;
    float b = VDotY * ay;
    float c = NDotV;
    return 1 / (NDotV + sqrt(a * a + b * b + c * c));
}

float3 CosineSampleHemisphere(float r1, float r2) {
    float phi = (2*M_PI) * r2;
    float3 dir;
    dir.xy = sqrt(r1) * float2(cos(phi), sin(phi));
    dir.z = sqrt(max(0, 1 - dot(dir.xy, dir.xy)));
    return dir;
}

float3 UniformSampleHemisphere(float r1, float r2) {
    float phi = (2*M_PI) * r2;
    return float3(sqrt(max(0, 1 - r1*r1)) * float2(cos(phi), sin(phi)), r1);
}

float3 UniformSampleSphere(float r1, float r2) {
    float phi = (2*M_PI) * r2;
    float z = 1 - 2*r1;
    return float3(sqrt(max(0, 1 - z * z)) * float2(cos(phi), sin(phi)), z);
}
float3 UniformSampleCone(float r1, float r2, float cosThetaMax) {
    float cosTheta = (1 - r1) + r1 * cosThetaMax;
    float sinTheta = sqrt(1 - cosTheta * cosTheta);
    float phi = r2 * 2*M_PI;
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float2 ConcentricSampleDisk(float r1, float r2) {
    float2 s = 2*float2(r1,r2) - 1;
    if (all(s == 0)) return 0;
    float theta, r;
    if (abs(s.x) > abs(s.y)) {
        r = s.x;
        theta = M_PI/4 * (s.y / s.x);
    } else {
        r = s.y;
        theta = M_PI/2 - M_PI/4 * (s.x / s.y);
    }
    return r * float2(cos(theta), sin(theta));
}

float powerHeuristic(float a, float b) {
    float t = a * a;
    return t / (b * b + t);
}