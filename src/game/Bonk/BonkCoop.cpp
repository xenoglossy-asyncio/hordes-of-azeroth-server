#include "BonkCoop.h"

#include "AccountMgr.h"
#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "Chat.h"
#include "Database/DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"

#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace
{
    char const* const GUEST_ACCOUNT_PREFIX = "HOAC";
    char const* const GUEST_ACCOUNT_TABLES[] = {
        "bonk_account", "bonk_account_bank", "bonk_account_disabled", "bonk_account_floor", "bonk_account_found", "bonk_account_mastery",
        "bonk_account_mastery_node", "bonk_account_mastery_preset", "bonk_account_mastery_preset_active", "bonk_account_quest",
        "bonk_account_starter", "bonk_account_talent", "bonk_account_tier", "bonk_account_unlock", "bonk_account_upgrade",
    };
    uint32 const REMOVE_DELAY_MS = 3000;

    std::mutex s_removedLock;
    std::mutex s_seatsLock;
    std::map<ObjectGuid, uint32> s_seats;
    std::set<uint32> s_removedAccounts;

    bool HasGuestPrefix(std::string const& username)
    {
        return username.compare(0, strlen(GUEST_ACCOUNT_PREFIX), GUEST_ACCOUNT_PREFIX) == 0;
    }

    Player* FindHost()
    {
        for (auto const& entry : sObjectAccessor.GetPlayers())
        {
            Player* candidate = entry.second;
            if (candidate && candidate->IsInWorld() && candidate->GetSession() && !HasGuestPrefix(candidate->GetSession()->GetUsername()))
                return candidate;
        }
        return nullptr;
    }

    bool WasRemoved(uint32 accountId)
    {
        std::lock_guard<std::mutex> guard(s_removedLock);
        return s_removedAccounts.count(accountId) != 0;
    }

    void SendAway(Player* guest, char const* reason)
    {
        ChatHandler(guest).SendSysMessage(reason);
        ObjectGuid const guid = guest->GetObjectGuid();
        guest->m_Events.AddLambdaEventAtOffset([guid]
        {
            if (Player* player = sObjectMgr.GetPlayer(guid))
                player->GetSession()->KickPlayer();
        }, REMOVE_DELAY_MS);
    }

    bool ClassTaken(Group const* group, Player const* guest)
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            if (slot.guid == guest->GetObjectGuid())
                continue;
            if (Player* member = sObjectMgr.GetPlayer(slot.guid))
                if (member->GetClass() == guest->GetClass())
                    return true;
        }
        return false;
    }
}

bool BonkCoop::IsGuestAccount(Player const* player)
{
    return player && player->GetSession() && HasGuestPrefix(player->GetSession()->GetUsername());
}

uint32 BonkCoop::CampSeat(Player const* player)
{
    if (!IsGuestAccount(player))
        return 0;
    std::lock_guard<std::mutex> guard(s_seatsLock);
    auto found = s_seats.find(player->GetObjectGuid());
    if (found != s_seats.end())
        return found->second;

    std::set<uint32> taken;
    for (auto itr = s_seats.begin(); itr != s_seats.end();)
    {
        if (ObjectAccessor::FindPlayerNotInWorld(itr->first))
        {
            taken.insert(itr->second);
            ++itr;
        }
        else
            itr = s_seats.erase(itr);
    }
    uint32 seat = 1;
    while (seat + 1 < BONK_CAMP_SEAT_COUNT && taken.count(seat))
        ++seat;
    s_seats[player->GetObjectGuid()] = seat;
    return seat;
}

void BonkCoop::JoinHostGroup(Player* guest)
{
    if (!IsGuestAccount(guest))
        return;
    if (WasRemoved(guest->GetSession()->GetAccountId()))
    {
        SendAway(guest, "The host removed you from this game.");
        return;
    }
    if (guest->GetGroup())
        return;
    Player* host = FindHost();
    if (!host)
        return;
    if (host->GetBonkPlayer() && host->GetBonkPlayer()->IsIronman())
    {
        SendAway(guest, "The host is playing an ironman character, and ironman characters play solo only.");
        return;
    }
    Group* group = host->GetGroup();
    if (group)
    {
        std::vector<ObjectGuid> away;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (slot.guid != host->GetObjectGuid() && !sObjectMgr.GetPlayer(slot.guid))
                away.push_back(slot.guid);
        for (ObjectGuid const& guid : away)
            if (Group* current = host->GetGroup())
                Player::RemoveFromGroup(current, guid);
        group = host->GetGroup();
    }
    if (group && ClassTaken(group, guest))
    {
        SendAway(guest, "Your class is already in this group. Make a character of another class.");
        return;
    }
    if (!group)
    {
        group = new Group;
        if (!group->Create(host->GetObjectGuid(), host->GetName()))
        {
            delete group;
            return;
        }
        sObjectMgr.AddGroup(group);
    }
    if (group->IsFull())
        return;
    group->AddMember(guest->GetObjectGuid(), guest->GetName());
}

void BonkCoop::OnLogout(Player* player)
{
    if (!IsGuestAccount(player))
        return;
    if (!player->GetSession()->IsConnected())
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: %s didn't rejoin in time", player->GetName());
    {
        std::lock_guard<std::mutex> guard(s_seatsLock);
        s_seats.erase(player->GetObjectGuid());
    }
    if (Group* group = player->GetGroup())
        Player::RemoveFromGroup(group, player->GetObjectGuid());
}

void BonkCoop::OnUninvited(ObjectGuid guid)
{
    Player* guest = sObjectMgr.GetPlayer(guid);
    if (!IsGuestAccount(guest))
        return;
    {
        std::lock_guard<std::mutex> guard(s_removedLock);
        s_removedAccounts.insert(guest->GetSession()->GetAccountId());
    }
    SendAway(guest, "The host removed you from this game.");
}

void BonkCoop::DeleteGuestAccounts()
{
    std::vector<uint32> guests;
    std::unique_ptr<QueryResult> result(LoginDatabase.PQuery("SELECT `id` FROM `account` WHERE `username` LIKE '%s%%'", GUEST_ACCOUNT_PREFIX));
    if (result)
    {
        do
            guests.push_back(result->Fetch()[0].GetUInt32());
        while (result->NextRow());
    }
    for (uint32 accountId : guests)
    {
        CharacterDatabase.DirectPExecute("DELETE i, ci FROM `character_inventory` ci JOIN `characters` c ON c.`guid` = ci.`guid` "
            "JOIN `item_instance` i ON i.`guid` = ci.`item_guid` WHERE c.`account` = %u", accountId);
        sAccountMgr.DeleteAccount(accountId);
        CharacterDatabase.DirectPExecute("DELETE i FROM `item_instance` i JOIN `bonk_account_bank` b ON b.`item_guid` = i.`guid` WHERE b.`account` = %u", accountId);
        for (char const* table : GUEST_ACCOUNT_TABLES)
            CharacterDatabase.DirectPExecute("DELETE FROM `%s` WHERE `account` = %u", table, accountId);
    }
    if (!guests.empty())
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: removed %u guest accounts from earlier games", uint32(guests.size()));
}
