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

		/// @brief 範囲付き(SERIALIZE_FIELD_RANGE 用)
		template<typename T, typename U>
		FieldRegistrar(MonoBehavior* owner, const char* name, T* ptr, U minValue, U maxValue)
		{
			owner->RegisterFieldPtr(name, ptr);
			owner->SetLastFieldRange(minValue, maxValue);
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

	/// @brief enabled が false→true になったフレームに呼ばれる
	virtual void OnEnable() {}
	/// @brief enabled が true→false になったフレームに呼ばれる
	virtual void OnDisable() {}
	/// @brief Entity が破棄される直前に呼ばれる
	virtual void OnDestroy() {}

	/// @brief false の間は Update / FixedUpdate / LateUpdate / Draw が止まる
	bool enabled = true;

	/// @brief enabled の変化を見て OnEnable / OnDisable を発火する
	/// @note ScriptSystem が毎フレーム呼ぶ。スクリプト側から呼ぶ必要はない
	void SyncEnableState()
	{
		if (enabled == m_PrevEnabled) return;
		m_PrevEnabled = enabled;
		if (enabled) OnEnable();
		else         OnDisable();
	}

	/// @brief 指定秒後に一度だけ実行する
	/// @param delaySeconds 待ち時間(秒)
	/// @param fn 実行する処理
	/// @return 取り消しに使うID
	/// @note enabled が false の間は時間が進まない。Destroy されれば当然消える
	int Invoke(_In_ float delaySeconds, _In_ std::function<void()> fn)
	{
		const int id = m_NextInvokeId++;
		m_Invokes.emplace_back(id, delaySeconds, false, std::move(fn));
		return id;
	}

	/// @brief 一定間隔で繰り返し実行する
	/// @param intervalSeconds 間隔(秒)
	int InvokeRepeating(_In_ float intervalSeconds, _In_ std::function<void()> fn)
	{
		const int id = m_NextInvokeId++;
		m_Invokes.emplace_back(id, intervalSeconds, true, std::move(fn));
		return id;
	}

	/// @brief Invoke / InvokeRepeating を取り消す
	void CancelInvoke(_In_ int id)
	{
		for (auto& iv : m_Invokes)
		{
			if (iv.id == id) iv.canceled = true;
		}
	}

	/// @brief 予約された処理の時間を進める
	/// @note ScriptSystem が OnUpdate の前に呼ぶ
	void TickInvokes(_In_ float deltatime)
	{
		if (m_Invokes.empty()) return;

		// 実行中に Invoke が増えることがあるので、走査はインデックスで回す
		for (size_t i = 0; i < m_Invokes.size(); ++i)
		{
			auto& iv = m_Invokes[i];
			if (iv.canceled) continue;

			iv.elapsed += deltatime;
			if (iv.elapsed < iv.delay) continue;

			if (iv.fn) iv.fn();

			if (iv.repeat) iv.elapsed -= iv.delay;
			else           iv.canceled = true;
		}

		m_Invokes.erase(
			std::remove_if(m_Invokes.begin(), m_Invokes.end(),
				[](const InvokeEntry& iv) { return iv.canceled; }),
			m_Invokes.end());
	}

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

	/// @brief 他のEntityのコンポーネントを取得する
	/// @tparam T 取得するコンポーネントの型
	/// @param entity 対象のEntity
	template<typename T>
	T& GetComponent(_In_ Entity entity)
	{
		return m_World->GetComponent<T>(entity);
	}

	/// @brief 他のEntityが指定したコンポーネントを持つか
	template<typename T>
	bool HasComponent(_In_ Entity entity) const
	{
		return m_World->HasComponent<T>(entity);
	}

	/// @brief Entityが生存しているか
	/// @note EntityRef は破棄後も値が残るので、参照前にこれで確認する
	bool IsAlive(_In_ Entity entity) const
	{
		return m_World->IsEntityAlive(entity);
	}

	/// @brief 名前でEntityを探す(最初に見つかったもの)
	/// @param name NameComponent の名前
	/// @return 見つからなければ INVALID_ENTITY
	/// @note 毎回の全走査になるので OnUpdate では呼ばず OnStart で拾っておくこと
	Entity Find(_In_ const std::string& name) const
	{
		Entity found = INVALID_ENTITY;
		m_World->Each<NameComponent>([&found, &name](Entity e, NameComponent& n)
			{
				if (found == INVALID_ENTITY && n.name == name) found = e;
			});
		return found;
	}

	/// @brief 親のEntityを取得する
	/// @return 親が無ければ INVALID_ENTITY
	Entity GetParent() const
	{
		if (m_World->HasComponent<TransformComponent>(m_Entity))
			return m_World->GetComponent<TransformComponent>(m_Entity).parent;
		if (m_World->HasComponent<RectTransformComponent>(m_Entity))
			return m_World->GetComponent<RectTransformComponent>(m_Entity).parent;
		return INVALID_ENTITY;
	}

	/// @brief 自分の子のEntityを集める
	std::vector<Entity> GetChildren() const
	{
		std::vector<Entity> children;
		const Entity self = m_Entity;
		m_World->Each<TransformComponent>([&children, self](Entity e, TransformComponent& tr)
			{
				if (tr.parent == self) children.push_back(e);
			});
		return children;
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
		// SERIALIZE_FIELD(コンストラクタ登録)と RegisterFields(virtual)は併用できる。
		// 片方が走ったからといってもう片方を飛ばさない
		if (m_RegisterFieldsCalled == false)
		{
			m_RegisterFieldsCalled = true;
			RegisterFields();
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
		else if constexpr (std::is_same_v<T, EntityRef>) return SerializeField::Type::Entity;
		else static_assert(sizeof(T) == 0, "Unsupported type for Field");
	}

	template<typename T>
	void RegisterFieldPtr(const std::string& name, T* ptr)
	{
		m_SerializeFields.push_back({ name, FieldTypeOf<T>(), ptr });
	}

	/// @brief 直前に登録したフィールドへ範囲を設定する
	template<typename T>
	void SetLastFieldRange(T minValue, T maxValue)
	{
		if (m_SerializeFields.empty()) return;
		m_SerializeFields.back().Range(minValue, maxValue);
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
	mutable bool m_RegisterFieldsCalled = false;

	bool m_PrevEnabled = true;

	/// @brief Invoke の予約
	struct InvokeEntry
	{
		int   id;
		float delay;
		float elapsed;
		bool  repeat;
		bool  canceled;
		std::function<void()> fn;

		InvokeEntry(int i, float d, bool r, std::function<void()> f)
			: id(i), delay(d), elapsed(0.0f), repeat(r), canceled(false), fn(std::move(f))
		{
		}
	};
	std::vector<InvokeEntry> m_Invokes;
	int m_NextInvokeId = 1;
};

/// @brief メンバ宣言と同時にインスペクタへ公開する
/// @note SERIALIZE_FIELD(float, speed, 5.0f) のように初期値を渡せる
#define SERIALIZE_FIELD(Type, name, ...)	\
	Type name{ __VA_ARGS__ };				\
	MonoBehavior::FieldRegistrar _field_##name{ this, #name, &name }

/// @brief 範囲付きで公開する(インスペクタがスライダーになる)
#define SERIALIZE_FIELD_RANGE(Type, name, minValue, maxValue, ...)	\
	Type name{ __VA_ARGS__ };										\
	MonoBehavior::FieldRegistrar _field_##name{ this, #name, &name, minValue, maxValue }
