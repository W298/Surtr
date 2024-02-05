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


PS_OUTPUT PS(VS_OUTPUT input)
{
    PS_OUTPUT output;
    output.color = float4(input.color, 1.0);

    return output;
}