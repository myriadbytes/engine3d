struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float3 normal : NORMAL;
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

VSOutput VSMain(float3 position : POSITION, float3 normal : NORMAL)
{
    VSOutput result;

    result.position = mul(camera, mul(model, float4(position, 1)));

    result.color.r = (position.x / 16);
    result.color.g = (position.y / 16);
    result.color.b = (position.z / 16);
    result.color.a = 1;

    result.normal = normal;

    return result;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 fake_light_dir = normalize(float3(-.7, 1, -1.9));
    float diffuse = max(dot(input.normal, fake_light_dir), 0.0);
    float ambient = 0.2;

    return input.color * diffuse + input.color * ambient;
}
