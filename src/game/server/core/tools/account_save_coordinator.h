#ifndef GAME_SERVER_CORE_TOOLS_ACCOUNT_SAVE_COORDINATOR_H
#define GAME_SERVER_CORE_TOOLS_ACCOUNT_SAVE_COORDINATOR_H

#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Game-thread only. Each key has at most one SQL write in flight. A newer
// pending snapshot replaces an intermediate one, but never the in-flight write.
// Failed accounts remain failed until operator intervention/restart: ownership
// must not be handed off as if their writes had succeeded.
class CAccountSaveCoordinator
{
public:
	using Done = std::function<void(bool)>;
	using Task = std::function<void(Done)>;

	static CAccountSaveCoordinator& Instance()
	{
		static CAccountSaveCoordinator Coordinator;
		return Coordinator;
	}

	void Submit(int AccountID, const std::string& Key, Task Write)
	{
		const auto Pair = std::make_pair(AccountID, Key);
		auto [It, Inserted] = m_Entries.try_emplace(Pair);
		if(!Inserted)
		{
			It->second = std::move(Write);
			return;
		}
		Dispatch(Pair, std::move(Write));
	}

	bool Pending(int AccountID) const
	{
		auto It = m_Entries.lower_bound({ AccountID, {} });
		return It != m_Entries.end() && It->first.first == AccountID;
	}

	bool Failed(int AccountID) const { return m_Failed.contains(AccountID); }

	void WhenSettled(int AccountID, Done Callback)
	{
		if(!Pending(AccountID))
		{
			Callback(!Failed(AccountID));
			return;
		}
		m_Waiters[AccountID].push_back(std::move(Callback));
	}

private:
	using KeyType = std::pair<int, std::string>;
	std::map<KeyType, Task> m_Entries;
	std::set<int> m_Failed;
	std::map<int, std::vector<Done>> m_Waiters;

	void Dispatch(const KeyType& Key, Task Write)
	{
		Write([this, Key](bool Success) {
			if(!Success)
				m_Failed.insert(Key.first);
			auto It = m_Entries.find(Key);
			if(It == m_Entries.end())
				return;
			if(It->second)
			{
				Task Next = std::move(It->second);
				It->second = {};
				Dispatch(Key, std::move(Next));
				return;
			}
			m_Entries.erase(It);
			if(Pending(Key.first))
				return;
			auto Waiters = m_Waiters.find(Key.first);
			if(Waiters == m_Waiters.end())
				return;
			auto Callbacks = std::move(Waiters->second);
			m_Waiters.erase(Waiters);
			for(auto& Callback : Callbacks)
				Callback(!Failed(Key.first));
		});
	}
};

#endif
