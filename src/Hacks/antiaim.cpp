#include "antiaim.h"

#include "../Hooks/hooks.h"
#include "../Utils/entity.h"
#include "../Utils/math.h"
#include "../interfaces.h"
#include "../settings.h"
#include "aimbot.h"
#include "valvedscheck.h"

bool Settings::AntiAim::Yaw::enabled = false;
bool Settings::AntiAim::Pitch::enabled = false;

AntiAimType_Y Settings::AntiAim::Yaw::type = AntiAimType_Y::NONE;
AntiAimType_Y Settings::AntiAim::Yaw::typeFake = AntiAimType_Y::NONE;
AntiAimType_X Settings::AntiAim::Pitch::type = AntiAimType_X::STATIC_DOWN;

bool Settings::AntiAim::Yaw::doNegativeFlicks = false;
int Settings::AntiAim::Yaw::manualOffset = 180;

bool Settings::AntiAim::HeadEdge::enabled = false;
float Settings::AntiAim::HeadEdge::distance = 25.0f;

bool Settings::AntiAim::AutoDisable::noEnemy = false;
bool Settings::AntiAim::AutoDisable::knifeHeld = false;
bool Settings::AntiAim::LBYBreaker::enabled = false;
float Settings::AntiAim::LBYBreaker::offset = 180.0f;

QAngle AntiAim::realAngle;
QAngle AntiAim::fakeAngle;
QAngle AntiAim::calculatedDesyncAngle{0, 0, 0};

float AntiAim::GetMaxDelta(CCSGOAnimState* animState) {
  float speedFraction =
      std::max(0.0f, std::min(animState->feetShuffleSpeed, 1.0f));

  float speedFactor =
      std::max(0.0f, std::min(1.0f, animState->feetShuffleSpeed2));

  float unk1 = ((animState->runningAccelProgress * -0.30000001) - 0.19999999) *
               speedFraction;
  float unk2 = unk1 + 1.0f;
  float delta;

  if (animState->duckProgress > 0) {
    unk2 += ((animState->duckProgress * speedFactor) * (0.5f - unk2));  // - 1.f
  }

  delta = *(float*)((uintptr_t)animState + 0x3A4) * unk2;

  return delta;
}

static float Distance(Vector a, Vector b) {
  return sqrt(pow(a.x - b.x, 2) + pow(a.y - b.y, 2) + pow(a.z - b.z, 2));
}

static bool GetBestHeadAngle(QAngle& angle) {
  C_BasePlayer* localplayer =
      (C_BasePlayer*)entityList->GetClientEntity(engine->GetLocalPlayer());

  Vector position =
      localplayer->GetVecOrigin() + localplayer->GetVecViewOffset();

  float closest_distance = 100.0f;

  float radius = Settings::AntiAim::HeadEdge::distance + 0.1f;
  float step = M_PI * 2.0 / 8;

  for (float a = 0; a < (M_PI * 2.0); a += step) {
    Vector location(radius * cos(a) + position.x, radius * sin(a) + position.y,
                    position.z);

    Ray_t ray;
    trace_t tr;
    ray.Init(position, location);
    CTraceFilter traceFilter;
    traceFilter.pSkip = localplayer;
    trace->TraceRay(ray, 0x4600400B, &traceFilter, &tr);

    float distance = Distance(position, tr.endpos);

    if (distance < closest_distance) {
      closest_distance = distance;
      angle.y = RAD2DEG(a);
    }
  }

  return closest_distance < Settings::AntiAim::HeadEdge::distance;
}
static bool HasViableEnemy() {
  C_BasePlayer* localplayer =
      (C_BasePlayer*)entityList->GetClientEntity(engine->GetLocalPlayer());

  for (int i = 1; i < engine->GetMaxClients(); ++i) {
    C_BasePlayer* entity = (C_BasePlayer*)entityList->GetClientEntity(i);

    if (!entity || entity == localplayer || entity->GetDormant() ||
        !entity->GetAlive() || entity->GetImmune())
      continue;

    if (!Aimbot::friends.empty())  // check for friends, if any
    {
      IEngineClient::player_info_t entityInformation;
      engine->GetPlayerInfo(i, &entityInformation);

      if (std::find(Aimbot::friends.begin(), Aimbot::friends.end(),
                    entityInformation.xuid) != Aimbot::friends.end())
        continue;
    }

    if (Settings::Aimbot::friendly || !Entity::IsTeamMate(entity, localplayer))
      return true;
  }

  return false;
}
static void DoAntiAimY(C_BasePlayer* const localplayer, QAngle& angle,
                       bool bSend) {
  AntiAimType_Y aa_type =
      bSend ? Settings::AntiAim::Yaw::typeFake : Settings::AntiAim::Yaw::type;

  const auto old_angle = angle;

  float maxDelta = AntiAim::GetMaxDelta(localplayer->GetAnimState());

  // float lby = *localplayer->GetLowerBodyYawTarget();
  static bool bFlip = false;
  bFlip = !bFlip;
  switch (aa_type) {
    case AntiAimType_Y::MAX_DELTA_LEFT:
      if (!bSend) angle.y -= maxDelta;
      break;
    case AntiAimType_Y::MAX_DELTA_RIGHT:
      if (!bSend) angle.y += maxDelta;
      break;
    case AntiAimType_Y::MAX_DELTA_FLIPPER:
      angle.y += bSend ? 180.f : -maxDelta;
      break;
    case AntiAimType_Y::MAX_DELTA_LBY_AVOID:

      break;
    case AntiAimType_Y::CUSTOM_FLIPPER:
      if (!bSend) angle.y += Settings::AntiAim::Yaw::manualOffset;
      break;
    case AntiAimType_Y::CUSTOM:
      break;
    case AntiAimType_Y::DESYNC:
      if (!bSend) angle.y += 180.f;
      break;
    default:
      break;
  }
  if (bSend) {
    AntiAim::fakeAngle.y = angle.y;
  } else {
    AntiAim::realAngle.y = old_angle.y;
  }
}

static void DoAntiAimX(QAngle& angle, bool bFlip, bool& clamp) {
  static float pDance = 0.0f;
  AntiAimType_X aa_type = Settings::AntiAim::Pitch::type;

  switch (aa_type) {
    case AntiAimType_X::STATIC_UP:
      angle.x = -89.0f;
      break;
    case AntiAimType_X::STATIC_DOWN:
      angle.x = 89.0f;
      break;
    case AntiAimType_X::DANCE:
      pDance += 45.0f;
      if (pDance > 100)
        pDance = 0.0f;
      else if (pDance > 75.f)
        angle.x = -89.f;
      else if (pDance < 75.f)
        angle.x = 89.f;
      break;
    case AntiAimType_X::FRONT:
      angle.x = 0.0f;
      break;
    case AntiAimType_X::STATIC_UP_FAKE:
      angle.x = bFlip ? 89.0f : -89.0f;
      break;
    case AntiAimType_X::STATIC_DOWN_FAKE:
      angle.x = bFlip ? -89.0f : 89.0f;
      break;
    case AntiAimType_X::LISP_DOWN:
      clamp = false;
      angle.x = 1800089.0f;
      break;
    case AntiAimType_X::ANGEL_DOWN:
      clamp = false;
      angle.x = 36000088.0f;
      break;
    case AntiAimType_X::ANGEL_UP:
      clamp = false;
      angle.x = 35999912.0f;
      break;
    default:
      break;
  }
}

void AntiAim::CreateMove(CUserCmd* cmd) {
  if (!Settings::AntiAim::Yaw::enabled && !Settings::AntiAim::Pitch::enabled &&
      !Settings::AntiAim::LBYBreaker::enabled)
    return;

  if (Settings::Aimbot::AimStep::enabled && Aimbot::aimStepInProgress) return;

  QAngle oldAngle = cmd->viewangles;
  float oldForward = cmd->forwardmove;
  float oldSideMove = cmd->sidemove;

  AntiAim::realAngle = AntiAim::fakeAngle = CreateMove::lastTickViewAngles;

  QAngle angle = cmd->viewangles;

  C_BasePlayer* localplayer =
      (C_BasePlayer*)entityList->GetClientEntity(engine->GetLocalPlayer());
  if (!localplayer || !localplayer->GetAlive()) return;

  C_BaseCombatWeapon* activeWeapon =
      (C_BaseCombatWeapon*)entityList->GetClientEntityFromHandle(
          localplayer->GetActiveWeapon());
  if (!activeWeapon) return;

  if (activeWeapon->GetCSWpnData()->GetWeaponType() ==
      CSWeaponType::WEAPONTYPE_GRENADE) {
    C_BaseCSGrenade* csGrenade = (C_BaseCSGrenade*)activeWeapon;

    if (csGrenade->GetThrowTime() > 0.f) return;
  }

  if (cmd->buttons & IN_USE || cmd->buttons & IN_ATTACK ||
      (cmd->buttons & IN_ATTACK2 && *activeWeapon->GetItemDefinitionIndex() ==
                                        ItemDefinitionIndex::WEAPON_REVOLVER))
    return;

  if (localplayer->GetMoveType() == MOVETYPE_LADDER ||
      localplayer->GetMoveType() == MOVETYPE_NOCLIP)
    return;

  // Knife
  if (Settings::AntiAim::AutoDisable::knifeHeld && localplayer->GetAlive() &&
      activeWeapon->GetCSWpnData()->GetWeaponType() ==
          CSWeaponType::WEAPONTYPE_KNIFE)
    return;

  if (Settings::AntiAim::AutoDisable::noEnemy && localplayer->GetAlive() &&
      !HasViableEnemy())
    return;

  QAngle edge_angle = angle;
  bool edging_head =
      Settings::AntiAim::HeadEdge::enabled && GetBestHeadAngle(edge_angle);

  static bool bSend = true;
  bSend = !bSend;

  bool should_clamp = true;

  bool needToFlick = false;
  static bool lbyBreak = false;
  static float lastCheck;
  float vel2D =
      localplayer->GetVelocity()
          .Length2D();  // localplayer->GetAnimState()->verticalVelocity +
                        // localplayer->GetAnimState()->horizontalVelocity;

  if (Settings::AntiAim::LBYBreaker::enabled) {
    if (vel2D >= 0.1f || !(localplayer->GetFlags() & FL_ONGROUND) ||
        localplayer->GetFlags() & FL_FROZEN) {
      lbyBreak = false;
      lastCheck = globalVars->curtime;
    } else {
      if (!lbyBreak && (globalVars->curtime - lastCheck) > 0.22) {
        angle.y -= Settings::AntiAim::LBYBreaker::offset;
        lbyBreak = true;
        lastCheck = globalVars->curtime;
        needToFlick = true;
      } else if (lbyBreak && (globalVars->curtime - lastCheck) > 1.1) {
        angle.y -= Settings::AntiAim::LBYBreaker::offset;
        lbyBreak = true;
        lastCheck = globalVars->curtime;
        needToFlick = true;
      }
    }
  }

  if (Settings::AntiAim::Yaw::enabled) {
    // if (Settings::AntiAim::Yaw::type == AntiAimType_Y::CUSTOM) {
    //   static int turn_position = 0;
    //   if (oldSideMove > 0.5f) {
    //     turn_position = 2;
    //   } else if (oldSideMove < -0.5f) {
    //     turn_position = 1;
    //   }

    //   const auto max_delta =
    //   AntiAim::GetMaxDelta(localplayer->GetAnimState());
    //   localplayer->GetAnimState();
    //   calculatedDesyncAngle = {0, max_delta, 0};

    //   angle.y += turn_position == 1 ? Settings::AntiAim::Yaw::manualOffset
    //                                 : -Settings::AntiAim::Yaw::manualOffset;

    //   if (Settings::AntiAim::Yaw::typeFake == AntiAimType_Y::DESYNC) {
    //     if (!bSend) angle.y += 180.f;
    //   }

    //   if (!bSend) {
    //     AntiAim::realAngle.y = angle.y;
    //   } else {
    //     AntiAim::fakeAngle.y = angle.y;
    //   }

    //   CreateMove::sendPacket = bSend;
    // } else
    if (!needToFlick) {
      DoAntiAimY(localplayer, angle, bSend);

      CreateMove::sendPacket = bSend;
      if (Settings::AntiAim::HeadEdge::enabled && edging_head && !bSend)
        angle.y = edge_angle.y;
    }
  }

  if (!ValveDSCheck::forceUT && (*csGameRules) && (*csGameRules)->IsValveDS()) {
    if (Settings::AntiAim::Pitch::type >= AntiAimType_X::STATIC_UP_FAKE)
      Settings::AntiAim::Pitch::type = AntiAimType_X::STATIC_UP;
  }

  if (Settings::AntiAim::Pitch::enabled) DoAntiAimX(angle, bSend, should_clamp);

  if (should_clamp) {
    Math::NormalizeAngles(angle);
    Math::ClampAngles(angle);
  }

  cmd->viewangles = angle;

  Math::CorrectMovement(oldAngle, cmd, oldForward, oldSideMove);
}
