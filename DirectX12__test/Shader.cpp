#include "Shader.hpp"

bool Shader::LoadFromFile(
	const std::wstring& filepath,
	const std::string& entryPoint,
	const std::string& profile,
	UINT compileFlags)
{
	m_Blob.Reset();

#if _DEBUG
	if (compileFlags == 0)
	{
		compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
	}
#endif

	ComPtr<ID3DBlob> errorBlob;
	const auto hr = D3DCompileFromFile(
		filepath.c_str(),
		nullptr,
		nullptr,
		entryPoint.c_str(),
		profile.c_str(),
		compileFlags,
		0,
		&m_Blob,
		&errorBlob);

	if (FAILED(hr))
	{
		if (errorBlob != nullptr)
		{
			OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
		}
		return false;
	}

	return true;
}