/*! ************************************************************
 * \file   EntityFactory.cpp
 * \brief  よく使う Entity の組み立て(UI 非依存)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "EntityFactory.hpp"

#include "Util.hpp"
#include "ModelLoader.hpp"
#include "MaterialLibrary.hpp"

// todo 親子化
// todo Canvasの描画順序を考慮する
// todo image, textの名前被りの解消

Entity EntityFactory::EnsureCanvas(World& world)
{
	// 宣言
	Entity canvas = INVALID_ENTITY;

	// 既に宣言されるか確認
	world.Each<CanvasComponent>([&](Entity e, CanvasComponent&)
		{
			// すでにCanvasが存在する場合はそれを返す
			if(canvas == INVALID_ENTITY)
				canvas = e;
		});

	// 見つからない場合は新規作成
	if (canvas == INVALID_ENTITY)
	{
		canvas = world.CreateEntity();
		world.AddComponent<NameComponent>(canvas, NameComponent{ "Canvas" });
		world.AddComponent<RectTransformComponent>(canvas, RectTransformComponent{});
		world.AddComponent<CanvasComponent>(canvas, CanvasComponent{});
	}

	// 値を返す
	return canvas;
}

Entity EntityFactory::CreateImage(World& world)
{
	// canvasがあるか確認
	// ない場合は作成
	EnsureCanvas(world);
	Entity e = world.CreateEntity();
	static int num = 1;
	std::string name = "Image_" + std::to_string(num++);
	world.AddComponent<NameComponent>(e, NameComponent{ name });
	auto& rt = world.AddComponent<RectTransformComponent>(e, RectTransformComponent{});
	rt.SizeDelta = { 100.0f,100.0f };
	world.AddComponent<UIImageComponent>(e, UIImageComponent{});
	return e;
}

Entity EntityFactory::CreatePrimitive(World& world, const std::string& tag)
{
	Entity e = world.CreateEntity();

	static int num = 1;
	const std::string base =
		(tag == kPrimitiveCube) ? "Cube_" :
		(tag == kPrimitiveCylinder) ? "Cylinder_" :
		(tag == kPrimitiveCone) ? "Cone_" :
		(tag == kPrimitiveCapsule) ? "Capsule_" :
		(tag == kPrimitivePlane) ? "Plane_" : "Sphere_";
	world.AddComponent<NameComponent>(e, NameComponent{ base + std::to_string(num++) });
	world.AddComponent<TransformComponent>(e, TransformComponent{});

	BuildPrimitiveEntity(world, e, tag);

	// 既定の .mat を割り当てる。共有なので、色を変えると同じ .mat を使う他のオブジェクトにも及ぶ
	// 個別に編集したいときはインスペクタの「割り当てを解除」を押す
	const std::string def = MaterialLibrary::Get().EnsureDefault();
	if (auto shared = MaterialLibrary::Get().Load(def))
	{
		auto& mc = world.GetComponent<MaterialComponent>(e);
		mc.material = shared;
		if (mc.materials.empty()) mc.materials.push_back(shared);
		else                      mc.materials[0] = shared;
		mc.materialAssets.resize(mc.materials.size());
		mc.materialAssets[0] = def;
	}
	return e;
}

Entity EntityFactory::CreateText(World& world)
{
	// canvasがあるか確認
	EnsureCanvas(world);
	Entity e = world.CreateEntity();

	static int num = 1;
	std::string name = "Text_" + std::to_string(num++);
	world.AddComponent<NameComponent>(e, NameComponent{ name });
	auto& rt = world.AddComponent<RectTransformComponent>(e, RectTransformComponent{});
	rt.SizeDelta = { 100.0f,50.0f };
	world.AddComponent<UITextComponent>(e, UITextComponent{});
	return e;
}

Entity EntityFactory::SpawnModelFromFile(
	World& world,
	const std::string& modelPath,
	const float3& pos,
	Scene* scene)
{
	Entity e = world.CreateEntity();
	// 先にTransform/名前だけ付けておく（メッシュは後から差し込む）
	TransformComponent tr{}; tr.position = pos; tr.RebuildWorld();
	world.AddComponent<TransformComponent>(e, tr);
	world.AddComponent<NameComponent>(e, NameComponent{ "Model_" + std::to_string(e) });

	ModelLoader::PopulateModelEntity(world, e, modelPath, scene);
	return e;
}
