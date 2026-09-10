#include "BuildSystem.hpp"
#include "Logger.hpp"
#include "Project.hpp"
#include "imguiinit.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <set>
#include <algorithm>
#include <cctype>
#include "json.hpp"

#undef min
#undef max

namespace fs = std::filesystem;

static std::atomic<bool>        s_Building{ false };
static std::mutex               s_LogMutex;
static std::vector<std::string> s_PendingLogs;      // ワーカー→メインスレッド受け渡し
static std::atomic<bool>        s_HasLogs{ false };
static std::atomic<float> s_Progress{ 0.0f };
static std::mutex s_StageMutex;
static std::string s_Stage;

// vcxproj内の <ClCompile Include= の数 ≒ コンパイルされる.cppの総数
static int CountCompileUnits(const fs::path& vcxproj)
{
    std::ifstream ifs(vcxproj);
    if (!ifs) return 0;
    int count = 0;
    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.find("<ClCompile Include=") != std::string::npos)
            count++;
    }
    return count;
}

static void SetStage(float progress, const std::string& stageUtf8)
{
    s_Progress = progress;
    std::lock_guard<std::mutex> lk(s_StageMutex);
    s_Stage = stageUtf8;
}

static const std::string s_msbuild =
"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe";

static void PushLog(std::string msg)
{
    std::lock_guard<std::mutex> lk(s_LogMutex);
    s_PendingLogs.push_back(std::move(msg));
    s_HasLogs = true;
}

// バイト列が正しいUTF-8かどうかの簡易判定
static bool IsValidUtf8(const std::string& s)
{
    int remain = 0;   // 継続バイトの残り数
    for (unsigned char c : s)
    {
        if (remain > 0)
        {
            if ((c & 0xC0) != 0x80) return false;
            remain--;
        }
        else if (c >= 0x80)
        {
            if ((c & 0xE0) == 0xC0) remain = 1;
            else if ((c & 0xF0) == 0xE0) remain = 2;
            else if ((c & 0xF8) == 0xF0) remain = 3;
            else return false;
        }
    }
    return remain == 0;
}

// Shift-JIS(CP932) → UTF-8。既にUTF-8ならそのまま返す
static std::string ToUTF8Smart(const std::string& s)
{
    if (IsValidUtf8(s)) return s;

    int wlen = MultiByteToWideChar(932, 0, s.c_str(), -1, nullptr, 0);
    if (wlen <= 1) return s;
    std::wstring w(wlen - 1, L'\0');
    MultiByteToWideChar(932, 0, s.c_str(), -1, w.data(), wlen);

    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return s;
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

// ScriptHost.cpp:74 LaunchBuild と同じパイプ方式でコマンドを同期実行
// コマンドを実行し、出力を1行ごとに onLine へ通知しながら完了を待つ
static int RunCommand(const std::string& cmd, std::string& output,
    const std::function<void(const std::string&)>& onLine = nullptr)
{
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    CreatePipe(&rd, &wr, &sa, 0);
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back('\0');

    STARTUPINFOA si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};

    int exitCode = -1;
    if (CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(wr); wr = nullptr;

        std::string lineBuf;
        char tmp[4096]; DWORD n = 0;
        while (ReadFile(rd, tmp, sizeof(tmp), &n, nullptr) && n > 0)
        {
            output.append(tmp, n);
            if (!onLine) continue;

            // 改行単位に切り出して通知
            lineBuf.append(tmp, n);
            size_t pos;
            while ((pos = lineBuf.find('\n')) != std::string::npos)
            {
                std::string line = lineBuf.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                onLine(line);
                lineBuf.erase(0, pos + 1);
            }
        }

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
    return exitCode;
}

// exeの場所からソリューション構成を逆算(ScriptHost::Openと同じ発想)
struct ProjectPaths
{
    fs::path exeDir;       // 現在のexeがある場所(x64/Debug 等)
    fs::path slnDir;       // .sln があるルート
    fs::path srcDir;       // エンジンソース(.hlsl / Assets がある場所)
};

static ProjectPaths ResolvePaths()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    ProjectPaths p;
    p.exeDir = fs::path(exePath).parent_path();

    // exeDir から上に辿って .sln を探す
    std::error_code ec;
    for (fs::path d = p.exeDir; !d.empty() && d != d.root_path(); d = d.parent_path())
    {
        for (auto& e : fs::directory_iterator(d, ec))
        {
            if (e.path().extension() == L".sln") { p.slnDir = d; break; }
        }
        if (!p.slnDir.empty()) break;
    }
    p.srcDir = p.slnDir / "DirectX12__test";
    return p;
}

// 総ファイル数を数えてから1ファイルずつコピーし、from~to 区間の進捗を進める
static bool CopyTreeWithProgress(const fs::path& from, const fs::path& to,
    float progressFrom, float progressTo)
{
    std::error_code ec;

    // 1パス目: 総ファイル数を数える
    size_t total = 0;
    for (auto& e : fs::recursive_directory_iterator(from, ec))
        if (e.is_regular_file()) total++;
    if (total == 0) total = 1;

    // 2パス目: コピーしながら進捗更新
    size_t done = 0;
    for (auto& e : fs::recursive_directory_iterator(from, ec))
    {
        fs::path rel = fs::relative(e.path(), from, ec);
        fs::path dest = to / rel;

        if (e.is_directory())
        {
            fs::create_directories(dest, ec);
            continue;
        }
        if (!e.is_regular_file()) continue;

        fs::create_directories(dest.parent_path(), ec);
        fs::copy_file(e.path(), dest, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            PushLog("[Build] コピー失敗: " + e.path().string() + " (" + ec.message() + ")");
            return false;
        }
        done++;
        s_Progress = progressFrom + (progressTo - progressFrom)
            * (float)done / (float)total;
    }
    return true;
}

// ---- 使用アセットの収集 ---- //

// モデルはテクスチャを相対パスで参照するので、ファイル単位ではなくフォルダごと持っていく
static bool IsModelExt(const std::string& ext)
{
    static const char* kModel[] = {
        ".pmx", ".pmd", ".fbx", ".obj", ".gltf", ".glb", ".dae", ".x" };
    for (const char* m : kModel) if (ext == m) return true;
    return false;
}

static std::string ToLowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// 文字列がアセットを指していれば files / dirs に積む。
// ClipPaths のように '|' 区切りで複数入っている場合も分解する
static void AddAssetRef(const std::string& raw, const fs::path& srcDir,
    std::set<fs::path>& files, std::set<fs::path>& dirs)
{
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, '|'))
    {
        if (token.empty()) continue;
        std::replace(token.begin(), token.end(), '\\', '/');

        // 絶対パスでも "Assets/" 以降を相対として扱う
        const size_t pos = ToLowerAscii(token).find("assets/");
        if (pos == std::string::npos) continue;
        const fs::path rel = fs::path(token.substr(pos)).lexically_normal();

        std::error_code ec;
        const fs::path abs = srcDir / rel;
        if (!fs::exists(abs, ec) || !fs::is_regular_file(abs, ec)) continue;

        if (IsModelExt(ToLowerAscii(rel.extension().string())))
            dirs.insert(rel.parent_path());
        else
            files.insert(rel);
    }
}

static void CollectFromJson(const nlohmann::json& j, const fs::path& srcDir,
    std::set<fs::path>& files, std::set<fs::path>& dirs)
{
    if (j.is_string())
    {
        AddAssetRef(j.get<std::string>(), srcDir, files, dirs);
        return;
    }
    if (j.is_array() || j.is_object())
        for (const auto& v : j) CollectFromJson(v, srcDir, files, dirs);
}

// 開始シーンを起点に、参照されているアセットを集める。
// 参照先が .json ならその中身も辿る(タイムライン・プレハブなど)
static bool CollectUsedAssets(const fs::path& srcDir, const std::string& startScene,
    std::set<fs::path>& files, std::set<fs::path>& dirs)
{
    const fs::path sceneRel =
        fs::path("Assets") / "Scenes" / (fs::path(startScene).stem().string() + ".json");

    std::error_code ec;
    if (!fs::exists(srcDir / sceneRel, ec))
    {
        PushLog("[Build] 開始シーンが見つかりません: " + sceneRel.string());
        return false;
    }

    std::vector<fs::path> pending{ sceneRel };
    std::set<fs::path>    visited;

    while (!pending.empty())
    {
        const fs::path rel = pending.back();
        pending.pop_back();
        if (!visited.insert(rel).second) continue;

        files.insert(rel);

        std::ifstream in(srcDir / rel);
        if (!in) continue;
        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (root.is_discarded()) continue;

        std::set<fs::path> found;
        CollectFromJson(root, srcDir, found, dirs);
        for (const auto& f : found)
        {
            files.insert(f);
            if (ToLowerAscii(f.extension().string()) == ".json") pending.push_back(f);
        }
    }
    return true;
}

// 収集した相対パス(ファイル群 + フォルダ群)だけをコピーする
static bool CopySelectedWithProgress(const fs::path& srcDir, const fs::path& dstAssetsRoot,
    const std::set<fs::path>& files, const std::set<fs::path>& dirs,
    float progressFrom, float progressTo)
{
    std::error_code ec;

    // コピー対象を実ファイル一覧に展開する
    std::set<fs::path> targets = files;
    for (const auto& d : dirs)
    {
        for (auto& e : fs::recursive_directory_iterator(srcDir / d, ec))
            if (e.is_regular_file())
                targets.insert(fs::relative(e.path(), srcDir, ec));
    }

    const size_t total = targets.empty() ? 1 : targets.size();
    size_t done = 0;

    for (const auto& rel : targets)
    {
        // rel は "Assets/..." 始まりなので、Assets を1段外して連結する
        const fs::path tail = fs::relative(rel, "Assets", ec);
        const fs::path dest = dstAssetsRoot / tail;

        fs::create_directories(dest.parent_path(), ec);
        fs::copy_file(srcDir / rel, dest, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            PushLog("[Build] コピー失敗: " + rel.string() + " (" + ec.message() + ")");
            return false;
        }
        done++;
        s_Progress = progressFrom + (progressTo - progressFrom) * (float)done / (float)total;
    }

    PushLog("[Build] 使用アセットのみコピー: " + std::to_string(targets.size()) + " ファイル");
    return true;
}

void BuildSystem::Build(const BuildSetting& settings)
{
    if (s_Building.exchange(true)) return;

    std::thread([settings] {
        SetStage(0.02f, IMGUI::ToUTF8("MSBuild 実行中..."));
        PushLog("[Build] ビルド開始 (" + settings.configuration + ")");
        ProjectPaths paths = ResolvePaths();
        std::error_code ec;

        if (paths.slnDir.empty())
        {
            SetStage(0.0f, "");
            PushLog("[Build] .sln が見つかりません。ビルド出力版からは実行できません");
            s_Building = false;
            return;
        }

        // ---- 1. エンジンexe を MSBuild ----
        // スクリプトはプロジェクト単位になり、ソリューションからは外れている。
        // ここではエンジンだけを建て、Scripts.dll は後段で個別に建てる
        fs::path stageDir = paths.slnDir / "x64" / "GameBuild";
        std::string cmd =
            "\"" + s_msbuild + "\" \"" + paths.slnDir.string() + "\\DirectX12__test.sln\""
            " /p:Configuration=" + settings.configuration + " /p:Platform=x64"
            " /p:OutDir=" + stageDir.string() + "\\"
            " /m /nologo /clp:NoSummary /v:minimal";

        constexpr float kMsBuildFrom = 0.02f;
        constexpr float kMsBuildTo = 0.38f;

        const int totalUnits =
            CountCompileUnits(paths.srcDir / "DirectX12__test.vcxproj");

        int   doneUnits = 0;
        SetStage(kMsBuildFrom, ToUTF8Smart("MSBuild 0% (0/" + std::to_string(totalUnits) + ")"));

        std::string out;
        int code = RunCommand(cmd, out, [&](const std::string& line) {
            // コンパイラが出す「ファイル名だけの行」(例: Material.cpp)を数える
            std::string t = line;
            t.erase(0, t.find_first_not_of(" \t"));
            const bool isCppLine =
                t.size() > 4 &&
                t.compare(t.size() - 4, 4, ".cpp") == 0 &&
                t.find(' ') == std::string::npos;      // エラー行等を除外

            if (!isCppLine) return;
            doneUnits++;
            const float ratio = std::min(1.0f, (float)doneUnits / std::max(1, totalUnits));
            const int   pct = (int)(ratio * 100.0f);
            s_Progress = kMsBuildFrom + (kMsBuildTo - kMsBuildFrom) * ratio;
            {
                std::lock_guard<std::mutex> lk(s_StageMutex);
                s_Stage = ToUTF8Smart("MSBuild " + std::to_string(pct) + "% ("
                    + std::to_string(doneUnits) + "/" + std::to_string(totalUnits) + ") " + t);
            }
            });
		SetStage(0.4f, IMGUI::ToUTF8("出力フォルダ作成中..."));

        // ---- 2. 出力フォルダ作成 ----
        fs::path outDir = fs::path(
            reinterpret_cast<const char8_t*>(settings.outputDir.c_str()));
        fs::path dataDir = outDir / (settings.gameName + "_Data");
        fs::path binOutDir = outDir / "Bin";
        fs::create_directories(dataDir / "Shaders", ec);
        fs::create_directories(binOutDir, ec);
        if (ec)
        {
            SetStage(0.0f, "");
            PushLog("[Build] 出力フォルダ作成失敗: " + ec.message());
            s_Building = false;
            return;
        }
		SetStage(0.45f, IMGUI::ToUTF8("exe / DLL コピー中..."));

        // ---- 3. exe はルート、DLL は Bin/ へ ----
        fs::path binDir = stageDir;

        fs::copy_file(binDir / "DirectX12__test.exe",
            outDir / (settings.gameName + ".exe"),
            fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            SetStage(0.0f, "");
            PushLog("[Build] exeコピー失敗: " + ec.message());
            s_Building = false;
            return;
        }

        // ---- Scripts.dll ---- //
        // スクリプトはプロジェクト単位になったので、プロジェクトの Scripts.vcxproj を
        // ここで建てて <Game>_Data/Library/ へ置く。
        // ゲームモードでは PROJECT が開かれず、ScriptHost はカレント(Data)からの
        // 相対 "Library/Scripts.dll" を見るため、この場所でないと読まれない
        {
            SetStage(0.5f, IMGUI::ToUTF8("Scripts.dll ビルド中..."));

            std::string err;
            if (!PROJECT->RefreshScriptProjectSources(err))
            {
                PushLog("[Build] " + err);
            }

            const fs::path scriptProj = PROJECT->GetScriptProjectPath();
            if (!fs::exists(scriptProj))
            {
                PushLog("[Build] 警告: Scripts.vcxproj がありません。スクリプト無しで続行します");
            }
            else
            {
                const std::string scriptCmd =
                    "\"" + s_msbuild + "\" \"" + scriptProj.string() + "\""
                    " /p:Configuration=" + settings.configuration + " /p:Platform=x64"
                    " /p:EngineDir=\"" + paths.srcDir.string() + "\\\\\""
                    " /p:EngineOutDir=\"" + stageDir.string() + "\\\\\""
                    " /nologo /clp:NoSummary /v:minimal";

                std::string scriptOut;
                const int scriptCode = RunCommand(scriptCmd, scriptOut);

                if (scriptCode != 0)
                {
                    PushLog("[Build] Scripts.dll のビルドに失敗 (exit "
                        + std::to_string(scriptCode) + ")");

                    std::istringstream iss(scriptOut);
                    std::string line;
                    while (std::getline(iss, line))
                    {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line.find(": error") != std::string::npos ||
                            line.find(": fatal") != std::string::npos)
                        {
                            PushLog(ToUTF8Smart(line));
                        }
                    }
                }
                else
                {
                    const fs::path scriptsDll = PROJECT->GetLibraryDir() / "Scripts.dll";
                    const fs::path scriptsOutDir = dataDir / "Library";
                    fs::create_directories(scriptsOutDir, ec);
                    ec.clear();

                    fs::copy_file(scriptsDll, scriptsOutDir / "Scripts.dll",
                        fs::copy_options::overwrite_existing, ec);
                    if (ec) PushLog("[Build] Scripts.dll のコピーに失敗: " + ec.message());
                    else    PushLog("[Build] Scripts.dll を配置: " + (scriptsOutDir / "Scripts.dll").string());
                }
            }
        }

        // エンジンが起動時にインポートするDLL(exe横に必須。Bin/ では起動できない)
        const bool isDebug = (settings.configuration == "Debug");

        std::vector<std::string> engineDlls = {
            "PhysX_64.dll",
            "PhysXCommon_64.dll",
            "PhysXFoundation_64.dll",
            "libbulletc.dll",
            // assimp は構成でランタイムが違う(d付きがDebug)
            isDebug ? "assimp-vc142-mtd.dll" : "assimp-vc142-mt.dll",
        };

        for (const std::string& dll : engineDlls)
        {
            // ステージング出力 → 現exe横 → プロジェクト直下 の順に探す
            fs::path src = stageDir / dll;
            if (!fs::exists(src)) src = paths.exeDir / dll;
            if (!fs::exists(src)) src = paths.srcDir / dll;
            if (fs::exists(src))
            {
                fs::copy_file(src, outDir / dll, fs::copy_options::overwrite_existing, ec);
                if (ec) PushLog("[Build] コピー失敗: " + dll + " (" + ec.message() + ")");
            }
            else
            {
                PushLog("[Build] 警告: " + dll + " が見つかりません");
            }
        }

        SetStage(0.6f, IMGUI::ToUTF8("エンジンリソースコピー中..."));

        // ---- 4. エンジン同梱リソースは exe 横の EngineAssets へそのまま ----
        // ユーザーからは見えないが実行には必要。Data/ ではなく outDir 直下に置く
        fs::copy(paths.exeDir / "EngineAssets", outDir / "EngineAssets",
            fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) PushLog("[Build] EngineAssets のコピーに失敗: " + ec.message());

        // ---- 5. プロジェクトの Assets は Data/Assets へ ----
        SetStage(0.55f, IMGUI::ToUTF8("Assets コピー中..."));

        // コピー元は開いているプロジェクト(エンジンのソースツリーではない)
        const fs::path assetsRoot = PROJECT->GetRoot();
        bool assetsOk = false;
        if (settings.usedAssetsOnly)
        {
            PushLog("[Build] 使用アセットを収集中...");
            std::set<fs::path> files, dirs;
            if (CollectUsedAssets(assetsRoot, settings.startScene, files, dirs))
            {
                for (const auto& d : dirs)
                    PushLog("[Build]   フォルダ: " + d.string());

                assetsOk = CopySelectedWithProgress(assetsRoot, dataDir / "Assets",
                    files, dirs, 0.55f, 0.98f);
            }
            else
            {
                // 収集できなかったときは取りこぼすより全部入れる
                PushLog("[Build] 収集に失敗したため Assets を全部コピーします");
                assetsOk = CopyTreeWithProgress(assetsRoot / "Assets", dataDir / "Assets",
                    0.55f, 0.98f);
            }
        }
        else
        {
            PushLog("[Build] Assets をコピー中...");
            assetsOk = CopyTreeWithProgress(assetsRoot / "Assets", dataDir / "Assets",
                0.55f, 0.98f);
        }

        if (!assetsOk)
        {
            SetStage(0.0f, "");
            s_Building = false;
            return;
        }

        // ---- 6. game.cfg 書き出し ----
        // 存在するとゲームモード起動。1行目=開始シーン, 2行目=Dataフォルダ名
        const std::string sceneName =
            fs::path(settings.startScene).stem().string();   // "Assets/Scenes/Foo.json" → "Foo"
        {
            std::ofstream cfg(outDir / "game.cfg");
            cfg << sceneName << "\n";
            cfg << settings.gameName + "_Data" << "\n";
        }

		SetStage(1.0f, IMGUI::ToUTF8("完了"));
        PushLog("[Build] 完了: " + fs::absolute(outDir).string());
        s_Building = false;
        }).detach();
}

void BuildSystem::Update()
{
    if (!s_HasLogs.exchange(false)) return;

    std::vector<std::string> logs;
    {
        std::lock_guard<std::mutex> lk(s_LogMutex);
        logs.swap(s_PendingLogs);
    }
    for (auto& l : logs)
    {
		std::string utf8 = ToUTF8Smart(l);
        if (l.find("失敗") != std::string::npos ||
            l.find(": error") != std::string::npos ||
            l.find(": fatal") != std::string::npos)
            LOG->LogError(utf8);
        else
            LOG->LogInfo(utf8);
    }
}

bool BuildSystem::IsBuilding() { return s_Building; }

float BuildSystem::GetProgress() { return s_Progress; }

std::string BuildSystem::GetStage()
{
    std::lock_guard<std::mutex> lk(s_StageMutex);
    return s_Stage;
}
