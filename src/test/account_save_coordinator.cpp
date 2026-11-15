#include <gtest/gtest.h>
#include <game/server/core/tools/account_save_coordinator.h>

#include <map>
#include <vector>

TEST(AccountSaveCoordinator, OrdersSnapshotsAndWaitsForAllKeys)
{
	CAccountSaveCoordinator Queue;
	std::vector<int> Dispatched;
	std::map<int, CAccountSaveCoordinator::Done> Finish;
	auto Write = [&](int Value) {
		return [&, Value](CAccountSaveCoordinator::Done Done) {
			Dispatched.push_back(Value);
			Finish[Value] = std::move(Done);
		};
	};
	Queue.Submit(1, "item:10", Write(1));
	Queue.Submit(1, "item:10", Write(2));
	Queue.Submit(1, "item:10", Write(3));
	Queue.Submit(1, "profession:4", Write(4));
	Queue.Submit(2, "item:10", Write(5));
	EXPECT_EQ(Dispatched, (std::vector<int>{1, 4, 5}));
	bool Called = false;
	bool Successful = false;
	Queue.WhenSettled(1, [&](bool Success) { Called = true; Successful = Success; });
	Finish.at(1)(true);
	EXPECT_EQ(Dispatched, (std::vector<int>{1, 4, 5, 3}));
	Finish.at(4)(true);
	EXPECT_FALSE(Called);
	Finish.at(3)(true);
	EXPECT_TRUE(Called);
	EXPECT_TRUE(Successful);
	EXPECT_FALSE(Queue.Pending(1));
	EXPECT_TRUE(Queue.Pending(2));
	Finish.at(5)(true);
}

TEST(AccountSaveCoordinator, FailedSaveBlocksHandoverEvenAfterLaterSuccess)
{
	CAccountSaveCoordinator Queue;
	std::vector<CAccountSaveCoordinator::Done> Finish;
	auto Write = [&](CAccountSaveCoordinator::Done Done) { Finish.push_back(std::move(Done)); };
	Queue.Submit(42, "item:10", Write);
	Queue.Submit(42, "item:10", Write);
	bool Called = false;
	bool Successful = true;
	Queue.WhenSettled(42, [&](bool Success) { Called = true; Successful = Success; });
	auto First = Finish[0];
	First(false);
	ASSERT_EQ(Finish.size(), 2u);
	EXPECT_FALSE(Called);
	Finish[1](true);
	EXPECT_TRUE(Called);
	EXPECT_FALSE(Successful);
	EXPECT_TRUE(Queue.Failed(42));
	Queue.WhenSettled(42, [&](bool Success) { EXPECT_FALSE(Success); });
}

TEST(AccountSaveCoordinator, SynchronousCompletionDoesNotLeavePendingEntry)
{
	CAccountSaveCoordinator Queue;
	Queue.Submit(1, "item:1", [](CAccountSaveCoordinator::Done Done) { Done(true); });
	EXPECT_FALSE(Queue.Pending(1));
	Queue.WhenSettled(1, [](bool Success) { EXPECT_TRUE(Success); });
}

#include <game/server/core/tools/account_session_registry.h>

TEST(AccountSessionRegistry, RequiresProofOfExactOwnerForLocalTakeover)
{
	CAccountSessionRegistry Registry;
	EXPECT_FALSE(Registry.KnowsFingerprint(42, "fingerprint"));
	Registry.Remember(42, "owner-token", "fingerprint");
	EXPECT_TRUE(Registry.KnowsFingerprint(42, "fingerprint"));
	EXPECT_FALSE(Registry.KnowsFingerprint(42, "different"));
	EXPECT_TRUE(Registry.Knows(42, "owner-token"));
	EXPECT_FALSE(Registry.Knows(42, "different-token"));
	EXPECT_FALSE(Registry.Knows(43, "owner-token"));
}

TEST(AccountSessionRegistry, RestartDoesNotTrustPreviousProcess)
{
	CAccountSessionRegistry BeforeRestart;
	BeforeRestart.Remember(42, "owner-token", "fingerprint");
	CAccountSessionRegistry AfterRestart;
	EXPECT_FALSE(AfterRestart.KnowsFingerprint(42, "fingerprint"));
	EXPECT_FALSE(AfterRestart.Knows(42, "owner-token"));
}

#include <game/server/core/tools/account_claim_lifecycle.h>

TEST(AccountClaimLifecycle, DisconnectDuringClaimWaitsForWriteBeforeRelease)
{
	std::vector<bool> Cleanup;
	CAccountClaimLifecycle Claim([&](bool Activated) { Cleanup.push_back(Activated); });
	Claim.Cancel();
	EXPECT_TRUE(Cleanup.empty());
	EXPECT_FALSE(Claim.ClaimCompleted());
	EXPECT_EQ(Cleanup, (std::vector<bool>{false}));
	Claim.Cancel();
	EXPECT_EQ(Cleanup.size(), 1u);
}

TEST(AccountClaimLifecycle, ReloadDuringAccountLoadReleasesOnce)
{
	std::vector<bool> Cleanup;
	CAccountClaimLifecycle Claim([&](bool Activated) { Cleanup.push_back(Activated); });
	ASSERT_TRUE(Claim.ClaimCompleted());
	Claim.Cancel();
	Claim.Cancel();
	EXPECT_EQ(Cleanup, (std::vector<bool>{false}));
	EXPECT_FALSE(Claim.BeginActivation());
}

TEST(AccountClaimLifecycle, DisconnectDuringActivationWaitsForCompletion)
{
	std::vector<bool> Cleanup;
	CAccountClaimLifecycle Claim([&](bool Activated) { Cleanup.push_back(Activated); });
	ASSERT_TRUE(Claim.ClaimCompleted());
	ASSERT_TRUE(Claim.BeginActivation());
	Claim.Cancel();
	EXPECT_TRUE(Cleanup.empty());
	EXPECT_FALSE(Claim.ActivationCompleted(true));
	EXPECT_EQ(Cleanup, (std::vector<bool>{true}));
}

TEST(AccountClaimLifecycle, FailedActivationCleansPossiblyCommittedRow)
{
	std::vector<bool> Cleanup;
	CAccountClaimLifecycle Claim([&](bool Activated) { Cleanup.push_back(Activated); });
	ASSERT_TRUE(Claim.ClaimCompleted());
	ASSERT_TRUE(Claim.BeginActivation());
	EXPECT_FALSE(Claim.ActivationCompleted(false));
	EXPECT_EQ(Cleanup, (std::vector<bool>{true}));
}

TEST(AccountSessionRegistry, RemovalIsTokenScopedAndReactivationReplacesProof)
{
	CAccountSessionRegistry Registry;
	ASSERT_TRUE(Registry.Remember(7, "old", "old-hash"));
	ASSERT_TRUE(Registry.Remember(7, "new", "new-hash"));
	Registry.Forget(7, "old");
	EXPECT_TRUE(Registry.Knows(7, "new"));
	EXPECT_FALSE(Registry.KnowsFingerprint(7, "old-hash"));
	Registry.Forget(7, "new");
	EXPECT_TRUE(Registry.Snapshot().empty());
}

TEST(AccountSessionRegistry, CapacityFailsClosedWithoutEviction)
{
	CAccountSessionRegistry Registry;
	for(size_t i = 0; i < CAccountSessionRegistry::MAX_ACCOUNTS; ++i)
		ASSERT_TRUE(Registry.Remember((int)i, "token", "hash"));
	EXPECT_FALSE(Registry.Remember((int)CAccountSessionRegistry::MAX_ACCOUNTS, "new", "new-hash"));
	EXPECT_TRUE(Registry.Knows(0, "token"));
	Registry.Forget(0, "token");
	EXPECT_TRUE(Registry.Remember((int)CAccountSessionRegistry::MAX_ACCOUNTS, "new", "new-hash"));
}
