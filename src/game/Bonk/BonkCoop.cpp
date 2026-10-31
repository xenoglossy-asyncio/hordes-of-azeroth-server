#include "BonkCoop.h"

#include "Group.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"

namespace
{
    char const* const GUEST_ACCOUNT_PREFIX = "HOAC";

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
}

bool BonkCoop::IsGuestAccount(Player const* player)
{
    return player && player->GetSession() && HasGuestPrefix(player->GetSession()->GetUsername());
}

void BonkCoop::JoinHostGroup(Player* guest)
{
    if (!IsGuestAccount(guest) || guest->GetGroup())
        return;
    Player* host = FindHost();
    if (!host)
        return;
    Group* group = host->GetGroup();
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
