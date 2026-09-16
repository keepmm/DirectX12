/*!*************************************************************
 * \file   UndoHistory.cpp
 * \brief  エディタの Undo / Redo
 *
 * 作成者 keeep
 * 作成日 2026/9/15
 * 更新履歴	9.15 作成
 * *********************************************************************/
#include "UndoHistory.hpp"

#include "DirectX.hpp"
#include "imguiinit.hpp"
#include "Scene.hpp"
#include "SceneSerializer.hpp"

using json = nlohmann::json;

namespace
{
	/// @brief 値の変更(from → to の差分を当て直す)
	class PropertyCommand final : public EditorCommand
	{
	public:
		PropertyCommand(Entity e, json before, json after)
			: m_Entity(e), m_Before(std::move(before)), m_After(std::move(after))
		{
			// どのキーが変わったかを名前にする(メニューに出す)
			std::string key;
			for (auto it = m_After.begin(); it != m_After.end(); ++it)
			{
				if (!m_Before.contains(it.key()) || m_Before[it.key()] != it.value()) { key = it.key(); break; }
			}
			if (key.empty())
			{
				for (auto it = m_Before.begin(); it != m_Before.end(); ++it)
					if (!m_After.contains(it.key())) { key = it.key(); break; }
			}
			m_Name = IMGUI::ToUTF8("変更: ") + key;
		}

		const char* Name() const override { return m_Name.c_str(); }
		void Undo(Scene& s) override { SceneSerializer::ApplyEntityDiff(s, m_Entity, m_After, m_Before); }
		void Redo(Scene& s) override { SceneSerializer::ApplyEntityDiff(s, m_Entity, m_Before, m_After); }

	private:
		Entity m_Entity;
		json m_Before, m_After;
		std::string m_Name;
	};

	/// @brief Entity の作成 / 削除(取り外して保管する)
	class LifetimeCommand final : public EditorCommand
	{
	public:
		/// @param created true なら「作成」(Undo で取り外す)、false なら「削除」(Undo で戻す)
		LifetimeCommand(std::vector<Entity> entities, bool created)
			: m_Entities(std::move(entities)), m_Created(created)
		{
			m_Name = IMGUI::ToUTF8(created ? "エンティティの作成" : "エンティティの削除");
		}

		/// @brief 削除のときは、積む前に取り外しておく
		void DetachNow(World& world) { Detach(world); }

		const char* Name() const override { return m_Name.c_str(); }
		void Undo(Scene& s) override { m_Created ? Detach(s.GetWorld()) : Attach(s.GetWorld()); }
		void Redo(Scene& s) override { m_Created ? Attach(s.GetWorld()) : Detach(s.GetWorld()); }

	private:
		void Detach(World& world)
		{
			m_Stash.clear();
			for (Entity e : m_Entities) m_Stash.push_back(world.DetachEntity(e));
		}
		void Attach(World& world)
		{
			for (auto& d : m_Stash) world.AttachEntity(std::move(d));
			m_Stash.clear();
		}

		std::vector<Entity> m_Entities;
		std::vector<World::DetachedEntity> m_Stash;	// 取り外している間だけ中身がある
		bool m_Created;
		std::string m_Name;
	};
}

// ------------------------------------------------------------------ //

void UndoHistory::Discard(std::vector<std::unique_ptr<EditorCommand>>& list)
{
	if (list.empty()) return;
	// 取り外していた Entity のコンポーネント(GPU リソース)がここで破棄されうるので、
	// 記録済みのフレームが使い終わるのを待つ
	APP->WaitForGPUIdle();
	list.clear();
}

void UndoHistory::Push(std::unique_ptr<EditorCommand> cmd)
{
	Discard(m_Redo);
	m_Undo.push_back(std::move(cmd));

	if (m_Undo.size() > kMaxHistory)
	{
		APP->WaitForGPUIdle();
		m_Undo.erase(m_Undo.begin());
	}
}

void UndoHistory::Undo(Scene& scene)
{
	if (m_Undo.empty()) return;
	auto cmd = std::move(m_Undo.back());
	m_Undo.pop_back();
	cmd->Undo(scene);
	m_Redo.push_back(std::move(cmd));
	m_Tracked = INVALID_ENTITY;	// 戻した結果を「確定」状態として撮り直す(差分として積まない)
}

void UndoHistory::Redo(Scene& scene)
{
	if (m_Redo.empty()) return;
	auto cmd = std::move(m_Redo.back());
	m_Redo.pop_back();
	cmd->Redo(scene);
	m_Undo.push_back(std::move(cmd));
	m_Tracked = INVALID_ENTITY;
}

void UndoHistory::Clear()
{
	Discard(m_Undo);
	Discard(m_Redo);
	m_Tracked = INVALID_ENTITY;
	m_TrackedScene = nullptr;
}

void UndoHistory::TrackEntity(Scene& scene, Entity entity, bool busy)
{
	World& world = scene.GetWorld();
	if (entity == INVALID_ENTITY || !world.IsEntityAlive(entity))
	{
		m_Tracked = INVALID_ENTITY;
		return;
	}

	// 選択が変わった: 今の状態を基準にするだけ
	if (entity != m_Tracked || &scene != m_TrackedScene)
	{
		m_Tracked = entity;
		m_TrackedScene = &scene;
		m_Stable = SceneSerializer::SaveEntity(world, entity);
		return;
	}

	// ドラッグ中は途中経過を積まない(離したときに 1 回にまとめる)
	if (busy) return;

	json now = SceneSerializer::SaveEntity(world, entity);
	if (now == m_Stable) return;

	Push(std::make_unique<PropertyCommand>(entity, std::move(m_Stable), now));
	m_Stable = std::move(now);
}

void UndoHistory::RecordCreated(Scene& scene, size_t entityCountBefore)
{
	const auto& entities = scene.GetWorld().GetEntities();
	if (entities.size() <= entityCountBefore) return;

	// CreateEntity は末尾に積むので、増えた分がそのまま新しい Entity
	// (UI Image が Canvas を一緒に作る場合もまとめて 1 回で戻る)
	std::vector<Entity> created(entities.begin() + entityCountBefore, entities.end());
	Push(std::make_unique<LifetimeCommand>(std::move(created), true));
}

void UndoHistory::DeleteEntity(Scene& scene, Entity entity)
{
	auto cmd = std::make_unique<LifetimeCommand>(std::vector<Entity>{ entity }, false);
	cmd->DetachNow(scene.GetWorld());
	Push(std::move(cmd));
	if (m_Tracked == entity) m_Tracked = INVALID_ENTITY;
}

void UndoHistory::RecordEdit(Scene& scene, Entity entity, const std::function<void()>& edit)
{
	World& world = scene.GetWorld();
	json before = SceneSerializer::SaveEntity(world, entity);
	edit();
	json after = SceneSerializer::SaveEntity(world, entity);
	if (before != after)
		Push(std::make_unique<PropertyCommand>(entity, std::move(before), std::move(after)));
}