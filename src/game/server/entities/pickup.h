/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_PICKUP_H
#define GAME_SERVER_ENTITIES_PICKUP_H
#include <game/server/entity.h>
#include <memory>

class CEntityDropPickup;
class CPickup : public CEntity
{
	int m_Type;
	int m_SubType;
	int m_SpawnTick;
	bool m_Projectile;
	bool m_MagnetEligible {};
	vec2 m_SpawnPos {};
	int m_MagnetClientID { -1 };
	int m_MagnetTicksLeft {};
	std::weak_ptr<void> m_MagnetLifetime;
	void CancelMagnet();

public:
	CPickup(CGameWorld *pGameWorld, int Type, int SubType, vec2 Pos, bool MagnetEligible = false);
	~CPickup() override;
	CPickup(CGameWorld *pGameWorld, int ProjType, vec2 Pos);

	bool CanMakeDrop() const;
	CEntityDropPickup* MakeToDrop();
	static bool ApplyTo(CCharacter* pCharacter, int Type, int Subtype);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void ScheduleRespawn();
	void Init(int Type, int Subtype, bool Projectile);
};

#endif
