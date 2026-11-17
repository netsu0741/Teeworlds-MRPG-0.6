#ifndef GAME_SERVER_CORE_TOOLS_MAGNET_RESERVE_H
#define GAME_SERVER_CORE_TOOLS_MAGNET_RESERVE_H

#include <algorithm>
#include <memory>

// Character-local, transient reserve. Each visible orbit holds five units.
class CMagnetReserve
{
public:
	enum class EResource { Health, Mana };
	static constexpr int UNITS_PER_ORBIT = 5;
	static constexpr int MAX_SLOTS = 4;
	static constexpr float ORBIT_RADIUS = 87.0f;
	static int Slots(int Enchant) { return 2 + std::clamp(Enchant, 0, MAX_SLOTS - 2); }
	static int OrbitPosition(EResource Resource, int Slot) { return Slot * 2 + (Resource == EResource::Mana ? 1 : 0); }
	int IncomingSlot(EResource Resource, int Enchant) const { return std::min(Stored(Resource) / UNITS_PER_ORBIT, Slots(Enchant) - 1); }

	bool Reserve(EResource Resource, int Enchant)
	{
		auto& Entry = Get(Resource);
		if(Entry.m_Stored + Entry.m_InFlight >= Slots(Enchant) * UNITS_PER_ORBIT)
			return false;
		++Entry.m_InFlight;
		return true;
	}

	void Cancel(EResource Resource)
	{
		auto& Entry = Get(Resource);
		if(Entry.m_InFlight > 0)
			--Entry.m_InFlight;
	}

	void Capture(EResource Resource)
	{
		auto& Entry = Get(Resource);
		if(Entry.m_InFlight > 0)
		{
			--Entry.m_InFlight;
			++Entry.m_Stored;
		}
	}

	int Consume(EResource Resource, int Missing)
	{
		auto& Entry = Get(Resource);
		const int Amount = std::min(Entry.m_Stored, std::max(0, Missing));
		Entry.m_Stored -= Amount;
		return Amount;
	}

	int Stored(EResource Resource) const { return Resource == EResource::Health ? m_Health.m_Stored : m_Mana.m_Stored; }
	int Orbits(EResource Resource) const { return (Stored(Resource) + UNITS_PER_ORBIT - 1) / UNITS_PER_ORBIT; }

private:
	struct SEntry { int m_Stored {}; int m_InFlight {}; };
	SEntry& Get(EResource Resource) { return Resource == EResource::Health ? m_Health : m_Mana; }
	SEntry m_Health;
	SEntry m_Mana;
};

// Owns one incoming reservation without extending the character's lifetime.
class CMagnetReservation
{
	std::weak_ptr<CMagnetReserve> m_Reserve;
	CMagnetReserve::EResource m_Resource { CMagnetReserve::EResource::Health };

public:
	CMagnetReservation() = default;
	CMagnetReservation(const CMagnetReservation&) = delete;
	CMagnetReservation& operator=(const CMagnetReservation&) = delete;
	~CMagnetReservation() { Cancel(); }

	bool TryAcquire(const std::shared_ptr<CMagnetReserve>& pReserve, CMagnetReserve::EResource Resource, int Enchant)
	{
		if(!m_Reserve.expired() || !pReserve->Reserve(Resource, Enchant))
			return false;
		m_Reserve = pReserve;
		m_Resource = Resource;
		return true;
	}

	bool BelongsTo(const std::shared_ptr<CMagnetReserve>& pReserve) const
	{
		return pReserve && m_Reserve.lock() == pReserve;
	}

	void Cancel()
	{
		if(auto pReserve = m_Reserve.lock())
			pReserve->Cancel(m_Resource);
		m_Reserve.reset();
	}

	void Capture()
	{
		if(auto pReserve = m_Reserve.lock())
			pReserve->Capture(m_Resource);
		m_Reserve.reset();
	}
};
#endif
