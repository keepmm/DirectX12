#include "ShaderLibrary.hpp"

const Shader* ShaderLibrary::Load(
    const std::wstring& filePath, 
    const std::string& entryPoint, 
    const std::string& profile, 
    UINT compileFlags)
{

	// すでに同じShaderKeyが存在するか確認
	const ShaderKey key{ filePath , entryPoint, profile, compileFlags };

    // 存在する場合はキャッシュを返す
	auto it = m_Shaders.find(key);
    if (it != m_Shaders.end())
    {
        return it->second.shader.get();
	}

    // 存在しない場合は新規に作成
	auto shader = std::make_shared<Shader>();
    if (!shader->LoadFromFile(
        filePath,
        entryPoint,
        profile,
        compileFlags))
    {
        return nullptr;
    }

	ShaderEntry entry{};
    entry.shader = shader;

    if(std::filesystem::exists(filePath))
    {
        entry.lastWriteTime = std::filesystem::last_write_time(filePath);
	}

    // キャッシュを保存して返す
    m_Shaders.emplace(key, shader);
    return shader.get();
}

void ShaderLibrary::Clear()
{
    m_Shaders.clear();
}

bool ShaderLibrary::ReoadChanged()
{
    bool changed = false;

    for (auto& [key, entry] : m_Shaders)
    {
        // ファイルの最終更新日時を取得
        if (!std::filesystem::exists(key.filePath))
        {
            // ファイルが存在しない場合はスキップ
            continue;
        }

		const auto current = std::filesystem::last_write_time(key.filePath);
        if (current == entry.lastWriteTime)
        {
            // 更新されていない場合はスキップ
			continue;
        }

        auto shader = std::make_shared<Shader>();

        // シェーダーが取得できない場合はスキップ
        if (!shader->LoadFromFile(
            key.filePath,
            key.entryPoint,
            key.profile,
            key.compileFlags))
        {
            continue;
        }

		entry.shader = shader;
        entry.lastWriteTime = current;
		changed = true;
    }
	return changed;
}
