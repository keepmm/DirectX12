struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

float4 BasicPS(PSInput input) : SV_TARGET
{
    return input.col;   // •âŠÔ‚³‚ê‚½F‚ª“ü‚Á‚Ä‚­‚é
}