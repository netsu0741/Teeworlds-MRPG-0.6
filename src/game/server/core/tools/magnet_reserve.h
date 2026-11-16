#ifndef GAME_SERVER_CORE_TOOLS_MAGNET_RESERVE_H
#define GAME_SERVER_CORE_TOOLS_MAGNET_RESERVE_H

#include <algorithm>

// Character-local, transient reserve. Each visible orbit holds five units.
class CMagnetReserve
{
public:
	enum class EResource { Health, Mana };
	static constexpr int UNITS_PER_ORBIT = 5;
	static constexpr float ORBIT_RADIUS = 87.0f;
	static int Slots(int Enchant) { return 2 + std::clamp(Enchant, 0, 2); }
	bool Reserve(EResource Resource, int Enchant)
	{
		auto& Entry = Get(Resource);
		if(Entry.m_Stored + Entry.m_InFlight >= Slots(Enchant) * UNITS_PER_ORBIT)
			return false;
		++Entry.m_InFlight;
		return true;
	}
	void Cancel(EResource Resource) { auto& Entry = Get(Resource); if(Entry.m_InFlight > 0) --Entry.m_InFlight; }
	void Capture(EResource Resource)
	{
		auto& Entry = Get(Resource);
		if(Entry.m_InFlight > 0) { --Entry.m_InFlight; ++Entry.m_Stored; }
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
#endif
