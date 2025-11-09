cbuffer TransformConstantBuffer : register(b0)
{
    float4x4 transform;
};

cbuffer CharConstantBuffer : register(b1)
{
    uint char_ascii_code;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOutput VSMain(uint vertex_id: SV_VertexID)
{
    VSOutput result;

    float2 positions[6] = {
        float2(-1.0, -1.0),  // Bottom-left
        float2( 1.0,  1.0),  // Top-right
        float2(-1.0,  1.0),  // Top-left

        float2(-1.0, -1.0),  // Bottom-left
        float2( 1.0, -1.0),  // Bottom-right
        float2( 1.0,  1.0),  // Top-right
    };

    // NOTE: Monogram-bitmap.png contains 16x8 chars
    const uint bitmap_chars_h = 16;
    const uint bitmap_chars_v = 8;

    const uint char_j = (char_ascii_code - 32) % bitmap_chars_h;
    const uint char_i = (char_ascii_code - 32) / bitmap_chars_h;

    const float uv_height = 1.0 / bitmap_chars_v;
    const float uv_width = 1.0 / bitmap_chars_h;
    const float2 uv_origin = float2(char_j * uv_width, char_i * uv_height);
    float2 uvs[6] = {
        uv_origin + float2(0, uv_height),
        uv_origin + float2(uv_width, 0),
        uv_origin,

        uv_origin + float2(0, uv_height),
        uv_origin + float2(uv_width, uv_height),
        uv_origin + float2(uv_width, 0),
    };

    result.position = mul(transform, float4(positions[vertex_id], 0, 1));
    result.uv = uvs[vertex_id];

    return result;
}

Texture2D font_texture : register(t0);
SamplerState font_sampler : register(s0);

float4 PSMain(VSOutput input) : SV_TARGET
{
    return font_texture.Sample(font_sampler, input.uv);
}
