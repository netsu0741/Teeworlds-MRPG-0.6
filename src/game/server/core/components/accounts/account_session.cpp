/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "account_session.h"

#include <base/system.h>
#include <game/server/gamecontext.h>

#include <algorithm>

namespace AccountSession
{
	namespace
	{
		constexpr const char* TABLE = "tw_account_sessions";
		constexpr int TOKEN_LENGTH = 48;
		constexpr size_t BATCH_SIZE = 100;

		// A reservation can be waiting on another server's writes. Age alone
		// cannot prove those writes are finished, so never steal a reservation.
		std::string ClaimableCondition()
		{
			return "Heartbeat <> 0";
		}
	}

	std::string GenerateToken()
	{
		char aToken[TOKEN_LENGTH + 1] {};
		secure_random_password(aToken, sizeof(aToken), TOKEN_LENGTH);
		return std::string(aToken);
	}

	void ReadRecord(int AccountID, ReadCallback Done)
	{
		auto pRead = Database->Prepare<DB::SELECT>("ServerID, ClientID, SessionToken, Heartbeat", TABLE,
			"WHERE AccountID = '{}'", AccountID);
		pRead->PrimaryRead(); // the row arbitrates between servers: a stale snapshot fails the claim below
		pRead->AtExecute([Done](ResultPtr pRes)
		{
			// "No row" is a valid answer; a failed query is not. Only the former may
			// lead to a claim of a free account.
			if(!pRes || !*pRes)
			{
				Done(std::nullopt);
				return;
			}

			SRecord Record;
			if(pRes->next())
			{
				Record.m_Exists = true;
				Record.m_ServerID = pRes->getInt("ServerID");
				Record.m_ClientID = pRes->getInt("ClientID");
				Record.m_Token = pRes->getString("SessionToken");
				Record.m_Heartbeat = pRes->getInt("Heartbeat");

			}
			Done(Record);
		});
	}

	void TryClaim(const SClaim& Claim, DoneCallback Done)
	{
		if(!Claim.m_Previous.m_Exists)
		{
			// The row was absent when read. ON DUPLICATE KEY UPDATE with a no-op
			// assignment reports 0 rows if someone created it meanwhile, instead of an error.
			Database->Execute<DB::INSERT>(std::move(Done), TABLE,
				"(AccountID, ServerID, ClientID, SessionToken, Heartbeat, UpdatedAt) VALUES ('{}', '{}', '{}', '{}', 0, NOW()) ON DUPLICATE KEY UPDATE AccountID = AccountID",
				Claim.m_AccountID, g_Config.m_SvSyncServerID, Claim.m_ClientID, Claim.m_Token);
			return;
		}

		// Compare-and-swap: matches only while the row still carries the token we read.
		Database->Execute<DB::UPDATE>(std::move(Done), TABLE,
			"ServerID = '{}', ClientID = '{}', SessionToken = '{}', Heartbeat = 0, UpdatedAt = NOW() WHERE AccountID = '{}' AND SessionToken = '{}' AND {}",
			g_Config.m_SvSyncServerID, Claim.m_ClientID, Claim.m_Token, Claim.m_AccountID, Claim.m_Previous.m_Token, ClaimableCondition());
	}

	void Activate(const SClaim& Claim, DoneCallback Done)
	{
		Database->Execute<DB::UPDATE>(std::move(Done), TABLE,
			"Heartbeat = UNIX_TIMESTAMP(), UpdatedAt = NOW() WHERE AccountID = '{}' AND SessionToken = '{}' AND Heartbeat = 0",
			Claim.m_AccountID, Claim.m_Token);
	}

	void Release(const SClaim& Claim, DoneCallback Done)
	{
		if(!Claim.m_Previous.m_Exists)
		{
			Database->Execute<DB::REMOVE>(std::move(Done), TABLE,
				"WHERE AccountID = '{}' AND SessionToken = '{}' AND Heartbeat = 0", Claim.m_AccountID, Claim.m_Token);
			return;
		}

		const SRecord& Prev = Claim.m_Previous;
		Database->Execute<DB::UPDATE>(std::move(Done), TABLE,
			"ServerID = '{}', ClientID = '{}', SessionToken = '{}', Heartbeat = '{}', UpdatedAt = NOW() WHERE AccountID = '{}' AND SessionToken = '{}' AND Heartbeat = 0",
			Prev.m_ServerID, Prev.m_ClientID, Prev.m_Token, Prev.m_Heartbeat, Claim.m_AccountID, Claim.m_Token);
	}

	void Remove(const SKey& Key, DoneCallback Done)
	{
		Database->Execute<DB::REMOVE>(std::move(Done), TABLE, "WHERE AccountID = '{}' AND SessionToken = '{}'", Key.m_AccountID, Key.m_Token);
	}

	void RefreshHeartbeats(const std::vector<SKey>& vKeys)
	{
		// Batched: these writes go to the primary, possibly over the WAN.
		for(size_t Begin = 0; Begin < vKeys.size(); Begin += BATCH_SIZE)
		{
			const size_t End = std::min(vKeys.size(), Begin + BATCH_SIZE);
			std::string Conditions;
			for(size_t i = Begin; i < End; ++i)
			{
				if(!Conditions.empty())
					Conditions += " OR ";
				Conditions += fmt_default("(AccountID = '{}' AND SessionToken = '{}')", vKeys[i].m_AccountID, vKeys[i].m_Token);
			}
			Database->Execute<DB::UPDATE>(TABLE, "Heartbeat = UNIX_TIMESTAMP(), UpdatedAt = NOW() WHERE Heartbeat <> 0 AND ({})", Conditions);
		}
	}

	void FetchTokens(const std::vector<int>& vAccountIDs, RowsCallback Done)
	{
		if(vAccountIDs.empty())
		{
			Done({});
			return;
		}

		std::string List;
		for(const int AccountID : vAccountIDs)
		{
			if(!List.empty())
				List += ", ";
			List += std::to_string(AccountID);
		}

		auto pFetch = Database->Prepare<DB::SELECT>("AccountID, SessionToken", TABLE, "WHERE AccountID IN ({})", List);
		pFetch->PrimaryRead(); // a stale token here would kick a session that has just claimed its row
		pFetch->AtExecute([Done](ResultPtr pRes)
		{
			// A failed query says nothing about ownership: deliver nothing.
			if(!pRes || !*pRes)
				return;

			std::vector<SKey> vRows;
			while(pRes->next())
				vRows.push_back({ pRes->getInt("AccountID"), pRes->getString("SessionToken") });
			Done(vRows);
		});
	}
}
