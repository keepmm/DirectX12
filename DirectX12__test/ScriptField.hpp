#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "Defines.hpp"

enum class FieldType : uint8_t
{
	Int,
	Float,
	Float2,
	Float3,
	Float4,
	String,
	Bool,
	Vector3,
	Vector4,
	Color,
	Entity,
	wstring,
	Enum,
	Texture,
	Font,
	Audio,
};

/// @brief exe側に保持する値
struct FieldValue
{
	FieldType type = FieldType::Int;
	int i = 0;
	float f[4] = { 0.0f,0.0f,0.0f,0.0f };
	bool b = false;
	std::string s;
};

struct FieldDesc
{
	std::string name;
	FieldType type = FieldType::Int;
	int rangeMin = 0;
	int rangeMax = 0;
};

struct ReflectedField
{
	std::string name;
	FieldType type;
	void* ptr;
	float minValue = 0.0f;
	float maxValue = 0.0f;
	void* ptr2 = nullptr; // material用の2つ目のポインタ
	std::vector<std::string> enumValues; // enum用の値リスト
};

struct FieldList
{
	std::vector<ReflectedField> fields;

	void Add(const std::string& n, int& v) { fields.push_back({ n, FieldType::Int,    &v }); }
	void Add(const std::string& n, float& v) { fields.push_back({ n, FieldType::Float,  &v }); }
	void Add(const std::string& n, float2& v) { fields.push_back({ n, FieldType::Float2, &v }); }
	void Add(const std::string& n, float3& v) { fields.push_back({ n, FieldType::Float3, &v }); }
	void Add(const std::string& n, float4& v) { fields.push_back({ n, FieldType::Color,  &v }); }
	void Add(const std::string& n, bool& v) { fields.push_back({ n, FieldType::Bool,   &v }); }
	void Add(const std::string& n, std::string& v) { fields.push_back({ n, FieldType::String, &v }); }
	void Add(const std::string& n, std::wstring& v) { fields.push_back({ n, FieldType::wstring, &v }); }

	// 範囲付き(float)
	void AddRange(const std::string& n, float& v, float min, float max)
	{
		fields.push_back({ n, FieldType::Float, &v, min, max });
	}

	// テクスチャ（material等のreset用にptr2を持つ）
	template <typename TRes>
	void AddTexture(const std::string& n, std::string& path, std::shared_ptr<TRes>& res)
	{
		fields.push_back({ n, FieldType::Texture, &path, 0,0, &res });
	}

	// フォント（リソース無し版でOK、パスのみ）
	void AddFont(const std::string& n, std::string& path)
	{
		fields.push_back({ n, FieldType::Font, &path });
	}

	// オーディオ
	template <typename TRes>
	void AddAudio(const std::string& n, std::string& path, std::shared_ptr<TRes>& res)
	{
		fields.push_back({ n, FieldType::Audio, &path, 0,0, &res });
	}

	// パスだけのシンプル版（reset不要なフィールド用）
	void AddAudio(const std::string& n, std::string& path)
	{
		fields.push_back({ n, FieldType::Audio, &path });
	}

	/// @brief Enum型のフィールドを追加する
	/// @param n フィールド名
	/// @param v Enumの値
	/// @param enumValues Enumの値リスト
	void AddEnum(const std::string& n, int& v, const std::vector<std::string>& enumValues)
	{
		fields.push_back({ n, FieldType::Enum, &v, 0.0f, 0.0f, nullptr, enumValues });
	}
};