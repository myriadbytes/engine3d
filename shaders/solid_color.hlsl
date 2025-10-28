struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

cbuffer ModelConstantBuffer : register(b0)
{
    float4x4 model;
};

cbuffer CameraConstantBuffer : register(b1)
{
    float4x4 camera;
};

cbuffer ColorConstantBuffer : register(b2)
{
    float4 color;
};

VSOutput VSMain(float3 position : POSITION, float3 normal : NORMAL)
{
    VSOutput result;

    result.position = mul(camera, mul(model, float4(position, 1)));

    result.color = color;

    return result;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return input.color;
}
