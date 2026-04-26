/*****************************************************************//**
 * \file   MeshRenderer.hpp
 * \brief  メッシュレンダラークラスの定義
 * 
 * 作成者 keeeep
 * 作成日 2026/4/27
 * 更新履歴 4.27 作成
 * *********************************************************************/
#pragma once

#include "Defines.hpp"

class Mesh;
class Material;
class RenderContext;

class MeshRenderer
{
public:
	/// @brief メッシュを設定する
	/// @param mesh 設定するメッシュ
	void SetMesh(_In_ const std::shared_ptr<Mesh>& mesh) { m_Mesh = mesh; }

	/// @brief マテリアルの適用
	/// @param subMeshIndex 適用するサブメッシュのインデックス
	/// @param material 適用するマテリアル
	void SetMaterial(
		_In_ UINT subMeshIndex,
		_In_ const std::shared_ptr<Material>& material)
	{
		if (subMeshIndex >= m_Materials.size())
		{
			m_Materials.resize(subMeshIndex + 1);
		}
		m_Materials[subMeshIndex] = material;
	}

	/// @brief 描画処理
	/// @param renderContext 描画コンテキスト
	/// @param worldMatrix ワールド行列
	void Draw(
		_In_ const RenderContext& renderContext,
		_In_ const float4x4& worldMatrix
	)const;
private:
	std::shared_ptr<Mesh> m_Mesh;
	std::vector<std::shared_ptr<Material>> m_Materials;
};

