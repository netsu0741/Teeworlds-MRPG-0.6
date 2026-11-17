/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "pickup.h"

#include <game/server/gamecontext.h>
#include <generated/server_data.h>
#include "character.h"
#include <game/server/core/entities/items/drop_pickup.h>

namespace
{
	constexpr int MAGNET_CHASE_SECONDS = 5;
	constexpr float MAGNET_RANGE = 160.f;
	constexpr float MAGNET_SPEED = 220.f;
}

CPickup::CPickup(CGameWorld *pGameWorld, int Type, int SubType, vec2 Pos, bool MagnetEligible)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP, Pos, 14.f)
{
	m_MagnetEligible = MagnetEligible;
	m_SpawnPos = Pos;
	Init(Type, SubType, false);
	GameWorld()->InsertEntity(this);
}

CPickup::CPickup(CGameWorld *pGameWorld, int ProjType, vec2 Pos)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP, Pos, 14.f)
{
	Init(ProjType, 0, true);
	GameWorld()->InsertEntity(this);
}

void CPickup::CancelMagnet()
{
	m_MagnetReservation.Cancel();
	m_MagnetClientID = -1;
	m_MagnetTicksLeft = 0;
	if(m_MagnetEligible)
		m_Pos = m_SpawnPos;
}

void CPickup::Init(int Type, int Subtype, bool Projectile)
{
	m_Type = Type;
	m_SubType = Subtype;
	m_Projectile = Projectile;
	CPickup::Reset();
}

void CPickup::Reset()
{
	CancelMagnet();
	if(g_pData->m_aPickups[m_Type].m_Spawndelay > 0)
	{
		m_SpawnTick = Server()->Tick() + Server()->TickSpeed() * g_pData->m_aPickups[m_Type].m_Spawndelay;
	}
	else
	{
		m_SpawnTick = -1;
	}
}

bool CPickup::CanMakeDrop() const
{
	const bool Supported = m_Type == POWERUP_HEALTH || m_Type == POWERUP_ARMOR ||
		(m_Type == POWERUP_WEAPON && (m_SubType == WEAPON_SHOTGUN || m_SubType == WEAPON_GRENADE || m_SubType == WEAPON_LASER));
	return !IsMarkedForDestroy() && !m_Projectile && m_MagnetClientID < 0 && m_SpawnTick == -1 &&
		Supported && g_pData->m_aPickups[m_Type].m_Respawntime >= 0;
}

CEntityDropPickup* CPickup::MakeToDrop()
{
	if(!CanMakeDrop())
		return nullptr;

	auto* pDrop = new CEntityDropPickup(GameWorld(), m_Pos, m_Type, m_SubType);
	ScheduleRespawn();
	return pDrop;
}

void CPickup::ScheduleRespawn()
{
	const int Seconds = g_pData->m_aPickups[m_Type].m_Respawntime;
	if(Seconds >= 0)
		m_SpawnTick = Server()->Tick() + maximum(1, Server()->TickSpeed() * Seconds);
}

bool CPickup::ApplyTo(CCharacter* pChar, int Type, int Subtype)
{
	if(!pChar || !pChar->IsAlive() || pChar->GetPlayer()->IsBot())
		return false;

	auto* pPlayer = pChar->GetPlayer();
	int Sound = SOUND_PICKUP_HEALTH;
	bool Picked = false;

	if (Type == POWERUP_HEALTH)
	{
		Picked = pChar->IncreaseHealth(translate_to_percent_rest(pPlayer->GetMaxHealth(), 1));
	}
	else if (Type == POWERUP_ARMOR)
	{
		Picked = pChar->IncreaseMana(translate_to_percent_rest(pPlayer->GetMaxMana(), 1));
		Sound = SOUND_PICKUP_ARMOR;
	}

	else if (Type == POWERUP_WEAPON && (Subtype == WEAPON_SHOTGUN || Subtype == WEAPON_GRENADE || Subtype == WEAPON_LASER))
	{
		const int Ammo = 10 + pPlayer->GetTotalAttributeValue(AttributeIdentifier::Ammo);
		Picked = pChar->GiveWeapon(Subtype, translate_to_percent_rest(Ammo, 40));
		Sound = Subtype == WEAPON_GRENADE ? SOUND_PICKUP_GRENADE : SOUND_PICKUP_SHOTGUN;
	}
	
	if(Picked)
		pChar->GS()->CreateSound(pChar->GetPos(), Sound);
	
	return Picked;
}

bool CPickup::TickMagnet()
{
	if(m_Type != POWERUP_HEALTH && m_Type != POWERUP_ARMOR)
		return false;

	const auto Resource = m_Type == POWERUP_HEALTH ? CMagnetReserve::EResource::Health : CMagnetReserve::EResource::Mana;
	if(m_MagnetClientID < 0)
	{
		for(auto* pCandidate = static_cast<CCharacter*>(GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER)); pCandidate; pCandidate = static_cast<CCharacter*>(pCandidate->TypeNext()))
		{
			if(distance_squared(m_Pos, pCandidate->m_Core.m_Pos) <= MAGNET_RANGE * MAGNET_RANGE && pCandidate->TryReserveMagnetPickup(m_MagnetReservation, Resource))
			{
				m_MagnetClientID = pCandidate->GetPlayer()->GetCID();
				m_MagnetTicksLeft = Server()->TickSpeed() * MAGNET_CHASE_SECONDS;
				break;
			}
		}
	}
	if(m_MagnetClientID < 0)
		return false;

	auto* pPlayer = GS()->GetPlayer(m_MagnetClientID, false);
	auto* pCharacter = pPlayer ? pPlayer->GetCharacter() : nullptr;
	if(!pCharacter || !pCharacter->IsAlive() || !pCharacter->OwnsMagnetReservation(m_MagnetReservation) ||
		!pPlayer->IsAuthed() || !pPlayer->GetItem(itMagnetItems)->IsEquipped())
	{
		CancelMagnet();
		return false;
	}
	if(m_MagnetTicksLeft-- <= 0)
	{
		CancelMagnet();
		ScheduleRespawn();
		return true;
	}

	const vec2 Offset = pCharacter->MagnetPickupTarget(Resource) - m_Pos;
	const float DistanceSquared = length_squared(Offset);
	const float Step = MAGNET_SPEED / Server()->TickSpeed();
	if(DistanceSquared <= Step * Step)
	{
		m_MagnetReservation.Capture();
		CancelMagnet();
		ScheduleRespawn();
	}
	else
		m_Pos += Offset * (Step / sqrtf(DistanceSquared));
	return true;
}

void CPickup::Tick()
{
	if(m_SpawnTick != -1)
	{
		if(Server()->Tick() < m_SpawnTick)
			return;

		m_SpawnTick = -1;
		if(m_Type == POWERUP_ARMOR_GRENADE || m_Type == POWERUP_ARMOR_SHOTGUN || m_Type == POWERUP_ARMOR_LASER)
			GS()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
	}

	if((!HasPlayersInView() && m_MagnetClientID < 0) || m_Projectile)
		return;

	if(m_MagnetEligible && TickMagnet())
		return;

	auto* pChar = static_cast<CCharacter*>(GameWorld()->ClosestEntity(m_Pos, 20.f, CGameWorld::ENTTYPE_CHARACTER, nullptr));
	if(ApplyTo(pChar, m_Type, m_SubType))
		ScheduleRespawn();
}

void CPickup::Snap(int SnappingClient)
{
	if(m_SpawnTick != -1 || NetworkClipped(SnappingClient))
		return;

	if(m_Projectile)
	{
		GS()->SnapProjectile(SnappingClient, GetID(), m_Pos, {}, Server()->Tick() - 4, m_Type);
	}
	else
	{
		GS()->SnapPickup(SnappingClient, GetID(), m_Pos, m_Type, m_SubType);
	}
}
