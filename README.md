# DirectX12 Game Engine / Editor

DirectX 12 を用いた自作ゲームエンジン + エディタ（Windows x64 専用）。
ImGui ベースのエディタ上でシーンを編集し、C++ スクリプトをホットリロードしながら
実行、そのまま実行可能な exe としてビルド出力できます。

## 主な機能

| 分類 | 内容 |
| --- | --- |
| 描画 | 前方描画 / Deferred（GBuffer + DeferredLighting）、PBR + IBL、シャドウマッピング、スカイボックス、ポストプロセス |
| シェーダ | Toon / GenshinToon + アウトライン、Rim、Fresnel、Dissolve、BlinnPhong、LaserBeam などを HLSL で同梱 |
| モデル | Assimp による汎用モデル読み込み、独自 PMX ローダ（MMD）、スキニング、モーフ |
| アニメーション | `Animator`、VMD モーション再生 |
| 物理 | NVIDIA PhysX（`PhysicsWorld`）、Bullet ベースの MMD 剛体（`MmdPhysics`） |
| ECS | 型消去ストレージによる自作 ECS（`World` / `Components.hpp` / `Systems.hpp`） |
| シーン | JSON によるシリアライズ（`SceneSerializer`）、加算ロード / アンロード、フェード遷移 |
| スクリプト | `MonoBehavior` 派生クラスを DLL 化し [cr.h](https://github.com/fungos/cr) でホットリロード。公開フィールドはインスペクタに自動表示 |
| エディタ | ヒエラルキー / インスペクタ / アセットブラウザ / ImGuizmo ギズモ / ログ |
| その他 | 非同期ロード（`AsyncLoader` + `ThreadPool`）、PSO キャッシュ、定数バッファアロケータ、オーディオ、パーティクル（花火） |

## 動作環境

- Windows 10 以降 / x64
- Visual Studio 2022（Platform Toolset **v143**、C++20）
- DirectX 12 対応 GPU

## ビルド

1. `DirectX12__test.sln` を Visual Studio 2022 で開く
2. NuGet パッケージを復元する（`packages.config`：Assimp / DirectXTex / PhysX / BulletSharp）
3. 構成を `Debug|x64` または `Release|x64` にしてビルド・実行

スクリプト DLL のみを再ビルドする場合はリポジトリ直下の `m.bat` を実行します
（`Scripts\Scripts.vcxproj` を msbuild でビルド。VS2022 Community のパスが直書きなので
環境に合わせて書き換えてください）。

## 実行モード

`Application` は 2 つのモードを持ちます。

- **エディタモード**（既定）: ImGui のエディタ UI を表示し、シーンビューとゲームビューを描画
- **ゲームモード**: 起動引数 `-game` を渡すか、exe と同じ階層に `game.cfg` がある場合に有効

`game.cfg` は 2 行のテキストで、1 行目が起動シーン、2 行目がデータフォルダ名です。

```
Assets/Scenes/SampleScene.json
MyGame_Data
```

エディタの Build 機能（`BuildSystem`）が、この `game.cfg`・アセット・必要な DLL を
まとめて出力ディレクトリ（既定 `Build/`）に配置します。

## ディレクトリ構成

```
DirectX12__test/
├── main.cpp                 エントリポイント（WinMain）
├── Application.*            エンジン実装 / エディタとゲームの切り替え
├── Engine/                  Engine 基底クラス、ThreadPool
├── World.hpp                ECS の中核（Entity / Component ストレージ）
├── Components.hpp           コンポーネント定義
├── Systems.hpp              各種システム（描画・スピンなど）
├── Scene* / SceneManager.*  シーン管理・JSON シリアライズ
├── DirectX.*                デバイス・スワップチェーン・コマンド関連
├── GBuffer.* / ShadowMap.* / RenderTexture.*   レンダーパス資源
├── Shader* / Pso*           シェーダ・PSO のロードとキャッシュ
├── *.hlsl / *.hlsli         シェーダ本体
├── ModelLoader.* / PMXLoader.*  モデル読み込み
├── PhysicsWorld.* / MmdPhysics.*  物理
├── ScriptHost.* / MonoBehavior.*  スクリプトホスト（cr.h）
├── EditorWindow.cpp / InspectorWindow.cpp / AssetWindow.cpp  エディタ UI
├── BuildSystem.*            ゲーム exe の出力
├── Assets/                  Model / Texture / Audio / Scenes / Scripts
└── imgui-master, assimp, DirectXTex, PhysicsX, json.hpp  外部ライブラリ
Scripts/                     ホットリロードされるスクリプト DLL プロジェクト
Build/                       ビルド出力サンプル
```

## スクリプトの追加

1. `DirectX12__test/Assets/Scripts/` に `MonoBehavior` を継承したクラスを追加
2. `.cpp` 側に `REGISTER_SCRIPT(クラス名);` を書いて登録する
   （`RegisterFields()` 内で `Field("表示名", メンバ変数)` するとインスペクタに公開される）
3. `m.bat`（または `Scripts.vcxproj` のビルド）で DLL を更新すると、
   実行中のエンジンが自動でリロードし、公開フィールドの値は保持されます
