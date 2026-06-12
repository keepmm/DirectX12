#include "cr.h"
#include "RegisterScript.hpp"
#include "World.hpp"
#include "Engine/Engine.hpp"

CR_EXPORT int cr_main(cr_plugin* ctx, cr_op operation)
{
	auto* host = static_cast<ScriptContext*>(ctx->userdata);

	return 0;
}
