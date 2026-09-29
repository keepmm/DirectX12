// ------------------------------------------------------------------ //
//  マテリアル定数バッファ(b3)の宣言。C++ の MaterialCB(ShaderTypes.hpp)と対。
//
//  シェーダーごとに書くと、並びがずれても黙って壊れる(読めない値が入る)。
//  ここ1箇所だけに置き、全シェーダーから #include する。
//  項目を足すときは ShaderTypes.hpp の MaterialCB と両方を直すこと。
// ------------------------------------------------------------------ //
#ifndef MATERIAL_CB_HLSLI
#define MATERIAL_CB_HLSLI

cbuffer Material : register(b3)
{
    float  roughness;
    float  metallic;
    float2 _pad;
    float4 rimColor;
    float4 mapFlags;      // x:hasNormal y:hasMetal z:hasRough w:envMaxMip
    float4 faceParam;     // x:isFace y:baseAlpha w:アウトライン幅
    float4 sssParams;     // x:SSS強度 y:ラップ z:逆光透過 w:布シーン
    float4 sssColor;
    float4 basecolor;     // インスペクタの「ベースカラー」
    float4 reflectParam;  // x:強度 y:フェード距離 z:ぼかし半径 w:反射RTの解像度
    float4 pbrParams;     // x:hasEmissive y:hasOcclusion z:エミッシブ強度
    float4 emissiveColor;
    float4 waveParams;    // x:法線の起伏 y:波長 z:流れる速さ w:鏡面の鋭さ
    float4 waterParams;   // x:最小不透明度 y:濁り z:白波のしきい値 w:コースティクス
    float4 waveParams2;   // x:頂点振幅(m) y:尖り z,w:予備
}

#endif // MATERIAL_CB_HLSLI
