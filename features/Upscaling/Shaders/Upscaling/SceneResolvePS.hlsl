#include "Upscaling/UpscaleVS.hlsl"

cbuffer ResolveConstants : register(b1)
{
	float2 SourceSize;
	float2 DestinationSize;
	float2 Jitter;
	uint Eyes;
	uint StencilBit;
	uint Interpolate;
	uint3 Padding;
};

Texture2D<float4> Source : register(t0);
Texture2D<uint> Stencil : register(t1);
SamplerState LinearSampler : register(s0);

float2 SourcePosition(float2 position)
{
	float2 uv = position / DestinationSize;
	uint eye = min((uint)(uv.x * Eyes), Eyes - 1);
	float2 pixel = uv * SourceSize - Jitter;
	float eyeWidth = SourceSize.x / Eyes;
	return clamp(pixel, float2(eye * eyeWidth + 0.5, 0.5), float2((eye + 1) * eyeWidth - 0.5, SourceSize.y - 0.5));
}

#if defined(RESOLVE_DEPTH_STENCIL)
struct DepthStencilOutput
{
	float depth: SV_Depth;
	uint stencil: SV_StencilRef;
};

DepthStencilOutput main(VS_OUTPUT input)
{
	int3 pixel = int3((int2)SourcePosition(input.Position.xy), 0);
	DepthStencilOutput result;
	result.depth = Source.Load(pixel).r;
	result.stencil = Stencil.Load(pixel);
	return result;
}
#elif defined(RESOLVE_STENCIL)
void main(VS_OUTPUT input)
{
	if ((Stencil.Load(int3((int2)SourcePosition(input.Position.xy), 0)) & StencilBit) == 0)
		discard;
}
#elif defined(RESOLVE_DEPTH)
float main(VS_OUTPUT input) : SV_Depth
{
	return Source.Load(int3((int2)SourcePosition(input.Position.xy), 0)).r;
}
#else
float4 main(VS_OUTPUT input) : SV_Target
{
	float2 position = SourcePosition(input.Position.xy);
	return Interpolate ? Source.SampleLevel(LinearSampler, position / SourceSize, 0) : Source.Load(int3((int2)position, 0));
}
#endif
