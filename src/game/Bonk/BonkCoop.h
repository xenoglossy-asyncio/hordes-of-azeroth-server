#ifndef BONK_COOP_H
#define BONK_COOP_H

#include "ObjectGuid.h"

class Player;

namespace BonkCoop
{
    bool IsGuestAccount(Player const* player);
    void JoinHostGroup(Player* guest);
    void OnUninvited(ObjectGuid guid);
    void DeleteGuestAccounts();
    void Update(uint32 diff);
}

#endif
