#include "AimbotProjectile.h"

#include "../Aimbot.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../../TickHandler/TickHandler.h"
#include "../../Visuals/Visuals.h"

//#define SPLASH_DEBUG1 // normal splash visualization
//#define SPLASH_DEBUG2 // obstructed splash visualization
//#define SPLASH_DEBUG3 // points visualization
//#define SPLASH_DEBUG4 // trace visualization
//#define SPLASH_DEBUG5 // trace count

#ifdef SPLASH_DEBUG5
static std::unordered_map<std::string, int> mTraceCount = {};
#endif

std::vector<Target_t> CAimbotProjectile::GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;
	const auto iSort = Vars::Aimbot::General::TargetSelection.Value;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	{
		EGroupType eGroupType = EGroupType::GROUP_INVALID;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
			eGroupType = EGroupType::PLAYERS_ENEMIES;
		if (Vars::Aimbot::Healing::AutoHeal.Value)
		{
			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_CROSSBOW: eGroupType = eGroupType == EGroupType::PLAYERS_ENEMIES ? EGroupType::PLAYERS_ALL : EGroupType::PLAYERS_TEAMMATES; break;
			case TF_WEAPON_LUNCHBOX: eGroupType = EGroupType::PLAYERS_TEAMMATES; break;
			}
		}

		for (auto pEntity : H::Entities.GetGroup(eGroupType))
		{
			bool bTeammate = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			if (bTeammate)
			{
				if (pEntity->As<CTFPlayer>()->m_iHealth() >= pEntity->As<CTFPlayer>()->GetMaxHealth()
					|| Vars::Aimbot::Healing::FriendsOnly.Value && !H::Entities.IsFriend(pEntity->entindex()) && !H::Entities.InParty(pEntity->entindex()))
					continue;
			}

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, bTeammate ? 0 : F::AimbotGlobal.GetPriority(pEntity->entindex()));
		}
	}

	if (Vars::Aimbot::General::Target.Value)
	{
		bool bIsRescueRanger = pWeapon->GetWeaponID() == TF_WEAPON_SHOTGUN_BUILDING_RESCUE;
		for (auto pEntity : H::Entities.GetGroup(bIsRescueRanger ? EGroupType::BUILDINGS_ALL : EGroupType::BUILDINGS_ENEMIES))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum() && pEntity->As<CBaseObject>()->m_iHealth() >= pEntity->As<CBaseObject>()->m_iMaxHealth())
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.emplace_back(pEntity, pEntity->IsSentrygun() ? TargetEnum::Sentry : pEntity->IsDispenser() ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Stickies)
	{
		bool bShouldAim = false;
		switch (pWeapon->m_iItemDefinitionIndex())
		{
		case Demoman_s_TheQuickiebombLauncher:
		case Demoman_s_TheScottishResistance:
		case Pyro_s_TheScorchShot:
			bShouldAim = true;
		}

		if (bShouldAim)
		{
			for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_PROJECTILES))
			{
				if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
					continue;

				Vec3 vPos = pEntity->GetCenter();
				Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
				float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
				if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
					continue;

				float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
				vTargets.emplace_back(pEntity, TargetEnum::Sticky, vPos, vAngleTo, flFOVTo, flDistTo);
			}
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs) // does not predict movement
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_NPC))
		{
			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}

std::vector<Target_t> CAimbotProjectile::SortTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	auto vTargets = GetTargets(pLocal, pWeapon);

	F::AimbotGlobal.SortTargets(&vTargets, Vars::Aimbot::General::TargetSelection.Value);
	vTargets.resize(std::min(size_t(Vars::Aimbot::General::MaxTargets.Value), vTargets.size()));
	F::AimbotGlobal.SortPriority(&vTargets);
	return vTargets;
}



static inline float GetSplashRadius(CTFWeaponBase* pWeapon, CTFPlayer* pLocal = nullptr)
{
	float flRadius = 0.f;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
		flRadius = 146.f;
	}
	if (!flRadius && pWeapon->m_iItemDefinitionIndex() == Pyro_s_TheScorchShot)
		flRadius = 110.f;
	if (!flRadius)
		return 0.f;

	flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		if (pLocal->InCond(TF_COND_BLASTJUMPING) && SDK::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
			flRadius *= 0.8f;
	}
	return flRadius * Vars::Aimbot::Projectile::SplashRadius.Value / 100;
}

static inline int GetSplashMode(CTFWeaponBase* pWeapon)
{
	if (Vars::Aimbot::Projectile::RocketSplashMode.Value)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
			return Vars::Aimbot::Projectile::RocketSplashMode.Value;
		}
	}

	return Vars::Aimbot::Projectile::RocketSplashModeEnum::Regular;
}

static inline float PrimeTime(CTFWeaponBase* pWeapon)
{
	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::UsePrimeTime && pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		static auto tf_grenadelauncher_livetime = U::ConVars.FindVar("tf_grenadelauncher_livetime");
		const float flLiveTime = tf_grenadelauncher_livetime ? tf_grenadelauncher_livetime->GetFloat() : 0.8f;
		return SDK::AttribHookValue(flLiveTime, "sticky_arm_time", pWeapon);
	}

	return 0.f;
}

int CAimbotProjectile::GetHitboxPriority(int nHitbox, Target_t& tTarget, Info_t& tInfo)
{
	bool bHeadshot = tTarget.m_iTargetType == TargetEnum::Player && tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW
		&& Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Head;
	if (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::BodyaimIfLethal && bHeadshot)
	{
		float flCharge = I::GlobalVars->curtime - tInfo.m_pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
		float flDamage = Math::RemapVal(flCharge, 0.f, 1.f, 50.f, 120.f);
		if (tInfo.m_pLocal->IsMiniCritBoosted())
			flDamage *= 1.36f;
		if (flDamage >= tTarget.m_pEntity->As<CTFPlayer>()->m_iHealth())
			bHeadshot = false;

		if (tInfo.m_pLocal->IsCritBoosted()) // for reliability
			bHeadshot = false;
	}
	bool bLower = tTarget.m_iTargetType == TargetEnum::Player && Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::AimBlastAtFeet
		&& tTarget.m_pEntity->As<CTFPlayer>()->IsOnGround() && tInfo.m_flRadius;

	if (bHeadshot)
		tTarget.m_nAimedHitbox = HITBOX_HEAD;

	if (!(Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Auto))
	{
		switch (nHitbox)
		{
		case 0: return Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Head ? 0 : 3;
		case 1: return Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Body ? 1 : 3;
		case 2: return Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Feet ? 2 : 3;
		}
	}
	else
	{
		switch (nHitbox)
		{
		case 0: return Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Head ? (bHeadshot ? 0 : 2) : 3;
		case 1: return Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Body ? (bHeadshot ? 3 : (bLower ? 1 : 0)) : 3;
		case 2: return Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Feet ? (bHeadshot ? 3 : (bLower ? 0 : 1)) : 3;
		}
	}

	return 3;
};

std::unordered_map<int, Vec3> CAimbotProjectile::GetDirectPoints(Target_t& tTarget, Info_t& tInfo)
{
	std::unordered_map<int, Vec3> mPoints = {};

	const Vec3 vMins = tTarget.m_pEntity->m_vecMins(), vMaxs = tTarget.m_pEntity->m_vecMaxs();
	for (int i = 0; i < 3; i++)
	{
		const int iPriority = GetHitboxPriority(i, tTarget, tInfo);
		if (iPriority == 3)
			continue;

		switch (i)
		{
		case 0:
			if (tTarget.m_nAimedHitbox == HITBOX_HEAD)
			{
				//Vec3 vOff = tTarget.m_pEntity->As<CBaseAnimating>()->GetHitboxOrigin(HITBOX_HEAD) - tTarget.m_pEntity->GetAbsOrigin();

				// https://www.youtube.com/watch?v=_PSGD-pJUrM, might be better??
				Vec3 vCenter, vBBoxMins, vBBoxMaxs; tTarget.m_pEntity->As<CBaseAnimating>()->GetHitboxInfo(HITBOX_HEAD, &vCenter, &vBBoxMins, &vBBoxMaxs);
				Vec3 vOff = vCenter + (vBBoxMins + vBBoxMaxs) / 2 - tTarget.m_pEntity->GetAbsOrigin();

				float flLow = 0.f;
				Vec3 vDelta = tTarget.m_vPos + tInfo.m_vTargetEye - tInfo.m_vLocalEye;
				if (vDelta.z > 0)
				{
					float flXY = vDelta.Length2D();
					if (flXY)
						flLow = Math::RemapVal(vDelta.z / flXY, 0.f, 0.5f, 0.f, 1.f);
					else
						flLow = 1.f;
				}

				float flLerp = (Vars::Aimbot::Projectile::HuntsmanLerp.Value + (Vars::Aimbot::Projectile::HuntsmanLerpLow.Value - Vars::Aimbot::Projectile::HuntsmanLerp.Value) * flLow) / 100.f;
				float flAdd = Vars::Aimbot::Projectile::HuntsmanAdd.Value + (Vars::Aimbot::Projectile::HuntsmanAddLow.Value - Vars::Aimbot::Projectile::HuntsmanAdd.Value) * flLow;
				vOff.z += flAdd;
				vOff.z = vOff.z + (vMaxs.z - vOff.z) * flLerp;

				vOff.x = std::clamp(vOff.x, vMins.x + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.x - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				vOff.y = std::clamp(vOff.y, vMins.y + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.y - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				vOff.z = std::clamp(vOff.z, vMins.z + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.z - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				mPoints[iPriority] = vOff;
			}
			else
				mPoints[iPriority] = Vec3(0, 0, vMaxs.z - Vars::Aimbot::Projectile::VerticalShift.Value);
			break;
		case 1: mPoints[iPriority] = Vec3(0, 0, (vMaxs.z - vMins.z) / 2); break;
		case 2: mPoints[iPriority] = Vec3(0, 0, vMins.z + Vars::Aimbot::Projectile::VerticalShift.Value); break;
		}
	}

	return mPoints;
}

// seode
static inline std::vector<std::pair<Vec3, int>> ComputeSphere(float flRadius, int iSamples, float flNthroot)
{
	std::vector<std::pair<Vec3, int>> vPoints;
	vPoints.reserve(iSamples);

	int iPointType = Vars::Aimbot::Projectile::SplashGrates.Value ? PointTypeEnum::Regular | PointTypeEnum::Obscured : PointTypeEnum::Regular;
	float flRotate = Vars::Aimbot::Projectile::SplashRotate.Value * PI / 180.f;
	for (int n = 0; n < iSamples; n++)
	{
		float flA = acosf(1.f - 2.f * n / iSamples);
		float flB = PI * (3.f - sqrtf(5.f)) * n + flRotate;

		Vec3 vPoint = Vec3(sinf(flA) * cosf(flB), sinf(flA) * sinf(flB), -cosf(flA));
		if (flNthroot != 1.f)
		{
			vPoint.x = powf(fabsf(vPoint.x), 1 / flNthroot) * sign(vPoint.x);
			vPoint.y = powf(fabsf(vPoint.y), 1 / flNthroot) * sign(vPoint.y);
			vPoint.z = powf(fabsf(vPoint.z), 1 / flNthroot) * sign(vPoint.z);
			vPoint.Normalize();
		}
		vPoint *= flRadius;

		vPoints.emplace_back(vPoint, iPointType);
	}

	return vPoints;
};

// possibly add air splash for autodet weapons
std::vector<Point_t> CAimbotProjectile::GetSplashPoints(Target_t& tTarget, std::vector<std::pair<Vec3, int>>& vSpherePoints, Info_t& tInfo, int iSimTime)
{
	std::vector<std::pair<Point_t, float>> vPointDistances = {};

	Vec3 vTargetEye = tTarget.m_vPos + tInfo.m_vTargetEye;

#if !defined(SPLASH_DEBUG1) && !defined(SPLASH_DEBUG2)
	auto checkPoint = [&](CGameTrace& trace, bool& bErase, bool& bNormal)
#else
	auto checkPoint = [&](CGameTrace& trace, bool& bErase, bool& bNormal, Vec3* pFrom = nullptr)
#endif
		{
			bErase = trace.fraction == 1.f || !trace.m_pEnt || !trace.m_pEnt->GetAbsVelocity().IsZero() || trace.surface.flags & 0x0004 /*SURF_SKY*/;
#if defined(SPLASH_DEBUG1) || defined(SPLASH_DEBUG2)
			if (pFrom && bErase)
				G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(*pFrom, trace.endpos), I::GlobalVars->curtime + 60.f, Vars::Colors::Halloween.Value);
#endif
			if (bErase)
				return false;

			Point_t tPoint = { trace.endpos, {} };
			if (!tInfo.m_flGravity)
			{
				Vec3 vForward = (trace.endpos - tInfo.m_vLocalEye).Normalized();
				bNormal = vForward.Dot(trace.plane.normal) >= 0;
			}
			if (!bNormal)
			{
				CalculateAngle(tInfo.m_vLocalEye, tPoint.m_vPoint, tInfo, iSimTime, tPoint.m_Solution);
				/*if (!tInfo.m_flGravity)
				{
					Vec3 vForward; Math::AngleVectors({ tPoint.m_Solution.m_flPitch, tPoint.m_Solution.m_flYaw, 0.f }, &vForward);
					bNormal = vForward.Dot(trace.plane.normal) >= 0;
				}
				else*/ if (tInfo.m_flGravity)
				{
					Vec3 vPos = tInfo.m_vLocalEye + Vec3(0, 0, (tInfo.m_flGravity * 800.f * pow(tPoint.m_Solution.m_flTime, 2)) / 2);
					Vec3 vForward = (tPoint.m_vPoint - vPos).Normalized();
					bNormal = vForward.Dot(trace.plane.normal) >= 0;
				}
			}
#if defined(SPLASH_DEBUG1) || defined(SPLASH_DEBUG2)
			if (pFrom)
			{
				G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(*pFrom, trace.endpos), I::GlobalVars->curtime + 60.f, !bNormal ? Vars::Colors::TeamBlu.Value : Vars::Colors::TeamRed.Value);
				//G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace.endpos, trace.endpos - vForward * 10), I::GlobalVars->curtime + 60.f, Vars::Colors::Local.Value);
			}
#endif
			if (bNormal)
				return false;

			bErase = tPoint.m_Solution.m_iCalculated == 1;
			if (!bErase || !tInfo.m_flPrimeTime && int(tPoint.m_Solution.m_flTime / TICK_INTERVAL) + 1 != iSimTime)
				return false;

			vPointDistances.emplace_back(tPoint, tPoint.m_vPoint.DistTo(tTarget.m_vPos));
			return true;
		};
	for (auto it = vSpherePoints.begin(); it != vSpherePoints.end();)
	{
		Vec3 vPoint = it->first + vTargetEye;
		int& iType = it->second;

		Solution_t solution; CalculateAngle(tInfo.m_vLocalEye, vPoint, tInfo, iSimTime, solution, false);
		
		if (solution.m_iCalculated == 3)
			iType = 0;
		else if (abs(solution.m_flTime - TICKS_TO_TIME(iSimTime)) < tInfo.m_flRadiusTime || tInfo.m_flPrimeTime && iSimTime == tInfo.m_iPrimeTime)
		{
			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnly filter = {};

			if (iType & PointTypeEnum::Regular)
			{
				bool bErase = false, bNormal = false;

				SDK::TraceHull(vTargetEye, vPoint, tInfo.m_vHull * -1, tInfo.m_vHull, MASK_SOLID, &filter, &trace);
#ifdef SPLASH_DEBUG5
				mTraceCount["Splash regular"]++;
#endif
#ifndef SPLASH_DEBUG1
				checkPoint(trace, bErase, bNormal);
#else
				checkPoint(trace, bErase, bNormal, &vTargetEye);
#endif

				if (bErase)
					iType = 0;
				else if (bNormal)
					iType &= ~PointTypeEnum::Regular;
				else
					iType &= ~PointTypeEnum::Obscured;
			}
			if (iType & PointTypeEnum::Obscured)
			{
				bool bErase = false, bNormal = false;

				switch (tInfo.m_iSplashMode)
				{
				// just do this for non rockets, it's less expensive
				case Vars::Aimbot::Projectile::RocketSplashModeEnum::Regular:
				{
					SDK::Trace(vPoint, vTargetEye, MASK_SHOT, &filter, &trace);
#ifdef SPLASH_DEBUG5
					mTraceCount["Splash grate check"]++;
#endif
					bNormal = trace.DidHit();
#ifdef SPLASH_DEBUG2
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vPoint, trace.endpos), I::GlobalVars->curtime + 60.f, bNormal ? Vars::Colors::IndicatorGood.Value : Vars::Colors::IndicatorBad.Value);
#endif
					if (!bNormal)
					{
						SDK::TraceHull(vPoint, vTargetEye, tInfo.m_vHull * -1, tInfo.m_vHull, MASK_SOLID, &filter, &trace);
#ifdef SPLASH_DEBUG5
						mTraceCount["Splash grate"]++;
#endif
#ifndef SPLASH_DEBUG2
						checkPoint(trace, bErase, bNormal);
#else
						checkPoint(trace, bErase, bNormal, &vPoint);
#endif
					}
					break;
				}
				// currently experimental, there may be a more efficient way to do this?
				case Vars::Aimbot::Projectile::RocketSplashModeEnum::SpecialLight:
				{
					SDK::Trace(vPoint, vTargetEye, MASK_SOLID, &filter, &trace);
#ifdef SPLASH_DEBUG5
					mTraceCount["Splash rocket"]++;
#endif
					bNormal = trace.fraction == 1.f /*|| trace.startsolid*/;
					if (!bNormal)
					{
#ifndef SPLASH_DEBUG2
						if (checkPoint(trace, bErase, bNormal))
#else
						G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vPoint, trace.endpos), I::GlobalVars->curtime + 60.f, Vars::Colors::IndicatorBad.Value);
						if (checkPoint(trace, bErase, bNormal, &vPoint))
#endif
						{
							SDK::Trace(trace.endpos + trace.plane.normal, vTargetEye, MASK_SHOT, &filter, &trace);
#ifdef SPLASH_DEBUG5
							mTraceCount["Splash rocket check"]++;
#endif
							if (trace.fraction < 1.f)
								vPointDistances.pop_back();
						}
					}
					break;
				}
				case Vars::Aimbot::Projectile::RocketSplashModeEnum::SpecialHeavy:
				{
					SDK::Trace(vTargetEye, vPoint, MASK_SOLID, &filter, &trace);
#ifdef SPLASH_DEBUG5
					mTraceCount["Splash rocket"]++;
#endif
					bErase = trace.fraction == 1.f /*|| trace.startsolid*/;
					if (!bNormal)
					{
						std::vector<std::tuple<Vec3, Vec3, bool>> vPoints = { { trace.endpos + (vPoint - trace.endpos).Normalized(), vPoint, false }, { vPoint, vTargetEye, true } };
						for (auto& [vFrom, vTo, bErases] : vPoints)
						{
							bool bAdded = false;

							SDK::Trace(vFrom, vTo, MASK_SOLID, &filter, &trace);
#ifdef SPLASH_DEBUG5
							mTraceCount["Splash rocket (2)"]++;
#endif
							if (trace.fraction < 1.f)
							{
#ifndef SPLASH_DEBUG2
								if (checkPoint(trace, bErase, bNormal))
#else
								G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vFrom, trace.endpos), I::GlobalVars->curtime + 60.f, bErases ? Vars::Colors::IndicatorBad.Value : Vars::Colors::IndicatorGood.Value);
								if (checkPoint(trace, bErase, bNormal, &vPoint))
#endif
								{
									SDK::Trace(trace.endpos + trace.plane.normal, vTargetEye, MASK_SHOT, &filter, &trace);
#ifdef SPLASH_DEBUG5
									mTraceCount["Splash rocket check"]++;
#endif
									bAdded = trace.fraction == 1.f;
									if (!bAdded)
										vPointDistances.pop_back();
								}
							}

							if (!bErases && !bAdded)
								bErase = bNormal = false;
							if (bErase || bNormal)
								break;
						}
					}
					break;
				}
				}

				if (bErase)
					iType = 0;
				else if (bNormal)
					iType &= ~PointTypeEnum::Obscured;
				else
					iType &= ~PointTypeEnum::Regular;
			}
		}

		if (!iType)
			it = vSpherePoints.erase(it);
		else
			++it;
	}

	std::sort(vPointDistances.begin(), vPointDistances.end(), [&](const auto& a, const auto& b) -> bool
		{
			return a.second < b.second;
		});

	std::vector<Point_t> vPoints = {};
	int iSplashCount = std::min(
		tInfo.m_flPrimeTime && iSimTime == tInfo.m_iPrimeTime ? Vars::Aimbot::Projectile::SplashCountDirect.Value : tInfo.m_iSplashCount,
		int(vPointDistances.size())
	);
	for (int i = 0; i < iSplashCount; i++)
		vPoints.push_back(vPointDistances[i].first);

	const Vec3 vOriginal = tTarget.m_pEntity->GetAbsOrigin();
	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	for (auto it = vPoints.begin(); it != vPoints.end();)
	{
		auto& vPoint = *it;
		bool bValid = vPoint.m_Solution.m_iCalculated;
		if (bValid)
		{
			Vec3 vPos = {}; reinterpret_cast<CCollisionProperty*>(tTarget.m_pEntity->GetCollideable())->CalcNearestPoint(vPoint.m_vPoint, &vPos);
			bValid = vPoint.m_vPoint.DistTo(vPos) < tInfo.m_flRadius;
		}

		if (bValid)
			++it;
		else
			it = vPoints.erase(it);
	}
	tTarget.m_pEntity->SetAbsOrigin(vOriginal);

	return vPoints;
}

static inline float AABBLine(Vec3 vMins, Vec3 vMaxs, Vec3 vStart, Vec3 vDir)
{
	Vec3 a = {
		(vMins.x - vStart.x) / vDir.x,
		(vMins.y - vStart.y) / vDir.y,
		(vMins.z - vStart.z) / vDir.z
	};
	Vec3 b = {
		(vMaxs.x - vStart.x) / vDir.x,
		(vMaxs.y - vStart.y) / vDir.y,
		(vMaxs.z - vStart.z) / vDir.z
	};
	Vec3 c = {
		std::min(a.x, b.x),
		std::min(a.y, b.y),
		std::min(a.z, b.z)
	};
	return std::max(std::max(c.x, c.y), c.z);
}
static inline Vec3 PullPoint(Vec3 vPoint, Vec3 vLocalPos, Info_t& tInfo, Vec3 vMins, Vec3 vMaxs, Vec3 vTargetPos)
{
	auto HeightenLocalPos = [&]()
		{	// basic trajectory pass
			const float flGrav = tInfo.m_flGravity * 800.f;
			if (!flGrav)
				return vPoint;

			const Vec3 vDelta = vTargetPos - vLocalPos;
			const float flDist = vDelta.Length2D();

			const float flRoot = pow(tInfo.m_flVelocity, 4) - flGrav * (flGrav * pow(flDist, 2) + 2.f * vDelta.z * pow(tInfo.m_flVelocity, 2));
			if (flRoot < 0.f)
				return vPoint;
			float flPitch = atan((pow(tInfo.m_flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));

			float flTime = flDist / (cos(flPitch) * tInfo.m_flVelocity) - tInfo.m_flOffsetTime;
			return vLocalPos + Vec3(0, 0, (flGrav * pow(flTime, 2)) / 2);
		};

	vLocalPos = HeightenLocalPos();
	Vec3 vForward, vRight, vUp; Math::AngleVectors(Math::CalcAngle(vLocalPos, vPoint), &vForward, &vRight, &vUp);
	vLocalPos += (vForward * tInfo.m_vOffset.x) + (vRight * tInfo.m_vOffset.y) + (vUp * tInfo.m_vOffset.z);
	return vLocalPos + (vPoint - vLocalPos) * fabsf(AABBLine(vMins + vTargetPos, vMaxs + vTargetPos, vLocalPos, vPoint - vLocalPos));
}



static inline void SolveProjectileSpeed(CTFWeaponBase* pWeapon, const Vec3& vLocalPos, const Vec3& vTargetPos, float& flVelocity, float& flDragTime, const float flGravity)
{
	if (!F::ProjSim.obj->IsDragEnabled() || F::ProjSim.obj->m_dragBasis.IsZero())
		return;

	const float flGrav = flGravity * 800.0f;
	const Vec3 vDelta = vTargetPos - vLocalPos;
	const float flDist = vDelta.Length2D();

	const float flRoot = pow(flVelocity, 4) - flGrav * (flGrav * pow(flDist, 2) + 2.f * vDelta.z * pow(flVelocity, 2));
	if (flRoot < 0.f)
		return;

	const float flPitch = atan((pow(flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
	const float flTime = flDist / (cos(flPitch) * flVelocity);

	float flDrag = 0.f;
	if (Vars::Aimbot::Projectile::DragOverride.Value)
		flDrag = Vars::Aimbot::Projectile::DragOverride.Value;
	else
	{
		switch (pWeapon->m_iItemDefinitionIndex()) // the remaps are dumb but they work so /shrug
		{
		case Demoman_m_GrenadeLauncher:
		case Demoman_m_GrenadeLauncherR:
		case Demoman_m_FestiveGrenadeLauncher:
		case Demoman_m_Autumn:
		case Demoman_m_MacabreWeb:
		case Demoman_m_Rainbow:
		case Demoman_m_SweetDreams:
		case Demoman_m_CoffinNail:
		case Demoman_m_TopShelf:
		case Demoman_m_Warhawk:
		case Demoman_m_ButcherBird:
		case Demoman_m_TheIronBomber: flDrag = Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.120f, 0.200f); break; // 0.120 normal, 0.200 capped, 0.300 v3000
		case Demoman_m_TheLochnLoad: flDrag = Math::RemapVal(flVelocity, 1504.f, k_flMaxVelocity, 0.070f, 0.085f); break; // 0.070 normal, 0.085 capped, 0.120 v3000
		case Demoman_m_TheLooseCannon: flDrag = Math::RemapVal(flVelocity, 1454.f, k_flMaxVelocity, 0.385f, 0.530f); break; // 0.385 normal, 0.530 capped, 0.790 v3000
		case Demoman_s_StickybombLauncher:
		case Demoman_s_StickybombLauncherR:
		case Demoman_s_FestiveStickybombLauncher:
		case Demoman_s_TheQuickiebombLauncher:
		case Demoman_s_TheScottishResistance: flDrag = Math::RemapVal(flVelocity, 922.f, k_flMaxVelocity, 0.085f, 0.190f); break; // 0.085 low, 0.190 capped, 0.230 v2400
		case Scout_s_TheFlyingGuillotine:
		case Scout_s_TheFlyingGuillotineG: flDrag = 0.31f; break;
		case Scout_t_TheSandman: flDrag = 0.180f; break;
		case Scout_t_TheWrapAssassin: flDrag = 0.285f; break;
		}
	}

	float flOverride = Vars::Aimbot::Projectile::TimeOverride.Value;
	flDragTime = powf(flTime, 2) * flDrag / (flOverride ? flOverride : 1.5f); // rough estimate to prevent m_flTime being too low
	flVelocity = flVelocity - flVelocity * flTime * flDrag;
}
void CAimbotProjectile::CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, Info_t& tInfo, int iSimTime, Solution_t& out, bool bAccuracy)
{
	if (out.m_iCalculated)
		return;

	const float flGrav = tInfo.m_flGravity * 800.f;
	Vec3 vAngleTo, vNewAngleTo;

	float flPitch, flYaw;
	{	// basic trajectory pass
		float flVelocity = tInfo.m_flVelocity, flDragTime = 0.f;
		if (F::ProjSim.obj->IsDragEnabled() && !F::ProjSim.obj->m_dragBasis.IsZero())
		{
			Vec3 vForward, vRight, vUp; Math::AngleVectors(Math::CalcAngle(vLocalPos, vTargetPos), &vForward, &vRight, &vUp);
			Vec3 vShootPos = vLocalPos + (vForward * tInfo.m_vOffset.x) + (vRight * tInfo.m_vOffset.y) + (vUp * tInfo.m_vOffset.z);
			SolveProjectileSpeed(tInfo.m_pWeapon, vShootPos, vTargetPos, flVelocity, flDragTime, tInfo.m_flGravity);
		}

		Vec3 vDelta = vTargetPos - vLocalPos;
		float flDist = vDelta.Length2D();

		vAngleTo = Math::CalcAngle(vLocalPos, vTargetPos);
		if (!flGrav)
			flPitch = -DEG2RAD(vAngleTo.x);
		else
		{	// arch
			float flRoot = pow(flVelocity, 4) - flGrav * (flGrav * pow(flDist, 2) + 2.f * vDelta.z * pow(flVelocity, 2));
			if (flRoot < 0.f)
			{
				out.m_iCalculated = 3; return;
			}
			flPitch = atan((pow(flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
		}
		out.m_flTime = flDist / (cos(flPitch) * flVelocity) - tInfo.m_flOffsetTime + flDragTime;
		out.m_flPitch = flPitch = -RAD2DEG(flPitch) - tInfo.m_vAngFix.x;
		out.m_flYaw = flYaw = vAngleTo.y - tInfo.m_vAngFix.y;
	}

	int iTimeTo = int(out.m_flTime / TICK_INTERVAL) + 1;
	if (iTimeTo > iSimTime ? 2 : 0)
	{
		out.m_iCalculated = 2; return;
	}

#ifdef SPLASH_DEBUG5
	int iFlags = (bAccuracy ? ProjSimEnum::Trace : ProjSimEnum::None) | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
	if (iFlags & ProjSimEnum::Trace)
	{
		if (Vars::Visuals::Trajectory::Override.Value)
		{
			if (!Vars::Visuals::Trajectory::Pipes.Value)
				mTraceCount["Setup trace"]++;
		}
		else
		{
			switch (tInfo.m_pWeapon->GetWeaponID())
			{
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
			case TF_WEAPON_RAYGUN:
			case TF_WEAPON_DRG_POMSON:
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE:
			case TF_WEAPON_COMPOUND_BOW:
			case TF_WEAPON_CROSSBOW:
			case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
			case TF_WEAPON_SYRINGEGUN_MEDIC:
			case TF_WEAPON_FLAME_BALL:
				mTraceCount["Setup trace"]++;
			}
		}
	}
	if (iFlags & ProjSimEnum::InitCheck)
		mTraceCount["Trace init check"]++;
#endif
	ProjectileInfo tProjInfo = {};
	if (!F::ProjSim.GetInfo(tInfo.m_pLocal, tInfo.m_pWeapon, { flPitch, flYaw, 0 }, tProjInfo, (bAccuracy ? ProjSimEnum::Trace : ProjSimEnum::None) | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum))
	{
		out.m_iCalculated = 3; return;
	}

	{	// calculate trajectory from projectile origin
		float flVelocity = tInfo.m_flVelocity, flDragTime = 0.f;
		SolveProjectileSpeed(tInfo.m_pWeapon, tProjInfo.m_vPos, vTargetPos, flVelocity, flDragTime, tInfo.m_flGravity);

		Vec3 vDelta = vTargetPos - tProjInfo.m_vPos;
		float flDist = vDelta.Length2D();

		vNewAngleTo = Math::CalcAngle(tProjInfo.m_vPos, vTargetPos);
		if (!flGrav)
			out.m_flPitch = -DEG2RAD(vNewAngleTo.x);
		else
		{	// arch
			float flRoot = pow(flVelocity, 4) - flGrav * (flGrav * pow(flDist, 2) + 2.f * vDelta.z * pow(flVelocity, 2));
			if (flRoot < 0.f)
			{
				out.m_iCalculated = 3; return;
			}
			out.m_flPitch = atan((pow(flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
		}
		out.m_flTime = flDist / (cos(out.m_flPitch) * flVelocity) + flDragTime;
	}

	{	// correct yaw

		Vec3 vShootPos = tProjInfo.m_vPos - vLocalPos; vShootPos.z = 0;
		Vec3 vTarget = vTargetPos - vLocalPos;
		Vec3 vForward; Math::AngleVectors(tProjInfo.m_vAng, &vForward); vForward.z = 0; vForward.Normalize();
		float flB = 2 * (vShootPos.x * vForward.x + vShootPos.y * vForward.y);
		float flC = vShootPos.Length2DSqr() - vTarget.Length2DSqr();
		auto vSolutions = Math::SolveQuadratic(1.f, flB, flC);
		if (!vSolutions.empty())
		{
			vShootPos += vForward * vSolutions.front();
			out.m_flYaw = flYaw - (RAD2DEG(atan2(vShootPos.y, vShootPos.x)) - flYaw);
			flYaw = RAD2DEG(atan2(vShootPos.y, vShootPos.x));
		}
	}

	{	// correct pitch
		if (flGrav)
		{
			flPitch -= tProjInfo.m_vAng.x;
			out.m_flPitch = -RAD2DEG(out.m_flPitch) + flPitch - tInfo.m_vAngFix.x;
		}
		else
		{
			Vec3 vShootPos = Math::RotatePoint(tProjInfo.m_vPos - vLocalPos, {}, { 0, -flYaw, 0 }); vShootPos.y = 0;
			Vec3 vTarget = Math::RotatePoint(vTargetPos - vLocalPos, {}, { 0, -flYaw, 0 });
			Vec3 vForward; Math::AngleVectors(tProjInfo.m_vAng - Vec3(0, flYaw, 0), &vForward); vForward.y = 0; vForward.Normalize();
			float flB = 2 * (vShootPos.x * vForward.x + vShootPos.z * vForward.z);
			float flC = (powf(vShootPos.x, 2) + powf(vShootPos.z, 2)) - (powf(vTarget.x, 2) + powf(vTarget.z, 2));
			auto vSolutions = Math::SolveQuadratic(1.f, flB, flC);
			if (!vSolutions.empty())
			{
				vShootPos += vForward * vSolutions.front();
				out.m_flPitch = flPitch - (RAD2DEG(atan2(-vShootPos.z, vShootPos.x)) - flPitch);
			}
		}
	}

	iTimeTo = int(out.m_flTime / TICK_INTERVAL) + 1;
	out.m_iCalculated = iTimeTo > iSimTime ? 2 : 1;
}



class CTraceFilterProjectileNoPlayer : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;
	CBaseEntity* pSkip = nullptr;
};
bool CTraceFilterProjectileNoPlayer::ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask)
{
	if (!pServerEntity || pServerEntity == pSkip)
		return false;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pServerEntity);

	switch (pEntity->GetClassID())
	{
	case ETFClassID::CBaseEntity:
	case ETFClassID::CBaseDoor:
	case ETFClassID::CDynamicProp:
	case ETFClassID::CPhysicsProp:
	case ETFClassID::CObjectCartDispenser:
	case ETFClassID::CFuncTrackTrain:
	case ETFClassID::CFuncConveyor:
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter: return true;
	}

	return false;
}
TraceType_t CTraceFilterProjectileNoPlayer::GetTraceType() const
{
	return TRACE_EVERYTHING;
}

bool CAimbotProjectile::TestAngle(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, Target_t& tTarget, Vec3& vPoint, Vec3& vAngles, int iSimTime, bool bSplash, bool* pHitSolid, std::deque<Vec3>* pProjectilePath)
{
#ifdef SPLASH_DEBUG5
	int iFlags = ProjSimEnum::Trace | ProjSimEnum::InitCheck | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
	if (iFlags & ProjSimEnum::Trace)
	{
		if (Vars::Visuals::Trajectory::Override.Value)
		{
			if (!Vars::Visuals::Trajectory::Pipes.Value)
				mTraceCount["Setup trace"]++;
		}
		else
		{
			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
			case TF_WEAPON_RAYGUN:
			case TF_WEAPON_DRG_POMSON:
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE:
			case TF_WEAPON_COMPOUND_BOW:
			case TF_WEAPON_CROSSBOW:
			case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
			case TF_WEAPON_SYRINGEGUN_MEDIC:
			case TF_WEAPON_FLAME_BALL:
				mTraceCount["Setup trace"]++;
			}
		}
	}
	if (iFlags & ProjSimEnum::InitCheck)
		mTraceCount["Trace init check"]++;
#endif
	ProjectileInfo tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, vAngles, tProjInfo, ProjSimEnum::Trace | ProjSimEnum::InitCheck | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum) || !F::ProjSim.Initialize(tProjInfo))
		return false;

	bool bDidHit = false;

	CGameTrace trace = {};
	CTraceFilterProjectile filter = {}; filter.pSkip = pLocal;
	CTraceFilterProjectileNoPlayer filterSplash = {};

#ifdef SPLASH_DEBUG4
	G::BoxStorage.emplace_back(vPoint, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 0), Color_t(0, 0, 0, 0));
#endif

	if (!tProjInfo.m_flGravity)
	{
		CTraceFilterWorldAndPropsOnly filterWorld = {};
		SDK::TraceHull(tProjInfo.m_vPos, vPoint, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, MASK_SOLID, &filterWorld, &trace);
#ifdef SPLASH_DEBUG5
		mTraceCount["Nograv trace"]++;
#endif
#ifdef SPLASH_DEBUG4
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tProjInfo.m_vPos, vPoint), I::GlobalVars->curtime + 5.f, Color_t(0, 0, 0));
#endif
		if (trace.fraction < 0.999f)
			return false;
	}

	bool bPrimeTime = false;

	const Vec3 vOriginal = tTarget.m_pEntity->GetAbsOrigin();
	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	for (int n = 1; n <= iSimTime; n++)
	{
		Vec3 vOld = F::ProjSim.GetOrigin();
		F::ProjSim.RunTick(tProjInfo);
		Vec3 vNew = F::ProjSim.GetOrigin();

		if (bDidHit)
		{
			trace.endpos = vNew;
			continue;
		}

		if (!bSplash)
		{
			SDK::TraceHull(vOld, vNew, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, MASK_SOLID, &filter, &trace);
#ifdef SPLASH_DEBUG5
			mTraceCount["Direct trace"]++;
#endif

#ifdef SPLASH_DEBUG4
			G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vOld, vNew), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 0));
#endif
		}
		else
		{
			static Vec3 vStaticPos = {};
			if (n == 1 || bPrimeTime)
				vStaticPos = vOld;
			if (n % Vars::Aimbot::Projectile::SplashTraceInterval.Value && n != iSimTime && !bPrimeTime)
				continue;

			SDK::TraceHull(vStaticPos, vNew, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, MASK_SOLID, &filterSplash, &trace);
#ifdef SPLASH_DEBUG5
			mTraceCount["Splash trace"]++;
#endif
#ifdef SPLASH_DEBUG4
			G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vStaticPos, vNew), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 0));
#endif
			vStaticPos = vNew;
		}
		if (trace.DidHit())
		{
			if (pHitSolid)
				*pHitSolid = true;

			bool bTime = bSplash
				? trace.endpos.DistTo(vPoint) < tProjInfo.m_flVelocity * TICK_INTERVAL + tProjInfo.m_vHull.z
				: iSimTime - n < 5 || pWeapon->GetWeaponID() == TF_WEAPON_LUNCHBOX; // projectile so slow it causes problems if we don't waive this check
			bool bTarget = trace.m_pEnt == tTarget.m_pEntity || bSplash;
			bool bValid = bTarget && bTime;
			if (bValid && bSplash)
			{
				bValid = SDK::VisPosWorld(nullptr, tTarget.m_pEntity, trace.endpos, vPoint, MASK_SOLID);
#ifdef SPLASH_DEBUG5
				mTraceCount["Splash vispos"]++;
#endif
				if (bValid)
				{
					switch (pWeapon->GetWeaponID())
					{
					case TF_WEAPON_ROCKETLAUNCHER:
					case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
					case TF_WEAPON_PARTICLE_CANNON:
						CGameTrace eyeTrace = {};
						CTraceFilterWorldAndPropsOnly filter = {};
						SDK::Trace(trace.endpos + trace.plane.normal, tTarget.m_vPos + tTarget.m_pEntity->As<CTFPlayer>()->GetViewOffset(), MASK_SHOT, &filter, &eyeTrace);
						bValid = eyeTrace.fraction == 1.f;
#ifdef SPLASH_DEBUG5
						mTraceCount["Rocket trace"]++;
#endif
					}
				}
			}

#ifdef SPLASH_DEBUG4
			G::BoxStorage.pop_back();
			if (bValid)
				G::BoxStorage.emplace_back(vPoint, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(0, 255, 0), Color_t(0, 0, 0, 0));
			else if (!bTime)
			{
				G::BoxStorage.emplace_back(vPoint, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 255), Color_t(0, 0, 0, 0));
				if (bSplash)
				{
					G::BoxStorage.emplace_back(trace.endpos, Vec3(-1, -1, -1), Vec3(1, 1, 1), Vec3(), I::GlobalVars->curtime + 5.f, Color_t(0, 0, 0), Color_t(0, 0, 0, 0));
					G::BoxStorage.emplace_back(vPoint, Vec3(-1, -1, -1), Vec3(1, 1, 1), Vec3(), I::GlobalVars->curtime + 5.f, Color_t(255, 255, 255), Color_t(0, 0, 0, 0));
				}
			}
			else
				G::BoxStorage.emplace_back(vPoint, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(0, 0, 255), Color_t(0, 0, 0, 0));
#endif

			if (bValid)
			{
				if (bSplash)
				{
					int iPopCount = Vars::Aimbot::Projectile::SplashTraceInterval.Value - trace.fraction * Vars::Aimbot::Projectile::SplashTraceInterval.Value;
					for (int i = 0; i < iPopCount && !tProjInfo.m_vPath.empty(); i++)
						tProjInfo.m_vPath.pop_back();
				}

				switch (Vars::Aimbot::General::AimType.Value)
				{
				case Vars::Aimbot::General::AimTypeEnum::Smooth:
				case Vars::Aimbot::General::AimTypeEnum::Assistive:
					{
						// attempted to have a headshot check though this seems more detrimental than useful outside of smooth aimbot
						if (tTarget.m_nAimedHitbox == HITBOX_HEAD)
						{	// i think this is accurate? nope, 220
							const Vec3 vOffset = (trace.endpos - vNew) + (vOriginal - tTarget.m_vPos);

							Vec3 vOld = F::ProjSim.GetOrigin() + vOffset;
							F::ProjSim.RunTick(tProjInfo);
							Vec3 vNew = F::ProjSim.GetOrigin() + vOffset;

							CGameTrace boneTrace = {};
							SDK::Trace(vOld, vNew, MASK_SHOT, &filter, &boneTrace);
#ifdef SPLASH_DEBUG5
							mTraceCount["Huntsman trace"]++;
#endif
							boneTrace.endpos -= vOffset;

							if (boneTrace.DidHit() && (boneTrace.m_pEnt != tTarget.m_pEntity || boneTrace.hitbox != HITBOX_HEAD))
								break;

							if (!boneTrace.DidHit()) // loop and see if closest hitbox is head
							{
								auto pModel = tTarget.m_pEntity->GetModel();
								if (!pModel) break;
								auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
								if (!pHDR) break;
								auto pSet = pHDR->pHitboxSet(tTarget.m_pEntity->As<CTFPlayer>()->m_nHitboxSet());
								if (!pSet) break;

								auto pBones = H::Entities.GetBones(tTarget.m_pEntity->entindex());
								if (!pBones)
									break;

								Vec3 vForward = vOld - vNew; vForward.Normalize();
								const Vec3 vPos = boneTrace.endpos + vForward * 16 + vOriginal - tTarget.m_vPos;

								//G::LineStorage.clear();
								//G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(pLocal->GetShootPos(), vPos), I::GlobalVars->curtime + 5.f, Vars::Colors::Prediction.Value);

								float closestDist = 0.f; int closestId = -1;
								for (int i = 0; i < pSet->numhitboxes; ++i)
								{
									auto pBox = pSet->pHitbox(i);
									if (!pBox)
										continue;

									Vec3 vCenter; Math::VectorTransform((pBox->bbmin + pBox->bbmax) / 2, pBones[pBox->bone], vCenter);

									const float flDist = vPos.DistTo(vCenter);
									if (closestId != -1 && flDist < closestDist || closestId == -1)
									{
										closestDist = flDist;
										closestId = i;
									}
								}

								if (closestId != 0)
									break;
								bDidHit = true;
							}
						}
					}
				}

				bDidHit = true;
			}
			else if (!bSplash && bTarget && pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
			{	// run for more ticks to check for splash
				iSimTime = n + 5;
				bSplash = bPrimeTime = true;
			}
			else
				break;

			if (!bSplash)
				trace.endpos = vNew;

			if (!bTarget || bSplash && !bPrimeTime)
				break;
		}
	}
	tTarget.m_pEntity->SetAbsOrigin(vOriginal);

	if (bDidHit && pProjectilePath)
	{
		tProjInfo.m_vPath.push_back(trace.endpos);
		*pProjectilePath = tProjInfo.m_vPath;
	}

	return bDidHit;
}



int CAimbotProjectile::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon,
	std::deque<Vec3>* pPlayerPath, std::deque<Vec3>* pProjectilePath, std::vector<DrawBox_t>* pBoxes, float* pTimeTo)
{
	//if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
	//	return false;

	PlayerStorage tStorage;
	ProjectileInfo tProjInfo = {};

	int iMaxTime, iSplash; Info_t tInfo = { pLocal, pWeapon };
	const float flSize = tTarget.m_pEntity->m_vecMins().DistTo(tTarget.m_pEntity->m_vecMaxs());
	{
#ifdef SPLASH_DEBUG5
		int iFlags = ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
		if (iFlags & ProjSimEnum::Trace)
		{
			if (Vars::Visuals::Trajectory::Override.Value)
			{
				if (!Vars::Visuals::Trajectory::Pipes.Value)
					mTraceCount["Setup trace"]++;
			}
			else
			{
				switch (tInfo.m_pWeapon->GetWeaponID())
				{
				case TF_WEAPON_ROCKETLAUNCHER:
				case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
				case TF_WEAPON_PARTICLE_CANNON:
				case TF_WEAPON_RAYGUN:
				case TF_WEAPON_DRG_POMSON:
				case TF_WEAPON_FLAREGUN:
				case TF_WEAPON_FLAREGUN_REVENGE:
				case TF_WEAPON_COMPOUND_BOW:
				case TF_WEAPON_CROSSBOW:
				case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
				case TF_WEAPON_SYRINGEGUN_MEDIC:
				case TF_WEAPON_FLAME_BALL:
					mTraceCount["Setup trace"]++;
				}
			}
		}
		if (iFlags & ProjSimEnum::InitCheck)
			mTraceCount["Trace init check"]++;
#endif
		if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, tProjInfo, ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum) || !F::ProjSim.Initialize(tProjInfo, false))
			return false;

		tInfo.m_vLocalEye = pLocal->GetShootPos();
		tInfo.m_vTargetEye = tTarget.m_pEntity->As<CTFPlayer>()->GetViewOffset();
		F::MoveSim.Initialize(tTarget.m_pEntity, tStorage);
		tTarget.m_vPos = tTarget.m_pEntity->m_vecOrigin();

		tInfo.m_flLatency = F::Backtrack.GetReal() + TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke());

		iMaxTime = TIME_TO_TICKS(std::min(tProjInfo.m_flLifetime, Vars::Aimbot::Projectile::PredictionTime.Value));

		Vec3 vVelocity = F::ProjSim.GetVelocity();
		tInfo.m_flVelocity = vVelocity.Length();
		Math::VectorAngles(vVelocity, tInfo.m_vAngFix);

		tInfo.m_vHull = tProjInfo.m_vHull.Min(3);
		tInfo.m_vOffset = tProjInfo.m_vPos - tInfo.m_vLocalEye; tInfo.m_vOffset.y *= -1;
		tInfo.m_flOffsetTime = tInfo.m_vOffset.Length() / tInfo.m_flVelocity; // silly

		tInfo.m_flGravity = tProjInfo.m_flGravity;
		tInfo.m_flRadius = GetSplashRadius(pWeapon, pLocal); tInfo.m_flRadiusTime = tInfo.m_flRadius / tInfo.m_flVelocity;
		tInfo.m_flBoundingTime = tInfo.m_flRadiusTime + flSize / tInfo.m_flVelocity;

		iSplash = Vars::Aimbot::Projectile::SplashPrediction.Value && tInfo.m_flRadius ? Vars::Aimbot::Projectile::SplashPrediction.Value : Vars::Aimbot::Projectile::SplashPredictionEnum::Off;
		tInfo.m_iSplashCount = !tProjInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashCountDirect.Value : Vars::Aimbot::Projectile::SplashCountArc.Value;

		tInfo.m_iSplashMode = GetSplashMode(pWeapon);
		tInfo.m_flPrimeTime = PrimeTime(pWeapon);
		tInfo.m_iPrimeTime = TIME_TO_TICKS(tInfo.m_flPrimeTime);
	}

	int iReturn = false;

	auto mDirectPoints = iSplash == Vars::Aimbot::Projectile::SplashPredictionEnum::Only ? std::unordered_map<int, Vec3>() : GetDirectPoints(tTarget, tInfo);
	auto vSpherePoints = !iSplash ? std::vector<std::pair<Vec3, int>>() : ComputeSphere(tInfo.m_flRadius + flSize, Vars::Aimbot::Projectile::SplashPoints.Value, Vars::Aimbot::Projectile::SplashNthRoot.Value);
#ifdef SPLASH_DEBUG3
	for (auto& [vPoint, _] : vSpherePoints)
		G::BoxStorage.emplace_back(tTarget.m_pEntity->m_vecOrigin() + tInfo.m_vTargetEye + vPoint * tInfo.m_flRadius / (tInfo.m_flRadius + flSize), Vec3(-1, -1, -1), Vec3(1, 1, 1), Vec3(), I::GlobalVars->curtime + 60.f, Color_t(0, 0, 0, 0), Vars::Colors::Local.Value);
#endif

	bool bSingleSplash = Vars::Aimbot::Projectile::SplashMode.Value == Vars::Aimbot::Projectile::SplashModeEnum::Single;
	bool bDoneRunningSplash = false;
	
	Vec3 vAngleTo, vPredicted, vTarget;
	int iLowestPriority = std::numeric_limits<int>::max(); float flLowestDist = std::numeric_limits<float>::max();
	int iLowestSmoothPriority = iLowestPriority; float flLowestSmoothDist = flLowestDist;
	for (int i = 1 - TIME_TO_TICKS(tInfo.m_flLatency); i <= iMaxTime; i++)
	{
		if (!tStorage.m_bFailed)
		{
			F::MoveSim.RunTick(tStorage);
			tTarget.m_vPos = tStorage.m_vPredictedOrigin;
		}
		if (i < 0)
			continue;

		bool bDirectBreaks = true;
		std::vector<Point_t> vSplashPoints = {};
		if (iSplash)
		{
			Solution_t solution; CalculateAngle(tInfo.m_vLocalEye, tTarget.m_vPos, tInfo, i, solution, false);
			if (solution.m_iCalculated != 3)
			{
				if (!bSingleSplash)
				{
					bDirectBreaks = false;

					const float flTimeTo = solution.m_flTime - TICKS_TO_TIME(i);
					if (flTimeTo < tInfo.m_flBoundingTime)
					{
						if (vSpherePoints.empty() || flTimeTo < -tInfo.m_flBoundingTime && (tInfo.m_flPrimeTime ? i > tInfo.m_iPrimeTime : true))
							break;
						else if (tInfo.m_flPrimeTime ? i >= tInfo.m_iPrimeTime : true)
							vSplashPoints = GetSplashPoints(tTarget, vSpherePoints, tInfo, i);
					}
				}
				else if (TICKS_TO_TIME(i) > solution.m_flTime && !bDoneRunningSplash)
				{
					vSplashPoints = GetSplashPoints(tTarget, vSpherePoints, tInfo, i);
					bDoneRunningSplash = true;
				}
			}
		}
		if (bDirectBreaks && mDirectPoints.empty())
			break;

		std::vector<std::tuple<Point_t, int, int>> vPoints = {};
		for (auto& [iIndex, vPoint] : mDirectPoints)
			vPoints.emplace_back(Point_t(tTarget.m_vPos + vPoint, {}), iIndex + (iSplash == Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer ? tInfo.m_iSplashCount : 0), iIndex);
		for (auto& vPoint : vSplashPoints)
			vPoints.emplace_back(vPoint, iSplash == Vars::Aimbot::Projectile::SplashPredictionEnum::Include ? 3 : 0, -1);

		int j = 0;
		for (auto& [vPoint, iPriority, iIndex] : vPoints) // get most ideal point
		{
			const bool bSplash = iIndex == -1;
			Vec3 vOriginalPoint = vPoint.m_vPoint;

			if (Vars::Aimbot::Projectile::HuntsmanPullPoint.Value && tTarget.m_nAimedHitbox == HITBOX_HEAD)
				vPoint.m_vPoint = PullPoint(vPoint.m_vPoint, tInfo.m_vLocalEye, tInfo, tTarget.m_pEntity->m_vecMins() + tProjInfo.m_vHull, tTarget.m_pEntity->m_vecMaxs() - tProjInfo.m_vHull, tTarget.m_vPos);
				//vPoint.m_vPoint = PullPoint(vPoint.m_vPoint, tInfo.m_vLocalEye, tInfo, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), tTarget.m_vPos);

			float flDist = bSplash ? tTarget.m_vPos.DistTo(vPoint.m_vPoint) : flLowestDist;
			bool bPriority = bSplash ? iPriority <= iLowestPriority : iPriority < iLowestPriority;
			bool bTime = bSplash || tInfo.m_iPrimeTime < i || tStorage.m_MoveData.m_vecVelocity.IsZero();
			bool bDist = !bSplash || flDist < flLowestDist;
			if (!bSplash && !bPriority)
				mDirectPoints.erase(iIndex);
			if (!bPriority || !bTime || !bDist)
				continue;

			CalculateAngle(tInfo.m_vLocalEye, vPoint.m_vPoint, tInfo, i, vPoint.m_Solution);
			if (!bSplash && (vPoint.m_Solution.m_iCalculated == 1 || vPoint.m_Solution.m_iCalculated == 3))
				mDirectPoints.erase(iIndex);
			if (vPoint.m_Solution.m_iCalculated != 1)
				continue;

			if (Vars::Aimbot::Projectile::HuntsmanPullPoint.Value && tTarget.m_nAimedHitbox == HITBOX_HEAD)
			{
				Solution_t tSolution;
				CalculateAngle(tInfo.m_vLocalEye, vOriginalPoint, tInfo, std::numeric_limits<int>::max(), tSolution);
				vPoint.m_Solution.m_flPitch = tSolution.m_flPitch, vPoint.m_Solution.m_flYaw = tSolution.m_flYaw;
			}

			Vec3 vAngles = Aim(G::CurrentUserCmd->viewangles, { vPoint.m_Solution.m_flPitch, vPoint.m_Solution.m_flYaw, 0.f });
			std::deque<Vec3> vProjLines; bool bHitSolid = false;

			if (TestAngle(pLocal, pWeapon, tTarget, vPoint.m_vPoint, vAngles, i, bSplash, &bHitSolid, &vProjLines))
			{
				iLowestPriority = iPriority; flLowestDist = flDist;
				vAngleTo = vAngles, vPredicted = tTarget.m_vPos, vTarget = vOriginalPoint;
				*pTimeTo = vPoint.m_Solution.m_flTime + tInfo.m_flLatency;
				*pPlayerPath = tStorage.m_vPath;
				if (!pPlayerPath->empty())
					pPlayerPath->push_back(tStorage.m_MoveData.m_vecAbsOrigin);
				*pProjectilePath = vProjLines;
			}
			else
			{
				switch (Vars::Aimbot::General::AimType.Value)
				{
				case Vars::Aimbot::General::AimTypeEnum::Smooth:
				case Vars::Aimbot::General::AimTypeEnum::Assistive:
					if (Vars::Aimbot::General::AssistStrength.Value != 0.f && Vars::Aimbot::General::AssistStrength.Value != 100.f)
					{
						bPriority = bSplash ? iPriority <= iLowestSmoothPriority : iPriority < flLowestSmoothDist;
						bDist = !bSplash || flDist < flLowestDist;
						if (!bPriority || !bDist)
							continue;

						Vec3 vPlainAngles = Aim({}, { vPoint.m_Solution.m_flPitch, vPoint.m_Solution.m_flYaw, 0.f }, Vars::Aimbot::General::AimTypeEnum::Plain);
						if (TestAngle(pLocal, pWeapon, tTarget, vPoint.m_vPoint, vPlainAngles, i, bSplash, &bHitSolid))
						{
							iLowestSmoothPriority = iPriority; flLowestSmoothDist = flDist;
							tTarget.m_vAngleTo = vAngles;
							iReturn = 2;
						}
					}
				}
			}

			if (!j && bHitSolid)
				*pTimeTo = vPoint.m_Solution.m_flTime + tInfo.m_flLatency;
			j++;
		}
	}
	F::MoveSim.Restore(tStorage);

	tTarget.m_vPos = vTarget;

	if (iLowestPriority != std::numeric_limits<int>::max() &&
		(tTarget.m_iTargetType == TargetEnum::Player ? !tStorage.m_bFailed : true)) // don't attempt to aim at players when movesim fails
	{
		tTarget.m_vAngleTo = vAngleTo;

		bool bBounds = Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::OnShot;
		bool bAimPoint = Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::AimPoint;
		if (bBounds || bAimPoint)
		{
			if (!Vars::Colors::BoundHitboxEdge.Value.a && !Vars::Colors::BoundHitboxFace.Value.a && !Vars::Colors::BoundHitboxEdgeClipped.Value.a && !Vars::Colors::BoundHitboxFaceClipped.Value.a)
				return true;

			tInfo.m_vHull = tInfo.m_vHull.Max(1);
			float flProjectileTime = TICKS_TO_TIME(pProjectilePath->size());
			float flTargetTime = tStorage.m_bFailed ? flProjectileTime : TICKS_TO_TIME(pPlayerPath->size());

			if (bBounds)
			{
				if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a)
					pBoxes->emplace_back(vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + (Vars::Visuals::Simulation::Timed.Value ? flTargetTime : Vars::Visuals::Hitbox::DrawDuration.Value), Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value);
				if (Vars::Colors::BoundHitboxEdgeClipped.Value.a || Vars::Colors::BoundHitboxFaceClipped.Value.a)
					pBoxes->emplace_back(vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + (Vars::Visuals::Simulation::Timed.Value ? flTargetTime : Vars::Visuals::Hitbox::DrawDuration.Value), Vars::Colors::BoundHitboxEdgeClipped.Value, Vars::Colors::BoundHitboxFaceClipped.Value, true);
			}

			if (bAimPoint)
			{
				if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a)
					pBoxes->emplace_back(vTarget, tInfo.m_vHull * -1, tInfo.m_vHull, Vec3(), I::GlobalVars->curtime + (Vars::Visuals::Simulation::Timed.Value ? flProjectileTime : Vars::Visuals::Hitbox::DrawDuration.Value), Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value);
				if (Vars::Colors::BoundHitboxEdgeClipped.Value.a || Vars::Colors::BoundHitboxFaceClipped.Value.a)
					pBoxes->emplace_back(vTarget, tInfo.m_vHull * -1, tInfo.m_vHull, Vec3(), I::GlobalVars->curtime + (Vars::Visuals::Simulation::Timed.Value ? flProjectileTime : Vars::Visuals::Hitbox::DrawDuration.Value), Vars::Colors::BoundHitboxEdgeClipped.Value, Vars::Colors::BoundHitboxFaceClipped.Value, true);

				if (Vars::Debug::Info.Value && tTarget.m_nAimedHitbox == HITBOX_HEAD) // huntsman head
				{
					const Vec3 vOriginOffset = tTarget.m_pEntity->m_vecOrigin() - vPredicted;

					auto pBones = H::Entities.GetBones(tTarget.m_pEntity->entindex());
					if (!pBones)
						return true;

					auto vBoxes = F::Visuals.GetHitboxes(pBones, tTarget.m_pEntity->As<CTFPlayer>(), { HITBOX_HEAD });
					for (auto& bBox : vBoxes)
					{
						bBox.m_vPos -= vOriginOffset;
						bBox.m_flTime = I::GlobalVars->curtime + (Vars::Visuals::Simulation::Timed.Value ? flTargetTime : Vars::Visuals::Hitbox::DrawDuration.Value);
						pBoxes->push_back(bBox);
					}
				}
				if (Vars::Debug::Info.Value && tTarget.m_nAimedHitbox == HITBOX_HEAD) // huntsman head, broken; removeme once 254 is fixed
				{
					const Vec3 vOriginOffset = tTarget.m_pEntity->m_vecOrigin() - vPredicted;

					auto pBones = H::Entities.GetBones(tTarget.m_pEntity->entindex());
					if (!pBones)
						return true;

					auto vBoxes = F::Visuals.GetHitboxes(pBones, tTarget.m_pEntity->As<CTFPlayer>(), { HITBOX_HEAD });
					for (auto& bBox : vBoxes)
					{
						bBox.m_vPos -= vOriginOffset;
						bBox.m_flTime = I::GlobalVars->curtime + (Vars::Visuals::Simulation::Timed.Value ? flTargetTime : Vars::Visuals::Hitbox::DrawDuration.Value);
						bBox.m_vRotation = Vec3();
						pBoxes->push_back(bBox);
					}
				}
			}
		}
		return true;
	}

	return iReturn;
}



Vec3 CAimbotProjectile::Aim(Vec3 vCurAngle, Vec3 vToAngle, int iMethod)
{
	Vec3 vReturn = {};

	Math::ClampAngles(vToAngle);

	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		vReturn = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
		vReturn = vCurAngle.LerpAngle(vToAngle, Vars::Aimbot::General::AssistStrength.Value / 100.f);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		Vec3 vMouseDelta = G::CurrentUserCmd->viewangles.DeltaAngle(G::LastUserCmd->viewangles);
		Vec3 vTargetDelta = vToAngle.DeltaAngle(G::LastUserCmd->viewangles);
		float flMouseDelta = vMouseDelta.Length2D(), flTargetDelta = vTargetDelta.Length2D();
		vTargetDelta = vTargetDelta.Normalized() * std::min(flMouseDelta, flTargetDelta);
		vReturn = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, Vars::Aimbot::General::AssistStrength.Value / 100.f);
		break;
	}

	return vReturn;
}

// assume angle calculated outside with other overload
void CAimbotProjectile::Aim(CUserCmd* pCmd, Vec3& vAngle)
{
	switch (Vars::Aimbot::General::AimType.Value)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		//pCmd->viewangles = vAngle; // retarded, overshooting with this uncommented
		I::EngineClient->SetViewAngles(vAngle);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	{
		bool bDoubleTap = F::Ticks.m_bDoubletap || F::Ticks.GetTicks(H::Entities.GetWeapon()) || F::Ticks.m_bSpeedhack;
		auto pWeapon = H::Entities.GetWeapon();
		if (G::Attacking == 1 || bDoubleTap || pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER)
		{
			SDK::FixMovement(pCmd, vAngle);
			pCmd->viewangles = vAngle;
			G::PSilentAngles = true;
		}
		break;
	}
	case Vars::Aimbot::General::AimTypeEnum::Locking:
	{
		SDK::FixMovement(pCmd, vAngle);
		pCmd->viewangles = vAngle;
	}
	}
}

static inline void CancelShot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, int& iLastTickCancel)
{
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_COMPOUND_BOW:
	{
		pCmd->buttons |= IN_ATTACK2;
		pCmd->buttons &= ~IN_ATTACK;
		break;
	}
	case TF_WEAPON_CANNON:
	{
		if (auto pSwap = pLocal->GetWeaponFromSlot(SLOT_SECONDARY))
		{
			pCmd->weaponselect = pSwap->entindex();
			iLastTickCancel = pWeapon->entindex();
		}
		break;
	}
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	{
		auto pSwap = pLocal->GetWeaponFromSlot(SLOT_PRIMARY);
		if (pSwap == pWeapon)
			pSwap = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
		if (pSwap)
		{
			pCmd->weaponselect = pSwap->entindex();
			iLastTickCancel = pWeapon->entindex();
		}
	}
	}
}

bool CAimbotProjectile::RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const int nWeaponID = pWeapon->GetWeaponID();

	static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
	const int iLastAimType = iStaticAimType;
	const int iRealAimType = iStaticAimType = Vars::Aimbot::General::AimType.Value;

	switch (nWeaponID)
	{
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		if (!Vars::Aimbot::General::AutoShoot.Value && !iRealAimType && iLastAimType && G::Attacking)
			Vars::Aimbot::General::AimType.Value = iLastAimType;
	}

	static int iAimType = 0;
	if (!G::Throwing)
		iAimType = Vars::Aimbot::General::AimType.Value;
	else if (iAimType)
		Vars::Aimbot::General::AimType.Value = iAimType;

	if (!F::Aimbot.m_bRunningSecondary && Vars::Aimbot::General::AimHoldsFire.Value == Vars::Aimbot::General::AimHoldsFireEnum::Always && !G::CanPrimaryAttack && G::LastUserCmd->buttons & IN_ATTACK && Vars::Aimbot::General::AimType.Value && !pWeapon->IsInReload())
		pCmd->buttons |= IN_ATTACK;
	// the F::Ticks.m_bDoubletap condition is not a great fix here and actually properly predicting when shots will be fired should likely be done over this, but it's fine for now
	if (!Vars::Aimbot::General::AimType.Value || !G::CanPrimaryAttack && !G::Reloading && !F::Ticks.m_bDoubletap && !F::Ticks.m_bSpeedhack && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent && nWeaponID != TF_WEAPON_PIPEBOMBLAUNCHER && nWeaponID != TF_WEAPON_CANNON && nWeaponID != TF_WEAPON_FLAMETHROWER)
		return false;

	auto vTargets = SortTargets(pLocal, pWeapon);
	if (vTargets.empty())
		return false;

	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon && iRealAimType
		&& (nWeaponID == TF_WEAPON_COMPOUND_BOW || nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER))
	{
		pCmd->buttons |= IN_ATTACK;
		if (!G::CanPrimaryAttack && !G::Reloading && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
			return false;
	}

#if defined(SPLASH_DEBUG1) || defined(SPLASH_DEBUG2) || defined(SPLASH_DEBUG4)
	G::LineStorage.clear();
#endif
#if defined(SPLASH_DEBUG3) || defined(SPLASH_DEBUG4)
	G::BoxStorage.clear();
#endif
	for (auto& tTarget : vTargets)
	{
		float flTimeTo = 0.f; std::deque<Vec3> vPlayerPath, vProjectilePath; std::vector<DrawBox_t> vBoxes = {};
		const int iResult = CanHit(tTarget, pLocal, pWeapon, &vPlayerPath, &vProjectilePath, &vBoxes, &flTimeTo);
		if (!iResult)
		{
			if (pWeapon->GetWeaponID() == TF_WEAPON_CANNON && !(pCmd->buttons & IN_ATTACK))
			{
				float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f
					? std::clamp(pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() - I::GlobalVars->curtime, 0.f, 1.f)
					: 1.f;
				if (!flTimeTo)
					flTimeTo = std::numeric_limits<float>::max();
				if (flCharge < flTimeTo)
				{
					if (pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f)
						CancelShot(pLocal, pWeapon, pCmd, m_iLastTickCancel);
				}
				else
				{
					if (m_iLastTickCancel)
						pCmd->weaponselect = m_iLastTickCancel = 0;
					pCmd->buttons |= IN_ATTACK;
				}
			}
			continue;
		}
		if (iResult == 2)
		{
			G::Target = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

		G::Target = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPosition = { tTarget.m_vPos, I::GlobalVars->tickcount };

		if (Vars::Aimbot::General::AutoShoot.Value)
		{
			pCmd->buttons |= IN_ATTACK;

			if (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
			{
				if (pWeapon->m_iClip1() > 0)
					pCmd->buttons &= ~IN_ATTACK;
			}
			else
			{
				switch (nWeaponID)
				{
				case TF_WEAPON_COMPOUND_BOW:
				case TF_WEAPON_PIPEBOMBLAUNCHER:
					if (pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f)
						pCmd->buttons &= ~IN_ATTACK;
					break;
				case TF_WEAPON_CANNON:
					if (pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f)
					{
						if (m_iLastTickCancel)
							pCmd->weaponselect = m_iLastTickCancel = 0;
						if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon)
						{
							float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() - I::GlobalVars->curtime;
							if (std::clamp(flCharge, 0.f, 1.f) < flTimeTo)
								pCmd->buttons &= ~IN_ATTACK;
						}
						else
							pCmd->buttons &= ~IN_ATTACK;
					}
					break;
				case TF_WEAPON_BAT_WOOD:
				case TF_WEAPON_BAT_GIFTWRAP:
				case TF_WEAPON_LUNCHBOX:
					pCmd->buttons &= ~IN_ATTACK, pCmd->buttons |= IN_ATTACK2;
				}
			}
		}

		F::Aimbot.m_bRan = G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);

		if (G::Attacking == 1 || !Vars::Aimbot::General::AutoShoot.Value)
		{
			bool bPlayerPath = Vars::Visuals::Simulation::PlayerPath.Value;
			bool bProjectilePath = Vars::Visuals::Simulation::ProjectilePath.Value && (G::Attacking == 1 || Vars::Debug::Info.Value);
			bool bBoxes = Vars::Visuals::Hitbox::BoundsEnabled.Value & (Vars::Visuals::Hitbox::BoundsEnabledEnum::OnShot | Vars::Visuals::Hitbox::BoundsEnabledEnum::AimPoint);
			if (bPlayerPath || bProjectilePath || bBoxes)
			{
				G::PathStorage.clear();
				G::BoxStorage.clear();
				G::LineStorage.clear();

				if (bPlayerPath)
				{
					if (Vars::Colors::PlayerPath.Value.a)
						G::PathStorage.emplace_back(vPlayerPath, Vars::Visuals::Simulation::Timed.Value ? -int(vPlayerPath.size()) : I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPath.Value, Vars::Visuals::Simulation::PlayerPath.Value);
					if (Vars::Colors::PlayerPathClipped.Value.a)
						G::PathStorage.emplace_back(vPlayerPath, Vars::Visuals::Simulation::Timed.Value ? -int(vPlayerPath.size()) : I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPathClipped.Value, Vars::Visuals::Simulation::PlayerPath.Value, true);
				}
				if (bProjectilePath)
				{
					if (Vars::Colors::ProjectilePath.Value.a)
						G::PathStorage.emplace_back(vProjectilePath, Vars::Visuals::Simulation::Timed.Value ? -int(vProjectilePath.size()) - TIME_TO_TICKS(F::Backtrack.GetReal()) : I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::ProjectilePath.Value, Vars::Visuals::Simulation::ProjectilePath.Value);
					if (Vars::Colors::ProjectilePathClipped.Value.a)
						G::PathStorage.emplace_back(vProjectilePath, Vars::Visuals::Simulation::Timed.Value ? -int(vProjectilePath.size()) - TIME_TO_TICKS(F::Backtrack.GetReal()) : I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::ProjectilePathClipped.Value, Vars::Visuals::Simulation::ProjectilePath.Value, true);
				}
				if (bBoxes)
					G::BoxStorage.insert(G::BoxStorage.end(), vBoxes.begin(), vBoxes.end());
			}
		}

		Aim(pCmd, tTarget.m_vAngleTo);
		if (G::PSilentAngles)
		{
			switch (nWeaponID)
			{
			case TF_WEAPON_FLAMETHROWER: // angles show up anyways
			case TF_WEAPON_CLEAVER: // can't psilent with these weapons, they use SetContextThink
			case TF_WEAPON_JAR:
			case TF_WEAPON_JAR_MILK:
			case TF_WEAPON_JAR_GAS:
			case TF_WEAPON_BAT_WOOD:
			case TF_WEAPON_BAT_GIFTWRAP:
				G::PSilentAngles = false, G::SilentAngles = true;
			}
		}
		return true;
	}

	return false;
}

void CAimbotProjectile::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const bool bSuccess = RunMain(pLocal, pWeapon, pCmd);
#if defined(SPLASH_DEBUG5)
	if (Vars::Aimbot::General::AimType.Value && !mTraceCount.empty())
	{
		int iTraceCount = 0;
		for (auto& [_, iTraces] : mTraceCount)
			iTraceCount += iTraces;
		SDK::Output("Traces", std::format("{}", iTraceCount).c_str());
		for (auto& [sType, iTraces] : mTraceCount)
			SDK::Output("Traces", std::format("{}: {}", sType, iTraces).c_str());
	}
	mTraceCount.clear();
#endif

	float flAmount = 0.f;
	if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		const float flCharge = pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() : 0.f;
		flAmount = Math::RemapVal(flCharge, 0.f, SDK::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon), 0.f, 1.f);
	}
	else if (pWeapon->GetWeaponID() == TF_WEAPON_CANNON)
	{
		const float flMortar = SDK::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
		const float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() : -flMortar;
		flAmount = flMortar ? Math::RemapVal(flCharge, -flMortar, 0.f, 0.f, 1.f) : 0.f;
	}

	if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER && G::OriginalMove.m_iButtons & IN_ATTACK && Vars::Aimbot::Projectile::AutoRelease.Value && flAmount > Vars::Aimbot::Projectile::AutoRelease.Value / 100)
		pCmd->buttons &= ~IN_ATTACK;
	else if (G::CanPrimaryAttack && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::CancelCharge)
	{
		if (m_bLastTickHeld && (G::LastUserCmd->buttons & IN_ATTACK && !(pCmd->buttons & IN_ATTACK) && !bSuccess || flAmount > 0.95f))
			CancelShot(pLocal, pWeapon, pCmd, m_iLastTickCancel);
	}

	m_bLastTickHeld = Vars::Aimbot::General::AimType.Value;
}