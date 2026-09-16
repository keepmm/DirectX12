/*****************************************************************//**
 * \file   UndoHistory.hpp
 * \brief  エディタの Undo / Redo
 * 
 * 作成者 keepmm
 * 作成日 2026/9/15
 * 更新履歴 9.15 作成
 * *********************************************************************/
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "json.hpp"
#include "World.hpp"

class Scene;

class EditorCommand
{
public:
	virtual ~EditorCommand() = default;
	virtual const char* Name() const = 0;	// UTF-8
	virtual void Undo(_In_ Scene& scene) = 0;
	virtual void Redo(_In_ Scene& scene) = 0;
};

class UndoHistory
{
public:
	~UndoHistory() { Clear(); }

	/// @brief 既に適用済みの変更を積む
	void Push(_In_ std::unique_ptr<EditorCommand> cmd);

	void Undo(_In_ Scene& scene);
	void Redo(_In_ Scene& scene);

	bool CanUndo() const noexcept { return !m_Undo.empty(); }
	bool CanRedo() const noexcept { return !m_Redo.empty(); }
	const char* UndoName() const { return CanUndo() ? m_Undo.back()->Name() : ""; }
	const char* RedoName() const { return CanRedo() ? m_Redo.back()->Name() : ""; }

	/// @brief 全部捨てる
	void Clear();

	/// @brief 選択中の Entity の変更を拾う(毎フレーム、UI を描き終わった後)
	/// @param busy ドラッグ中 / ギズモ操作中なら true。離すまで 1 回にまとめる
	void TrackEntity(_In_ Scene& scene, Entity entity, bool busy);

	/// @brief entityCountBefore 以降に増えた Entity を「作成」として記録する
	void RecordCreated(_In_ Scene& scene, size_t entityCountBefore);

	/// @brief Entity を削除し、元に戻せるように記録する
	void DeleteEntity(_In_ Scene& scene, Entity entity);

	/// @brief 選択していない Entity を書き換えるときに、前後を記録する(親子付けなど)
	void RecordEdit(_In_ Scene& scene, Entity entity, _In_ const std::function<void()>& edit);

private:
	void Discard(std::vector<std::unique_ptr<EditorCommand>>& list);

	std::vector<std::unique_ptr<EditorCommand>> m_Undo;
	std::vector<std::unique_ptr<EditorCommand>> m_Redo;

	// 値の変更の検出
	const Scene* m_TrackedScene = nullptr;
	Entity m_Tracked = INVALID_ENTITY;
	nlohmann::json m_Stable;	// 最後に確定した状態

	static constexpr size_t kMaxHistory = 100;
};

