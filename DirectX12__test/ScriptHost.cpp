#define CR_HOST CR_DISABLE

// cr.h の失敗理由(LoadLibrary の GetLastError など)をエンジンのログへ流す。
// 既定は CR_DEBUG のときだけ stderr へ出るので、ウィンドウアプリでは捨てられて見えない
#include "Debug.hpp"
#include <cstdio>
#include <cstdarg>
static void CrLogPrintf(const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	OutputDebugStringA(buf);
	LOG->LogError(std::string("[cr] ") + buf);
}
#define CR_DEBUG
#define CR_ERROR(...) CrLogPrintf(__VA_ARGS__)
#define CR_LOG(...)   ((void)0)
#define CR_TRACE
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
#include "PrefabLibrary.hpp"
#include "Project.hpp"
#include "SceneManager.hpp"

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
static std::filesystem::path s_ExePath;         // ABI の新しさを比べる基準
static bool s_AutoBuild = false;                // ゲームモードではビルドしない
static SceneManager* s_SceneManager = nullptr;  // シーン遷移の橋渡し先
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

/// @brief Assets 配下のスクリプト(.cpp/.hpp)の最終更新時刻を返す
/// @note  起動時の陳腐化判定で使う。ファイルが1つも無ければ既定値(0)を返す
static std::filesystem::file_time_type NewestScriptTime()
{
    std::error_code ec;
    std::filesystem::file_time_type maxT{};

    // スクリプトは Assets のどこに置いてもよいので再帰で見る
    for (auto& e : std::filesystem::recursive_directory_iterator(s_ScriptsSrcDir, ec))
    {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;

        const auto ext = e.path().extension();
        if (ext != ".cpp" && ext != ".hpp") continue;

        const auto t = std::filesystem::last_write_time(e, ec);
        if (!ec && t > maxT) maxT = t;
    }
    return maxT;
}

std::filesystem::path ScriptDllPath()
{
    std::error_code ec;
    return  std::filesystem::absolute(
		PROJECT->GetLibraryDir() / "Scripts.dll", ec
    );
}

/// @brief cr に渡すパス。
/// @note  cr は受け取った文字列を UTF-8 として MultiByteToWideChar する
///        (cr.h の cr_utf8_to_wstring)。path::string() は MSVC では ANSI(CP932)
///        なので、フォルダ名に日本語が入ると変換が壊れ、cr_exists が false になって
///        cr_plugin_open が何もログを出さずに失敗する
std::string ScriptDllPathForCr()
{
    return PathToUtf8(ScriptDllPath());
}

static void CheckAndBuild()
{
    // 配布した exe に MSBuild は無い。プロジェクトが開いている=エディタのときだけ回す
    if (!s_AutoBuild) return;
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

void ScriptHost::SetSceneManager(SceneManager* sceneManager)
{
    s_SceneManager = sceneManager;
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

    // プレハブ生成。PrefabLibrary も Scene も exe 側にあるのでここで橋渡しする
    s_ctx.instantiate = [](const char* prefabName) -> std::uint32_t
        {
            auto* rs = RuntimeScene::Current();
            if (rs == nullptr || prefabName == nullptr) return INVALID_ENTITY;

            if (!PrefabLibrary::Get().HasPrefab(prefabName))
            {
                LOG->LogWarning(std::string("Instantiate: プレハブが見つかりません: ") + prefabName);
                return INVALID_ENTITY;
            }
            return PrefabLibrary::Get().Instantiate(prefabName, *rs, rs->GetWorld());
        };

    // 破棄は即時にしない。更新中のストレージを壊さないようフレーム末へ回す
    s_ctx.destroyEntity = [](std::uint32_t entity)
        {
            if (auto* rs = RuntimeScene::Current())
                rs->GetWorld().DestroyEntityDeferred(static_cast<Entity>(entity));
        };

    s_ctx.loadScene = [](const char* sceneName, bool withFade)
        {
            if (s_SceneManager == nullptr || sceneName == nullptr) return;

            // 未登録の名前は LoadScene が弾くので、先に登録しておく
            s_SceneManager->RegisterScene(sceneName);

            if (withFade) s_SceneManager->RequestSceneChangeWithFade(sceneName);
            else          s_SceneManager->LoadScene(sceneName);
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

    s_ExePath = exeDir / std::filesystem::path(exePath).filename();
    s_AutoBuild = PROJECT->IsOpen();

    const std::filesystem::path dll = ScriptDllPath();

    // ---- ABI の食い違いを防ぐ ---- //
    // Scripts.dll はエンジンのヘッダをそのまま取り込んでいるので、
    // エンジンを作り直したのに DLL が古いままだと MonoBehavior の
    // レイアウトがずれて即クラッシュする。
    // CheckAndBuild はスクリプトの更新しか見ないため、ここで明示的に比べる
    if (s_AutoBuild)
    {
        std::error_code ec;
        const bool dllMissing = !std::filesystem::exists(dll, ec);
        bool dllStale = false;

        bool srcNewer = false;

        if (!dllMissing)
        {
            const auto dllTime = std::filesystem::last_write_time(dll, ec);
            const auto exeTime = std::filesystem::last_write_time(s_ExePath, ec);
            if (!ec) dllStale = exeTime > dllTime;

            // エディタを閉じている間に追加・編集されたスクリプトを拾う。
            // CheckAndBuild は「初回は基準値を取るだけ」で起動直後にビルドしないので、
            // ここで見ないと次に誰かがファイルを保存するまで永久に取り込まれない
            const auto srcTime = NewestScriptTime();
            if (srcTime.time_since_epoch().count() != 0 && srcTime > dllTime)
            {
                srcNewer = true;
                dllStale = true;
            }
        }

        if (dllStale)
        {
            // 消してから作り直す。残したままだと下の cr_plugin_open や
            // Update の後追いオープンが、古いABIのDLLを掴んでクラッシュする
            LOG->LogInfo(srcNewer
                ? "[Scripts] スクリプトの方が新しいので Scripts.dll を作り直します"
                : "[Scripts] エンジンの方が新しいので Scripts.dll を作り直します");
            std::filesystem::remove(dll, ec);
        }

        if (dllMissing || dllStale)
        {
            LaunchBuild();
        }
    }

    // ログは UTF-8 で書く。path::string() は ANSI(CP932) なので混ざると化ける
    LOG->LogInfo("[Scripts] watch  = " + PathToUtf8(s_ScriptsSrcDir));
    LOG->LogInfo("[Scripts] proj   = " + PathToUtf8(s_ProjPath));
    LOG->LogInfo("[Scripts] dll    = " + PathToUtf8(dll));

    if (!cr_plugin_open(s_plugin, ScriptDllPathForCr().c_str()))
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
        // ビルド中は開かない。
        // リンカがファイルを作った瞬間に exists が true になるため、
        // 書き込み途中の DLL を掴んで CR_BAD_IMAGE になる
        const std::filesystem::path dll = ScriptDllPath();
        if (!s_building && std::filesystem::exists(dll) &&
            cr_plugin_open(s_plugin, ScriptDllPathForCr().c_str()))
        {
            s_isOpen = true;
            LOG->LogInfo("[Scripts] cr_plugin_open 後追い成功: " + PathToUtf8(dll));
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

    // 失敗したら閉じてやり直す。
    // 特に CR_INITIAL_FAILURE は「壊れたプラグインは以後リロードしない」なので、
    // 放置するとスクリプトが二度と動かない
    if (s_plugin.failure != CR_NONE)
    {
        LOG->LogError("[Scripts] cr failure = "
            + std::to_string(static_cast<int>(s_plugin.failure))
            + " (開き直します)");

        // CR_BAD_IMAGE(=LoadLibrary 失敗)の原因モジュールを特定する。
        // ERROR_MOD_NOT_FOUND(126) は「依存のどれかが見つからない」としか言わないので、
        // 直接依存を1つずつ当たって、落ちている名前をログに残す。
        // 1回だけ出せば十分(毎フレーム失敗するとログが埋まる)
        static bool s_probed = false;
        if (!s_probed && s_plugin.failure == CR_BAD_IMAGE)
        {
            s_probed = true;

            wchar_t self[MAX_PATH]{};
            GetModuleFileNameW(nullptr, self, MAX_PATH);
            LOG->LogError("[Scripts] probe: 実行中のexe = " + WideToUtf8(self));

            wchar_t cwd[MAX_PATH]{};
            GetCurrentDirectoryW(MAX_PATH, cwd);
            LOG->LogError("[Scripts] probe: カレント = " + WideToUtf8(cwd));

            // Scripts.dll が直接インポートしているモジュール
            const wchar_t* deps[] = {
                L"DirectX12__test.exe",
                L"MSVCP140.dll",
                L"VCRUNTIME140.dll",
                L"VCRUNTIME140_1.dll",
                L"KERNEL32.dll",
            };
            for (const wchar_t* d : deps)
            {
                const bool loaded = (GetModuleHandleW(d) != nullptr);
                std::string line = "[Scripts] probe: " + WideToUtf8(d)
                    + (loaded ? " = ロード済み" : " = 未ロード");
                if (!loaded)
                {
                    HMODULE h = LoadLibraryW(d);
                    if (h) { line += " / 単体ロードは成功"; FreeLibrary(h); }
                    else   { line += " / 単体ロードも失敗 err=" + std::to_string(GetLastError()); }
                }
                LOG->LogError(line);
            }
        }

        cr_plugin_close(s_plugin);
        s_isOpen = false;
        s_plugin.failure = CR_NONE;
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
