#ifndef GAME_SERVER_CORE_TOOLS_ACCOUNT_SESSION_REGISTRY_H
#define GAME_SERVER_CORE_TOOLS_ACCOUNT_SESSION_REGISTRY_H

#include <map>
#include <set>
#include <string>

// Process-local proof that this process activated the old owner. It deliberately
// survives world reloads but not process restarts. Unknown owners fail closed.
class CAccountSessionRegistry
{
public:
	static CAccountSessionRegistry& Instance()
	{
		static CAccountSessionRegistry Registry;
		return Registry;
	}
	void Remember(int AccountID, const std::string& Token, const std::string& Fingerprint)
	{
		m_Tokens[AccountID].insert(Token);
		m_Fingerprints[AccountID].insert(Fingerprint);
	}
	bool Knows(int AccountID, const std::string& Token) const
	{
		auto It = m_Tokens.find(AccountID);
		return It != m_Tokens.end() && It->second.contains(Token);
	}
	bool KnowsFingerprint(int AccountID, const std::string& Fingerprint) const
	{
		auto It = m_Fingerprints.find(AccountID);
		return It != m_Fingerprints.end() && It->second.contains(Fingerprint);
	}
private:
	std::map<int, std::set<std::string>> m_Tokens;
	std::map<int, std::set<std::string>> m_Fingerprints;
};
#endif
