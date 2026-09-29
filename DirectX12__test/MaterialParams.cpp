/*! ************************************************************
 * \file   MaterialParams.cpp
 * \brief  マテリアルの見た目パラメータ(ただのデータ)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 Material から分離して作成
 * *********************************************************************/
#include "MaterialParams.hpp"

#include "DirectX.hpp"
#include "ShaderTypes.hpp"

void MaterialParams::FillCB(MaterialCB& out) const
{
	out.basecolor = baseColor;
	out.emissiveColor = emissiveColor;
	out.roughness = roughness;
	out.metallic = metallic;
	out.rimColor = rimColor;

	out.pbrParams.z = emissiveStrength;
	out.pbrParams.w = 0.0f;

	out.faceParam.x = isFace ? 1.0f : 0.0f;	// 顔マテリアルをシェーダーへ渡す
	out.faceParam.y = baseAlpha;
	out.faceParam.w = outlineWidth;			// アウトラインの太さ

	out.reflectParam = float4(reflectStrength, reflectFade,
		reflectBlur, APP->GetReflectionScale());

	out.sssParams = { sssStrength, sssWrap, sssTrans, sheen };
	out.sssColor = sssColor;

	out.waveParams = { waveHeight, waveLength, waveSpeed, waterGloss };
	out.waterParams = { waterOpacity, waterTurbidity, waterFoam, 0.0f };
	out.waveParams2 = { waveAmplitude, waveSteepness, 0.0f, 0.0f };
}
