#ifndef BONK_COOP_H
#define BONK_COOP_H

#include "ObjectGuid.h"

class Player;
struct BonkMap;
struct BonkTier;
struct BonkRunChoice;

namespace BonkCoop
{
    bool IsGuestAccount(Player const* player);
    void JoinHostGroup(Player* guest);
    void OnUninvited(ObjectGuid guid);
    void DeleteGuestAccounts();
    void Update(uint32 diff);
    bool RequestGroupStart(Player* host, BonkTier const& tier, BonkRunChoice const& choice);
    void AnswerReady(Player* player, bool ready);
    void UpdateReadyChecks(uint32 diff);
    void OnHostEnteredStage(Player* host, BonkMap const& map, uint32 stage);
    void OnHostFinished(Player* host);
}

#endif
