# DirectX12__test

自作 DirectX 12 ゲームエンジン + エディタ（学習目的）。C++17 / Visual Studio 2022 / x64。

## 進め方（重要）

- **コードは提示するだけ。ファイルは編集しない。** ユーザーが自分で書いて学ぶため。
  「〜したい」「作って」「組み込みたい」は *設計と提示* の依頼であって、Write/Edit の許可ではない。
  ディスクに反映するのは「ファイルに書いて」「保存して」「編集して」と明示されたときだけ。迷ったら聞く。
- **コードには必ず場所を添える。** 「`DirectX.cpp:823` 付近」「`GBuffer.hpp` のメンバに追加」のように、
  ファイル名と行番号／挿入位置をブロックごとに書く。ファイル単位でまとめる。位置なしのコード投下は禁止。
- 調査のための読み取り・検索は自由。

## 構成

```
DirectX12__test.sln
├ DirectX12__test/        エンジン本体（exe）
│  ├ main.cpp             WinMain。-game 引数 or game.cfg でゲームモード起動
│  ├ Application.*        Engine を継承したシングルトン（APPLICATION マクロ）
│  ├ Engine/              Engine 基底、ThreadPool
│  ├ DirectX.*            DirectXApp: デバイス/スワップチェイン/PSO/描画パス
│  ├ World.hpp            ECS の World（ECS/World.hpp は空。実体はこっち）
│  ├ Components.hpp       コンポーネント定義 / ComponentRegistry.cpp で登録
│  ├ Systems.hpp          Render/Light/Script/Transform/Animator/Particle… 各 System
│  ├ MonoBehavior.*       スクリプト基底（OnStart/OnUpdate/OnDraw/OnCollisionEnter…）
│  ├ ScriptHost.*         cr.h による Scripts.dll ホットリロード
│  ├ FramePipeline.*      フレームパイプライン（FrameAllocator / FrameObject）
│  ├ *.hlsl / *.hlsli     シェーダー（実行時コンパイル）
│  └ Assets/              Model / Texture / Audio / Scenes / Scripts
└ Scripts/                ゲームスクリプト（Scripts.dll）
```

外部ライブラリ: assimp, Bullet, PhysX, ImGui + ImGuizmo, DirectXTex, nlohmann/json, cr.h。

## ビルドの落とし穴

### ゲーム起動中に msbuild を叩かない
`ScriptHost.cpp` が `Assets/Scripts` を 0.5 秒ごとに監視し、変更を検知すると
バックグラウンドで `Scripts/Scripts.vcxproj` を MSBuild → cr が `Scripts.dll` をホットリロードする。
ここに手動ビルドを重ねると衝突する:
- exe ビルド → LNK1104（exe/obj がロック中）
- Scripts ビルド → C1041（`vc143.pdb` を 2 つの CL.EXE が同時オープン）

**スクリプト変更は保存するだけでよい。** exe を再ビルドしたいときはゲームを閉じてもらう。

### スクリプトからエンジンを呼ぶ制限
Scripts.dll はヘッダ inline なエンジンコードしか呼べない（テンプレート、`World::GetComponent`、`transform()` など）。
本体が exe 側の .cpp にしかない関数（`Logger::GetInstance` / `LogInfo` 等）は DLL 側で LNK2019 になる。
スクリプト内では `OutputDebugStringA` を使うか、エンジン側でシンボルを export する。

### HLSL は実行時コンパイル
シェーダーは `ShaderLibrary::Load` → `Shader::LoadFromFile` で `D3DCompileFromFile` / DXC により実行時にコンパイルされる。
ビルド時の FxCompile は不要で、`.cso` は使っていない。

- `.hlsl` / `.hlsli` は **`.vcxproj` と `.vcxproj.filters` の両方で `<None>` のまま**にする。
- VS はシェーダー追加時に自動で `<FxCompile>` にしてしまう。そうなると
  プロジェクト既定の `<ShaderModel>6.8</ShaderModel>` で dxc が走り、`EntryPointName` 未指定のため `main` を探して失敗する
  （実際のエントリは `PbrPS` / `BasicVS` など）→ 「missing entry point」「dxc.exe exited with code 1」。
- シェーダーを追加したら `DirectXApp::RegisterBuiltinShaders`（[DirectX.cpp:971](DirectX12__test/DirectX.cpp:971)）に 1 行足す。
- 実行時はプロジェクトディレクトリ（デバッグ作業ディレクトリ）から読むので、出力コピーは不要。

## 現在の作業: FramePipeline ブランチ

CEDEC2022 トイロジック「FramePipelineSystem」を段階導入中。

**完了**: `DirectXApp` の記録/実行分離。`Defines.hpp` の `#define _FRAMEPIPELINE` で
旧パス（`BeginRender`/`EndRender`）と新パス（`BeginFrameRecord`/`CloseFrameRecord`/`ExecuteAndPresent`）を切り替え。
コマンドリストはスロットごとに 3 本（キーは `frameNumber % RTV_NUM`）。
バックバッファ番号は予測式で、Present 後に実測値で再同期する自己同期方式（初期アライメント依存を排除）。
共通描画関数は `Cmd()` / `RecordSlot()` / `BackBufferIndex()` ヘルパー経由で両モードから共有。動作確認済み。

**次のステップ**:
1. `ExecuteAndPresent` の FrameThread 化（`m_SyncFrameNumber` / `m_SyncBackBuffer` を atomic 化 or mutex 保護が必要）
2. `FramePipeline` / `FrameObject` クラス導入
3. Game / Render 分離

ImGui がシングルトンなので、当面パイプライン化はゲームモード（`-game`）限定の方針。

## 規約

- 型エイリアスは `Defines.hpp`（`float3` = `XMFLOAT3`、`matrix` = `XMMATRIX`、`ComPtr`、`MakeUnique` など）。
  演算子オーバーロードもここ。新規の共通定義はここに置く。
- バックバッファ数は `RTV_NUM`（= 3）。ハードコードしない。
- ファイル先頭に Doxygen 風のヘッダコメント（`\file` / `\brief` / 作成者 / 作成日 / 更新履歴）を付ける。
- ソースは Shift-JIS。コメントは日本語。
- メンバ変数は `m_PascalCase`。
