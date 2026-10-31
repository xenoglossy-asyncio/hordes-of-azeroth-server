#include "BonkRunStats.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "BonkRun.h"
#include "CellImpl.h"
#include "Creature.h"
#include "Database/DBCFileLoader.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "Timer.h"
#include "World.h"

#include <algorithm>

#include <list>
#include <mutex>
#include <set>

namespace
{
    uint32 const BONK_BOSS_BAR_MS    = 250;
    float const BONK_BOSS_BAR_RANGE  = 150.0f;

    struct BossBar
    {
        uint32 timer = 0;
        std::string lastLine;
    };

    std::mutex s_lock;
    std::map<ObjectGuid, BonkRunStats> s_stats;
    std::map<ObjectGuid, BossBar> s_bossBars;

    bool InRun(Player const* player)
    {
        return player->GetBonkPlayer()->GetRun() != nullptr;
    }

    BonkRun const* GetPausedRunOf(Unit const* victim)
    {
        Creature const* mob = victim->ToCreature();
        BonkMobAI const* ai = mob ? dynamic_cast<BonkMobAI const*>(mob->AI()) : nullptr;
        Player* owner = ai ? sObjectAccessor.FindPlayer(ai->GetOwnerGuid()) : nullptr;
        BonkRun const* run = owner ? owner->GetBonkPlayer()->GetRun() : nullptr;
        return run && run->IsPaused() && !run->IsAway() ? run : nullptr;
    }

    void LogPausedHit(Unit* attacker, Unit* victim, uint32 damage, SpellEntry const* spellProto)
    {
        if (!GetPausedRunOf(victim))
            return;
        uint32 const createdBy = attacker->GetUInt32Value(UNIT_CREATED_BY_SPELL);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Bonk paused hit: %s (entry %u, %s, created by spell %u, time frozen %s) hit %s (entry %u) for %u with spell %u %s, health %u of %u",
            attacker->GetName(), attacker->GetEntry(), attacker->GetGuidStr().c_str(), createdBy, attacker->IsTimeFrozen() ? "yes" : "no",
            victim->GetName(), victim->GetEntry(), damage, spellProto ? spellProto->Id : 0, spellProto ? spellProto->SpellName[0].c_str() : "melee",
            victim->GetHealth(), victim->GetMaxHealth());
    }

    std::string KeywordStacksText(Player* player, Creature const* victim)
    {
        BonkRun const* run = player->GetBonkPlayer()->GetRun();
        std::string text;
        if (!run)
            return text;
        for (auto const& itr : sBonkMgr.GetKeywords())
            if (uint32 const stacks = run->GetKeywordStacks(victim, itr.second.id))
                text += (text.empty() ? "" : ",") + std::to_string(uint32(itr.second.id)) + ":" + std::to_string(stacks);
        return text;
    }

    char const* const BONK_SCHOOL_TYPES[MAX_SPELL_SCHOOL] = { "phys", "holy", "fire", "nature", "frost", "shadow", "arcane" };

    std::string SchoolType(uint32 spellId)
    {
        SpellEntry const* spellInfo = spellId ? sSpellMgr.GetSpellEntry(spellId) : nullptr;
        return BONK_SCHOOL_TYPES[spellInfo && spellInfo->School < MAX_SPELL_SCHOOL ? spellInfo->School : SPELL_SCHOOL_NORMAL];
    }

    std::string DamageType(uint32 spellId)
    {
        if (BonkKeywordInfo const* keyword = spellId ? sBonkMgr.GetKeywordBySpell(spellId) : nullptr)
        {
            switch (keyword->id)
            {
                case BONK_KEYWORD_BLEED:   return "bleed";
                case BONK_KEYWORD_POISON:  return "poison";
                case BONK_KEYWORD_BURNING: return "burn";
                case BONK_KEYWORD_DECAY:   return "decay";
                case BONK_KEYWORD_AGONY:   return "agony";
                case BONK_KEYWORD_MOONFIRE: return "moonfire";
                default:                   break;
            }
        }
        return SchoolType(spellId);
    }

    uint32 const BONK_RECAP_WINDOW_MS   = 10000;
    uint32 const BONK_RECAP_SLOT_MS     = 100;
    uint32 const BONK_RECAP_MAX_SOURCES = 4;
    uint32 const BONK_RECAP_MAX_EVENTS  = 150;
    size_t const BONK_RECAP_LINE_CHARS  = 220;
    char const* const BONK_ICON_PREFIX  = "Interface\\Icons\\";
    char const* const BONK_MELEE_ICON   = "INV_Sword_04";
    char const* const BONK_UNKNOWN_ICON = "INV_Misc_QuestionMark";

    std::string SpellIconName(SpellEntry const* spellInfo)
    {
        static std::map<uint32, std::string> icons;
        static std::once_flag loaded;
        std::call_once(loaded, []()
        {
            DBCFileLoader file;
            std::string const path = sWorld.GetDataPath() + std::to_string(uint32(SUPPORTED_CLIENT_BUILD)) + "/dbc/SpellIcon.dbc";
            if (!file.Load(path.c_str(), "ns"))
                return;
            size_t const prefixLength = strlen(BONK_ICON_PREFIX);
            for (uint32 i = 0; i < file.GetNumRows(); ++i)
            {
                DBCFileLoader::Record row = file.getRecord(i);
                char const* texture = row.getString(1);
                if (!texture || !StringStartsWithCaseInsensitive(texture, BONK_ICON_PREFIX))
                    continue;
                icons[row.getUInt(0)] = texture + prefixLength;
            }
        });
        if (!spellInfo)
            return BONK_MELEE_ICON;
        auto itr = icons.find(spellInfo->SpellIconID);
        return itr != icons.end() ? itr->second : BONK_UNKNOWN_ICON;
    }

    BonkSpawnKind SpawnKindOf(Creature* creature)
    {
        BonkMobAI const* ai = creature ? dynamic_cast<BonkMobAI const*>(creature->AI()) : nullptr;
        return ai ? ai->GetKind() : BONK_SPAWN_NORMAL;
    }

    Creature* NamedSource(Unit* attacker)
    {
        Creature* creature = attacker->ToCreature();
        if (!creature || *creature->GetName() || !creature->IsTemporarySummon())
            return creature;
        Creature* summoner = attacker->GetMap()->GetCreature(static_cast<TemporarySummon*>(creature)->GetSummonerGuid());
        return summoner && *summoner->GetName() ? summoner : creature;
    }

    BonkRecentHit DescribeHit(Unit* attacker, SpellEntry const* spellProto)
    {
        BonkRecentHit hit;
        Creature* source = NamedSource(attacker);
        hit.source = source ? source->GetName() : attacker->GetName();
        hit.kind = SpawnKindOf(source);
        hit.spell = spellProto ? spellProto->SpellName[0] : "Melee";
        hit.icon = SpellIconName(spellProto);
        if (hit.source.empty())
        {
            hit.source = hit.spell;
            hit.spell.clear();
        }
        return hit;
    }

    void TrimRecentHits(std::deque<BonkRecentHit>& hits, uint32 now)
    {
        while (!hits.empty() && WorldTimer::getMSTimeDiff(hits.front().ms, now) > BONK_RECAP_WINDOW_MS)
            hits.pop_front();
    }

    void PushRecentHit(Player* player, BonkRecentHit hit)
    {
        hit.ms = WorldTimer::getMSTime();
        std::lock_guard<std::mutex> guard(s_lock);
        std::deque<BonkRecentHit>& hits = s_stats[player->GetObjectGuid()].recentHits;
        hits.push_back(std::move(hit));
        TrimRecentHits(hits, hits.back().ms);
    }

    char const* KindTag(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_BOSS:     return "b";
            case BONK_SPAWN_MINIBOSS: return "m";
            case BONK_SPAWN_ELITE:    return "e";
            default:                  return "";
        }
    }

    struct RecapSource
    {
        BonkRecentHit const* sample = nullptr;
        uint64 damage = 0;
        uint32 index = 0;
    };

    std::string SourceKey(BonkRecentHit const& hit)
    {
        return hit.source + "~" + hit.spell;
    }

    void SendLines(BonkPlayer* bonk, std::string const& prefix, std::vector<std::string> const& items)
    {
        std::string line;
        for (std::string const& item : items)
        {
            if (!line.empty() && line.size() + item.size() + 1 > BONK_RECAP_LINE_CHARS)
            {
                bonk->SendAddon(prefix + line);
                line.clear();
            }
            line += (line.empty() ? "" : ",") + item;
        }
        if (!line.empty())
            bonk->SendAddon(prefix + line);
    }

    std::vector<RecapSource*> RankSources(std::map<std::string, RecapSource>& byKey, RecapSource* killer)
    {
        std::vector<RecapSource*> ranked;
        for (auto& itr : byKey)
            ranked.push_back(&itr.second);
        std::sort(ranked.begin(), ranked.end(), [](RecapSource const* a, RecapSource const* b) { return a->damage > b->damage; });

        auto const shownEnd = ranked.begin() + std::min<size_t>(ranked.size(), BONK_RECAP_MAX_SOURCES);
        if (std::find(ranked.begin(), shownEnd, killer) == shownEnd)
            std::iter_swap(shownEnd - 1, std::find(ranked.begin(), ranked.end(), killer));

        for (size_t i = 0; i < ranked.size(); ++i)
            ranked[i]->index = uint32(std::min<size_t>(i, BONK_RECAP_MAX_SOURCES) + 1);
        return ranked;
    }

    std::vector<std::string> RecapEvents(std::deque<BonkRecentHit> const& hits, std::map<std::string, RecapSource>& byKey, uint32 windowStart, uint32 maxHealth)
    {
        std::vector<std::string> events;
        uint32 const lastSlot = BONK_RECAP_WINDOW_MS / BONK_RECAP_SLOT_MS;
        uint32 slotKey = UINT32_MAX;
        uint32 slotAmount = 0;
        uint32 slotHealth = 0;
        auto flush = [&]()
        {
            if (slotKey != UINT32_MAX)
                events.push_back(std::to_string(slotKey >> 8) + ":" + std::to_string(slotKey & 0xFF) + ":" + std::to_string(slotAmount) + ":" +
                    std::to_string(uint64(slotHealth) * 1000 / maxHealth));
        };
        for (BonkRecentHit const& hit : hits)
        {
            uint32 const elapsed = WorldTimer::getMSTimeDiff(windowStart, hit.ms);
            uint32 const slot = elapsed > BONK_RECAP_WINDOW_MS ? 0 : std::min(lastSlot, elapsed / BONK_RECAP_SLOT_MS);
            uint32 const key = (slot << 8) | (hit.heal ? 0 : byKey[SourceKey(hit)].index);
            if (key != slotKey)
            {
                flush();
                slotKey = key;
                slotAmount = 0;
            }
            slotAmount += hit.amount;
            slotHealth = hit.healthAfter;
        }
        flush();
        if (events.size() > BONK_RECAP_MAX_EVENTS)
            events.erase(events.begin(), events.end() - BONK_RECAP_MAX_EVENTS);
        return events;
    }

    void SendDeathRecap(Player* player, uint32 overkill)
    {
        std::deque<BonkRecentHit> hits;
        {
            std::lock_guard<std::mutex> guard(s_lock);
            hits = s_stats[player->GetObjectGuid()].recentHits;
        }
        if (hits.empty() || hits.back().heal)
            return;

        uint32 const maxHealth = std::max<uint32>(1, player->GetMaxHealth());
        std::map<std::string, RecapSource> byKey;
        uint64 taken = 0;
        uint64 healed = 0;
        for (BonkRecentHit const& hit : hits)
        {
            if (hit.heal)
            {
                healed += hit.amount;
                continue;
            }
            taken += hit.amount;
            RecapSource& source = byKey[SourceKey(hit)];
            source.sample = &hit;
            source.damage += hit.amount;
        }

        BonkRecentHit const& last = hits.back();
        BonkRecentHit const& first = hits.front();
        RecapSource* killer = &byKey[SourceKey(last)];
        std::vector<RecapSource*> const ranked = RankSources(byKey, killer);
        uint32 const startHealth = std::min(maxHealth, first.heal ? first.healthAfter - std::min(first.healthAfter, first.amount) : first.healthAfter + first.amount);

        BonkPlayer* bonk = player->GetBonkPlayer();
        bonk->SendAddon("dr~" + std::to_string(maxHealth) + "~" + std::to_string(startHealth * 1000 / maxHealth) + "~" + std::to_string(killer->index) + "~" +
            std::to_string(last.amount) + "~" + std::to_string(overkill) + "~" + std::to_string(taken) + "~" + std::to_string(healed));

        uint64 othersDamage = 0;
        for (RecapSource const* source : ranked)
        {
            if (source->index > BONK_RECAP_MAX_SOURCES)
            {
                othersDamage += source->damage;
                continue;
            }
            BonkRecentHit const& sample = *source->sample;
            bonk->SendAddon("drs~" + std::to_string(source->index) + "~" + sample.icon + "~" + sample.source + "~" + sample.spell + "~" +
                KindTag(sample.kind) + "~" + std::to_string(source->damage));
        }
        if (othersDamage)
            bonk->SendAddon("drs~" + std::to_string(BONK_RECAP_MAX_SOURCES + 1) + "~" + BONK_UNKNOWN_ICON + "~Others~~~" + std::to_string(othersDamage));

        SendLines(bonk, "dre~", RecapEvents(hits, byKey, last.ms - BONK_RECAP_WINDOW_MS, maxHealth));
    }
}

std::string BonkStats::DescribeDamageTypes(ObjectGuid player, uint32 spellId)
{
    std::string text = SchoolType(spellId);
    std::lock_guard<std::mutex> guard(s_lock);
    auto stats = s_stats.find(player);
    if (stats == s_stats.end())
        return text;
    auto dots = stats->second.dotBySpell.find(spellId);
    if (dots == stats->second.dotBySpell.end())
        return text;
    for (auto const& dot : dots->second)
        if (dot.second.damage)
            text += "/" + DamageType(dot.first) + ":" + std::to_string(dot.second.damage);
    return text;
}

void BonkStats::Reset(ObjectGuid player)
{
    std::lock_guard<std::mutex> guard(s_lock);
    s_stats[player] = BonkRunStats();
    s_bossBars.erase(player);
}

uint32 BonkStats::CountKill(ObjectGuid player, ObjectGuid enemy, BonkSpawnKind kind)
{
    std::lock_guard<std::mutex> guard(s_lock);
    BonkRunStats& stats = s_stats[player];
    if (kind < MAX_BONK_SPAWN_KIND)
        ++stats.kindKills[kind];

    uint32 spellId = 0;
    auto itr = stats.killingSpells.find(enemy);
    if (itr != stats.killingSpells.end())
    {
        spellId = itr->second;
        stats.killingSpells.erase(itr);
    }
    ++stats.bySpell[spellId].kills;
    auto dot = stats.killingDots.find(enemy);
    if (dot != stats.killingDots.end())
    {
        ++stats.dotBySpell[spellId][dot->second].kills;
        stats.killingDots.erase(dot);
    }
    return spellId;
}

BonkRunStats::Spell BonkStats::GetSpellStats(ObjectGuid player, uint32 spellId)
{
    std::lock_guard<std::mutex> guard(s_lock);
    auto stats = s_stats.find(player);
    if (stats == s_stats.end())
        return BonkRunStats::Spell();
    auto spell = stats->second.bySpell.find(spellId);
    return spell != stats->second.bySpell.end() ? spell->second : BonkRunStats::Spell();
}

namespace
{
    uint32 const BONK_SHAPESHIFT_CREDITS[] = { 1082, 6807, 16689 };
}

std::vector<uint32> BonkStats::GetWeaponSpells(BonkWeapon const& weapon)
{
    std::vector<uint32> spellIds;
    for (uint32 spellId : { weapon.spellId, weapon.altSpellId })
    {
        SpellEntry const* spellInfo = spellId ? sSpellMgr.GetSpellEntry(spellId) : nullptr;
        if (!spellInfo)
            continue;
        spellIds.push_back(spellId);
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spellInfo->EffectTriggerSpell[i])
                spellIds.push_back(spellInfo->EffectTriggerSpell[i]);
    }
    if (weapon.behavior == BONK_BEHAVIOR_SHAPESHIFT)
        spellIds.insert(spellIds.end(), std::begin(BONK_SHAPESHIFT_CREDITS), std::end(BONK_SHAPESHIFT_CREDITS));
    return spellIds;
}

void BonkStats::SendSummary(Player* player, std::vector<BonkSummaryWeapon> const& weapons)
{
    BonkRunStats stats;
    {
        std::lock_guard<std::mutex> guard(s_lock);
        stats = s_stats[player->GetObjectGuid()];
    }

    BonkPlayer* bonk = player->GetBonkPlayer();
    bonk->SendAddon("sum~" + std::to_string(stats.damageTaken) + "~" + std::to_string(stats.kindKills[BONK_SPAWN_ELITE]) + "~" +
        std::to_string(stats.kindKills[BONK_SPAWN_MINIBOSS]) + "~" + std::to_string(stats.kindKills[BONK_SPAWN_BOSS]));

    std::set<uint32> sent;
    for (BonkSummaryWeapon const& weapon : weapons)
    {
        if (sent.count(weapon.spellId))
            continue;
        uint64 damage = 0;
        uint32 kills = 0;
        std::vector<uint32> const spellIds = weapon.spellIds.empty() ? std::vector<uint32>{ weapon.spellId } : weapon.spellIds;
        for (uint32 spellId : spellIds)
        {
            if (!sent.insert(spellId).second)
                continue;
            auto itr = stats.bySpell.find(spellId);
            if (itr == stats.bySpell.end())
                continue;
            damage += itr->second.damage;
            kills += itr->second.kills;
        }
        bonk->SendAddon("sumw~" + weapon.icon + "~" + weapon.name + "~" + std::to_string(weapon.level) + "~" + std::to_string(weapon.maxLevel) + "~" +
            (weapon.evolved ? "1" : "0") + "~" + std::to_string(damage) + "~" + std::to_string(kills) + "~" + SchoolType(weapon.spellId));

        std::map<uint32, BonkRunStats::Spell> dots;
        for (uint32 spellId : spellIds)
        {
            auto itr = stats.dotBySpell.find(spellId);
            if (itr == stats.dotBySpell.end())
                continue;
            for (auto const& dot : itr->second)
            {
                dots[dot.first].damage += dot.second.damage;
                dots[dot.first].kills += dot.second.kills;
            }
        }
        for (auto const& dot : dots)
        {
            BonkKeywordInfo const* keyword = sBonkMgr.GetKeywordBySpell(dot.first);
            SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(dot.first);
            std::string const icon = keyword && !keyword->icon.empty() ? keyword->icon : "INV_Misc_QuestionMark";
            std::string const name = keyword ? keyword->name : spellInfo ? spellInfo->SpellName[0] : "Spell " + std::to_string(dot.first);
            bonk->SendAddon("sums~" + icon + "~" + name + "~" + std::to_string(dot.second.damage) + "~" + std::to_string(dot.second.kills));
        }
    }

    for (auto const& itr : stats.bySpell)
    {
        if (sent.count(itr.first) || (!itr.second.damage && !itr.second.kills))
            continue;
        std::string icon = "INV_Misc_QuestionMark";
        std::string name = "Spell " + std::to_string(itr.first);
        if (!itr.first)
        {
            icon = "INV_Sword_04";
            name = "Melee";
        }
        else if (BonkKeywordInfo const* keyword = sBonkMgr.GetKeywordBySpell(itr.first))
        {
            icon = keyword->icon.empty() ? icon : keyword->icon;
            name = keyword->name;
        }
        else if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(itr.first))
            name = spellInfo->SpellName[0];
        bonk->SendAddon("sumw~" + icon + "~" + name + "~0~0~0~" + std::to_string(itr.second.damage) + "~" + std::to_string(itr.second.kills) + "~" +
            DamageType(itr.first));
    }

    bonk->SendAddon("sumend");
}

void BonkStats::UpdateBossBar(Player* player, uint32 diff)
{
    BossBar* bar;
    {
        std::lock_guard<std::mutex> guard(s_lock);
        bar = &s_bossBars[player->GetObjectGuid()];
    }
    bar->timer += diff;
    if (bar->timer < BONK_BOSS_BAR_MS)
        return;
    bar->timer = 0;

    Creature* shown = nullptr;
    if (player->IsInWorld() && !player->IsBeingTeleported())
    {
        std::list<Creature*> creatures;
        MaNGOS::AnyUnitInObjectRangeCheck check(player, BONK_BOSS_BAR_RANGE);
        MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(creatures, check);
        Cell::VisitGridObjects(player, searcher, BONK_BOSS_BAR_RANGE);

        float shownDistance = 0.0f;
        bool shownIsBoss = false;
        for (Creature* creature : creatures)
        {
            if (!creature->IsAlive() || !creature->IsTemporarySummon() ||
                static_cast<TemporarySummon*>(creature)->GetSummonerGuid() != player->GetObjectGuid())
                continue;
            BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(creature->AI());
            if (!ai || (ai->GetKind() != BONK_SPAWN_BOSS && ai->GetKind() != BONK_SPAWN_MINIBOSS))
                continue;

            bool const isBoss = ai->GetKind() == BONK_SPAWN_BOSS;
            float const distance = player->GetDistance(creature);
            if (!shown || (isBoss && !shownIsBoss) || (isBoss == shownIsBoss && distance < shownDistance))
            {
                shown = creature;
                shownDistance = distance;
                shownIsBoss = isBoss;
            }
        }
    }

    std::string line = "boss~";
    if (shown)
    {
        BonkMobAI const* ai = static_cast<BonkMobAI const*>(shown->AI());
        line += std::string(shown->GetName()) + "~" + std::to_string(ai->GetTrueHealth(shown->GetHealth())) + "~" + std::to_string(ai->GetTrueHealth(shown->GetMaxHealth())) + "~" +
            (ai->GetKind() == BONK_SPAWN_BOSS ? "boss" : "mini") + "~" + ai->GetAffixNames() + "~" + KeywordStacksText(player, shown) + "~" + BonkMobAI::GetUnitGuidText(shown);
    }
    if (line == bar->lastLine)
        return;
    bar->lastLine = line;
    player->GetBonkPlayer()->SendAddon(line);
}

void BonkOnDamage(Unit* attacker, Unit* victim, uint32 damage, SpellEntry const* spellProto)
{
    if (!damage || attacker == victim)
        return;

    LogPausedHit(attacker, victim, damage, spellProto);

    uint32 const health = victim->GetHealth();
    uint32 const dealt = std::min(damage, health);

    bool const swing = !spellProto;
    SpellEntry const* const hitSpell = spellProto;
    Player* owner = nullptr;
    if (attacker->IsCreature() && !victim->IsPlayer())
        if (Player* hero = attacker->GetCharmerOrOwnerPlayerOrPlayerItself())
            if (uint32 const createdBy = attacker->GetUInt32Value(UNIT_CREATED_BY_SPELL))
            {
                owner = hero;
                spellProto = sSpellMgr.GetSpellEntry(createdBy);
            }

    if (Player* player = owner ? owner : attacker->ToPlayer())
    {
        if (!victim->IsCreature() || !InRun(player))
            return;

        BonkRun* run = player->GetBonkPlayer()->GetRun();
        std::vector<std::pair<uint32, uint32>> credits;
        if (spellProto)
            run->CreditDamageOverTime(victim, spellProto->Id, dealt, credits);
        uint32 const dotSpellId = !credits.empty() ? spellProto->Id : 0;
        if (credits.empty())
            credits.emplace_back(spellProto ? spellProto->Id : 0, dealt);

        if (!owner)
        {
            run->OnTalentDamage(victim, spellProto);
            run->PlayMeleeHitFeel(victim, hitSpell);
        }
        if (damage >= health && !victim->GetInvincibilityHpThreshold())
        {
            run->OnKillingBlow(victim);
            run->PlayKillFeel(victim, damage, health);
            if (!owner && spellProto && !dotSpellId)
            {
                run->ResetOnKill(spellProto->Id);
                run->OnSpellKill(spellProto->Id);
            }
            if (owner && !dotSpellId)
                run->OnSummonKill(attacker, victim);
            if (dotSpellId)
                sBonkQuests.OnProgress(player, BONK_QUEST_DOT_KILLS, 0, 1);
        }
        else if (!dotSpellId)
        {
            run->OnWeaponHit(victim, spellProto);
            if (!owner)
                run->StaggerOnHit(victim);
        }
        if (owner && swing)
            run->OnPetBite(attacker->ToCreature(), victim);
        if (owner && attacker->GetEntry() != BONK_PROJECTILE && damage < health)
            run->ApplySummonHitKeyword(attacker, victim, hitSpell);
        if ((!owner || attacker->GetEntry() == BONK_PROJECTILE) && !dotSpellId)
        {
            double const trueDamage = double(BonkRun::GetTrueAmount(victim, damage));
            run->OnHeroHit(victim, trueDamage, spellProto);
        }
        if (!owner)
            run->CountThunderHit(spellProto);

        bool newBiggestHit = false;
        {
            std::lock_guard<std::mutex> guard(s_lock);
            BonkRunStats& stats = s_stats[player->GetObjectGuid()];
            for (auto const& credit : credits)
            {
                stats.bySpell[credit.first].damage += BonkRun::GetTrueAmount(victim, credit.second);
                if (dotSpellId && credit.first != dotSpellId)
                    stats.dotBySpell[credit.first][dotSpellId].damage += BonkRun::GetTrueAmount(victim, credit.second);
            }
            uint64 const trueHit = BonkRun::GetTrueAmount(victim, damage);
            if (trueHit > stats.biggestHit)
            {
                stats.biggestHit = trueHit;
                newBiggestHit = true;
            }
            if (damage >= health)
            {
                stats.killingSpells[victim->GetObjectGuid()] = credits.front().first;
                if (dotSpellId && credits.front().first != dotSpellId)
                    stats.killingDots[victim->GetObjectGuid()] = dotSpellId;
            }
        }
        if (newBiggestHit && run->IsSandbox())
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Bonk biggest hit: %s %llu from spell %u | %s", player->GetName(), (unsigned long long)BonkRun::GetTrueAmount(victim, damage), spellProto ? spellProto->Id : 0,
                run->DescribeBigHit(spellProto, victim).c_str());
    }
    else if (Player* player = victim->ToPlayer())
    {
        if (!InRun(player))
            return;

        {
            std::lock_guard<std::mutex> guard(s_lock);
            s_stats[player->GetObjectGuid()].damageTaken += dealt;
        }

        BonkRecentHit hit = DescribeHit(attacker, spellProto);
        hit.amount = dealt;
        hit.healthAfter = health - dealt;
        PushRecentHit(player, std::move(hit));
        if (damage >= health)
            SendDeathRecap(player, damage - health);
    }
}

void BonkStats::RecordHeroHeal(Player* player, uint32 amount)
{
    if (!amount || !InRun(player))
        return;
    BonkRecentHit hit;
    hit.heal = true;
    hit.amount = amount;
    hit.healthAfter = player->GetHealth();
    PushRecentHit(player, std::move(hit));
}
