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
    result.color.r = color.r * (position.x / 16);
    result.color.g = color.g * (position.y / 16);
    result.color.b = color.b * (position.z / 16);
    result.color.a = color.a;

    return result;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return input.color;
}
