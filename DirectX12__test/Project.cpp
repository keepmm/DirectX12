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

    // name は UI 由来の UTF-8。narrow のまま連結すると日本語のフォルダ名が化ける
    const fs::path root = parentDir / Utf8ToPath(name);

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
	m_StartScene = "SampleScene";

    if (!Save(outError))
    {
        return false;
    }
    if (!EnsureScriptProject(outError))
    {
        return false;
    }
    {
        std::string ignore;
        RefreshScriptProjectSources(ignore);
    }
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
    // 旧形式では "Assets/Scenes/Foo.json" のようにパスで入っていることがある。
    // SceneManager::ScenePathFromName が二重に組み立ててしまうので名前へ落とす
    m_StartScene = fs::path(j.value("startScene", std::string("SampleScene"))).stem().string();
    if (m_StartScene.empty())
    {
        m_StartScene = "SampleScene";
    }

    if (!fs::exists(m_Root / "Assets", ec))
    {
        outError = "Assets フォルダが存在しません: " + (m_Root / "Assets").string();
        m_Root.clear();
		return false;
    }

    if (!EnsureScriptProject(outError))
    {
        return false;
    }

    // 一覧が空のままだと VS 側でヘッダを解決できないので、開いた時点で埋めておく。
    // 失敗してもプロジェクト自体は開けるので致命ではない
    {
        std::string ignore;
        RefreshScriptProjectSources(ignore);
    }

    if (!Activate(outError))
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

bool Project::CreateSkeleton(const fs::path& root, std::string& outError)
{
    std::error_code ec;
    fs::create_directories(root / "Assets" / "Scenes", ec);
    if (ec)
    {
        outError = "フォルダ作成に失敗: " + (root / "Assets" / "Scenes").string()
            + " (" + ec.message() + ")";
        return false;
    }

    // Scripts.dll と cr の世代コピー置き場。Assets の外に置いて
    // アセットウィンドウに出さない
    fs::create_directories(root / "Library", ec);
    return true;
}

bool Project::WriteEmptyScene(const std::filesystem::path& scenePath) const
{
    // キー名は SceneSerializer::SaveToString に合わせる。
    // "name" だと LoadFromString の sceneName 判定に引っかからない
    json j;
	j["sceneName"] = scenePath.stem().string();
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
    // JSON は UTF-8 前提(CP932 のバイト列を積むと dump が例外を投げる)。
    // Launcher 側も UTF-8 として読むので、ここで揃えておく
    const std::string s = PathToUtf8(root);
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

bool Project::RefreshScriptProjectSources(std::string& outError)
{
    const fs::path proj = GetScriptProjectPath();
    if (!fs::exists(proj))
    {
        outError = "Scripts.vcxproj がありません: " + proj.string();
        return false;
    }

    // Assets 配下を走査。プロジェクト相対で持つとフォルダごと移動しても壊れない
    std::vector<std::string> cpps, hpps;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(m_Root / "Assets", ec))
    {
        if (!e.is_regular_file(ec)) continue;

        const auto ext = e.path().extension();
        const std::string rel =
            fs::relative(e.path(), m_Root, ec).make_preferred().string();

        if (ext == ".cpp")      cpps.push_back(rel);
        else if (ext == ".hpp") hpps.push_back(rel);
    }

    std::string items;
    for (const auto& c : cpps)
    {
        // obj はソースのフォルダ構成に合わせて掘る。
        // 平坦なままだと別フォルダの同名 .cpp が同じ obj を取り合って
        // MSB8027 になり、フォルダ分けした途端にビルドが壊れる
        items += "    <ClCompile Include=\"" + c + "\">\r\n"
            "      <ObjectFileName>$(IntDir)%(RelativeDir)</ObjectFileName>\r\n"
            "    </ClCompile>\r\n";
    }
    for (const auto& h : hpps)
        items += "    <ClInclude Include=\"" + h + "\" />\r\n";

    // マーカーの間だけを差し替える
    std::ifstream in(proj, std::ios::binary);
    std::string xml((std::istreambuf_iterator<char>(in)), {});
    in.close();

    const std::string beginMark = "<!-- SCRIPTS_BEGIN -->";
    const std::string endMark = "<!-- SCRIPTS_END -->";

    const size_t b = xml.find(beginMark);
    const size_t e = xml.find(endMark);
    if (b == std::string::npos || e == std::string::npos || e < b)
    {
        outError = "Scripts.vcxproj のマーカーが見つかりません(手で編集された？)";
        return false;
    }

    const std::string updated =
        xml.substr(0, b + beginMark.size()) + "\r\n" + items + "    " + xml.substr(e);

    if (updated == xml) return true;   // 変化なし(VSの再読み込みを起こさない)

    std::ofstream out(proj, std::ios::binary);
    if (!out)
    {
        outError = "Scripts.vcxproj の更新に失敗: " + proj.string();
        return false;
    }
    out << updated;
    return true;
}

// 生成する Scripts.vcxproj の中身。
// エンジン側のパスは焼き込まず、ビルド時に /p:EngineDir /p:EngineOutDir で受け取る。
// ソースはワイルドカードで拾うので、ファイルを足しても再生成は要らない。
//
// 生文字列の区切りに XML を付けているのは、テンプレート内の
// exists('...')" に )" が現れて R"( ... )" だと途中で終わってしまうため
static const char* kScriptProjectTemplate =
R"XML(<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>{9C1F2D8A-3B4E-4A7C-9E15-6D0F8B2A4C31}</ProjectGuid>
    <RootNamespace>Scripts</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)'=='Debug'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>MultiByte</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Release'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ImportGroup Label="PropertySheets">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <PropertyGroup>
    <!-- ScriptHost は /p:EngineDir /p:EngineOutDir で渡す。
         VS で開いたとき用に既定値を焼いておく(IntelliSense と F7 のため)。
         Condition 付きなので /p: が来ればそちらが勝つ -->
    <EngineDir Condition="'$(EngineDir)'==''">@ENGINE_DIR@</EngineDir>
    <EngineOutDir Condition="'$(EngineOutDir)'==''">@ENGINE_OUT_DIR@</EngineOutDir>
    <!-- リンクするエンジンの import library。ゲームビルドでは exe 名が
         <ゲーム名>.exe になるので /p:EngineLibName で差し替えられるようにする -->
    <EngineLibName Condition="'$(EngineLibName)'==''">DirectX12__test.lib</EngineLibName>
    <EngineScriptsDir>$(EngineDir)..\Scripts\</EngineScriptsDir>
    <OutDir>$(ProjectDir)Library\</OutDir>
    <IntDir>$(ProjectDir)Library\obj\$(Configuration)\</IntDir>
    <TargetName>Scripts</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
      <PreprocessorDefinitions>SCRIPTS_EXPORTS;_WINDOWS;_USRDLL;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalIncludeDirectories>$(EngineDir);$(EngineScriptsDir);$(ProjectDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <SubSystem>Windows</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <EnableUAC>false</EnableUAC>
      <AdditionalLibraryDirectories>$(EngineOutDir);%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>$(EngineLibName);%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <!-- エンジン側の足場 -->
    <ClCompile Include="$(EngineScriptsDir)cr_main.cpp" />
    <ClCompile Include="$(EngineScriptsDir)dllmain.cpp" />
    <ClCompile Include="$(EngineScriptsDir)pch.cpp" />
    <!-- プロジェクトのスクリプト -->
    <!-- この2行の間は RefreshScriptProjectSources が書き換える。
         ワイルドカードにしないのは、VS で開いた時点で展開されて
         固定リストに書き戻されてしまうため -->
    <!-- SCRIPTS_BEGIN -->
    <!-- SCRIPTS_END -->
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
)XML";

// Scripts.vcxproj だけを含む最小の sln。
// エンジンの sln と別ファイルにすることで、VS が別インスタンスで開いてくれる
static const char* kScriptSolutionTemplate =
R"XML(Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "Scripts", "Scripts.vcxproj", "{9C1F2D8A-3B4E-4A7C-9E15-6D0F8B2A4C31}"
EndProject
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|x64 = Debug|x64
		Release|x64 = Release|x64
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
		{9C1F2D8A-3B4E-4A7C-9E15-6D0F8B2A4C31}.Debug|x64.ActiveCfg = Debug|x64
		{9C1F2D8A-3B4E-4A7C-9E15-6D0F8B2A4C31}.Debug|x64.Build.0 = Debug|x64
		{9C1F2D8A-3B4E-4A7C-9E15-6D0F8B2A4C31}.Release|x64.ActiveCfg = Release|x64
		{9C1F2D8A-3B4E-4A7C-9E15-6D0F8B2A4C31}.Release|x64.Build.0 = Release|x64
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
EndGlobal
)XML";

bool Project::EnsureScriptProject(std::string& outError)
{
    if (m_Root.empty())
    {
        outError = "プロジェクトが開かれていません";
        return false;
    }

    std::error_code ec;
    fs::create_directories(GetLibraryDir(), ec);

    const fs::path out = GetScriptProjectPath();
    if (fs::exists(out, ec))
    {
        // 既にある。ユーザーが手で直している可能性があるので上書きしない
        return true;
    }

    std::ofstream ofs(out, std::ios::binary);
    if (!ofs)
    {
        outError = "Scripts.vcxproj の書き出しに失敗: " + out.string();
        return false;
    }

    // 既定値のプレースホルダを実パスに置き換えてから書き出す
    {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();
        fs::path slnDir = exeDir.parent_path().parent_path();

        // 末尾の区切りは付ける($(EngineDir)..\Scripts\ の連結が前提)
        fs::path engineDirPath = slnDir / "DirectX12__test";
        const std::string engineDir = engineDirPath.make_preferred().string() + "\\";
        const std::string engineOut = exeDir.make_preferred().string() + "\\";

        std::string xml = kScriptProjectTemplate;

        auto replaceAll = [](std::string& s, const std::string& from, const std::string& to)
            {
                for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size()))
                {
                    s.replace(p, from.size(), to);
                }
            };
        replaceAll(xml, "@ENGINE_DIR@", engineDir);
        replaceAll(xml, "@ENGINE_OUT_DIR@", engineOut);

        ofs << xml;
    }
    ofs.close();

    // VS から開くための sln も一緒に用意する
    const fs::path sln = GetScriptSolutionPath();
    if (!fs::exists(sln, ec))
    {
        std::ofstream slnOfs(sln, std::ios::binary);
        if (!slnOfs)
        {
            outError = "Scripts.sln の書き出しに失敗: " + sln.string();
            return false;
        }
        slnOfs << kScriptSolutionTemplate;
    }

    return true;
}