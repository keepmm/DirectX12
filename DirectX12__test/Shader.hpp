/*****************************************************************//**
 * \file   Shader.hpp
 * \brief  シェーダー関連をやるクラス
 * 
 * 作成者 keeeep
 * 作成日 2026/4/27
 * 更新履歴	4.24 作成
 *			5.9 MeshShaderに対応
 * *********************************************************************/

#pragma once
#include "Defines.hpp"
#include <dxcapi.h>

class Shader
{
public:
	bool LoadFromFile(
		_In_ const std::wstring& filepath,
		_In_ const std::string& entryPoint,
		_In_ const std::string& profile,
		UINT compileFlags = 0);

	bool IsValid() const { return m_Blob != nullptr; }

	D3D12_SHADER_BYTECODE GetByteCode() const
	{
		D3D12_SHADER_BYTECODE byteCode{};
		if(m_DxcBlob != nullptr)
		{
			byteCode.pShaderBytecode = m_DxcBlob->GetBufferPointer();
			byteCode.BytecodeLength = m_DxcBlob->GetBufferSize();
		}
		if (m_Blob != nullptr)
		{
			byteCode.pShaderBytecode = m_Blob->GetBufferPointer();
			byteCode.BytecodeLength = m_Blob->GetBufferSize();
		}
		return byteCode;
	}
private:
	bool CompileWithDxc(
		_In_ const std::wstring& filepath,
		_In_ const std::string& entryPoint,
		_In_ const std::string& profile,
		UINT compileFlags);

	bool CompileWithD3DCompile(
		_In_ const std::wstring& filepath,
		_In_ const std::string& entryPoint,
		_In_ const std::string& profile,
		UINT compileFlags);
private:
	ComPtr<ID3DBlob> m_Blob;
	ComPtr<IDxcBlob> m_DxcBlob;
};

