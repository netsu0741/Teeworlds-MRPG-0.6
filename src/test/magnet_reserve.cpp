#include <gtest/gtest.h>
#include <game/server/core/tools/magnet_reserve.h>

using Resource = CMagnetReserve::EResource;

TEST(MagnetReserve, CapacityIncludesIncomingPickups)
{
	CMagnetReserve Reserve;
	for(int i = 0; i < 10; ++i)
		ASSERT_TRUE(Reserve.Reserve(Resource::Health, 0));
	EXPECT_FALSE(Reserve.Reserve(Resource::Health, 0));
	EXPECT_TRUE(Reserve.Reserve(Resource::Mana, 0));
	Reserve.Cancel(Resource::Health);
	EXPECT_TRUE(Reserve.Reserve(Resource::Health, 0));
}

TEST(MagnetReserve, ThreeLevelsAndConsumptionFreeOrbitSlots)
{
	CMagnetReserve Reserve;
	for(int i = 0; i < 20; ++i)
	{
		ASSERT_TRUE(Reserve.Reserve(Resource::Health, 2));
		Reserve.Capture(Resource::Health);
	}
	EXPECT_FALSE(Reserve.Reserve(Resource::Health, 2));
	EXPECT_EQ(Reserve.Orbits(Resource::Health), 4);
	EXPECT_EQ(Reserve.Consume(Resource::Health, 15), 15);
	EXPECT_EQ(Reserve.Stored(Resource::Health), 5);
	EXPECT_EQ(Reserve.Orbits(Resource::Health), 1);
	EXPECT_TRUE(Reserve.Reserve(Resource::Health, 2));
	EXPECT_EQ(CMagnetReserve::Slots(0), 2);
	EXPECT_EQ(CMagnetReserve::Slots(1), 3);
	EXPECT_EQ(CMagnetReserve::Slots(2), 4);
}

TEST(MagnetReserve, SeparateAlternatingPositionsAndLimits)
{
	CMagnetReserve Reserve;
	EXPECT_EQ(CMagnetReserve::OrbitPosition(Resource::Health, 0), 0);
	EXPECT_EQ(CMagnetReserve::OrbitPosition(Resource::Mana, 0), 1);
	EXPECT_EQ(CMagnetReserve::OrbitPosition(Resource::Health, 1), 2);
	EXPECT_EQ(CMagnetReserve::OrbitPosition(Resource::Mana, 1), 3);
	for(const auto Kind : { Resource::Health, Resource::Mana })
	{
		for(int i = 0; i < 10; ++i)
		{
			EXPECT_EQ(Reserve.IncomingSlot(Kind, 0), i / 5);
			ASSERT_TRUE(Reserve.Reserve(Kind, 0));
			Reserve.Capture(Kind);
		}
		EXPECT_EQ(Reserve.Orbits(Kind), 2);
		EXPECT_FALSE(Reserve.Reserve(Kind, 0));
	}
}

TEST(MagnetReserve, IndependentResourcesAndPartialOrbit)
{
	CMagnetReserve Reserve;
	for(int i = 0; i < 6; ++i)
	{
		ASSERT_TRUE(Reserve.Reserve(Resource::Mana, 1));
		Reserve.Capture(Resource::Mana);
	}
	EXPECT_EQ(Reserve.Orbits(Resource::Mana), 2);
	EXPECT_EQ(Reserve.Orbits(Resource::Health), 0);
	EXPECT_EQ(Reserve.Consume(Resource::Mana, 1), 1);
	EXPECT_EQ(Reserve.Orbits(Resource::Mana), 1);
	EXPECT_EQ(Reserve.Consume(Resource::Mana, 100), 5);
	EXPECT_EQ(Reserve.Orbits(Resource::Mana), 0);
}
