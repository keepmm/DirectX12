#include "cr.h"
#include "ScriptContext.hpp"
#include <windows.h>
#include "World.hpp"
#include "MonoBehavior.hpp"
#include "Components.hpp"
#include <DirectXMath.h>
#include "RegisterScript.hpp"
#include "Debug.hpp"

static void BuildOne(World* w, Entity e, ScriptComponent& sc, const std::string& name)
{
	auto b = RegisterScript::Get().Create(name);
	if (!b) return;
	b->Attach(w, e);

	// メタ情報を exe側にスナップショット
	auto& descs = sc.fieldDescs[name];
	descs.clear();
	for (auto& f : b->GetField())
		descs.push_back({ f.name, f.type, f.RangeMin, f.RangeMax });

	// 値：保存済みがあれば適用、無ければ現在値(デフォルト)を取り込む
	auto& vals = sc.values[name];
	if (!vals.empty()) b->ApplyValues(vals);
	else               b->CaptureValues(vals);

	sc.behaviors.push_back(std::move(b));
}

static void RebuildBehaviors(World& w)
{
	w.Each<ScriptComponent>([&w](Entity e, ScriptComponent& sc)
		{
			sc.behaviors.clear();
			for (auto& name : sc.scriptNames)
			{
				BuildOne(&w, e, sc, name);
			}
		});
}

CR_EXPORT int cr_main(cr_plugin* ctx, cr_op operation)
{
	auto* host = static_cast<ScriptContext*>(ctx->userdata);

	switch (operation)
	{	
	case CR_LOAD:
		if (host->savedScripts) *host->savedScripts = RegisterScript::Get().Names();
		if (host->world) RebuildBehaviors(*host->world);
		// LOG登録
		Debug::g_LogInfo	= host->logInfo;
		Debug::g_LogWarning = host->logWarning;
		Debug::g_LogError	= host->logError;
		break;
	case CR_STEP:
		if (host->world)
			host->world->Each<ScriptComponent>([&](Entity e, ScriptComponent& sc)
				{
					if (sc.behaviors.size() != sc.scriptNames.size())
					{
						sc.behaviors.clear();
						for (auto& name : sc.scriptNames) BuildOne(host->world, e, sc, name);
					}
					for (size_t i = 0; i < sc.behaviors.size(); ++i)
					{
						sc.behaviors[i]->ApplyValues(sc.values[sc.scriptNames[i]]); // exe値を反映
						if (host->isPlaing)
						{
							try
							{
								sc.behaviors[i]->OnUpdate(host->deltaTime);
							}
							catch (const std::exception& e)
							{
								OutputDebugStringA(("[Script] 例外: " + std::string(e.what()) + "\n").c_str());
							}
						}
					}
				});
		break;
	case CR_UNLOAD:
		OutputDebugStringA("[Scripts] UNLOAD\n");
		if (host->world)
			host->world->Each<ScriptComponent>([](Entity, ScriptComponent& sc) {
			sc.behaviors.clear();
				});
		break;
	case CR_CLOSE:
		if (host->world) host->world->Each<ScriptComponent>([](Entity, ScriptComponent& sc) {
			sc.behaviors.clear();
			});
		break;
	default:
		break;
	}

	return 0;
}
