#include "Shader.hpp"

bool UseDxcProfile(const std::string& profile)
{
	return profile.find("6_") != std::string::npos ||
		profile.find("ms_", 0) == 0 ||
		profile.find("as_", 0) == 0;
}

using DxcCreateInstanceProc = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);

HRESULT CreateDxcInstance(REFCLSID clsid, REFIID iid, void** ppv)
{
	static HMODULE module = LoadLibraryW(L"dxcompiler.dll");
	if (module == nullptr)
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}

	auto proc = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module, "DxcCreateInstance"));
	if (proc == nullptr)
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}

	return proc(clsid, iid, ppv);
}

bool Shader::LoadFromFile(
	const std::wstring& filepath,
	const std::string& entryPoint,
	const std::string& profile,
	UINT compileFlags)
{
	m_Blob.Reset();
	m_DxcBlob.Reset();

	if (UseDxcProfile(profile))
	{
		return CompileWithDxc(filepath, entryPoint, profile, compileFlags);
	}

	return CompileWithD3DCompile(filepath, entryPoint, profile, compileFlags);
}

bool Shader::CompileWithDxc(const std::wstring& filepath, const std::string& entryPoint, const std::string& profile, UINT compileFlags)
{
	ComPtr<IDxcUtils> utils;
	ComPtr<IDxcCompiler3> compiler;

	if (FAILED(CreateDxcInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))))
	{
		return false;
	}

	if(FAILED(CreateDxcInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
	{
		return false;
	}

	ComPtr<IDxcBlobEncoding> source;
	if(FAILED(utils->LoadFile(filepath.c_str(), nullptr, &source)))
	{
		return false;
	}

	DxcBuffer buffer{};
	buffer.Ptr = source->GetBufferPointer();
	buffer.Size = source->GetBufferSize();
	buffer.Encoding = DXC_CP_UTF8;

	std::wstring entryPointW(entryPoint.begin(), entryPoint.end());
	std::wstring profileW(profile.begin(), profile.end());

	std::vector<LPCWSTR> args;
	args.push_back(L"-E"); args.push_back(entryPointW.c_str());
	args.push_back(L"-T"); args.push_back(profileW.c_str());
	args.push_back(L"-HV"); args.push_back(L"2021");

#if _DEBUG
	args.push_back(L"-Zi");
	args.push_back(L"-Qembed_debug");
	args.push_back(L"-0d");
#else
	args.push_back(L"-O3");
#endif
	ComPtr<IDxcIncludeHandler> includeHandler;
	if (FAILED(utils->CreateDefaultIncludeHandler(&includeHandler)))
	{
		return false;
	}

	ComPtr<IDxcResult> result;
	if (FAILED(compiler->Compile(
		&buffer,
		args.data(),
		static_cast<UINT>(args.size()),
		includeHandler.Get(),
		IID_PPV_ARGS(&result))))
	{
		return false;
	}

	ComPtr<IDxcBlobUtf8> errors;
	if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)))
	{
		if (errors != nullptr && errors->GetStringLength() > 0)
		{
			OutputDebugStringA(errors->GetStringPointer());
		}
	}

	HRESULT status = S_OK;
	if (FAILED(result->GetStatus(&status)) || FAILED(status))
	{
		return false;
	}

	if(FAILED(result->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(&m_DxcBlob), nullptr)))
	{
		return false;
	}

	return true;
}

bool Shader::CompileWithD3DCompile(const std::wstring& filepath, const std::string& entryPoint, const std::string& profile, UINT compileFlags)
{
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
