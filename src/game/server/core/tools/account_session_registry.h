#ifndef GAME_SERVER_CORE_TOOLS_ACCOUNT_SESSION_REGISTRY_H
#define GAME_SERVER_CORE_TOOLS_ACCOUNT_SESSION_REGISTRY_H

#include <map>
#include <string>
#include <vector>
#include <utility>

// Process-local proof of the current locally activated owner. Unknown owners
// after process restart fail closed. Forget only after a confirmed removal or
// after a primary read proves a different owner is active.
class CAccountSessionRegistry
{
public:
	static CAccountSessionRegistry& Instance()
	{
		static CAccountSessionRegistry Registry;
		return Registry;
	}
	static constexpr size_t MAX_ACCOUNTS = 8192;
	bool Remember(int AccountID, const std::string& Token, const std::string& Fingerprint)
	{
		if(!m_Owners.contains(AccountID) && m_Owners.size() >= MAX_ACCOUNTS)
			return false; // Reject rather than silently discard ownership proof.
		m_Owners[AccountID] = { Token, Fingerprint };
		return true;
	}
	std::vector<std::pair<int, std::string>> Snapshot() const
	{
		std::vector<std::pair<int, std::string>> Result;
		for(const auto& [AccountID, Owner] : m_Owners)
			Result.emplace_back(AccountID, Owner.m_Token);
		return Result;
	}
	bool Knows(int AccountID, const std::string& Token) const
	{
		auto It = m_Owners.find(AccountID);
		return It != m_Owners.end() && It->second.m_Token == Token;
	}
	bool KnowsFingerprint(int AccountID, const std::string& Fingerprint) const
	{
		auto It = m_Owners.find(AccountID);
		return It != m_Owners.end() && It->second.m_Fingerprint == Fingerprint;
	}
	void Forget(int AccountID, const std::string& Token)
	{
		if(Knows(AccountID, Token))
			m_Owners.erase(AccountID);
	}
private:
	struct SOwner { std::string m_Token; std::string m_Fingerprint; };
	std::map<int, SOwner> m_Owners;
};
#endif
