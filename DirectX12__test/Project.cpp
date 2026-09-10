/*****************************************************************//**
 * \file   Project.cpp
 * \brief  プロジェクトの作成・オープン・カレント管理の実装
 * 
 * 作成者 keep
 * 作成日 2026/9/10
 * 更新履歴 9.10 作成
 * *********************************************************************/
#include "Project.hpp"
#include "json.hpp"

#include <Windows.h>
#include <shobjidl.h>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

Project* Project::GetInstance()
{
    static Project instance;
    return &instance;
}

std::string PickProjectFolder()
{
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dlg))))
    {
        return {};
    }

    std::string result;

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS);

    if (SUCCEEDED(dlg->Show(nullptr)))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)))
        {
            PWSTR wpath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath)))
            {
                result = fs::path(wpath).string();
                CoTaskMemFree(wpath);
            }
            item->Release();
        }
    }

    dlg->Release();
    return result;
}

// ---- 最近開いたプロジェクト ----
// エンジン exe とランチャー exe の両方から読み書きするので、
// exe 横ではなく %LOCALAPPDATA% に置いて配置に依存しないようにする
static fs::path RecentsPath()
{
    wchar_t* appData = nullptr;
    size_t len = 0;
    fs::path dir;

    if (_wdupenv_s(&appData, &len, L"LOCALAPPDATA") == 0 && appData != nullptr)
    {
        dir = fs::path(appData) / L"DirectX12Engine";
        free(appData);
    }
    else
    {
        // 取得できない場合は exe 横へ退避
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        dir = fs::path(exePath).parent_path();
    }

    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / L"recents.json";
}

bool Project::Create(const std::filesystem::path& parentDir, const std::string& name, std::string& outError)
{
	// プロジェクト名が空ならエラー
    if (name.empty())
    {
        outError = "プロジェクト名が空です";
        return false;
    }

    const fs::path root = parentDir / name;

	// 既存のフォルダが空でない場合はエラー
    std::error_code ec;
    if (fs::exists(root, ec) && !fs::is_empty(root, ec))
    {
        outError = "フォルダが既に存在し、空ではありません: " + root.string();
        return false;
    }

	// プロジェクトの骨格を作る
    if (!CreateSkeleton(root, outError))
    {
        return false;
    }

    if(!WriteEmptyScene(root / "Assets" / "Scenes" / "SampleScene.json"))
    {
        outError = "初期シーンの書き出しに失敗しました";
        return false;
	}

	// カレントディレクトリをプロジェクトルートに切り替える
    m_Root = fs::absolute(root);
	m_Name = name;
	m_StartScene = "Assets/Scenes/SampleScene.json";

	// プロジェクトファイルを保存する
    if(!Save(outError))
    {
        return false;
	}

	// プロジェクトをアクティブにする
    if (!Activate(outError))
    {
        return false;
    }

	// 最近開いたプロジェクトに追加する
	PushRecents(m_Root);
	//LOG->LogInfo("プロジェクト作成: " + m_Root.string());
    return true;
}

bool Project::Open(const std::filesystem::path& path, std::string& outError)
{
    std::error_code ec;

    // .dxproj そのものか、それを含むフォルダかを探す
    fs::path projFile = path;
    if (fs::is_directory(path, ec))
    {
        projFile.clear();
        for (const auto& e : fs::directory_iterator(path, ec))
        {
            if (e.path().extension() == L".dxproj") { projFile = e.path(); break; }
        }
    }

    if (projFile.empty() || !fs::exists(projFile, ec))
    {
        outError = ".dxproj が見つかりません: " + path.string();
        return false;
    }

    std::ifstream ifs(projFile);
    if (!ifs)
    {
		outError = ".dxprojを開けません: " + projFile.string();
        return false;
    }

	// .json を解析する
    json j;
    try
    {
        ifs >> j;
    }
    catch (const std::exception& e)
    {
        outError = std::string(".dxproj の解析に失敗: ") + e.what();
        return false;
	}

	m_Root = fs::absolute(projFile.parent_path());
    m_Name = j.value("name", m_Root.filename().string());
    m_StartScene = j.value("startScene", std::string("Assets/Scenes/SampleScene.json"));

    if (!fs::exists(m_Root / "Assets", ec))
    {
        outError = "Assets フォルダが存在しません: " + (m_Root / "Assets").string();
        m_Root.clear();
		return false;
    }

    if(!Activate(outError))
    {
        return false;
	}

	PushRecents(m_Root);
	//LOG->LogInfo("プロジェクトを開きました: " + m_Root.string());
    return true;
}

std::filesystem::path Project::Resolve(const std::string& relative) const
{
    return m_Root.empty() ? fs::path(relative) : m_Root / relative;
}

bool Project::Save(std::string& outError) const
{
    if(m_Root.empty())
    {
        outError = "プロジェクトが開かれていません";
        return false;
	}

    json j;
    j["name"] = m_Name;
	j["startScene"] = m_StartScene;
	j["Version"] = 1;

	const fs::path out = m_Root / (m_Name + ".dxproj");
    std::ofstream ofs(out);
    if (!ofs)
    {
        outError = ".dxproj を書き出せません: " + out.string();
		return false;
    }

    ofs << j.dump(2);
    return true;
}

void Project::LoadRecents()
{
    m_Recents.clear();

    std::ifstream ifs(RecentsPath());
    if (!ifs) return;

    try
    {
        json j;
        ifs >> j;
        for (const auto& e : j)
        {
            m_Recents.push_back(e.get<std::string>());
        }
    }
    catch (const std::exception&)
    {
		m_Recents.clear();
    }
}

bool Project::CreateSkeleton(const std::filesystem::path& root, std::string& outError)
{
    std::error_code ec;
	fs::create_directories(root / "Assets" / "Scenes", ec);
    if (ec)
    {
        outError = "フォルダ作成に失敗: " + (root / "Assets" / "Scenes").string()
            + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

bool Project::WriteEmptyScene(const std::filesystem::path& scenePath) const
{
    json j;
	j["name"] = scenePath.stem().string();
	j["entities"] = json::array();

    std::ofstream ofs(scenePath);
    if (!ofs)
    {
        return false;
    }
    ofs << j.dump(4);
    return true;
}

bool Project::Activate(std::string& outError)
{
    if (!SetCurrentDirectoryW(m_Root.c_str()))
    {
        outError = "カレントディレクトリの変更に失敗: " + m_Root.string();
		return false;
    }

    return true;
}

void Project::PushRecents(const std::filesystem::path& root)
{
    const std::string s = root.string();
    m_Recents.erase(std::remove(m_Recents.begin(), m_Recents.end(), s), m_Recents.end());
    m_Recents.insert(m_Recents.begin(), s);
    if (m_Recents.size() > 10) m_Recents.resize(10);
    SaveRecents();
}

void Project::SaveRecents() const
{
    std::ofstream ofs(RecentsPath());
    if (!ofs) return;
    ofs << json(m_Recents).dump(2);
}

bool LaunchLauncher()
{
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const fs::path launcher = fs::path(exePath).parent_path() / L"Launcher.exe";

    if (!fs::exists(launcher))
    {
        return false;
    }

    std::wstring cmd = L"\"" + launcher.wstring() + L"\"";

    // CreateProcessW は第2引数を書き換えるので可変バッファを渡す
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(launcher.c_str(), buf.data(), nullptr, nullptr, FALSE,
        0, nullptr, nullptr, &si, &pi))
    {
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
