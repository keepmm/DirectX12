#pragma once

#include "Defines.hpp"
#include "RenderContext.hpp"
#include <memory>
#include <vector>
#include <string>
#include "ScriptField.hpp"
#include "AudioEngine.hpp"
#include "ModelData.hpp"

class Mesh;
class Material;
class Camera;
class MonoBehavior;
class MmdPhysics;

namespace PhysX
{
	class PxRigidActor;
	class PxShape;
}

struct TransformComponent
{
	POSITION position{ 0.0f, 0.0f, 0.0f };
	QUATERNION rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
	SCALE scale{ 1.0f,1.0f,1.0f };

	float3 EulerAngles{ 0.0f,0.0f,0.0f };

	Entity parent = INVALID_ENTITY;
	float4x4 world{};

	void Reflect(FieldList& f)
	{
		f.Add("Position", position);
		f.Add("Rotation", rotation);
		f.Add("Scale", scale);
	}

	/// @brief Euler -> Quaternion変換
	void ApplyEuler()
	{
		using namespace DirectX;
		const float pitch = XMConvertToRadians(EulerAngles.x);
		const float yaw = XMConvertToRadians(EulerAngles.y);
		const float roll = XMConvertToRadians(EulerAngles.z);
		XMVECTOR q = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
		XMStoreFloat4(&rotation, q);
	}

	/// @brief Quaternion -> Euler変換
	void SyncEulerFromQuaternion()
	{
		using namespace DirectX;
		XMFLOAT4 q = rotation;
		float sinp = 2.0f * (q.w * q.x - q.y * q.z);
		float pitch = (fabsf(sinp) >= 1.0f) ? copysignf(XM_PIDIV2, sinp) : asinf(sinp);
		float yaw = atan2f(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
		float roll = atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.z * q.z + q.x * q.x));
		EulerAngles = { XMConvertToDegrees(pitch), XMConvertToDegrees(yaw), XMConvertToDegrees(roll) };
	}

	/// @brief positionの値を設定
	/// @param pos POSITION型の値 (x , y , x)
	void SetPosition(
		_In_ const POSITION& pos) { position = pos;}

	/// @brief positionの値を設定
	/// @param x float
	/// @param y float
	/// @param z float
	void SetPosition(
		_In_ float x, 
		_In_ float y, 
		_In_ float z) { position = POSITION{ x,y,z };}

	/// @brief positionの値を加算する
	/// @param delta POSITION型の値 (x , y , x)
	void Translate(
		_In_ const POSITION& delta) {
		position.x += delta.x; position.y += delta.y; position.z += delta.z;
	}

	/// @brief positionの値を加算する
	/// @param x float
	/// @param y float
	/// @param z float
	void Translate(
		_In_ float x,
		_In_ float y,
		_In_ float z) {
		position.x += x; position.y += y; position.z += z;
	}


	void RebuildWorld()
	{
		const matrix scaleMatrix = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z);

		const auto rot = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&rotation));
		DirectX::XMStoreFloat4(&rotation, rot);
		const matrix rotMatrix = DirectX::XMMatrixRotationQuaternion(rot);
		const matrix transMatrix = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
		const matrix worldMatrix = scaleMatrix * rotMatrix * transMatrix;
		DirectX::XMStoreFloat4x4(&world, worldMatrix);
	}
};

struct SpinComponent
{
	float angle = 0.0f;
	float speed = 1.0f;

	void Reflect(FieldList& f)
	{
		f.Add("Angle", angle);
		f.Add("Speed", speed);
	}
};

struct MeshComponent
{
	std::shared_ptr<Mesh> mesh;
	std::string FilePath;
	float scale = 1.0f;

	void Reflect(FieldList& f)
	{
		f.Add("FilePath", FilePath);
		f.AddRange("Scale", scale, 0.01f, 100.0f);

		std::string name = "Mesh";
		f.AddTexture(name, FilePath, mesh);
	}
};

struct SubMaterialRestore
{
	std::string shaderName;
	float roughness = 0.5f;
	float metallic = 0.0f;
	float sssStrength = 0.0f;
	float sssWrap = 0.4f;
	float sssTrans = 0.0f;
	float sheen = 0.0f;
	COLOR sssColor = { 0.9f,0.35f,0.25f,1.0f };
	float baseAlpha = 1.0f;
};

struct MaterialComponent
{
	std::shared_ptr<Material> material;	// 単一
	std::vector<std::shared_ptr<Material>> materials;	// 複数
	std::vector<std::string> materialnames;	// 複数
	ID3D12PipelineState* overridePso = nullptr;

	std::string FilePath;
	std::string RampFilePath;
	std::string shaderName = "Basic";
	std::vector<SubMaterialRestore> pendingSubs;

	void Reflect(FieldList& f)
	{
		f.Add("FilePath", FilePath);
		f.Add("RampFilePath", RampFilePath);
		f.Add("ShaderName", shaderName);
		f.AddTexture("Texture", FilePath, material);
	}
};

struct RigidBodyComponent
{
	PhysX::PxRigidActor* actor = nullptr;
	float mass = 1.0f;
	bool isKinematic = false;
	bool isStatic = false;
	bool useGravity = true;

	void Reflect(FieldList& f)
	{
		f.AddRange("Mass", mass, 0.01f, 1000.0f);
		f.Add("IsKinematic", isKinematic);
		f.Add("IsStatic", isStatic);
		f.Add("UseGravity", useGravity);
	}
};

struct ColliderComponent
{
	enum class ShapeType
	{
		Box,
		Sphere,
		Capsule,
		Mesh
	} shapeType = ShapeType::Box;
	SCALE size{ 1.0f, 1.0f, 1.0f };
	float radius = 0.5f;
	float friction = 0.5f;
	float restitution = 0.5f;
	float density = 1.0f;
	bool isTrigger = false;

	PhysX::PxShape* shape = nullptr;
	bool isShow = false;

	void Reflect(FieldList& f)
	{
		f.AddEnum("ShapeType", (int&)shapeType, { "Box", "Sphere", "Capsule", "Mesh" });
		f.Add("Size", size);
		f.AddRange("Radius", radius, 0.01f, 100.0f);
		f.AddRange("Friction", friction, 0.0f, 1.0f);
		f.AddRange("Restitution", restitution, 0.0f, 1.0f);
		f.AddRange("Density", density, 0.01f, 1000.0f);
		f.Add("IsTrigger", isTrigger);
		f.Add("IsShow", isShow);
	}
};

struct CameraComponent
{
	/// @brief 描画タイプ
	enum class Projection
	{
		Perspective,
		Orthographic
	} projection = Projection::Perspective;

	enum class CameraType
	{
		Main,
		Secondary
	} cameraType = CameraType::Main;
	float orhoSize = 10.0f;
	float fovY = 60.0f;
	float nearZ = 0.1f;
	float farZ = 100.0f;
	bool isActive = true;

	float4x4 view{};
	float4x4 proj{};

	void Reflect(FieldList& f)
	{
		f.AddEnum("Projection", (int&)projection, { "Perspective", "Orthographic" });
		f.AddEnum("CameraType", (int&)cameraType, { "Main", "Secondary" });
		f.AddRange("OrthoSize", orhoSize, 0.01f, 100.0f);
		f.AddRange("FovY", fovY, 1.0f, 179.0f);
		f.AddRange("NearZ", nearZ, 0.01f, 100.0f);
		f.AddRange("FarZ", farZ, 1.0f, 10000.0f);
		f.Add("IsActive", isActive);
	}
};

struct CameraAnimationComponent
{
	std::string vmdPath;
	bool  playing = false;
	bool  loop = false;
	float time = 0.0f;

	CameraClip clip;      // 実行時データ(シリアライズ対象外)
	bool loaded = false;

	void Reflect(FieldList& f)
	{
		f.Add("VmdPath", vmdPath);
		f.Add("Playing", playing);
		f.Add("Loop", loop);
	}
};

struct FreeLookComponent
{
	float moveSpeed = 5.0f;
	float rotateSpeed = 0.0025f;
	float yaw = 0.0f;
	float pitch = 0.0f;
	bool Enabled = true;

	void Reflect(FieldList& f)
	{
		f.AddRange("MoveSpeed", moveSpeed, 0.01f, 100.0f);
		f.AddRange("RotateSpeed", rotateSpeed, 0.0001f, 1.0f);
		f.AddRange("Yaw", yaw, -360.0f, 360.0f);
		f.AddRange("Pitch", pitch, -89.0f, 89.0f);
		f.Add("Enabled", Enabled);
	}
};

struct MusicSyncComponent
{
	float offset = 0.0f;        // モーションを曲に対して前後させる(秒、+で遅らせる)
	bool  syncAnimators = true; // 全AnimatorをこのAudioに同期
	bool  syncCamera = true;    // カメラVMDも同期
	float musicTime = 0.0f;
	// ランタイム状態(シリアライズ対象外)
	std::uint64_t startSamples = 0;
	bool started = false;
	float seekBase = 0.0f;			// シーク先の曲位置(秒)
	bool  resyncRequested = false;	// startSamplesを取り直す

	void Reflect(FieldList& f)
	{
		f.Add("Offset", offset);
		f.Add("SyncAnimators", syncAnimators);
		f.Add("SyncCamera", syncCamera);
	}
};

struct LightComponent
{
	enum class LightType
	{
		Directional,
		Point,
		Spot,
		Laser
	} type = LightType::Directional;

	enum class SwingAxis : uint8_t
	{
		Pan,
		Tilt,
		PanTilt
	};
	COLOR color{ 1.0f, 1.0f, 1.0f, 1.0f };
	COLOR ambientColor{ 0.2f, 0.2f, 0.2f, 1.0f };
	float intensity = 1.0f;
	float range = 10.0f;
	POSITION direction{ 0.0f, -1.0f, 0.0f };
	float spotAngle = 45.0f;
	bool isActive = true;

	bool ShowBeam = false;
	float beamWidth = 0.05f;

	bool swingEnable = false;
	SwingAxis swingAxis = SwingAxis::Pan;
	float swingSpeed = 45.0f;
	float swingAngle = 30.0f;

	COLOR beamColorEnd{ 1.0f, 1.0f, 1.0f, 1.0f };
	float glowPower = 6.0f;
	float glowIntensity = 3.0f;
	float volumetricIntensity = 1.0f;

	void Reflect(FieldList& f)
	{
		f.AddEnum("Type", (int&)type, { "Directional", "Point", "Spot","Laser"});

		f.Add("Color", color);
		f.Add("AmbientColor", ambientColor);
		f.AddRange("Intensity", intensity, 0.0f, 10.0f);
		f.AddRange("Range", range, 0.0f, 100.0f);
		f.Add("Direction", direction);
		f.AddRange("SpotAngle", spotAngle, 1.0f, 179.0f);
		f.Add("IsActive", isActive);
		f.Add("IsShow", isShow);
		f.Add("ShowBeam", ShowBeam);
		f.AddRange("BeamWidth", beamWidth, 0.01f, 2.0f);

		f.Add("SwingEnabled", swingEnable);
		f.AddEnum("SwingAxis", (int&)swingAxis, { "Pan", "Tilt", "PanTilt" });
		f.AddRange("SwingSpeed", swingSpeed, 0.0f, 360.0f);
		f.AddRange("SwingAngle", swingAngle, 0.0f, 90.0f);

		f.Add("BeamColorEnd", beamColorEnd);
		f.AddRange("GlowPower", glowPower, 1.0f, 32.0f);
		f.AddRange("GlowIntensity", glowIntensity, 0.1f, 20.0f);
		f.AddRange("VolumetricIntensity", volumetricIntensity, 0.0f, 10.0f);
	}
	bool castShadows = true;
	bool isShow = false;
};


struct NameComponent
{
	std::string name;
};


struct PrefabComponent
{
	std::string name;
	std::string guid;
};


struct SpriteComponent
{
	std::wstring texturePath;
	float2 size{ 1.0f, 1.0f };
	std::shared_ptr<Material> material;

	void Reflect(FieldList& f)
	{
		f.Add("TexturePath", texturePath);
		f.Add("Size", size);
	}
};


struct ScriptComponent
{
	std::vector<std::string> scriptNames;
	std::vector<std::unique_ptr<MonoBehavior>> behaviors;

	std::unordered_map<std::string, std::vector<FieldDesc>>                        fieldDescs; // 表示用メタ
	std::unordered_map<std::string, std::unordered_map<std::string, FieldValue>>   values;     // 値
};

struct CanvasComponent
{
	float2 ReferenceResolution{ 1920.0f, 1080.0f };	// 基準解像度
	int sortingOrder = 0;							// 描画順序

	void Reflect(FieldList& f)
	{
		f.Add("ReferenceResolution", ReferenceResolution);
		f.Add("SortingOrder", sortingOrder);
	}
};

struct RectTransformComponent
{
	float2 AnchoredPosition{ 0.0f, 0.0f };	// アンカー位置
	float2 SizeDelta{ 100.0f, 100.0f };
	float2 pivot{ 0.5f, 0.5f };				// ピボット位置

	Entity parent = INVALID_ENTITY;			// 親のEntity

	void Reflect(FieldList& f)
	{
		f.Add("AnchoredPosition", AnchoredPosition);
		f.Add("SizeDelta", SizeDelta);
		f.Add("Pivot", pivot);
	}
};

struct UIImageComponent
{
	std::string texturePath;
	COLOR color{ 1.0f, 1.0f, 1.0f, 1.0f };
	std::shared_ptr<Material> material;

	void Reflect(FieldList& f)
	{
		f.Add("TexturePath", texturePath);
		f.Add("Color", color);

		f.AddTexture("Texture", texturePath, material);
	}
};

struct UITextComponent
{
	std::string text = "Text";
	// Windowsフォント
	std::string fontPath = "C:\\Windows\\Fonts\\meiryo.ttc";
	float fontSize = 32.0f;
	COLOR color{ 1.0f, 1.0f, 1.0f, 1.0f };
	std::shared_ptr<Mesh> mesh;

	bool isDirty = true;
	std::string _lastText;

	void Reflect(FieldList& f)
	{
		f.Add("Text", text);
		f.Add("FontPath", fontPath);
		f.AddRange("FontSize", fontSize, 1.0f, 200.0f);
		f.Add("Color", color);

		f.AddFont("Font", fontPath);
	}
};

struct AudioSourceComponent
{
	std::string clipPath;       // 音声ファイル
	float volume = 1.0f;
	bool  loop = false;
	bool  playOnStart = false;  // Play開始時に自動再生

	// 3D設定
	bool is3D = true;
	float minDistance = 1.0f;
	float maxDistance = 100.0f;

	// ランタイム状態（シリアライズ対象外）
	IXAudio2SourceVoice* voice = nullptr;
	std::shared_ptr<AudioClip> clip;
	bool playRequested = false; // スクリプトから立てる
	bool stopRequested = false;

	bool  seekRequested = false;	// 指定秒へシーク
	float seekSeconds = 0.0f;
	bool  pauseRequested = false;	// 位置を保ったまま停止
	bool  resumeRequested = false;	// 停止位置から再開

	void Reflect(FieldList& f)
	{
		f.AddAudio("Clip", clipPath, clip);
		f.AddRange("Volume", volume, 0.0f, 1.0f);
		f.Add("Loop", loop);
		f.Add("PlayOnStart", playOnStart);
		f.Add("Is3D", is3D);
		f.AddRange("MinDistance", minDistance, 0.1f, 100.0f);
		f.AddRange("MaxDistance", maxDistance, 1.0f, 500.0f);
	}
};

struct AudioListenerComponent
{
	void Reflect(FieldList& f)
	{
		// AudioListenerには特にフィールドはない
	}
};

struct AnimatorComponent
{
	Skeleton skeleton;
	SkinData skinData;
	std::vector<AnimationClip> clips;
	int currentClip = 0;
	float time = 0.0f;
	bool playing = false;
	std::vector<float4x4> palette;
	std::vector<std::string> extraClipNames;	// 追加のアニメーション名

	float speed = 1.0f;			// 再生速度
	bool  loop = true;			// ループ再生
	bool  scrubbing = false;		// スライダー操作中(この間は物理を止める)
	bool  physicsResetRequest = false;	// 次フレームで物理を再同期する

	std::string clipPathsStr;  
	bool clipsRestored = false;

	MorphSet morphs;					// もーフ定義
	std::vector<float> morphWeights;	// 各モーフの重み(0.0 ~ 1.0)
	std::vector<float3> morphoffsets;	// CPUでブレンド済みの頂点オフセット
	bool morphDirty = true;				// モーフの重みが変更されたかどうか

	void Reflect(FieldList& f)
	{
		f.Add("Time", time);
		f.Add("Playing", playing);
		f.Add("ClipPaths", clipPathsStr);
		f.Add("Speed", speed);
		f.Add("Loop",loop);
	}
};

struct MmdPhysicsComponent
{
	std::shared_ptr<MmdPhysics> impl;
	void Reflect(FieldList& f) {}
};

struct ParticleEmitterComponent
{
	enum class EmitShape : uint8_t
	{
		Cone,
		Sphere,
		Box
	}shape = EmitShape::Cone;

	bool emitting		= true;	// スクリプトから ON / OFF
	float emitRate		= 20.0f;	// 1秒あたりの発生数
	int maxParticles	= 200;

	float lifeTime = 1.5f;
	float lifeTimeVariance = 0.5f;	// ランダム幅

	float startSize = 0.03f;
	float endSize = 0.0f;

	COLOR startColor{ 1.0f, 1.0f, 1.0f, 0.6f };
	COLOR endColor{ 1.0f, 1.0f, 1.0f, 0.0f };

	float speed = 0.3f;				// ビーム方向への流れ速度
	float speedVariance = 0.15f;
	float drift = 0.05f;

	bool followLight = true;
	float3 gravity{ 0.0f,0.0f,0.0f };

	void Reflect(FieldList& f)
	{
		f.AddEnum("Shape", (int&)shape, { "Cone", "Sphere", "Box" });
		f.Add("Emitting", emitting);
		f.AddRange("EmitRate", emitRate, 0.0f, 500.0f);
		f.AddRange("MaxParticles", maxParticles, 1.0f, 2000.0f);
		f.AddRange("LifeTime", lifeTime, 0.1f, 10.0f);
		f.AddRange("LifeTimeVariance", lifeTimeVariance, 0.0f, 5.0f);
		f.AddRange("StartSize", startSize, 0.001f, 1.0f);
		f.AddRange("EndSize", endSize, 0.0f, 1.0f);
		f.Add("StartColor", startColor);
		f.Add("EndColor", endColor);
		f.AddRange("Speed", speed, 0.0f, 10.0f);
		f.AddRange("SpeedVariance", speedVariance, 0.0f, 5.0f);
		f.AddRange("Drift", drift, 0.0f, 1.0f);
		f.Add("FollowLight", followLight);
	}

	// --- ランタイム専用（シリアライズ不要）---
	struct Particle
	{
		float3 pos;
		float3 velocity;
		float age = 0.0f;
		float life = 1.0f;
	};
	std::vector<Particle> particles;   // プールとして使い回す(容量固定・再利用)
	float spawnAccumulator = 0.0f;
};
