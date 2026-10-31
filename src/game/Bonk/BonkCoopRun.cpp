#include "BonkCoop.h"

#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "BonkRun.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Unit.h"
#include "ObjectMgr.h"
#include "Player.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace
{
    uint32 const READY_CHECK_MS = 20000;
    uint32 const LEASH_TICK_MS = 250;
    uint32 const LEASH_WARN_MS = 1000;
    float const LEASH_RADIUS = 80.0f;
    float const LEASH_PAD = 6.0f;
    float const LEASH_MIN_FACTOR = 0.15f;
    float const LEASH_WARN_MARGIN = 10.0f;
    float const LEASH_FACTOR_STEP = 0.02f;

    uint32 s_leashTimer = 0;
    uint32 s_leashWarnTimer = 0;
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

    uint32 const BLEEDOUT_MS = 30000;
    uint32 const CHOICE_WAIT_MS = 20000;
    uint32 const WAIT_SEND_MS = 1000;
    uint32 const REVIVE_MS = 3000;
    uint32 const DOWNED_SEND_MS = 250;
    float const REVIVE_RADIUS = 5.0f;
    float const REVIVE_HEALTH = 0.3f;

    struct Downed
    {
        uint32 bleedMs = BLEEDOUT_MS;
        uint32 reviveMs = 0;
        bool spectator = false;
    };

    struct GroupRun
    {
        uint32 pool = 0;
        uint32 cap = 0;
        uint32 sendTimer = 0;
        bool wiped = false;
        bool sharedPause = true;
        uint32 waitSendMs = 0;
        std::string lastWait;
        std::map<ObjectGuid, Downed> downed;
        std::map<ObjectGuid, uint32> choosingMs;
    };

    std::map<ObjectGuid, GroupRun> s_groups;
    std::map<ObjectGuid, std::set<ObjectGuid>> s_hiddenObjects;

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

    ObjectGuid HostOf(Player const* player)
    {
        BonkRun* run = player->GetBonkPlayer() ? player->GetBonkPlayer()->GetRun() : nullptr;
        if (run && run->IsCoopGuest())
            return run->GetCoopHost();
        return player->GetObjectGuid();
    }

    std::vector<Player*> GroupMembers(Player* host)
    {
        std::vector<Player*> members = { host };
        for (Player* member : OtherMembers(host))
            if (BonkRun* run = member->GetBonkPlayer()->GetRun())
                if (run->IsCoopGuest() && run->GetCoopHost() == host->GetObjectGuid())
                    members.push_back(member);
        return members;
    }

    bool AnyoneStanding(Player* host)
    {
        for (Player* member : GroupMembers(host))
            if (member->IsAlive())
                return true;
        return false;
    }

    Player* FindReviver(Player* downed, std::vector<Player*> const& members)
    {
        for (Player* member : members)
            if (member != downed && member->IsAlive() && member->IsInWorld() && member->GetMapId() == downed->GetMapId() &&
                member->GetDistance2d(downed) <= REVIVE_RADIUS)
                return member;
        return nullptr;
    }

    void SendDowned(GroupRun const& group, std::vector<Player*> const& members)
    {
        std::string list;
        for (auto const& entry : group.downed)
        {
            Player* downed = ObjectAccessor::FindPlayerNotInWorld(entry.first);
            if (!downed)
                continue;
            if (!list.empty())
                list += ",";
            list += std::string(downed->GetName()) + ":" + std::to_string((entry.second.bleedMs + 999) / IN_MILLISECONDS) + ":" +
                std::to_string(entry.second.reviveMs * 100 / REVIVE_MS) + ":" + (entry.second.spectator ? "1" : "0");
        }
        std::string const line = "coopdown~" + std::to_string(group.pool) + "~" + std::to_string(group.cap) + "~" + list;
        for (Player* member : members)
            member->GetBonkPlayer()->SendAddon(line);
    }

    void Wipe(Player* host, GroupRun& group)
    {
        group.wiped = true;
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: %s's group is all down, run over", host->GetName());
        if (BonkRun* run = host->GetBonkPlayer()->GetRun())
            run->EndInWipe();
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
        uint32 players = 1;
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
                continue;
            }
            ++players;
        }
        if (players > 1)
            BonkCoop::BeginGroupRun(host, players);
    }
}

namespace
{
    bool InLeash(Player* player)
    {
        BonkRun* run = player->GetBonkPlayer()->GetRun();
        return run && (run->IsActive() || run->IsPortalOpen()) && player->IsAlive() && player->IsInWorld();
    }

    void SetLeash(Player* player, float factor, float distance)
    {
        BonkPlayer* bonk = player->GetBonkPlayer();
        bonk->SetLeashDistance(distance);
        if (std::fabs(bonk->GetLeashFactor() - factor) < LEASH_FACTOR_STEP && (factor < 1.0f || bonk->GetLeashFactor() == 1.0f))
            return;
        bonk->SetLeashFactor(factor);
        for (UnitMoveType type : { MOVE_WALK, MOVE_RUN, MOVE_RUN_BACK, MOVE_SWIM })
            player->UpdateSpeed(type, false);
    }

    float LeashFactor(float distance, float previousDistance)
    {
        if (distance <= LEASH_RADIUS || distance < previousDistance)
            return 1.0f;
        float const into = std::min(1.0f, (distance - LEASH_RADIUS) / LEASH_PAD);
        return 1.0f - into * (1.0f - LEASH_MIN_FACTOR);
    }
}

void BonkCoop::UpdateLeash(uint32 diff)
{
    s_leashTimer += diff;
    if (s_leashTimer < LEASH_TICK_MS)
        return;
    s_leashWarnTimer += s_leashTimer;
    s_leashTimer = 0;
    bool const warn = s_leashWarnTimer >= LEASH_WARN_MS;
    if (warn)
        s_leashWarnTimer = 0;

    std::set<Player*> leashed;
    for (auto const& entry : sObjectAccessor.GetPlayers())
    {
        Player* host = entry.second;
        if (!host || !InLeash(host) || host->GetBonkPlayer()->GetRun()->IsCoopGuest())
            continue;
        std::vector<Player*> group = { host };
        for (Player* guest : OtherMembers(host))
        {
            BonkRun* run = guest->GetBonkPlayer()->GetRun();
            if (run && run->IsCoopGuest() && run->GetCoopHost() == host->GetObjectGuid() && InLeash(guest) && guest->GetMapId() == host->GetMapId())
                group.push_back(guest);
        }
        if (group.size() < 2)
            continue;

        float centreX = 0.0f, centreY = 0.0f;
        for (Player* member : group)
        {
            centreX += member->GetPositionX();
            centreY += member->GetPositionY();
        }
        centreX /= float(group.size());
        centreY /= float(group.size());

        for (Player* member : group)
        {
            leashed.insert(member);
            float const distance = member->GetDistance2d(centreX, centreY);
            float const previous = member->GetBonkPlayer()->GetLeashDistance();
            float const factor = LeashFactor(distance, previous);
            SetLeash(member, factor, distance);
            if (warn && distance > LEASH_RADIUS - LEASH_WARN_MARGIN)
                member->GetBonkPlayer()->SendAddon("leash~" + std::to_string(int32(distance)) + "~" + std::to_string(int32(LEASH_RADIUS)) +
                    "~" + std::to_string(int32(std::lround((1.0f - factor) * 100.0f))));
        }
    }

    for (auto const& entry : sObjectAccessor.GetPlayers())
    {
        Player* player = entry.second;
        if (player && !leashed.count(player) && player->GetBonkPlayer() && player->GetBonkPlayer()->GetLeashFactor() != 1.0f)
            SetLeash(player, 1.0f, 0.0f);
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
    auto group = s_groups.find(host->GetObjectGuid());
    if (group != s_groups.end() && group->second.pool < group->second.cap)
    {
        ++group->second.pool;
        for (Player* member : GroupMembers(host))
            member->GetBonkPlayer()->Notify("Floor cleared! Your group gains a revive.");
    }
}

void BonkCoop::OnHostFinished(Player* host)
{
    auto group = s_groups.find(host->GetObjectGuid());
    bool const wiped = group != s_groups.end() && group->second.wiped;
    for (Player* member : GroupMembers(host))
        member->GetBonkPlayer()->SendAddon("coopdown~0~0~");
    if (group != s_groups.end())
        s_groups.erase(group);
    for (BonkRun* run : GuestRunsOf(host))
    {
        if (wiped)
            run->EndInWipe();
        else
            run->EndWithHost();
    }
}

std::vector<Player*> BonkCoop::RunGroupOf(Player* player)
{
    if (!player->GetGroup())
        return { player };
    Player* host = ObjectAccessor::FindPlayerNotInWorld(HostOf(player));
    if (!host)
        return { player };
    return GroupMembers(host);
}

void BonkCoop::HideObjectFor(ObjectGuid object, ObjectGuid player)
{
    s_hiddenObjects[object].insert(player);
}

bool BonkCoop::IsObjectHiddenFor(ObjectGuid object, ObjectGuid player)
{
    if (s_hiddenObjects.empty())
        return false;
    auto found = s_hiddenObjects.find(object);
    return found != s_hiddenObjects.end() && found->second.count(player);
}

void BonkCoop::ForgetHiddenObject(ObjectGuid object)
{
    s_hiddenObjects.erase(object);
}

void BonkCoop::UpdateSharedPause(uint32 diff)
{
    for (auto& entry : s_groups)
    {
        GroupRun& group = entry.second;
        Player* host = ObjectAccessor::FindPlayerNotInWorld(entry.first);
        if (!host || group.wiped || !group.sharedPause)
            continue;

        std::vector<std::pair<Player*, BonkRun*>> members;
        for (Player* member : GroupMembers(host))
            if (BonkRun* run = member->GetBonkPlayer()->GetRun())
                if (run->IsActive() || run->IsPortalOpen())
                    members.push_back({ member, run });

        std::vector<std::string> waitingOn;
        uint32 longestLeftMs = 0;
        for (auto const& member : members)
        {
            ObjectGuid const guid = member.first->GetObjectGuid();
            if (!member.second->IsChoosing())
            {
                group.choosingMs.erase(guid);
                continue;
            }
            uint32& choosing = group.choosingMs[guid];
            if (choosing >= CHOICE_WAIT_MS)
                continue;
            choosing += diff;
            if (choosing >= CHOICE_WAIT_MS)
            {
                member.second->ResolveChoiceForGroup();
                continue;
            }
            waitingOn.push_back(member.first->GetName());
            longestLeftMs = std::max(longestLeftMs, CHOICE_WAIT_MS - choosing);
        }

        bool const held = !waitingOn.empty();
        for (auto const& member : members)
            member.second->SetGroupHeld(held);

        group.waitSendMs += diff;
        std::string names;
        for (std::string const& name : waitingOn)
            names += (names.empty() ? "" : ",") + name;
        if (names == group.lastWait && (names.empty() || group.waitSendMs < WAIT_SEND_MS))
            continue;
        group.waitSendMs = 0;
        group.lastWait = names;
        std::string const line = "coopwait~" + names + "~" + std::to_string((longestLeftMs + 999) / IN_MILLISECONDS);
        for (auto const& member : members)
            member.first->GetBonkPlayer()->SendAddon(line);
    }
}

void BonkCoop::BeginGroupRun(Player* host, uint32 players)
{
    GroupRun& group = s_groups[host->GetObjectGuid()];
    group = GroupRun();
    group.cap = players + 1;
    group.pool = group.cap;
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: %s starts a group run, %u players, pool %u", host->GetName(), players, group.pool);
    SendDowned(group, GroupMembers(host));
}

bool BonkCoop::IsDowned(Player const* player)
{
    auto group = s_groups.find(HostOf(player));
    return group != s_groups.end() && group->second.downed.count(player->GetObjectGuid());
}

bool BonkCoop::Down(Player* player)
{
    ObjectGuid const hostGuid = HostOf(player);
    auto found = s_groups.find(hostGuid);
    Player* host = ObjectAccessor::FindPlayerNotInWorld(hostGuid);
    if (found == s_groups.end() || !host)
        return false;
    GroupRun& group = found->second;
    Downed& downed = group.downed[player->GetObjectGuid()];
    downed = Downed();
    downed.spectator = group.pool == 0;
    std::string const name = player->GetName();
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: %s is down, pool %u/%u", name.c_str(), group.pool, group.cap);
    for (Player* member : GroupMembers(host))
    {
        if (member == player)
            member->GetBonkPlayer()->Notify(downed.spectator ? "You are down, and your group has no revives left." :
                "You are down! Stay close to your group so they can revive you.");
        else
            member->GetBonkPlayer()->Notify(downed.spectator ? name + " is down, and your group has no revives left." :
                name + " is down! Stand next to them to revive them.");
    }
    group.sendTimer = DOWNED_SEND_MS;
    if (!AnyoneStanding(host))
        Wipe(host, group);
    return true;
}

void BonkCoop::UpdateDowned(uint32 diff)
{
    std::vector<ObjectGuid> hosts;
    for (auto const& entry : s_groups)
        hosts.push_back(entry.first);
    for (ObjectGuid const& hostGuid : hosts)
    {
        auto found = s_groups.find(hostGuid);
        if (found == s_groups.end())
            continue;
        GroupRun& group = found->second;
        Player* host = ObjectAccessor::FindPlayerNotInWorld(hostGuid);
        if (!host || !host->GetBonkPlayer()->GetRun())
        {
            s_groups.erase(found);
            continue;
        }
        if (group.wiped)
            continue;
        std::vector<Player*> const members = GroupMembers(host);
        bool paused = false;
        for (Player* member : members)
            if (BonkRun* run = member->GetBonkPlayer()->GetRun())
                if (run->IsPaused())
                    paused = true;
        for (auto it = group.downed.begin(); it != group.downed.end();)
        {
            Player* downedPlayer = ObjectAccessor::FindPlayerNotInWorld(it->first);
            if (!downedPlayer || downedPlayer->IsAlive() || !downedPlayer->GetBonkPlayer()->GetRun())
            {
                it = group.downed.erase(it);
                continue;
            }
            Downed& downed = it->second;
            if (downed.spectator || paused)
            {
                ++it;
                continue;
            }
            Player* reviver = group.pool ? FindReviver(downedPlayer, members) : nullptr;
            if (reviver)
            {
                downed.reviveMs += diff;
                if (downed.reviveMs >= REVIVE_MS)
                {
                    --group.pool;
                    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: %s revived %s, pool %u/%u", reviver->GetName(), downedPlayer->GetName(), group.pool, group.cap);
                    downedPlayer->GetBonkPlayer()->GetRun()->ReviveFromGroup(REVIVE_HEALTH);
                    std::string const left = group.pool == 1 ? "1 revive left." : std::to_string(group.pool) + " revives left.";
                    for (Player* member : members)
                        member->GetBonkPlayer()->Notify(std::string(reviver->GetName()) + " revived " + downedPlayer->GetName() + "! " + left);
                    it = group.downed.erase(it);
                    group.sendTimer = DOWNED_SEND_MS;
                    continue;
                }
            }
            else
            {
                downed.reviveMs = 0;
                downed.bleedMs = downed.bleedMs > diff ? downed.bleedMs - diff : 0;
                if (downed.bleedMs == 0)
                {
                    downed.spectator = true;
                    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op: %s bled out", downedPlayer->GetName());
                    for (Player* member : members)
                        member->GetBonkPlayer()->Notify(std::string(downedPlayer->GetName()) + " bled out and watches the rest of the run.");
                    group.sendTimer = DOWNED_SEND_MS;
                }
            }
            ++it;
        }
        if (!AnyoneStanding(host))
        {
            Wipe(host, group);
            continue;
        }
        group.sendTimer += diff;
        if (group.sendTimer >= DOWNED_SEND_MS)
        {
            group.sendTimer = 0;
            SendDowned(group, members);
        }
    }
}
