struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

cbuffer ColorConstantBuffer : register(b0)
{
    float4 color;
};

cbuffer ModelConstantBuffer : register(b1)
{
    float4x4 model;
};

cbuffer CameraConstantBuffer : register(b2)
{
    float4x4 camera;
};

VSOutput VSMain(float4 position : POSITION)
{
    VSOutput result;

    result.position = mul(camera, mul(model, position));
    result.color = color;

    return result;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return input.color;
}
