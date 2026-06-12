// cbuffer ... Constant buffer(定数バッファ)
// GPU側で舞フレーム変わる可能性のあるデータ(行列・色)を
// シェーダーに渡すための仕組み
cbuffer Frame : register(b0)
{
    float4x4 viewProj;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
}

cbuffer Object : register(b1)
{
    float4x4 world;
}

// VSInput ... 頂点シェーダーに渡される1頂点のデータ
// float3 pos : POSITION; ... 頂点の位置
// float4 col : COLOR; ... 頂点の色
// : POSITIONやCOLORはセマンティクスと呼ばれ
// GPUに「これは位置だ」「これは色だ」と伝えるもの
struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
};

// PSInput ... ピクセルシェーダーに渡すデータ
// float3 pos : SV_POSITION; ... スクリーン座標に変換された頂点位置
// float4 col : COLOR; ... 頂点の色
struct PSInput
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float3 worldPos : TEXCOORD1;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
};

// BasicVS ... 頂点シェーダーのエントリーポイント
// VSInput BasicVS(VSInput input) ... VSInput型のデータを受け取り、VSInput型のデータを返す関数
// PSInput output ... ピクセルシェーダーに渡すデータを格納する変数
PSInput BasicVS(VSInput input)
{
    PSInput output;
    float4 wp = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(wp, viewProj);
    output.normal = normalize(mul(float4(input.normal, 0.0f), world).xyz);
    output.worldPos = wp.xyz;
    output.col = input.col;
    output.uv = input.uv;
    return output;
}