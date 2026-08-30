# CLAUDE.md

このリポジトリで作業する際のガイド。概要・機能一覧・ビルド手順は `README.md` を参照。

## 環境に関する重要な前提

- **Windows / MSVC 専用プロジェクト**。Linux 上のセッション（Claude Code on the web など）では
  **ビルドも実行もできない**。コード変更は「読んで書いてプッシュする」までで、
  最終的なビルド確認はユーザーが Visual Studio 2022 で行う。
  ビルドできていないものを「動作確認済み」と報告しないこと。
- ソースファイルの多くは **Shift_JIS (CP932)** で保存されている。
  そのまま `cat` すると日本語コメントが文字化けする。読むときは
  `iconv -f CP932 -t UTF-8 <file>` を通す。
  **既存ファイルを編集するときは元のエンコーディングを壊さないこと**（UTF-8 で上書き保存しない）。
  新規の `.md` は UTF-8 で問題ない。
- テストフレームワークや CI は存在しない。検証手段はレビューと静的な読解のみ。

## 外部ライブラリ（原則として編集しない）

`DirectX12__test/` 直下にベンダリングされている以下は自前のコードではない。
バグ修正や機能追加をここに入れないこと。

- `imgui-master/`, `imgui*.cpp/h`, `ImGuizmo.*`
- `assimp/`, `DirectXTex/`, `PhysicsX/`, `lib/`
- `json.hpp`（nlohmann/json）, `cr.h`, `d3dx12.h`, `imstb_*.h`

自前のラッパは `imguiinit.*`（ImGui 初期化）などに分かれている。

## アーキテクチャの要点

- **エントリ**: `main.cpp` (WinMain) → `Application`（`Engine` の派生、シングルトン `APPLICATION`）。
  `Engine::Init/Run/Terminate` が骨格で、`OnInit/OnUpdate/OnShutDown/ConfigureContext` を
  `Application` が実装する。
- **ECS**: `World.hpp` が中核。`Entity` は `std::uint32_t`（`0` は `INVALID_ENTITY`）。
  コンポーネントは型ごとの型消去ストレージ（`std::unordered_map<Entity, T>`）に保持され、
  走査は `world.Each<A, B>([](Entity, A&, B&){ ... })`。
  コンポーネント定義は `Components.hpp`、システムは `Systems.hpp`。
  新しいコンポーネントを追加したら `ComponentRegistry` と `SceneSerializer` の対応も必要かを確認する。
- **シーン**: `Scene` / `RuntimeScene` / `SceneManager`。シーンは
  `Assets/Scenes/<name>.json` に JSON で保存され、`SceneSerializer` が入出力する。
  加算ロード・アンロード・フェード遷移（`TransitionPhase`）に対応。
- **描画**: `DirectX.cpp`（デバイス・スワップチェーン・コマンド）を土台に、
  `GBuffer` / `ShadowMap` / `RenderTexture` の各パスを `RenderContext` 経由で構成する。
  PSO は `PsoRegistry` / `PipelineStateCache`、シェーダは `ShaderLibrary` が管理する。
  シェーダを追加したら PSO 登録側もセットで見ること。
- **スクリプト**: `Scripts/` が別 DLL プロジェクトで、`cr.h` によりホットリロードされる。
  ゲーム側スクリプトの実体は `DirectX12__test/Assets/Scripts/`。
  `MonoBehavior` を継承 →`.cpp` に `REGISTER_SCRIPT(型名);` →
  `RegisterFields()` 内の `Field("名前", 変数)` がインスペクタに出る。
  リロード時は `cr_main` の `CR_LOAD` で behaviors が再構築され、フィールド値は
  `ScriptComponent::values` から復元される。ここのライフタイムは壊しやすいので慎重に。
- **エディタ / ゲームの分岐**: `Application::m_GameMode`。`-game` 引数か
  exe 隣の `game.cfg` の存在で決まる。`BuildSystem::Build` が exe・アセット・DLL・
  `game.cfg` を出力ディレクトリへ配置する。
- **非同期**: `AsyncLoader` + `Engine/ThreadPool`。D3D12 リソース生成はスレッド安全性に注意。

## コーディング規約（既存コードに合わせる）

- C++20 / `stdcpp20`。ヘッダは `#pragma once`。
- インデントは**タブ**。`{` は次の行に置く（Allman）。
- 命名: クラス `PascalCase`、メンバ変数 `m_PascalCase`、グローバル `g_xxx`、
  ローカル・引数 `camelCase`、メソッド `PascalCase`。
- 出力・入出力の意図を示す SAL 注釈（`_In_`, `_Out_`）を既存コードは付けている。踏襲する。
- 主要なヘッダ先頭には Doxygen 風のファイルコメント（`\file` / `\brief` / 作成者 / 更新履歴）がある。
  既存ファイルを大きく変更したときは更新履歴に追記する。
- コメントは日本語。周囲のコメント密度に合わせる。
- ログは `Debug::Log` / `LOG->LogInfo` 系（`Logger.hpp` / `Debug.hpp`）を使う。

## 変更するときの注意

- D3D12 リソースは `ComPtr` 管理。アップロードバッファやコマンドリストの寿命は
  過去にリークとクラッシュの修正が入っている箇所なので、
  `ConstantBufferAllocator` / `AsyncLoader` / `DirectX.cpp` を触るときは GPU 完了待ちの
  フェンス処理を必ず確認する。
- `Assets/` 配下のバイナリ（モデル・テクスチャ・音声）と `Build/` の生成物は基本的に触らない。
- ブランチ運用は指示されたブランチのみに push する。

## コミットメッセージの規約

`[プレフィックス]内容` の形式で書く。プレフィックスと内容の間にスペースは入れない。
内容は日本語で、何を変更したかが分かるように簡潔に書く。

| プレフィックス | 用途 | 例 |
| --- | --- | --- |
| `[Feat]` | 新規機能追加 | `[Feat]プレイヤーの移動追加` |
| `[Fix]` | バグ修正 | `[Fix]プレイヤーのキー入力ができない問題を修正` |
| `[Refactor]` | 挙動を変えない内部整理 | `[Refactor]プレイヤーの内部処理を整理` |
| `[Docs]` | コメント・ドキュメント修正 | `[Docs]コメント追加` |
| `[Chore]` | 雑務的変更（git 管理など） | `[Chore]ignore設定変更` |
