/*****************************************************************//**
 * \file   DropFiles.hpp
 * \brief  エクスプローラーからのファイルドロップの受け皿
 *
 * 作成者 keep
 * 作成日 2026/9/10
 * 更新履歴 9/10 作成
 * *********************************************************************/
#pragma once

#include <string>
#include <vector>

 /// @brief WM_DROPFILES で受け取った内容を1フレーム保持する
 /// @note ウィンドウプロシージャ(Engine.cpp)が積み、
 ///       アセットウィンドウが自分の領域なら消費する
class DropFiles
{
public:
    static DropFiles& Get()
    {
        static DropFiles instance;
        return instance;
    }

    /// @brief ドロップされた内容を積む
    /// @param paths ドロップされた絶対パス(ファイル / フォルダ)
    /// @param clientX / clientY ドロップ位置(ウィンドウのクライアント座標)
    void Push(std::vector<std::string> paths, int clientX, int clientY)
    {
        m_Paths = std::move(paths);
        m_X = clientX;
        m_Y = clientY;
    }

    bool HasPending() const { return !m_Paths.empty(); }

    int X() const { return m_X; }
    int Y() const { return m_Y; }

    /// @brief 内容を取り出して空にする
    std::vector<std::string> Consume()
    {
        std::vector<std::string> out;
        out.swap(m_Paths);
        return out;
    }

    /// @brief 誰も受け取らなかったぶんを捨てる
    /// @note フレーム末に呼ぶ。溜めたままにすると次フレームで誤爆する
    void Discard() { m_Paths.clear(); }

private:
    DropFiles() = default;
    DropFiles(const DropFiles&) = delete;
    void operator=(const DropFiles&) = delete;

    std::vector<std::string> m_Paths;
    int m_X = 0;
    int m_Y = 0;
};