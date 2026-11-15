#ifndef GAME_SERVER_CORE_TOOLS_ACCOUNT_CLAIM_LIFECYCLE_H
#define GAME_SERVER_CORE_TOOLS_ACCOUNT_CLAIM_LIFECYCLE_H

#include <functional>
#include <utility>

// Game-thread state for one login. The SQL completion owns a shared reference,
// so a cancelled in-flight write is cleaned only after that write resolves.
class CAccountClaimLifecycle
{
public:
	enum class EPhase { Claiming, Reserved, Activating, Finished };
	using Cleanup = std::function<void(bool ActivationAttempted)>;
	explicit CAccountClaimLifecycle(Cleanup Fn) : m_Cleanup(std::move(Fn)) {}
	bool Cancelled() const { return m_Cancelled; }
	void Cancel()
	{
		m_Cancelled = true;
		if(m_Phase == EPhase::Reserved)
			FinishAndCleanup(false);
	}
	// Even a reported failure can have committed before a lost connection.
	bool ClaimCompleted()
	{
		if(m_Phase != EPhase::Claiming)
			return false;
		m_Phase = EPhase::Reserved;
		if(m_Cancelled)
		{
			FinishAndCleanup(false);
			return false;
		}
		return true;
	}
	bool BeginActivation()
	{
		if(m_Cancelled || m_Phase != EPhase::Reserved)
			return false;
		m_Phase = EPhase::Activating;
		return true;
	}
	bool ActivationCompleted(bool Success)
	{
		if(m_Phase != EPhase::Activating)
			return false;
		if(m_Cancelled || !Success)
		{
			FinishAndCleanup(true);
			return false;
		}
		m_Phase = EPhase::Finished;
		return true;
	}
	// The caller has arranged its own cleanup after a refused claim.
	void Finished() { m_Phase = EPhase::Finished; }
private:
	void FinishAndCleanup(bool ActivationAttempted)
	{
		m_Phase = EPhase::Finished;
		m_Cleanup(ActivationAttempted);
	}
	EPhase m_Phase = EPhase::Claiming;
	bool m_Cancelled = false;
	Cleanup m_Cleanup;
};
#endif
