#include "BonkCoop.h"

#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "BonkRun.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"

#include <map>
#include <vector>

namespace
{
    uint32 const READY_CHECK_MS = 20000;
    int const ANSWER_WAITING = 0;
    int const ANSWER_YES = 1;
    int const ANSWER_NO = -1;

    struct ReadyCheck
    {
        ObjectGuid host;
        BonkTier const* tier = nullptr;
        BonkRunChoice choice;
        std::map<ObjectGuid, int> answers;
        uint32 leftMs = READY_CHECK_MS;
    };

    std::vector<ReadyCheck> s_checks;

    std::vector<Player*> OtherMembers(Player* player)
    {
        std::vector<Player*> members;
        Group* group = player->GetGroup();
        if (!group)
            return members;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (slot.guid != player->GetObjectGuid())
                if (Player* member = ObjectAccessor::FindPlayerNotInWorld(slot.guid))
                    members.push_back(member);
        return members;
    }

    std::vector<BonkRun*> GuestRunsOf(Player* host)
    {
        std::vector<BonkRun*> runs;
        for (Player* member : OtherMembers(host))
            if (BonkRun* run = member->GetBonkPlayer()->GetRun())
                if (run->IsCoopGuest() && run->GetCoopHost() == host->GetObjectGuid())
                    runs.push_back(run);
        return runs;
    }

    std::string StartText(BonkTier const& tier, BonkRunChoice const& choice)
    {
        std::string text = "The host wants to start " + tier.name;
        if (choice.startFloor > 1)
            text += " floor " + std::to_string(choice.startFloor);
        return text + ".";
    }

    bool Done(ReadyCheck const& check)
    {
        if (check.leftMs == 0)
            return true;
        for (auto const& answer : check.answers)
            if (answer.second == ANSWER_WAITING)
                return false;
        return true;
    }

    void Conclude(ReadyCheck const& check)
    {
        Player* host = sObjectMgr.GetPlayer(check.host);
        for (auto const& answer : check.answers)
            if (Player* guest = sObjectMgr.GetPlayer(answer.first))
                guest->GetBonkPlayer()->SendAddon("coopreadyend");
        if (!host || host->GetBonkPlayer()->GetRun())
            return;

        BonkRunChoice choice = check.choice;
        choice.groupReady = true;
        std::string error;
        if (!host->GetBonkPlayer()->StartRun(*check.tier, error, choice))
        {
            host->GetBonkPlayer()->Notify(error);
            return;
        }
        for (auto const& answer : check.answers)
        {
            Player* guest = sObjectMgr.GetPlayer(answer.first);
            if (!guest)
                continue;
            if (answer.second != ANSWER_YES)
            {
                host->GetBonkPlayer()->Notify(std::string(guest->GetName()) + " wasn't ready and stays in town.");
                continue;
            }
            std::string guestError;
            if (!guest->GetBonkPlayer()->StartGuestRun(*check.tier, choice, host, guestError))
            {
                guest->GetBonkPlayer()->Notify(guestError);
                host->GetBonkPlayer()->Notify(std::string(guest->GetName()) + " couldn't join the run: " + guestError);
            }
        }
    }
}

bool BonkCoop::RequestGroupStart(Player* host, BonkTier const& tier, BonkRunChoice const& choice)
{
    std::vector<Player*> guests;
    for (Player* member : OtherMembers(host))
        if (!member->GetBonkPlayer()->GetRun())
            guests.push_back(member);
    if (guests.empty())
        return false;

    for (ReadyCheck const& check : s_checks)
        if (check.host == host->GetObjectGuid())
        {
            host->GetBonkPlayer()->Notify("Your group is still answering.");
            return true;
        }

    ReadyCheck check;
    check.host = host->GetObjectGuid();
    check.tier = &tier;
    check.choice = choice;
    std::string const text = StartText(tier, choice);
    for (Player* guest : guests)
    {
        check.answers[guest->GetObjectGuid()] = ANSWER_WAITING;
        guest->GetBonkPlayer()->SendAddon("coopready~" + text + "~" + std::to_string(READY_CHECK_MS / IN_MILLISECONDS));
    }
    s_checks.push_back(check);
    host->GetBonkPlayer()->Notify("Asking your group if they're ready.");
    return true;
}

void BonkCoop::AnswerReady(Player* player, bool ready)
{
    for (ReadyCheck& check : s_checks)
    {
        auto answer = check.answers.find(player->GetObjectGuid());
        if (answer != check.answers.end())
            answer->second = ready ? ANSWER_YES : ANSWER_NO;
    }
}

void BonkCoop::UpdateReadyChecks(uint32 diff)
{
    for (size_t i = 0; i < s_checks.size();)
    {
        ReadyCheck& check = s_checks[i];
        check.leftMs = check.leftMs > diff ? check.leftMs - diff : 0;
        if (!Done(check))
        {
            ++i;
            continue;
        }
        ReadyCheck const finished = check;
        s_checks.erase(s_checks.begin() + i);
        Conclude(finished);
    }
}

void BonkCoop::OnHostEnteredStage(Player* host, BonkMap const& map, uint32 stage)
{
    for (BonkRun* run : GuestRunsOf(host))
        run->FollowToStage(map, stage);
}

void BonkCoop::OnHostFinished(Player* host)
{
    for (BonkRun* run : GuestRunsOf(host))
        run->EndWithHost();
}
