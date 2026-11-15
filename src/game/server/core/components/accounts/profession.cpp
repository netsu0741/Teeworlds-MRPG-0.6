#include "profession.h"
#include <engine/server.h>
#include <engine/server/sql_string_helpers.h>

#include <game/server/entity_manager.h>
#include <game/server/gamecontext.h>
#include <game/server/core/components/achievements/achievement_data.h>
#include <game/server/core/tools/account_save_coordinator.h>
#include <game/server/core/tools/db_async_context.h>

namespace
{
	void SaveProfession(CPlayer* pPlayer, int ProfessionID, std::string Data)
	{
		const int UserID = pPlayer->Account()->GetID();
		auto pContext = DbAsync::MakeContext(pPlayer->GetCID());
		CAccountSaveCoordinator::Instance().Submit(UserID, "profession:" + std::to_string(ProfessionID),
			[UserID, ProfessionID, Data = sqlstr::HexEncode(Data), pContext](CAccountSaveCoordinator::Done Done) {
				auto Complete = [Done, pContext, UserID](bool Success, bool Updated) {
					(void)Updated; // a valid no-op is still a successful save
					Done(Success);
					if(!Success)
					{
						dbg_msg("SQL Error", "Profession save failed for account %d; session handover is blocked", UserID);
						if(auto* pCurrent = pContext->GetPlayer(); pCurrent && pCurrent->IsAuthed() && pCurrent->Account()->GetID() == UserID)
							pContext->Server()->Kick(pContext->GetClientID(), "Profession save failed; contact an administrator");
					}
				};
				Database->ExecuteWithStatus<DB::INSERT>(Complete, "tw_accounts_professions",
					"(UserID, ProfessionID, Data) VALUES ('{}', '{}', UNHEX('{}')) ON DUPLICATE KEY UPDATE Data = VALUES(Data)",
					UserID, ProfessionID, Data);
			});
	}
}

CGS* CProfession::GS() const
{
	return (CGS*)Instance::GameServerPlayer(m_ClientID);
}

CPlayer* CProfession::GetPlayer() const
{
	return GS()->GetPlayer(m_ClientID);
}

CProfession::CProfession(ProfessionIdentifier ProfID, int ProfessionType)
{
	m_Level = 1;
	m_Experience = 0;
	m_UpgradePoint = 0;
	m_ProfessionID = ProfID;
	m_ProfessionType = ProfessionType;
}

void CProfession::Init(int ClientID, const std::optional<std::string>& jsonData)
{
	m_ClientID = ClientID;

	// first initialize
	if(!jsonData.has_value())
	{
		SaveProfession(GetPlayer(), (int)m_ProfessionID, GetPreparedJsonString());
		return;
	}

	// loading from data
	mystd::json::parse(jsonData.value(), [this](nlohmann::json& json)
	{
		m_Level = json.value("level", 1);
		m_Experience = json.value("exp", 0);
		m_UpgradePoint = json.value("up", 0);
		for(auto& [ID, Value] : json["attributes"].items())
		{
			m_Attributes[(AttributeIdentifier)std::stoi(ID)] = Value;
		}
		m_EquippedSlots.load(json["equipped"].dump());
	});
}

void CProfession::Save()
{
	// save to database
	SaveProfession(GetPlayer(), (int)m_ProfessionID, GetPreparedJsonString());
}

void CProfession::AddExperience(uint64_t Experience)
{
	auto* pPlayer = GetPlayer();
	auto ExperienceNeed = computeExperience(m_Level);
	const char* pProfessionName = GetProfessionName(m_ProfessionID);
	g_EventListenerManager.Notify<IEventListener::PlayerProfessionExperience>(pPlayer, this, Experience);

	// append experience
	m_Experience += Experience;

	// check level up
	while(m_Experience >= ExperienceNeed)
	{
		// implement level up
		m_Experience -= ExperienceNeed;
		m_Level++;
		m_UpgradePoint++;
		ExperienceNeed = computeExperience(m_Level);

		// post leveling visual effects and messages
		if(m_Experience < ExperienceNeed)
		{
			if(pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsAlive())
			{
				GS()->CreateSound(pPlayer->GetCharacter()->m_Core.m_Pos, 4);
				GS()->CreateBirthdayEffect(pPlayer->GetCharacter()->m_Core.m_Pos, m_ClientID);
				GS()->EntityManager()->Text(pPlayer->GetCharacter()->m_Core.m_Pos + vec2(0, -40), 40, pProfessionName);
			}

			GS()->Chat(m_ClientID, "'{}' Level UP. Now Level {}!", pProfessionName, m_Level);
			GS()->Core()->SaveAccount(pPlayer, SAVE_PROFESSION);
		}
	}

	// A final gain must not depend on another random save before disconnect.
	Save();

	// notify listener & progress bar
	g_EventListenerManager.Notify<IEventListener::PlayerProfessionLeveling>(pPlayer, this, m_Level);
	pPlayer->ProgressBar(pProfessionName, m_Level, m_Experience, ExperienceNeed, Experience);
}

bool CProfession::Upgrade(AttributeIdentifier ID, int Units, int PricePerUnit)
{
	if(!m_Attributes.contains(ID))
		return false;

	const auto TotalPrice = Units * PricePerUnit;
	if(m_UpgradePoint < TotalPrice)
		return false;

	m_Attributes[ID] += Units;
	m_UpgradePoint -= TotalPrice;
	Save();
	return true;
}

void CProfession::ResetUpgrades()
{
	auto* pPlayer = GetPlayer();
	if(m_Attributes.empty())
	{
		GS()->Chat(m_ClientID, "This profession has no attributes to reset.");
		return;
	}

	// reset by all attributes
	int RefundedPointsTotal = 0;
	for(auto& [ID, CurrentValue] : m_Attributes)
	{
		int InitialValue = (m_ProfessionType == PROFESSION_TYPE_OTHER ? 1 : 0);
		if(CurrentValue > InitialValue)
		{
			int PointsToRefund = CurrentValue - InitialValue;
			auto* pAttInfo = GS()->GetAttributeInfo(ID);

			PointsToRefund *= pAttInfo->GetUpgradePrice();
			m_UpgradePoint += PointsToRefund;
			RefundedPointsTotal += PointsToRefund;
			CurrentValue = InitialValue;

			// update player by raw attribute
			auto TotalAttribute = pPlayer->GetTotalRawAttributeValue(ID);
			pPlayer->UpdateTotalAttributeValue(ID, TotalAttribute);
		}
		else if(CurrentValue < InitialValue)
			CurrentValue = InitialValue;
	}

	// information
	if(RefundedPointsTotal > 0)
	{
		GS()->Chat(m_ClientID, "Upgrades have been reset. You received {} points back.", RefundedPointsTotal);
		GS()->Chat(m_ClientID, "You now have {} total upgrade points.", m_UpgradePoint);
		Save();
		return;
	}

	GS()->Chat(m_ClientID, "No upgrade points were refunded. Attributes may already be at their base values.");
}


std::string CProfession::GetPreparedJsonString() const
{
	nlohmann::json json;
	json["level"] = m_Level;
	json["exp"] = m_Experience;
	json["up"] = m_UpgradePoint;
	for(auto [ID, Value] : m_Attributes)
	{
		json["attributes"][std::to_string((int)ID)] = Value;
	}

	json["equipped"] = m_EquippedSlots.dumpJson();
	return json.dump();
}
