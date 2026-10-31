#ifndef BONK_COOP_H
#define BONK_COOP_H

class Player;

namespace BonkCoop
{
    bool IsGuestAccount(Player const* player);
    void JoinHostGroup(Player* guest);
}

#endif
