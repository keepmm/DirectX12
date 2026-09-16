/*****************************************************************//**
 * \file   MaterialLibrary.hpp
 * \brief  マテリアルアセット(.mat)の読み書きと共有
 * 
 * 作成者 keepmm
 * 作成日 2026/9/14
 * 更新履歴
 * *********************************************************************/
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <Windows.h>

class Material;

class MaterialLibrary
{
public:
	static MaterialLibrary& Get();

	/// @brief .mat を読む。読み込み済みなら共有インスタンスを返す
	/// @return 失敗したら nullptr
	std::shared_ptr<Material> Load(_In_ const std::string& path);

	/// @brief 読み込み済みの .mat を、今の値で書き戻す(インスペクタの「保存」)
	bool Save(_In_ const std::string& path);

	/// @brief 既存のマテリアルから .mat を作る
	/// @param fallbackShader src の shaderName が空(全体設定を継承)のときに書くシェーダー
	/// @param name ファイル名にする名前(UTF-8。PMX の材質名など)
	/// @return 作ったファイルのパス。失敗なら空
	std::string CreateFromMaterial(
		_In_ const Material& src,
		_In_ const std::string& fallbackShader,
		_In_ const std::string& dir,
		_In_ const std::string& name);

	/// @brief 白い PBR の .mat を作る(アセットブラウザの「作成」)
	std::string CreateDefault(_In_ const std::string& dir);

	/// @brief 既定のマテリアル(Assets/Materials/Default.mat)のパス。無ければ作る
	/// @return 相対パス。失敗したら空
	std::string EnsureDefault();

	/// @brief 共有をやめて、独立したコピーを作る(割り当て解除用)
	std::shared_ptr<Material> Clone(_In_ const Material& src);

	/// @brief 読み込み済みの .mat のテクスチャを差し替える
	bool SetTexture(_In_ const std::string& path, UINT slot, _In_ const std::string& texturePath);

	static bool IsMaterialPath(_In_ const std::string& path);

	/// @brief キャッシュを捨てる(プロジェクト切り替え時)
	void Clear() { m_Cache.clear(); }

private:
	MaterialLibrary() = default;

	/// @brief キャッシュのキー。GUID が取れればそれ、無ければ小文字の相対パス
	std::string KeyOf(_In_ const std::string& path) const;

	std::unordered_map<std::string, std::shared_ptr<Material>> m_Cache;
};

