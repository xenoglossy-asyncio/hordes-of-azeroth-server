#include "BonkCoop.h"

#include "AccountMgr.h"
#include "Chat.h"
#include "Database/DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"

#include <mutex>
#include <set>
#include <vector>

namespace
{
    char const* const GUEST_ACCOUNT_PREFIX = "HOAC";
    uint32 const REMOVE_DELAY_MS = 3000;

    std::mutex s_removedLock;
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
    Group* group = host->GetGroup();
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
        sAccountMgr.DeleteAccount(accountId);
    if (!guests.empty())
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: removed %u guest accounts from earlier games", uint32(guests.size()));
}
