#pragma once

#include "RenderContext.hpp"
#include "World.hpp"
#include "Input.hpp"
#include "Time.hpp"
#include "Components.hpp"
#include "ScriptField.hpp"


/// @brief unityライクのシリアライズフィールド
struct SerializeField
{
	std::string name;
	using Type = FieldType;
	Type type;
	void* ptr;
	int RangeMin = 0;
	int RangeMax = 0;

	template <typename T>
	void Range(T min, T max)
	{
		RangeMin = static_cast<int>(min);
		RangeMax = static_cast<int>(max);
	}
};

struct EntityRef
{
	Entity id = INVALID_ENTITY;
	operator Entity() const { return id; }
};

/*
*	画面上に配置するオブジェクト
*/
class MonoBehavior
{
public:
	struct FieldRegistrar
	{
		template<typename T>
		FieldRegistrar(MonoBehavior* owner, const char* name, T* ptr)
		{
			owner->RegisterFieldPtr(name, ptr);
		}
	};

	MonoBehavior() = default;

	/*
	*	ライフサイクル
	*/

	virtual ~MonoBehavior() = default;
	virtual void OnStart() {}
	virtual void OnUpdate(_In_ float deltatime) {}
	virtual void OnFixedUpdate(_In_ float deltatime) {}
	virtual void OnLateUpdate(_In_ float deltatime) {}
	virtual void OnDraw(_In_ const RenderContext& context) {}

	/*
	*	当たり判定 
	*/

	virtual void OnCollisionEnter(_In_ Entity other) {}
	virtual void OnCollisionExit(_In_ Entity other) {}
	virtual void OnTriggerEnter(_In_ Entity other) {}
	virtual void OnTriggerExit(_In_ Entity other) {}

	/// @brief Behaviorが紐づいているEntityから指定したコンポーネントを取得する
	/// @tparam T 取得するコンポーネントの型
	/// @return 取得したコンポーネントの参照
	template<typename T>
	T& GetComponent()
	{
		return m_World->GetComponent<T>(m_Entity);
	}

	/// @brief Behaviorが紐づいているEntityに指定したコンポーネントが存在するか確認する
	/// @tparam T 確認するコンポーネントの型
	/// @return 存在する場合はtrue、存在しない場合はfalse
	template<typename T>
	bool HasComponent() const
	{
		return m_World->HasComponent<T>(m_Entity);
	}

	/// @brief Behaviorが紐づいているEntityを取得する
	/// @return EntityのID
	Entity GetEntity() const { return m_Entity; }

	/// @brief Behaviorが紐づいているWorldを取得する
	/// @return Worldの参照
	World& GetWorld() const { return *m_World; }

	/// @brief transformコンポーネントを取得する
	/// @return transformコンポーネントの参照
	TransformComponent& transform() { return GetComponent<TransformComponent>(); }

	/// @brief Behaviorにコンポーネントを追加する
	/// @tparam T 追加するコンポーネントの型
	/// @param component 追加するコンポーネントのデータ
	/// @return 成功時は追加したコンポーネントの参照を返す
	template <typename T>
	T& AddComponent(const T& component)
	{
		return m_World->AddComponent<T>(m_Entity, component);
	}

	/// @brief BehaviorにWolrdとEntityを紐づける
	/// @param world 紐づけるWolrdのポインタ
	/// @param entity EntityのID
	void Attach(World* world, Entity entity)
	{
		m_World = world;
		m_Entity = entity;
	}


	/// @brief Behaviorに登録されているシリアライズフィールドを取得する
	/// @return シリアライズフィールドの配列の参照
	inline const std::vector<SerializeField>& GetField() noexcept
	{
		if (m_FieldsBuilt == false)
		{
			RegisterFields();
			m_FieldsBuilt = true;
		}
		return m_SerializeFields;
	}

	inline std::string	GetName() const noexcept { return m_ScriptName; }

public:
	// exe側の値を生成直後のオブジェクトへ流し込む
	void ApplyValues(const std::unordered_map<std::string, FieldValue>& vals)
	{
		for (auto& f : GetField())
		{
			auto it = vals.find(f.name);
			if (it != vals.end()) WriteField(f, it->second);
		}
	}
	// 現在のメンバ値を exe側へ吸い出す（初回デフォルト取得用）
	void CaptureValues(std::unordered_map<std::string, FieldValue>& out)
	{
		for (auto& f : GetField()) out[f.name] = ReadField(f);
	}

	private:
		static FieldValue ReadField(const SerializeField& f)
		{
			FieldValue v; v.type = f.type;
			switch (f.type)
			{
			case FieldType::Int:    v.i = *(int*)f.ptr; break;
			case FieldType::Float:  v.f[0] = *(float*)f.ptr; break;
			case FieldType::Float2: { auto& a = *(float2*)f.ptr; v.f[0] = a.x; v.f[1] = a.y; } break;
			case FieldType::Float3:
			case FieldType::Vector3:
			case FieldType::Color: { auto& a = *(float3*)f.ptr; v.f[0] = a.x; v.f[1] = a.y; v.f[2] = a.z; } break;
			case FieldType::Float4:
			case FieldType::Vector4: { auto& a = *(float4*)f.ptr; v.f[0] = a.x; v.f[1] = a.y; v.f[2] = a.z; v.f[3] = a.w; } break;
			case FieldType::Bool:   v.b = *(bool*)f.ptr; break;
			case FieldType::String: v.s = *(std::string*)f.ptr; break;
			case FieldType::Entity: v.i = (int)((EntityRef*)f.ptr)->id; break;
			}
			return v;
		}
		static void WriteField(const SerializeField& f, const FieldValue& v)
		{
			switch (f.type)
			{
			case FieldType::Int:    *(int*)f.ptr = v.i; break;
			case FieldType::Float:  *(float*)f.ptr = v.f[0]; break;
			case FieldType::Float2: *(float2*)f.ptr = float2{ v.f[0], v.f[1] }; break;
			case FieldType::Float3:
			case FieldType::Vector3:
			case FieldType::Color:  *(float3*)f.ptr = float3{ v.f[0], v.f[1], v.f[2] }; break;
			case FieldType::Float4:
			case FieldType::Vector4:*(float4*)f.ptr = float4{ v.f[0], v.f[1], v.f[2], v.f[3] }; break;
			case FieldType::Bool:   *(bool*)f.ptr = v.b; break;
			case FieldType::String: *(std::string*)f.ptr = v.s; break;
			case FieldType::Entity: ((EntityRef*)f.ptr)->id = (Entity)v.i; break;
			}
		}
protected:
	template <typename T>
	static constexpr SerializeField::Type FieldTypeOf()
	{
		if constexpr (std::is_same_v<T, int>) return SerializeField::Type::Int;
		else if constexpr (std::is_same_v<T, float>) return SerializeField::Type::Float;
		else if constexpr (std::is_same_v<T, float2>) return SerializeField::Type::Float2;
		else if constexpr (std::is_same_v<T, float3>) return SerializeField::Type::Float3;
		else if constexpr (std::is_same_v<T, float4>) return SerializeField::Type::Float4;
		else if constexpr (std::is_same_v<T, std::string>) return SerializeField::Type::String;
		else if constexpr (std::is_same_v<T, bool>) return SerializeField::Type::Bool;
		else static_assert(sizeof(T) == 0, "Unsupported type for Field");
	}

	template<typename T>
	void RegisterFieldPtr(const std::string& name, T* ptr)
	{
		m_SerializeFields.push_back({ name, FieldTypeOf<T>(), ptr });
		m_FieldsBuilt = true;   // コンストラクタ登録済みフラグ
	}

	template<typename T>
	void Field(_In_ const std::string& name, _In_ T& value)
	{
		// int型
		if constexpr (std::is_same_v<T, int>)
		{
			m_SerializeFields.push_back({ name, SerializeField::Type::Int, &value });
		}
		// float型
		else if constexpr (std::is_same_v<T, float>)
		{
			m_SerializeFields.push_back({ name, SerializeField::Type::Float, &value });
		}
		// float2型
		else if constexpr (std::is_same_v<T, float2>)
		{
			m_SerializeFields.push_back({ name, SerializeField::Type::Float2, &value });
		}
		// float3型
		else if constexpr (std::is_same_v<T, float3>)
		{
			m_SerializeFields.push_back({ name, SerializeField::Type::Float3, &value });
		}
		// float4型
		else if constexpr (std::is_same_v<T, float4>)
		{
			m_SerializeFields.push_back({ name, SerializeField::Type::Float4, &value });
		}
		// string型
		else if constexpr (std::is_same_v<T, std::string>)
		{
			m_SerializeFields.push_back({ name, SerializeField::Type::String, &value });
		}
		// bool型
		else if constexpr (std::is_same_v<T, bool>)
		{
			m_SerializeFields.push_back({ name, SerializeField::Type::Bool, &value });
		}
		else if constexpr (std::is_same_v<T, EntityRef>)
		{
			m_SerializeFields.push_back({ name, SerializeField::Type::Entity, &value });
		}
		// 不明
		else
		{
			static_assert(sizeof(T) == 0, "Unsupported type for Field");
		}
	}
	virtual void RegisterFields() {}
private:
	friend struct ScriptComponent;

	World* m_World = nullptr;
	Entity m_Entity = INVALID_ENTITY;

	std::string m_ScriptName;

	mutable std::vector<SerializeField> m_SerializeFields;
	mutable bool m_FieldsBuilt = false;
};

#define SERIALIZE_FIELD(Type,name, ...)		\
	Type name{___VA_ARGS___};				\
	MonoBehavior::FieldRegistrar _field_##name{ this, #name, &name}
