#include "hooks.h"

#include "../interfaces.h"

#include "../Hacks/asuswalls.h"
#include "../Hacks/customglow.h"
#include "../Hacks/fakelatency.h"
#include "../Hacks/noflash.h"
#include "../Hacks/nosmoke.h"
#include "../Hacks/resolver.h"
#include "../Hacks/skinchanger.h"
#include "../Hacks/skybox.h"
#include "../Hacks/thirdperson.h"
#include "../Hacks/view.h"

typedef void (*FrameStageNotifyFn)(void*, ClientFrameStage_t);

void Hooks::FrameStageNotify(void* thisptr, ClientFrameStage_t stage) {
  CustomGlow::FrameStageNotify(stage);
  SkinChanger::FrameStageNotifyModels(stage);
  SkinChanger::FrameStageNotifySkins(stage);
  Noflash::FrameStageNotify(stage);
  View::FrameStageNotify(stage);
  Resolver::FrameStageNotify(stage);
  SkyBox::FrameStageNotify(stage);
  ASUSWalls::FrameStageNotify(stage);
  NoSmoke::FrameStageNotify(stage);
  ThirdPerson::FrameStageNotify(stage);
  if (stage == ClientFrameStage_t::FRAME_NET_UPDATE_POSTDATAUPDATE_START)
    FakeLatency::UpdateIncomingSequences();

  if (SkinChanger::forceFullUpdate) {
    GetLocalClient(-1)->m_nDeltaTick = -1;
    SkinChanger::forceFullUpdate = false;
  }

  clientVMT->GetOriginalMethod<FrameStageNotifyFn>(37)(thisptr, stage);

  Resolver::PostFrameStageNotify(stage);
  View::PostFrameStageNotify(stage);
}
