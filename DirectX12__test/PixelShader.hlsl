struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

float4 BasicPS(PSInput input) : SV_TARGET
{
    return input.col;   // 補間された色が入ってくる
}

float4 WireFramePS(PSInput input) : SV_Target
{
    return float4(0.0f, 0.0f, 0.0f, 1.0f); // ワイヤーフレームは常に黒
}