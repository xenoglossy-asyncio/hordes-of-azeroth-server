#ifndef BONK_COOP_H
#define BONK_COOP_H

#include "ObjectGuid.h"

#include <vector>

class Player;
struct BonkMap;
struct BonkTier;
struct BonkRunChoice;

namespace BonkCoop
{
    bool IsGuestAccount(Player const* player);
    uint32 CampSeat(Player const* player);
    void JoinHostGroup(Player* guest);
    void OnUninvited(ObjectGuid guid);
    void OnLogout(Player* player);
    void DeleteGuestAccounts();
    void Update(uint32 diff);
    bool RequestGroupStart(Player* host, BonkTier const& tier, BonkRunChoice const& choice);
    void AnswerReady(Player* player, bool ready);
    void UpdateReadyChecks(uint32 diff);
    void UpdateLeash(uint32 diff);
    void OnHostEnteredStage(Player* host, BonkMap const& map, uint32 stage);
    void OnHostFinished(Player* host);
    void BeginGroupRun(Player* host, uint32 players);
    bool Down(Player* player);
    bool IsDowned(Player const* player);
    void UpdateDowned(uint32 diff);
    void UpdateSharedPause(uint32 diff);
    std::vector<Player*> RunGroupOf(Player* player);
    void ShareExperience(Player* collector, float levelFraction);
    void HideObjectFor(ObjectGuid object, ObjectGuid player);
    bool IsObjectHiddenFor(ObjectGuid object, ObjectGuid player);
    void ForgetHiddenObject(ObjectGuid object);
}

#endif
