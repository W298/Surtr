#define PI 3.1415926538


//--------------------------------------------------------------------------------------
// Constant Buffer / Root Constant Variables
//--------------------------------------------------------------------------------------
struct OpaqueCBType
{
    float4x4 viewProjMatrix;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4x4 shadowTransform;
};

struct RootConstantType
{
    uint id;
    uint debug;
};

ConstantBuffer<OpaqueCBType> cb : register(b0);
ConstantBuffer<RootConstantType> rootConstant : register(b4, space0);


//--------------------------------------------------------------------------------------
// Structured Buffer Variables
//--------------------------------------------------------------------------------------
struct MeshSBType
{
    float4x4 worldMatrix;
};

StructuredBuffer<MeshSBType> sb : register(t7);


//--------------------------------------------------------------------------------------
// I/O Structures
//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

struct VS_OUTPUT
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float3 catPos : POSITION;
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
SamplerComparisonState samShadow : register(s1);
SamplerState anisotropicClampMip1 : register(s2);


//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
VS_OUTPUT VS(VS_INPUT input)
{
    VS_OUTPUT output;
    
    // Multiply WVP matrices.
    if (rootConstant.id != 99999)
        output.position = mul(float4(input.position, 1.0f), sb[rootConstant.id].worldMatrix);
    else
        output.position = float4(input.position, 1.0f);
    
    output.position = mul(output.position, cb.viewProjMatrix);
    
    output.color = input.color;
    if (rootConstant.debug == 1)
        output.color = float3(0.5, 0, 0);
    else if (rootConstant.debug == 2)
        output.color = float3(1, 0.5, 0);
    
    output.normal = input.normal;
    output.catPos = input.position;
    
    return output;
}

float CalcShadowFactor(float4 shadowPosH)
{
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;

    // Depth in NDC space.
    float depth = shadowPosH.z;

    uint width, height, numMips;
    texMap[4].GetDimensions(0, width, height, numMips);

    // Texel size.
    float dx = 1.0f / (float) width;

    float percentLit = 0.0f;
    const float2 offsets[9] =
    {
        float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx, +dx), float2(0.0f, +dx), float2(dx, +dx)
    };

    [unroll(9)]
    for (int i = 0; i < 9; ++i)
    {
        percentLit += texMap[4].SampleCmpLevelZero(samShadow, saturate(shadowPosH.xy + offsets[i]), depth).r;
    }
    
    return percentLit / 9.0f;
}

PS_OUTPUT PS(VS_OUTPUT input)
{
    PS_OUTPUT output;

    float3 texColor = input.color;
    float3 normal = input.normal;
    
    if (normal.x == 0 && normal.y == 0 && normal.z == 0)
    {
        output.color = float4(texColor, 1);
        return output;
    }
    
    float3 diffuse = saturate(dot(normal, -cb.lightDirection.xyz)) * cb.lightColor.xyz;
    float3 ambient = float3(0.08f, 0.08f, 0.08f) * cb.lightColor.xyz;
    float shadowFactor = CalcShadowFactor(mul(float4(input.catPos, 1.0f), cb.shadowTransform));

    float4 final = float4(saturate((diffuse * saturate(shadowFactor) + ambient)) * texColor.rgb, 1);
    
    output.color = final;
    return output;
}