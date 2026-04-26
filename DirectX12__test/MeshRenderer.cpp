#include "MeshRenderer.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "RenderContext.hpp"

void MeshRenderer::Draw(const RenderContext& renderContext, const float4x4& worldMatrix) const
{
	if (renderContext.CommandList == nullptr || m_Mesh == nullptr)return;

	const UINT subMeshCount = m_Mesh->GetSubMeshCount();
	for(UINT i = 0; i < subMeshCount; i++)
	{
		if(i >= m_Materials.size() || m_Materials[i] == nullptr)
		{
			// マテリアルが設定されていないサブメッシュは描画しない
			continue;
		}

		m_Materials[i]->Apply(
			renderContext.CommandList,
			worldMatrix,
			renderContext.view,
			renderContext.projection,
			renderContext.wireframe);

		m_Mesh->DrawSubMesh(renderContext.CommandList, i);
	}
}
