#pragma compile dxc -spirv -T cs_6_7 -E main


Texture2D<float> gInput;
RWTexture2D<float> gOutput;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gInput.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	gOutput[index.xy] = sqrt(gInput[index.xy]);
}