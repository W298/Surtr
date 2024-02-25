#define PI 3.1415926538

//--------------------------------------------------------------------------------------
// I/O Structures
//--------------------------------------------------------------------------------------
struct VS_OUTPUT
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

struct PS_OUTPUT
{
    float4 color : SV_Target;
};


//--------------------------------------------------------------------------------------
// Texture & Sampler Variables
//--------------------------------------------------------------------------------------
Texture2D texMap[5] : register(t0);
SamplerState samAnisotropic : register(s0);

PS_OUTPUT PS(VS_OUTPUT input)
{
    PS_OUTPUT output;

    float3 texColor = input.color;
    float4 final = float4(texColor.rgb, 1);

    output.color = final;

    return output;
}