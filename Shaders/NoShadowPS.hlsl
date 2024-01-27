#define PI 3.1415926538


//--------------------------------------------------------------------------------------
// Constant Buffer Variables
//--------------------------------------------------------------------------------------
struct OpaqueCBType
{
    float4x4 worldMatrix;
    float4x4 viewProjMatrix;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4x4 shadowTransform;
};

ConstantBuffer<OpaqueCBType> cb : register(b0);


//--------------------------------------------------------------------------------------
// I/O Structures
//--------------------------------------------------------------------------------------
struct VS_OUTPUT
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
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

    float3 texColor = float3(0.25f, 0.25f, 0.25f);
    float3 normal = input.normal;
    
    float3 diffuse = saturate(dot(normal, -cb.lightDirection.xyz)) * cb.lightColor.xyz;
    float3 ambient = float3(0.08f, 0.08f, 0.08f) * cb.lightColor.xyz;

    float4 final = float4(
		saturate((diffuse + ambient)) * texColor.rgb, 1);

    output.color = final;

    return output;
}