#define PI 3.1415926538


//--------------------------------------------------------------------------------------
// Constant Buffer Variables
//--------------------------------------------------------------------------------------
struct ShadowCBType
{
    float4x4 lightWorldMatrix;
    float4x4 lightViewProjMatrix;
    float4 cameraPosition;
};

ConstantBuffer<ShadowCBType> cb : register(b1);


//--------------------------------------------------------------------------------------
// I/O Structures
//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float3 position : POSITION;
};

struct VS_OUTPUT
{
    float4 position : SV_Position;
};


//--------------------------------------------------------------------------------------
// Texture & Sampler Variables
//--------------------------------------------------------------------------------------
Texture2D texMap[5] : register(t0);
SamplerState samAnisotropic : register(s0);


//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
VS_OUTPUT VS(VS_INPUT input)
{
    VS_OUTPUT output;
    
    // Multiply MVP matrices.
    output.position = mul(float4(input.position, 1.0f), cb.lightWorldMatrix);
    output.position = mul(output.position, cb.lightViewProjMatrix);

    return output;
}

void PS(VS_OUTPUT input)
{
    // Nothing to do.
}