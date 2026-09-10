#define CR_HOST CR_DISABLE
#include "cr.h"
#include "ScriptContext.hpp"
#include "ScriptHost.hpp"
#include <windows.h>
#include <stdio.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <mutex>
#include "Debug.hpp"
#include <sstream>
#include "PlayState.hpp"
#include "imguiinit.hpp"
#include "RuntimeScene.hpp"
#include "Project.hpp"

static cr_plugin s_plugin;
static ScriptContext s_ctx;
static std::vector<std::string> s_scriptNames;
static bool s_isOpen = false;

// ---- 自動ビルド用 ---- //
static std::filesystem::path s_ScriptsSrcDir;   // 監視対象(プロジェクトの Assets)
static std::filesystem::path s_ProjPath;        // プロジェクトの Scripts.vcxproj
static std::filesystem::path s_SlnDir;
static std::filesystem::path s_EngineDir;       // エンジンのヘッダがある場所
static std::filesystem::path s_EngineOutDir;    // DirectX12__test.lib がある場所(=exe横)
static std::string s_msbuild =
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe";
static std::filesystem::file_time_type s_lastSrcTime{};
static std::atomic<bool> s_building{ false };

// ビルド結果をLoggerに受けわたし
static std::mutex s_BuildMutex;
static std::string s_BuildOutput;
static int s_BuildExit = 0;
static std::atomic<bool> s_BuildDone{ false };

static void LogInfoBridge(const char* msg) { LOG->LogInfo(msg); }
static void LogWarningBridge(const char* msg) { LOG->LogWarning(msg); }
static void LogErrorBridge(const char* msg) { LOG->LogError(msg); }

static void DrainBuildResult()
{
    if (!s_BuildDone.exchange(false)) return;   // 結果が来ていなければ何もしない

    std::string out; int code;
    {
        std::lock_guard<std::mutex> lk(s_BuildMutex);
        out = std::move(s_BuildOutput);
        code = s_BuildExit;
    }

    if (code == 0)
    {
        LOG->LogInfo(IMGUI::ToUTF8("[Scripts] ビルド成功 -> リロード"));
        return;
    }

    LOG->LogError(IMGUI::ToUTF8("[Scripts] ビルド失敗 (exit " + std::to_string(code) + ")"));

    // error / warning / fatal を含む行だけ抜き出して出す
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find(": error") != std::string::npos ||
            line.find(": fatal") != std::string::npos ||
            line.find(": warning") != std::string::npos)
        {
            LOG->LogError(IMGUI::ToUTF8(line));
        }
    }
}

static void LaunchBuild()
{
    if (s_building.exchange(true)) return;

    // ファイルが増減している可能性があるので一覧を作り直してからビルドする
    std::string err;
    if (!PROJECT->RefreshScriptProjectSources(err))
    {
        LOG->LogWarning(err);
    }

    std::thread([] {
        // エンジン自身のビルド構成と必ず一致させる(Debug/Release混在はABI不一致で即クラッシュ)
#ifdef _DEBUG
        constexpr const char* kConfig = "Debug";
#else
        constexpr const char* kConfig = "Release";
#endif
        // 生成した vcxproj はエンジンの場所を焼き込んでいないので /p: で渡す。
        // 末尾の区切りは付ける($(EngineDir)..\Scripts\ の連結が前提)
        std::string cmd =
            "\"" + s_msbuild + "\" \"" + s_ProjPath.string() + "\""
            " /p:Configuration=" + std::string(kConfig) + " /p:Platform=x64"
            " /p:EngineDir=\"" + s_EngineDir.string() + "\\\\\""
            " /p:EngineOutDir=\"" + s_EngineOutDir.string() + "\\\\\""
            " /nologo /clp:NoSummary /v:minimal";

        // 子プロセスの stdout/stderr を受け取るパイプ
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE rd = nullptr, wr = nullptr;
        CreatePipe(&rd, &wr, &sa, 0);
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0); // 読み側は継承しない

        std::vector<char> buf(cmd.begin(), cmd.end());
        buf.push_back('\0');

        STARTUPINFOA si{ sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = wr;
        si.hStdError = wr;
        PROCESS_INFORMATION pi{};

        std::string output;
        int exitCode = -1;

        if (CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(wr); wr = nullptr;  // 親側の書き込み端は閉じる(EOF検出のため)

            char tmp[4096]; DWORD n = 0;
            while (ReadFile(rd, tmp, sizeof(tmp), &n, nullptr) && n > 0)
                output.append(tmp, n);

            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
            exitCode = (int)code;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        else
        {
            output = "CreateProcess failed";
        }
        if (wr) CloseHandle(wr);
        CloseHandle(rd);

        {
            std::lock_guard<std::mutex> lk(s_BuildMutex);
            s_BuildOutput = std::move(output);
            s_BuildExit = exitCode;
            s_BuildDone = true;     // メインスレッドに通知
        }
        s_building = false;
        }).detach();
}

static void CheckAndBuild()
{
    if (s_building) return;
    std::error_code ec;
    std::filesystem::file_time_type maxT{};
    int count = 0;
    // スクリプトは Assets のどこに置いてもよいので再帰で見る
    for (auto& e : std::filesystem::recursive_directory_iterator(s_ScriptsSrcDir, ec))
    {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        auto ext = e.path().extension();
        if (ext == ".cpp" || ext == ".hpp")
        {
            count++;
            auto t = std::filesystem::last_write_time(e, ec);
            if (!ec && t > maxT) maxT = t;
        }
    }
        {
            char b[512];
            sprintf_s(b, "[watch] dir=%s files=%d ec=%d\n",
                s_ScriptsSrcDir.string().c_str(), count, ec.value());
            OutputDebugStringA(b);
        }

    // 初回は基準値を取るだけ（起動直後にビルドしない）
    if (s_lastSrcTime.time_since_epoch().count() == 0) { s_lastSrcTime = maxT; return; }

    if (maxT > s_lastSrcTime)
    {
        s_lastSrcTime = maxT;
        OutputDebugStringA("[ScriptHost] スクリプト変更検知 -> build\n");
        LaunchBuild();
    }
}

void ScriptHost::Open(World* world)
{
    s_ctx.world = world;
    s_plugin.userdata = &s_ctx;
    s_ctx.savedScripts = &s_scriptNames;

	// LOGの橋渡し関数をセット
    Debug::g_LogInfo = [](const char* m) { LOG->LogInfo(m); };
    Debug::g_LogWarning = [](const char* m) { LOG->LogWarning(m); };
    Debug::g_LogError = [](const char* m) { LOG->LogError(m); };

    // DLLに渡す関数ポインタ（ScriptContext）も設定
    s_ctx.logInfo = [](const char* m) { LOG->LogInfo(m); };
    s_ctx.logWarning = [](const char* m) { LOG->LogWarning(m); };
    s_ctx.logError = [](const char* m) { LOG->LogError(m); };
    s_ctx.launchFirework = [](float x, float y, float z, int shape,
        float r, float g, float b, const char* text)
        {
            if (auto* rs = RuntimeScene::Current())
                rs->LaunchFirework(float3{ x,y,z }, shape, float3{ r,g,b }, text);
        };

    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    const auto exeDir = std::filesystem::path(exePath).parent_path();

    // ---- エンジン側(ヘッダ / 足場 / DirectX12__test.lib) ---- //
    s_SlnDir = exeDir.parent_path().parent_path();
    s_EngineDir = s_SlnDir / "DirectX12__test";
    s_EngineOutDir = exeDir;

    // ---- プロジェクト側 ---- //
    s_ScriptsSrcDir = PROJECT->GetRoot() / "Assets";
    s_ProjPath = PROJECT->GetScriptProjectPath();

    const std::filesystem::path dll = PROJECT->GetLibraryDir() / "Scripts.dll";

    LOG->LogInfo("[Scripts] watch  = " + s_ScriptsSrcDir.string());
    LOG->LogInfo("[Scripts] proj   = " + s_ProjPath.string());
    LOG->LogInfo("[Scripts] dll    = " + dll.string());

    if (!cr_plugin_open(s_plugin, dll.string().c_str()))
    {
        // 初回はまだビルドされていないので普通に起きる。Update 側で開き直す
        LOG->LogInfo("[Scripts] DLL 未生成。ビルド後に開き直します");
        return;
    }
    LOG->LogInfo("[Scripts] cr_plugin_open 成功");
    s_isOpen = true;
}

void ScriptHost::Update(float dt, World* world)
{
    // まだ開けていなければ、DLLの存在を見て開く（初回ビルド/後追い対応）
    if (!s_isOpen)
    {
        const std::filesystem::path dll = PROJECT->GetLibraryDir() / "Scripts.dll";
        if (std::filesystem::exists(dll) &&
            cr_plugin_open(s_plugin, dll.string().c_str()))
        {
            s_isOpen = true;
            LOG->LogInfo("[Scripts] cr_plugin_open 後追い成功: " + dll.string());
        }
    }

    // 0.5秒ごとに変更チェック
    static float acc = 0.0f;
    acc += dt;
    if (acc >= 0.5f) { acc = 0.0f; CheckAndBuild(); }

    DrainBuildResult();

    if (!s_isOpen) return;       // まだ開けてないならここまで
    s_ctx.deltaTime = dt;
    s_ctx.world = world;
    s_ctx.isPlaing = PLAY.isPlaying();

    const unsigned int prevVersion = s_plugin.version;
    cr_plugin_update(s_plugin);

    // ロード/リロードが起きたときだけ結果を出す(毎フレーム出すとログが埋まる)
    if (s_plugin.version != prevVersion)
    {
        LOG->LogInfo("[Scripts] リロード完了 version=" + std::to_string(s_plugin.version)
            + " scripts=" + std::to_string(s_scriptNames.size()));
    }

    static int lastFailure = -1;
    if (static_cast<int>(s_plugin.failure) != lastFailure)
    {
        lastFailure = static_cast<int>(s_plugin.failure);
        if (s_plugin.failure != CR_NONE)
        {
            LOG->LogError("[Scripts] cr failure = " + std::to_string(lastFailure));
        }
    }
}
void ScriptHost::Close()
{
	if (!s_isOpen) return;
	cr_plugin_close(s_plugin);
	s_isOpen = false;
}

const std::vector<std::string>& ScriptHost::GetScriptNames()
{
    return *s_ctx.savedScripts;
}

bool ScriptHost::isOpen()
{
    return s_isOpen;
}
