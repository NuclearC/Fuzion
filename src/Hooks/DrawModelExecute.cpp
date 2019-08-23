#include "hooks.h"

#include "../interfaces.h"
#include "../settings.h"

#include "../Hacks/backtrack.h"
#include "../Hacks/chams.h"
#include "../Hacks/esp.h"
#include "../Utils/xorstring.h"

typedef void (*DrawModelExecuteFn)(void*, void*, void*,
                                   const ModelRenderInfo_t&, matrix3x4_t*);

IMaterial* material_backtrack;

void Hooks::DrawModelExecute(void* thisptr, void* context, void* state,
                             const ModelRenderInfo_t& pInfo,
                             matrix3x4_t* pCustomBoneToWorld) {
  if (!Settings::ScreenshotCleaner::enabled || !engine->IsTakingScreenshot()) {
    Chams::DrawModelExecute(thisptr, context, state, pInfo, pCustomBoneToWorld);
  }

  if (!material_backtrack)
    material_backtrack = Util::CreateMaterial(XORSTR("VertexLitGeneric"),
                                              XORSTR("VGUI/white_additive"),
                                              false, true, true, true, true);
  ;

  Color first_color = Color(255, 255, 0, 255),
        fade_color = Color(100, 0, 240, 255), color = first_color;

  const auto max_ticks = BackTrack::backtrack_frames.size();
  for (auto&& frame : BackTrack::backtrack_frames) {
    for (auto&& ticks : frame.records) {
      if (pInfo.entity_index < engine->GetMaxClients() &&
          entityList->GetClientEntity(pInfo.entity_index) == ticks.entity) {
        auto tick_difference = (globalVars->tickcount - frame.tick_count);
        if (tick_difference <= 1) continue;
        color.r = first_color.r + (fade_color.r-first_color.r)*(1 - (float)tick_difference / (float)max_ticks);
        color.g = first_color.g + (fade_color.g-first_color.g)*(1 - (float)tick_difference / (float)max_ticks);
        color.b = first_color.b + (fade_color.b-first_color.b)*(1 - (float)tick_difference / (float)max_ticks);

        material_backtrack->ColorModulate(color);
        material_backtrack->AlphaModulate(0.3 + 0.5 * (1 - (float)tick_difference / (float)max_ticks));

        modelRender->ForcedMaterialOverride(material_backtrack);
        modelRenderVMT->GetOriginalMethod<DrawModelExecuteFn>(21)(
            thisptr, context, state, pInfo, (matrix3x4_t*)ticks.bone_matrix);
      }
    }
  }

  modelRenderVMT->GetOriginalMethod<DrawModelExecuteFn>(21)(
      thisptr, context, state, pInfo, pCustomBoneToWorld);
  modelRender->ForcedMaterialOverride(nullptr);

  if (!Settings::ScreenshotCleaner::enabled || !engine->IsTakingScreenshot()) {
    ESP::DrawModelExecute();
  }
}
