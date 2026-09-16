/*****************************************************************//**
 * \file   MaterialPreview.hpp
 * \brief  マテリアルを球に貼ってオフスクリーンへ
 * 
 * 作成者 
 * 作成日 2026/9/12
 * 更新履歴
 * *********************************************************************/
#pragma once

#include <memory>
#include <unordered_map>

#include "Defines.hpp"
#include "Mesh.hpp"
#include "RenderTexture.hpp"

class Material;

class MaterialPreview
{
public:
	static MaterialPreview& Get();

	/// @brief 球メッシュと共有の深度バッファを共有
	/// @param size 
	void Init(UINT size = 256);

	D3D12_GPU_DESCRIPTOR_HANDLE Request(_In_ const std::shared_ptr<Material>& mat);

	void RenderRequested(_In_ ID3D12GraphicsCommandList* cmd, UINT frameIndex);

	void SetCameraAngle(float yaw, float pitch);

	void Release();

	/// @brief プレビュー用の深度バッファ(ThumbnailCache と共有。サイズは Size())
	D3D12_CPU_DESCRIPTOR_HANDLE DepthHandle() const noexcept { return m_DsvHandle; }
	UINT Size() const noexcept { return m_Size; }
	bool IsReady() const noexcept { return m_Initialized; }

	/// @brief マテリアルのサムネイル用の球
	const Mesh& Sphere() const noexcept { return m_Sphere; }

	/// @brief プレビュー用の固定ライトを b2 に張る
	/// @note シーンのライトに影響されないほうが、アセット同士を比べやすい
	static void BindPreviewLight(_In_ ID3D12GraphicsCommandList* cmd, UINT frameIndex);
private:
	struct Slot
	{
		RenderTexture rt;
		std::weak_ptr<Material> material;
		bool dirty = true;
		int idleFrames = 0;
		int deadFrames = 0;
	};

	Slot* FindOrCreate(_In_ const std::shared_ptr<Material>& mat);

	void RenderOne(
		_In_ ID3D12GraphicsCommandList* cmd,
		_In_ Slot& slot,
		_In_ Material& mat,
		UINT frameIndex
		);

	bool EnsureDepth();

	Mesh m_Sphere;
	UINT m_Size = 256;

	ComPtr<ID3D12Resource> m_Depth;
	D3D12_CPU_DESCRIPTOR_HANDLE m_DsvHandle{};

	// Material* をキーにする(shared_ptr は weak で持って寿命を見る)
	std::unordered_map<const Material*, std::unique_ptr<Slot>> m_Slots;

	float m_Yaw = 0.6f;
	float m_Pitch = 0.25f;

	bool m_Initialized = false;
};

