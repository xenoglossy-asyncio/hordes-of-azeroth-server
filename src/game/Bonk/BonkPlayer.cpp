#include "BonkPlayer.h"
#include "BonkCoop.h"
#include "Bag.h"
#include "BonkEnchant.h"
#include "BonkFoundGear.h"
#include "BonkLocale.h"
#include "BonkMastery.h"
#include "BonkMgr.h"
#include "BonkQuests.h"
#include "BonkRun.h"
#include "BonkRunStats.h"
#include "Chat.h"
#include "Config/Config.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "MasterPlayer.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellAuras.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>

namespace
{
    char const* const BONK_ACCOUNT_TABLES[] = { "bonk_account", "bonk_account_mastery", "bonk_account_mastery_node", "bonk_account_quest",
        "bonk_account_starter", "bonk_account_talent", "bonk_account_tier", "bonk_account_upgrade", "bonk_account_floor",
        "bonk_account_found", "bonk_account_mastery_preset", "bonk_account_mastery_preset_active" };
    uint8 const BONK_MASTERY_PRESET_SLOTS = 3;
    char const* const BONK_PROGRESS_ONLY_TABLES[] = { "bonk_account_disabled", "bonk_account_unlock" };
    uint32 const BONK_IRONMAN_ACCOUNT_BIT = 0x80000000;

    std::mutex s_ironmanRequestsLock;
    std::set<uint32> s_ironmanRequests;

    float const BONK_CAMP_RADIUS = 300.0f;
    uint32 const BONK_CAMP_SIT_DELAY_MS = 500;
    uint32 const BONK_REVIVE_OUTSIDE_RUN_MS = 3000;
    uint32 const BONK_MAILBOX_REFRESH_DELAY_MS = 1500;
    size_t const BONK_PACKET_TOP = 4;
    float const BONK_CAMP_CHAIR_RANGE = 3.0f;
    float const BONK_CAMP_CHAIR_TAKEN = 1.0f;

    float const BONK_SHOP_RANGE = 15.0f;

    uint32 const BONK_GEAR_VENDOR = 900004;
    float const BONK_GEAR_VENDOR_RANGE = 10.0f;

    uint32 const BONK_MAILBOX_BEACON = 900129;
    uint32 const BONK_MAILBOX_CHECK_MS = 1000;
    float const BONK_MAILBOX_GLOW_RANGE = 100.0f;

    uint32 const BONK_ITEM_STARTER_BAG = 900225;
    uint32 const BONK_ITEM_RUN_HEARTHSTONE = 900230;
    uint32 const BONK_ITEM_OLD_ARROWS = 900226;
    uint32 const BONK_ITEM_OLD_BULLETS = 900227;

    std::string FormatStat(float value)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%g", value);
        return buffer;
    }

    std::string FormatStatList(float const* stats)
    {
        std::string list;
        for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
            if (stats[effect] != 0.0f)
                list += (list.empty() ? "" : ",") + std::to_string(effect) + ":" + FormatStat(stats[effect]);
        return list;
    }

    BonkQuest const* GetLockingQuest(Player* player, BonkQuestReward type, uint32 id)
    {
        if (!sBonkQuests.IsLocked(player->GetBonkPlayer()->GetProgressAccount(), type, id))
            return nullptr;
        return sBonkQuests.GetUnlockingQuest(type, id);
    }

    struct AccountBankItem
    {
        uint32 itemGuid;
        uint32 itemId;
        uint8 slot;
        uint32 holderGuid;
    };

    uint8 TakeBankSlot(std::set<uint8>& used, uint8 wanted, uint8 first, uint8 end)
    {
        if (wanted >= first && wanted < end && used.insert(wanted).second)
            return wanted;
        for (uint8 slot = first; slot < end; ++slot)
            if (used.insert(slot).second)
                return slot;
        return NULL_SLOT;
    }

    void MoveHeldBankItem(AccountBankItem const& item, uint8 slot, uint32 guidLow)
    {
        CharacterDatabase.DirectPExecute("UPDATE `character_inventory` SET `guid` = %u, `slot` = %u WHERE `item_guid` = %u", guidLow, uint32(slot), item.itemGuid);
        CharacterDatabase.DirectPExecute("UPDATE `character_inventory` SET `guid` = %u WHERE `bag` = %u", guidLow, item.itemGuid);
    }

    void MoveParkedBankItem(AccountBankItem const& item, uint8 slot, uint32 guidLow)
    {
        CharacterDatabase.DirectPExecute("INSERT INTO `character_inventory` (`guid`, `bag`, `slot`, `item_guid`, `item_id`) VALUES (%u, 0, %u, %u, %u)",
            guidLow, uint32(slot), item.itemGuid, item.itemId);
        CharacterDatabase.DirectPExecute("INSERT INTO `character_inventory` (`guid`, `bag`, `slot`, `item_guid`, `item_id`) "
            "SELECT %u, `bag`, `slot`, `item_guid`, `item_id` FROM `bonk_account_bank` WHERE `bag` = %u", guidLow, item.itemGuid);
        CharacterDatabase.DirectPExecute("DELETE FROM `bonk_account_bank` WHERE `item_guid` = %u OR `bag` = %u", item.itemGuid, item.itemGuid);
    }

    std::string RequirementText(std::string const& name, uint8 ranks, uint8 maxRank)
    {
        if (maxRank <= 1)
            return "Requires " + name + ".";
        return "Requires " + std::to_string(ranks) + (ranks == 1 ? " rank in " : " ranks in ") + name + ".";
    }
}

BonkPlayer::BonkPlayer(Player* player) : m_player(player)
{
}

BonkPlayer::~BonkPlayer() = default;

void BonkPlayer::OnLogin()
{
    uint32 const accountId = GetProgressAccount();
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `gold` FROM `bonk_account` WHERE `account` = %u", accountId));
    if (result)
        m_player->SetMoney(result->Fetch()[0].GetUInt32());
    else
        CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `gold`) VALUES (%u, %u)", accountId, m_player->GetMoney());
    m_goldLoaded = true;

    for (uint8 i = 0; i < PLAYER_EXPLORED_ZONES_SIZE; ++i)
        m_player->SetUInt32Value(PLAYER_EXPLORED_ZONES_1 + i, 0xFFFFFFFF);

    m_bestStages.clear();
    result = CharacterDatabase.PQuery("SELECT `tier`, `best_stage` FROM `bonk_account_tier` WHERE `account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            m_bestStages[fields[0].GetUInt32()] = fields[1].GetUInt32();
        } while (result->NextRow());
    }

    m_record = Record();
    result = CharacterDatabase.PQuery("SELECT `runs`, `best_stage`, `best_time_sec`, `kills`, `gold_earned` FROM `bonk_character_stats` WHERE `guid` = %u",
        m_player->GetGUIDLow());
    if (result)
    {
        Field* fields = result->Fetch();
        m_record.runs = fields[0].GetUInt32();
        m_record.bestStage = fields[1].GetUInt32();
        m_record.bestSeconds = fields[2].GetUInt32();
        m_record.kills = fields[3].GetUInt32();
        m_record.goldEarned = fields[4].GetUInt32();
    }

    LoadPurchases();
    LoadMastery();
    LoadStarter();
    sBonkQuests.OnLogin(m_player);
    ReconcileMastery();
    TakeTalentResetNotice();

    ResetCharacter();
    ClearAuras();
    m_campCheckPending = true;
    ClearInterruptedRunItems();
    for (auto const& itr : sBonkMgr.GetGear())
        if (m_player->HasItemCount(itr.first, 1, true))
            m_player->DestroyItemCount(itr.first, 255, true, false, true);
    if (m_player->HasItemCount(BONK_ITEM_RUN_HEARTHSTONE, 1, true))
        m_player->DestroyItemCount(BONK_ITEM_RUN_HEARTHSTONE, 255, true, false, true);
    for (uint32 ammo : { BONK_ITEM_OLD_ARROWS, BONK_ITEM_OLD_BULLETS })
        if (uint32 const count = m_player->GetItemCount(ammo, true))
            m_player->DestroyItemCount(ammo, count, true, false, true);
    if (m_player->GetUInt32Value(PLAYER_AMMO_ID))
        m_player->RemoveAmmo();

    GiveStarterBags();
    ResetReworkedTrinkets();
    TeachWeaponSkills();
    m_player->GetCamera().UpdateVisibilityForOwner();
    BonkCoop::JoinHostGroup(m_player);
}

void BonkPlayer::TeachWeaponSkills()
{
    uint32 const swords = 201, twoHandedSwords = 202, maces = 198, twoHandedMaces = 199, daggers = 1180, staves = 227;
    uint32 const bows = 264, guns = 266, crossbows = 5011, shield = 9116, dualWield = 674;
    std::vector<uint32> spells;
    switch (m_player->GetClass())
    {
        case CLASS_WARRIOR: spells = { swords, twoHandedSwords, shield, dualWield }; break;
        case CLASS_PALADIN: spells = { maces, swords, twoHandedMaces, twoHandedSwords, shield }; break;
        case CLASS_SHAMAN:  spells = { maces, twoHandedMaces, shield }; break;
        case CLASS_ROGUE:   spells = { daggers, swords, dualWield }; break;
        case CLASS_HUNTER:  spells = { bows, guns, crossbows }; break;
        case CLASS_DRUID:   spells = { staves, twoHandedMaces }; break;
        case CLASS_MAGE:
        case CLASS_WARLOCK: spells = { staves, swords, daggers }; break;
        case CLASS_PRIEST:  spells = { staves, maces }; break;
        default: break;
    }
    for (uint32 spell : spells)
        if (!m_player->HasSpell(spell))
            m_player->LearnSpell(spell, false);
}

void BonkPlayer::ClearInterruptedRunItems()
{
    uint32 const owner = m_player->GetGUIDLow();
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `item`, `slot` FROM `bonk_run_item` WHERE `owner` = %u", owner));
    if (!result)
        return;

    std::map<uint8, ObjectGuid> brought;
    do
    {
        Field* fields = result->Fetch();
        ObjectGuid const guid(HIGHGUID_ITEM, fields[0].GetUInt32());
        uint8 const slot = fields[1].GetUInt8();
        if (slot != BONK_RUN_ITEM_FIND_SLOT)
            brought[slot] = guid;
        else if (Item* item = m_player->GetItemByGuid(guid))
            m_player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    } while (result->NextRow());

    SetEquippingGear(true);
    for (auto const& itr : brought)
    {
        if (itr.first >= EQUIPMENT_SLOT_END || m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, itr.first))
            continue;
        Item* item = m_player->GetItemByGuid(itr.second);
        uint16 dest;
        if (item && !item->IsEquipped() && m_player->CanEquipItem(itr.first, dest, item, false) == EQUIP_ERR_OK)
            m_player->SwapItem(item->GetPos(), dest);
    }
    SetEquippingGear(false);

    CharacterDatabase.PExecute("DELETE FROM `bonk_run_item` WHERE `owner` = %u", owner);
    m_player->SaveInventoryAndGoldToDB();
}

void BonkPlayer::GiveStarterBags()
{
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        if (!m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            m_player->StoreNewItemInBestSlots(BONK_ITEM_STARTER_BAG, 1);

    uint8 const bankBagSlots = BANK_SLOT_BAG_END - BANK_SLOT_BAG_START;
    if (m_player->GetBankBagSlotCount() < bankBagSlots)
        m_player->SetBankBagSlotCount(bankBagSlots);

    for (uint8 slot = BANK_SLOT_BAG_START; slot < BANK_SLOT_BAG_END; ++slot)
        if (!m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            GiveBankBag(slot);
}

void BonkPlayer::GiveBankBag(uint8 slot)
{
    Item* bag = Item::CreateItem(BONK_ITEM_STARTER_BAG, 1, m_player->GetObjectGuid());
    if (!bag)
        return;

    ItemPosCountVec dest;
    uint8 bagSlot = 0;
    if (m_player->CanBankItem(INVENTORY_SLOT_BAG_0, slot, dest, bag, false, bagSlot) == EQUIP_ERR_OK)
        m_player->BankItem(dest, bag, true);
    else
        delete bag;
}

void BonkPlayer::LoadPurchases()
{
    uint32 const accountId = GetProgressAccount();

    m_upgradeRanks.clear();
    std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery("SELECT `upgrade`, `rank` FROM `bonk_account_upgrade` WHERE `account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            m_upgradeRanks[fields[0].GetUInt32()] = fields[1].GetUInt8();
        } while (result->NextRow());
    }

    m_shards = 0;
    m_determination = 0;
    m_scrap = 0;
    m_quartermasterUpgrade = false;
    result = CharacterDatabase.PQuery("SELECT `shards`, `determination`, `scrap`, `gear_notice` FROM `bonk_account` WHERE `account` = %u", accountId);
    if (result)
    {
        m_shards = result->Fetch()[0].GetUInt32();
        m_determination = result->Fetch()[1].GetUInt8();
        m_scrap = result->Fetch()[2].GetUInt32();
        m_gearNoticePending = result->Fetch()[3].GetUInt8() != 0;
    }
    result = CharacterDatabase.PQuery("SELECT 1 FROM `bonk_character_notice` WHERE `guid` = %u AND `old_legendaries` = 1", m_player->GetGUIDLow());
    m_legendaryPromptPending = result != nullptr;

    m_foundGear.clear();
    result = CharacterDatabase.PQuery("SELECT `item` FROM `bonk_account_found` WHERE `account` = %u", accountId);
    if (result)
    {
        do
            m_foundGear.insert(result->Fetch()[0].GetUInt32());
        while (result->NextRow());
    }

    m_talentRanks.clear();
    m_enabledTalents.clear();
    result = CharacterDatabase.PQuery("SELECT `talent`, `rank`, `enabled` FROM `bonk_account_talent` WHERE `account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const talentId = fields[0].GetUInt32();
            m_talentRanks[talentId] = fields[1].GetUInt8();
            if (fields[2].GetUInt8())
                m_enabledTalents.insert(talentId);
        } while (result->NextRow());
    }
}

void BonkPlayer::LoadMastery()
{
    uint32 const accountId = GetProgressAccount();
    uint8 const classId = m_player->GetClass();

    m_masteryXp = 0;
    std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery("SELECT `xp` FROM `bonk_account_mastery` WHERE `account` = %u AND `class` = %u",
        accountId, uint32(classId));
    if (result)
        m_masteryXp = result->Fetch()[0].GetUInt32();

    m_classFloor = 0;
    result = CharacterDatabase.PQuery("SELECT `best_floor` FROM `bonk_account_floor` WHERE `account` = %u AND `class` = %u",
        accountId, uint32(classId));
    if (result)
        m_classFloor = result->Fetch()[0].GetUInt32();

    m_accountHasRun = bool(CharacterDatabase.PQuery("SELECT 1 FROM `bonk_account_mastery` WHERE `account` = %u AND `xp` > 0 LIMIT 1", accountId));

    m_titleId = 0;
    result = CharacterDatabase.PQuery("SELECT `title` FROM `bonk_character_title` WHERE `guid` = %u", m_player->GetGUIDLow());
    if (result)
        m_titleId = result->Fetch()[0].GetUInt32();

    m_masteryRanks.clear();
    result = CharacterDatabase.PQuery("SELECT `node`, `rank` FROM `bonk_account_mastery_node` WHERE `account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BonkMasteryNode const* node = sBonkMastery.GetNode(fields[0].GetUInt32());
            if (node && node->classId == classId && fields[1].GetUInt8())
                m_masteryRanks[node->id] = std::min(fields[1].GetUInt8(), node->maxRank);
        } while (result->NextRow());
    }

    LoadMasteryPresets();
}

void BonkPlayer::ReconcileMastery()
{
    uint32 const earned = GetMasteryPoints();
    if (GetMasterySpent() <= earned)
        return;
    std::string ignored;
    if (ResetMastery(ignored))
        m_masteryNotice = "Mastery levels were recalculated, so your talents were reset. You have " + std::to_string(earned) +
            (earned == 1 ? " talent point" : " talent points") + " to spend again.";
}

void BonkPlayer::TakeTalentResetNotice()
{
    uint32 const accountId = GetProgressAccount();
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `talent_reset` FROM `bonk_account` WHERE `account` = %u", accountId));
    if (!result || !result->Fetch()[0].GetUInt8())
        return;
    CharacterDatabase.PExecute("UPDATE `bonk_account` SET `talent_reset` = 0 WHERE `account` = %u", accountId);
    m_masteryNotice = "Your talents were reset for the new trees. All points have been refunded.";
}

void BonkPlayer::FlushMasteryNotice()
{
    if (m_masteryNotice.empty())
        return;
    Notify(m_masteryNotice);
    m_masteryNotice.clear();
}

std::string BonkPlayer::GetClassName() const
{
    if (ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(m_player->GetClass()))
        if (char const* name = entry->name[sWorld.GetDefaultDbcLocale()])
            return name;
    return "Hero";
}

uint32 BonkPlayer::GetMasteryLevel() const
{
    return BonkMastery::GetLevel(m_masteryXp);
}

uint32 BonkPlayer::GetMasteryPoints() const
{
    return GetMasteryLevel() + sBonkQuests.GetMasteryPoints(GetProgressAccount(), m_player->GetClass()) + GetFloorTalentPoints();
}

std::string BonkPlayer::DescribeLoadout() const
{
    auto describeRanks = [](std::map<uint32, uint8> const& ranks)
    {
        std::string text;
        for (auto const& itr : ranks)
            if (itr.second)
                text += (text.empty() ? "" : ",") + std::to_string(itr.first) + ":" + std::to_string(itr.second);
        return text;
    };
    return "mastery level " + std::to_string(GetMasteryLevel()) + ", talent points " + std::to_string(GetMasterySpent()) + " of " +
        std::to_string(GetMasteryPoints()) + ", best floor " + std::to_string(m_classFloor) + ", determination " + std::to_string(GetDetermination()) +
        "; talents [" + describeRanks(m_masteryRanks) + "]; upgrades [" + describeRanks(m_upgradeRanks) + "]; fortunes [" +
        describeRanks(m_talentRanks) + "]; gear [" + DescribeGear() + "]; found [" + DescribeFoundGear() + "]; gold " +
        std::to_string(m_player->GetMoney()) + ", scrap " + std::to_string(m_scrap) + ", tickets " + std::to_string(m_shards);
}

std::string BonkPlayer::DescribeFoundGear() const
{
    std::string found;
    for (uint32 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item const* item = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        uint32 const tier = item ? BonkFound::GetItemTier(item) : 0;
        if (!tier)
            continue;
        BonkFoundGear gear;
        sBonkFound.Get(item->GetGUIDLow(), gear);
        found += (found.empty() ? "" : ",") + std::to_string(slot) + ":" + std::to_string(tier) + ":" + std::to_string(gear.lines.size());
    }
    return found;
}

std::string BonkPlayer::DescribeGear() const
{
    std::string gear;
    for (uint32 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item const* item = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            gear += (gear.empty() ? "" : ",") + std::to_string(slot) + ":" + std::to_string(item->GetEntry());
    return gear;
}

uint32 BonkPlayer::GetFloorTalentPoints() const
{
    return std::min(BONK_FLOOR_TALENT_POINTS_MAX, m_classFloor / BONK_FLOORS_PER_TALENT_POINT);
}

std::vector<uint32> BonkPlayer::GetCheckpoints(BonkTier const& tier) const
{
    std::vector<uint32> checkpoints;
    if (tier.key != "mythic" || m_classFloor < BONK_CHECKPOINT_GAP + BONK_CHECKPOINT_STEP)
        return checkpoints;
    uint32 const highest = (m_classFloor - BONK_CHECKPOINT_GAP) / BONK_CHECKPOINT_STEP * BONK_CHECKPOINT_STEP;
    for (uint32 floor = BONK_CHECKPOINT_STEP; floor <= highest; floor += BONK_CHECKPOINT_STEP)
        checkpoints.push_back(floor);
    return checkpoints;
}

void BonkPlayer::SendCheckpoints() const
{
    for (auto const& itr : sBonkMgr.GetTiers())
    {
        std::string list;
        for (uint32 floor : GetCheckpoints(itr.second))
            list += (list.empty() ? "" : ",") + std::to_string(floor);
        if (!list.empty())
            SendAddon("checkpoints~" + std::to_string(itr.first) + "~" + list);
    }
}

void BonkPlayer::RecordClassFloor(uint32 floor)
{
    if (floor <= m_classFloor)
        return;
    uint32 const pointsBefore = GetFloorTalentPoints();
    m_classFloor = floor;
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_floor` (`account`, `class`, `best_floor`) VALUES (%u, %u, %u)",
        GetProgressAccount(), uint32(m_player->GetClass()), floor);
    if (GetFloorTalentPoints() > pointsBefore)
    {
        Notify("You earned a talent point for clearing Mythic floor " + std::to_string(floor) + "!");
        SendMasteryState();
    }
}

uint32 BonkPlayer::GetMasterySpent() const
{
    uint32 spent = 0;
    for (auto const& itr : m_masteryRanks)
        spent += itr.second;
    return spent;
}

uint32 BonkPlayer::GetMasterySpentInTree(uint32 treeId) const
{
    uint32 spent = 0;
    for (auto const& itr : m_masteryRanks)
        if (BonkMasteryNode const* node = sBonkMastery.GetNode(itr.first))
            if (node->tree == treeId)
                spent += itr.second;
    return spent;
}

uint8 BonkPlayer::GetMasteryRank(uint32 nodeId) const
{
    if (BonkMasteryNode const* node = sBonkMastery.GetNode(nodeId))
        if (sBonkMastery.IsFreeNode(*node))
            return node->maxRank;
    auto itr = m_masteryRanks.find(nodeId);
    return itr != m_masteryRanks.end() ? itr->second : 0;
}

BonkMasteryBonuses BonkPlayer::GetMasteryBonuses() const
{
    std::map<uint32, uint8> ranks = m_masteryRanks;
    for (auto const& itr : sBonkMastery.GetNodes())
        if (itr.second.classId == m_player->GetClass() && sBonkMastery.IsFreeNode(itr.second))
            ranks[itr.first] = itr.second.maxRank;
    return sBonkMastery.Evaluate(ranks);
}

bool BonkPlayer::HasLearnedParent(BonkMasteryNode const& node, uint32 ignoredId) const
{
    if (node.parents.empty())
        return true;
    for (uint32 parentId : node.parents)
        if (parentId != ignoredId && GetMasteryRank(parentId))
            return true;
    return false;
}

uint32 BonkPlayer::GetMasterySpentAbove(uint32 treeId, uint8 row) const
{
    uint32 spent = 0;
    for (auto const& itr : m_masteryRanks)
        if (BonkMasteryNode const* placed = sBonkMastery.GetNode(itr.first))
            if (placed->tree == treeId && placed->row < row)
                spent += itr.second;
    return spent;
}

void BonkPlayer::SendMasteryState() const
{
    uint32 const level = GetMasteryLevel();
    uint32 const earned = GetMasteryPoints();
    uint32 const spent = GetMasterySpent();
    SendAddon("mastery~" + GetClassName() + "~" + std::to_string(level) + "~" + std::to_string(BonkMastery::GetXpIntoLevel(m_masteryXp)) + "~" +
        std::to_string(level < BONK_MASTERY_MAX_LEVEL ? BonkMastery::GetXpForLevel(level + 1) : 0) + "~" +
        std::to_string(BONK_MASTERY_MAX_LEVEL) + "~" + std::to_string(earned > spent ? earned - spent : 0) + "~" +
        (sBonkMastery.HasTrees(m_player->GetClass()) ? "1" : "0"));
}

bool BonkPlayer::CanSeeTownObject(uint32 entry, bool gameObject) const
{
    if (!gameObject && entry == BONK_TIER_VENDOR)
        return IsTierCleared("heroic");
    if (gameObject && entry == BONK_DECK_OBJECT)
        return m_accountHasRun;
    return true;
}

BonkTitle const* BonkPlayer::GetActiveTitle() const
{
    if (!m_titleId || !sBonkQuests.HasTitle(GetProgressAccount(), m_titleId))
        return nullptr;
    return sBonkQuests.GetTitle(m_titleId);
}

void BonkPlayer::SendTitles() const
{
    BonkTitle const* active = GetActiveTitle();
    std::string line = "titles~" + std::to_string(active ? active->id : 0);
    for (BonkTitle const* title : sBonkQuests.GetEarnedTitles(GetProgressAccount()))
        line += "~" + std::to_string(title->id) + "~" + title->pattern + "~" + std::to_string(title->stat) + "~" + std::to_string(title->value);
    SendAddon(line);
}

bool BonkPlayer::SetTitle(uint32 titleId, std::string& error)
{
    if (titleId && !sBonkQuests.HasTitle(GetProgressAccount(), titleId))
    {
        error = "You haven't earned that title.";
        return false;
    }
    if (GetRun())
    {
        error = "You can't change your title during a run.";
        return false;
    }

    m_titleId = titleId;
    if (titleId)
        CharacterDatabase.PExecute("REPLACE INTO `bonk_character_title` (`guid`, `title`) VALUES (%u, %u)", m_player->GetGUIDLow(), titleId);
    else
        CharacterDatabase.PExecute("DELETE FROM `bonk_character_title` WHERE `guid` = %u", m_player->GetGUIDLow());
    SendTitles();
    return true;
}

void BonkPlayer::AddMasteryXp(uint32 xp)
{
    if (!xp)
        return;

    uint32 const before = GetMasteryLevel();
    m_masteryXp += xp;
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_mastery` (`account`, `class`, `xp`) VALUES (%u, %u, %u)",
        GetProgressAccount(), uint32(m_player->GetClass()), m_masteryXp);

    uint32 const after = GetMasteryLevel();
    if (after > before)
        sBonkQuests.OnProgress(m_player, BONK_QUEST_MASTERY_LEVEL, m_player->GetClass(), after, true);
    std::string const name = GetClassName() + " Mastery";
    if (after > before)
        Notify("Your " + name + " reached level " + std::to_string(after) + "! You have " + std::to_string(after - before) +
            (after - before == 1 ? " new talent point" : " new talent points") + " to spend at the Talent Trainer.");
    else if (after < BONK_MASTERY_MAX_LEVEL)
        Notify("You gained " + std::to_string(xp) + " " + name + " experience (" + std::to_string(BonkMastery::GetXpIntoLevel(m_masteryXp)) + "/" +
            std::to_string(BonkMastery::GetXpForLevel(after + 1)) + ").");
    SendMasteryState();
}

bool BonkPlayer::LearnMastery(uint32 nodeId, std::string& message)
{
    BonkMasteryNode const* node = sBonkMastery.GetNode(nodeId);
    if (!node || node->classId != m_player->GetClass())
        return false;

    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    uint8 const rank = GetMasteryRank(nodeId);
    if (rank >= node->maxRank)
    {
        message = node->name + " is already at its highest rank.";
        return false;
    }

    if (sBonkMastery.IsFreeNode(*node))
    {
        message = node->name + " is always yours.";
        return false;
    }

    uint32 const treePoints = sBonkMastery.GetRowGate(*node);
    if (GetMasterySpentInTree(node->tree) < treePoints)
    {
        message = "Requires " + std::to_string(treePoints) + " points in this tree.";
        return false;
    }

    if (!HasLearnedParent(*node, 0))
    {
        message = "Learn a talent that leads to " + node->name + " first.";
        return false;
    }

    BonkMasteryNode const* required = sBonkMastery.GetNode(node->requiredId);
    if (required && GetMasteryRank(required->id) < node->requiredRank)
    {
        message = RequirementText(required->name, node->requiredRank, required->maxRank);
        return false;
    }

    if (BonkMasteryNode const* rival = FindLearnedRival(*node))
    {
        message = node->effect == BONK_MASTERY_CAPSTONE && rival->effect == BONK_MASTERY_CAPSTONE ?
            "You can only have one capstone. Unlearn " + rival->name + " first." :
            node->name + " can't be learned with " + rival->name + ". Unlearn it first.";
        return false;
    }

    if (GetMasterySpent() >= GetMasteryPoints())
    {
        message = "You have no talent points left. Level up your Mastery to earn more.";
        return false;
    }

    m_masteryRanks[nodeId] = rank + 1;
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_mastery_node` (`account`, `node`, `rank`) VALUES (%u, %u, %u)",
        GetProgressAccount(), nodeId, uint32(rank + 1));
    message = "You learned " + node->name + (node->maxRank > 1 ? " (rank " + std::to_string(rank + 1) + ")." : ".");
    SendMasteryState();
    return true;
}

BonkMasteryNode const* BonkPlayer::FindLearnedRival(BonkMasteryNode const& node) const
{
    for (auto const& itr : m_masteryRanks)
    {
        BonkMasteryNode const* learned = sBonkMastery.GetNode(itr.first);
        if (!learned || !itr.second || learned->id == node.id)
            continue;
        if (learned->id == node.excludes || learned->excludes == node.id)
            return learned;
        if (learned->effect == BONK_MASTERY_CAPSTONE && node.effect == BONK_MASTERY_CAPSTONE)
            return learned;
    }
    return nullptr;
}

bool BonkPlayer::ResetMastery(std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }
    if (m_masteryRanks.empty())
    {
        message = "You have not spent any talent points.";
        return false;
    }

    std::string nodes;
    for (auto const& itr : m_masteryRanks)
        nodes += (nodes.empty() ? "" : ",") + std::to_string(itr.first);
    CharacterDatabase.PExecute("DELETE FROM `bonk_account_mastery_node` WHERE `account` = %u AND `node` IN (%s)",
        GetProgressAccount(), nodes.c_str());

    uint32 const refunded = GetMasterySpent();
    m_masteryRanks.clear();
    message = "You got back " + std::to_string(refunded) + (refunded == 1 ? " talent point." : " talent points.");
    SendMasteryState();
    return true;
}

void BonkPlayer::LoadMasteryPresets()
{
    uint32 const accountId = GetProgressAccount();
    uint8 const classId = m_player->GetClass();

    m_masteryPresets.clear();
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT `slot`, `node`, `rank` FROM `bonk_account_mastery_preset` WHERE `account` = %u AND `class` = %u", accountId, uint32(classId)));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint8 const slot = fields[0].GetUInt8();
            BonkMasteryNode const* node = sBonkMastery.GetNode(fields[1].GetUInt32());
            if (slot && slot <= BONK_MASTERY_PRESET_SLOTS && node && node->classId == classId && fields[2].GetUInt8())
                m_masteryPresets[slot][node->id] = std::min(fields[2].GetUInt8(), node->maxRank);
        } while (result->NextRow());
    }

    m_activeMasteryPreset = 1;
    result = CharacterDatabase.PQuery("SELECT `slot` FROM `bonk_account_mastery_preset_active` WHERE `account` = %u AND `class` = %u",
        accountId, uint32(classId));
    if (result)
    {
        uint8 const slot = result->Fetch()[0].GetUInt8();
        if (slot && slot <= BONK_MASTERY_PRESET_SLOTS)
            m_activeMasteryPreset = slot;
    }
}

std::map<uint32, uint8> BonkPlayer::ReadMasteryPreset(uint8 slot) const
{
    auto const itr = m_masteryPresets.find(slot);
    return itr != m_masteryPresets.end() ? itr->second : std::map<uint32, uint8>();
}

void BonkPlayer::WriteMasteryPreset(uint8 slot, std::map<uint32, uint8> const& ranks)
{
    m_masteryPresets[slot] = ranks;
    uint32 const accountId = GetProgressAccount();
    uint32 const classId = m_player->GetClass();
    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM `bonk_account_mastery_preset` WHERE `account` = %u AND `class` = %u AND `slot` = %u",
        accountId, classId, uint32(slot));
    for (auto const& itr : ranks)
        CharacterDatabase.PExecute("INSERT INTO `bonk_account_mastery_preset` (`account`, `class`, `slot`, `node`, `rank`) VALUES (%u, %u, %u, %u, %u)",
            accountId, classId, uint32(slot), itr.first, uint32(itr.second));
    CharacterDatabase.CommitTransaction();
}

bool BonkPlayer::LoadMasteryPreset(uint8 slot, std::string& message)
{
    if (!slot || slot > BONK_MASTERY_PRESET_SLOTS)
        return false;
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    uint8 const active = m_activeMasteryPreset;
    if (slot == active)
    {
        message = "Preset " + std::to_string(slot) + " is already active.";
        return false;
    }

    std::map<uint32, uint8> const preset = ReadMasteryPreset(slot);
    WriteMasteryPreset(active, m_masteryRanks);
    std::string ignored;
    ResetMastery(ignored);
    uint32 wanted = 0;
    for (auto const& itr : preset)
        wanted += itr.second;
    for (bool placed = true; placed;)
    {
        placed = false;
        for (auto const& itr : preset)
            while (GetMasteryRank(itr.first) < itr.second && LearnMastery(itr.first, ignored))
                placed = true;
    }
    m_activeMasteryPreset = slot;
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_mastery_preset_active` (`account`, `class`, `slot`) VALUES (%u, %u, %u)",
        GetProgressAccount(), uint32(m_player->GetClass()), uint32(slot));

    uint32 const missing = wanted - std::min(wanted, GetMasterySpent());
    message = "Preset " + std::to_string(slot) + " is now active." + (preset.empty() ? " Spend your talent points to build it." : "");
    if (missing)
        message += " " + std::to_string(missing) + (missing == 1 ? " talent point" : " talent points") + " could not be placed.";
    return true;
}

void BonkPlayer::SendMasteryPresetRanks() const
{
    size_t const nodesPerLine = 16;
    for (auto const& preset : m_masteryPresets)
    {
        if (preset.first == m_activeMasteryPreset || preset.second.empty())
            continue;
        std::string ranks;
        size_t count = 0;
        for (auto const& itr : preset.second)
        {
            ranks += (ranks.empty() ? "" : ",") + std::to_string(itr.first) + ":" + std::to_string(itr.second);
            if (++count % nodesPerLine == 0 || count == preset.second.size())
            {
                SendAddon("presetranks~" + std::to_string(preset.first) + "~" + ranks);
                ranks.clear();
            }
        }
    }
}

std::string BonkPlayer::DescribeMasteryPresets() const
{
    uint8 const active = m_activeMasteryPreset;
    std::string line = "presets~" + std::to_string(active);
    for (uint8 slot = 1; slot <= BONK_MASTERY_PRESET_SLOTS; ++slot)
    {
        uint32 spent = 0;
        if (slot == active)
            spent = GetMasterySpent();
        else
            for (auto const& itr : ReadMasteryPreset(slot))
                spent += itr.second;
        line += "~" + std::to_string(spent);
    }
    return line;
}

bool BonkPlayer::RefundMastery(uint32 nodeId, std::string& message)
{
    BonkMasteryNode const* node = sBonkMastery.GetNode(nodeId);
    if (!node || node->classId != m_player->GetClass())
        return false;

    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    uint8 const rank = GetMasteryRank(nodeId);
    if (!rank)
    {
        message = "You have not learned " + node->name + ".";
        return false;
    }

    for (auto const& itr : m_masteryRanks)
    {
        BonkMasteryNode const* other = sBonkMastery.GetNode(itr.first);
        if (!other || !itr.second || other->id == nodeId)
            continue;

        if (other->requiredId == nodeId && rank - 1 < other->requiredRank)
        {
            message = "Refund " + other->name + " first, because it requires this one.";
            return false;
        }

        if (rank == 1 && std::find(other->parents.begin(), other->parents.end(), nodeId) != other->parents.end() && !HasLearnedParent(*other, nodeId))
        {
            message = "Refund " + other->name + " first, because it leads from this one.";
            return false;
        }

        if (other->tree == node->tree && other->row > node->row)
        {
            if (GetMasterySpentAbove(other->tree, other->row) - 1 < sBonkMastery.GetRowGate(*other))
            {
                message = "Refund " + other->name + " first, because it needs these points spent in this tree.";
                return false;
            }
        }
    }

    if (rank > 1)
    {
        m_masteryRanks[nodeId] = rank - 1;
        CharacterDatabase.PExecute("REPLACE INTO `bonk_account_mastery_node` (`account`, `node`, `rank`) VALUES (%u, %u, %u)",
            GetProgressAccount(), nodeId, uint32(rank - 1));
    }
    else
    {
        m_masteryRanks.erase(nodeId);
        CharacterDatabase.PExecute("DELETE FROM `bonk_account_mastery_node` WHERE `account` = %u AND `node` = %u",
            GetProgressAccount(), nodeId);
    }
    message = "You refunded " + (node->maxRank > 1 ? "rank " + std::to_string(rank) + " of " : std::string()) + node->name + " and got 1 talent point back.";
    SendMasteryState();
    return true;
}

void BonkPlayer::OnLogout()
{
    m_previewTelegraphs.Dismiss(m_player);
    if (m_run)
    {
        m_run->Abort();
        m_run.reset();
    }
    ClearAuras();
    m_addonReady = false;
    sBonkQuests.Save(GetProgressAccount());
    BonkCoop::OnLogout(m_player);

    WriteHeroScreenStats();
}

void BonkPlayer::RecordRun(uint32 stage, uint32 seconds, uint32 kills, uint32 goldEarned)
{
    if (IsInSandbox())
        return;
    ++m_record.runs;
    m_accountHasRun = true;
    m_record.bestStage = std::max(m_record.bestStage, stage);
    m_record.bestSeconds = std::max(m_record.bestSeconds, seconds);
    m_record.kills += kills;
    m_record.goldEarned += goldEarned;

    CharacterDatabase.PExecute("REPLACE INTO `bonk_character_stats` (`guid`, `runs`, `best_stage`, `best_time_sec`, `kills`, `gold_earned`) "
        "VALUES (%u, %u, %u, %u, %u, %u)", m_player->GetGUIDLow(), m_record.runs, m_record.bestStage, m_record.bestSeconds,
        m_record.kills, m_record.goldEarned);
}

void BonkPlayer::WriteHeroScreenStats() const
{
    std::string const path = sConfig.GetStringDefault("Bonk.GlueStatsFile", "");
    if (path.empty() || !m_goldLoaded)
        return;

    struct Hero
    {
        std::string name;
        uint8 classId = 0;
        uint32 played = 0;
        Record record;
        uint32 items = 0;
        uint32 itemLevels = 0;
        uint32 gearValue = 0;
        bool ironman = false;
        uint32 gold = 0;
        uint32 ironmanMasteryXp = 0;
    };
    std::map<uint32, Hero> heroes;

    auto addItem = [](Hero& hero, uint32 slot, uint32 entry)
    {
        if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
            return;
        if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry))
        {
            ++hero.items;
            hero.itemLevels += proto->ItemLevel;
            hero.gearValue += proto->BuyPrice;
        }
    };

    uint32 const accountId = m_player->GetSession()->GetAccountId();
    uint32 const ownGuid = m_player->GetGUIDLow();

    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `guid`, `name`, `class`, `played_time_total` FROM `characters` WHERE `account` = %u", accountId));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            Hero& hero = heroes[fields[0].GetUInt32()];
            hero.name = fields[1].GetCppString();
            hero.classId = fields[2].GetUInt8();
            hero.played = fields[3].GetUInt32();
        } while (result->NextRow());
    }

    result = CharacterDatabase.PQuery("SELECT i.`guid`, a.`gold`, m.`xp` FROM `bonk_ironman` i JOIN `characters` c ON c.`guid` = i.`guid` "
        "LEFT JOIN `bonk_account` a ON a.`account` = (%u | i.`guid`) "
        "LEFT JOIN `bonk_account_mastery` m ON m.`account` = (%u | i.`guid`) AND m.`class` = c.`class` "
        "WHERE c.`account` = %u", GetIronmanAccount(0), GetIronmanAccount(0), accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            auto hero = heroes.find(fields[0].GetUInt32());
            if (hero == heroes.end())
                continue;
            hero->second.ironman = true;
            hero->second.gold = fields[1].GetUInt32();
            hero->second.ironmanMasteryXp = fields[2].GetUInt32();
        } while (result->NextRow());
    }

    std::map<uint8, uint32> masteryXp;
    result = CharacterDatabase.PQuery("SELECT `class`, `xp` FROM `bonk_account_mastery` WHERE `account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            masteryXp[fields[0].GetUInt8()] = fields[1].GetUInt32();
        } while (result->NextRow());
    }
    if (!IsIronman())
        masteryXp[m_player->GetClass()] = m_masteryXp;

    uint32 sharedGold = m_player->GetMoney();
    if (IsIronman())
    {
        result = CharacterDatabase.PQuery("SELECT `gold` FROM `bonk_account` WHERE `account` = %u", accountId);
        sharedGold = result ? result->Fetch()[0].GetUInt32() : 0;
    }

    result = CharacterDatabase.PQuery("SELECT s.`guid`, s.`runs`, s.`best_stage`, s.`best_time_sec`, s.`kills`, s.`gold_earned` "
        "FROM `bonk_character_stats` s JOIN `characters` c ON c.`guid` = s.`guid` WHERE c.`account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            auto hero = heroes.find(fields[0].GetUInt32());
            if (hero == heroes.end() || hero->first == ownGuid)
                continue;
            hero->second.record.runs = fields[1].GetUInt32();
            hero->second.record.bestStage = fields[2].GetUInt32();
            hero->second.record.bestSeconds = fields[3].GetUInt32();
            hero->second.record.kills = fields[4].GetUInt32();
            hero->second.record.goldEarned = fields[5].GetUInt32();
        } while (result->NextRow());
    }

    result = CharacterDatabase.PQuery("SELECT i.`guid`, i.`slot`, i.`item_id` FROM `character_inventory` i JOIN `characters` c ON c.`guid` = i.`guid` "
        "WHERE c.`account` = %u AND i.`bag` = 0 AND i.`slot` < %u", accountId, uint32(EQUIPMENT_SLOT_END));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            auto hero = heroes.find(fields[0].GetUInt32());
            if (hero != heroes.end() && hero->first != ownGuid)
                addItem(hero->second, fields[1].GetUInt32(), fields[2].GetUInt32());
        } while (result->NextRow());
    }

    Hero& self = heroes[ownGuid];
    self.name = m_player->GetName();
    self.record = m_record;
    self.played = m_player->GetTotalPlayedTime();
    self.ironman = IsIronman();
    self.gold = m_player->GetMoney();
    self.ironmanMasteryXp = m_masteryXp;
    for (uint32 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item const* item = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            addItem(self, slot, item->GetEntry());

    std::ostringstream lua;
    lua << "{\n";
    lua << "\tgold = " << sharedGold << ",\n";
    lua << "\tlastHero = \"" << m_player->GetName() << "\",\n";
    lua << "\theroes = {\n";
    for (auto const& itr : heroes)
    {
        Hero const& hero = itr.second;
        if (hero.name.empty() || hero.name.find_first_of("\\\"\n") != std::string::npos)
            continue;
        lua << "\t\t[\"" << hero.name << "\"] = { runs = " << hero.record.runs
            << ", bestStage = " << hero.record.bestStage
            << ", bestTime = " << hero.record.bestSeconds
            << ", kills = " << hero.record.kills
            << ", goldEarned = " << hero.record.goldEarned
            << ", items = " << hero.items
            << ", itemLevel = " << (hero.items ? hero.itemLevels / hero.items : 0)
            << ", gearValue = " << hero.gearValue
            << ", played = " << hero.played
            << ", mastery = " << BonkMastery::GetLevel(hero.ironman ? hero.ironmanMasteryXp : masteryXp[hero.classId]);
        if (hero.ironman)
            lua << ", ironman = true, gold = " << hero.gold;
        lua << " },\n";
    }
    lua << "\t},\n}";
    WriteHeroScreenFile(m_player->GetSession()->GetUsername(), lua.str());
}

namespace
{
    char const* const BONK_STATS_HEADER = "BONK_STATS_ACCOUNTS = BONK_STATS_ACCOUNTS or {};\n";
    char const* const BONK_STATS_BLOCK = "BONK_STATS_ACCOUNTS[\"";

    std::string StatsBlockStart(std::string const& username)
    {
        return BONK_STATS_BLOCK + username + "\"]";
    }

    std::string OtherAccountsStats(std::string const& path, std::string const& username)
    {
        std::ifstream file(path);
        std::string const own = StatsBlockStart(username);
        std::string kept, line;
        bool keeping = false;
        while (std::getline(file, line))
        {
            if (line.compare(0, strlen(BONK_STATS_BLOCK), BONK_STATS_BLOCK) == 0)
                keeping = line.compare(0, own.size(), own) != 0;
            if (keeping)
                kept += line + "\n";
        }
        return kept;
    }
}

void BonkPlayer::WriteHeroScreenFile(std::string const& username, std::string const& stats)
{
    std::string const path = sConfig.GetStringDefault("Bonk.GlueStatsFile", "");
    if (path.empty() || username.find_first_of("\\\"\n") != std::string::npos)
        return;

    std::string const others = OtherAccountsStats(path, username);
    std::string const temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::out | std::ios::trunc);
        if (!file)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Bonk: cannot write hero stats to %s", temporary.c_str());
            return;
        }
        file << BONK_STATS_HEADER << others << StatsBlockStart(username) << " = " << stats << ";\n";
    }
    std::remove(path.c_str());
    if (std::rename(temporary.c_str(), path.c_str()) != 0)
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Bonk: cannot replace hero stats file %s", path.c_str());
}

bool BonkPlayer::IsWipeName(std::string const& name)
{
    return name == "Wipeaccount";
}

bool BonkPlayer::IsPlayableClass(uint8 classId)
{
    return classId == CLASS_WARRIOR || classId == CLASS_HUNTER || classId == CLASS_MAGE || classId == CLASS_SHAMAN || classId == CLASS_PRIEST || classId == CLASS_ROGUE || classId == CLASS_PALADIN ||
        classId == CLASS_WARLOCK || classId == CLASS_DRUID;
}

void BonkPlayer::WipeAccount(uint32 accountId, std::string const& username)
{
    for (char const* table : BONK_ACCOUNT_TABLES)
        CharacterDatabase.PExecute("DELETE FROM `%s` WHERE `account` = %u", table, accountId);
    CharacterDatabase.PExecute("DELETE i FROM `item_instance` i JOIN `bonk_account_bank` b ON b.`item_guid` = i.`guid` WHERE b.`account` = %u", accountId);
    CharacterDatabase.PExecute("DELETE FROM `bonk_account_bank` WHERE `account` = %u", accountId);

    WriteHeroScreenFile(username, "{\n\tgold = 0,\n\tlastHero = \"\",\n\theroes = {\n\t},\n}");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Bonk: account %u wiped from the hero screen", accountId);
}

bool BonkPlayer::IsIronmanName(std::string const& name)
{
    return name == "Ironmanmode";
}

void BonkPlayer::RequestIronman(uint32 accountId)
{
    std::lock_guard<std::mutex> guard(s_ironmanRequestsLock);
    s_ironmanRequests.insert(accountId);
}

bool BonkPlayer::TakeIronmanRequest(uint32 accountId)
{
    std::lock_guard<std::mutex> guard(s_ironmanRequestsLock);
    return s_ironmanRequests.erase(accountId) > 0;
}

void BonkPlayer::MarkIronman(uint32 guidLow)
{
    CharacterDatabase.DirectPExecute("INSERT IGNORE INTO `bonk_ironman` (`guid`) VALUES (%u)", guidLow);
}

void BonkPlayer::DeleteIronmanProgress(uint32 guidLow)
{
    for (char const* table : BONK_ACCOUNT_TABLES)
        CharacterDatabase.PExecute("DELETE FROM `%s` WHERE `account` = %u", table, GetIronmanAccount(guidLow));
    for (char const* table : BONK_PROGRESS_ONLY_TABLES)
        CharacterDatabase.PExecute("DELETE FROM `%s` WHERE `account` = %u", table, GetIronmanAccount(guidLow));
    CharacterDatabase.PExecute("DELETE FROM `bonk_ironman` WHERE `guid` = %u", guidLow);
}

uint32 BonkPlayer::GetIronmanAccount(uint32 guidLow)
{
    return BONK_IRONMAN_ACCOUNT_BIT | guidLow;
}

void BonkPlayer::GatherAccountBank(uint32 accountId, uint32 guidLow)
{
    if (!CharacterDatabase.PQuery("SELECT 1 FROM `characters` c WHERE c.`guid` = %u AND c.`account` = %u "
        "AND NOT EXISTS (SELECT 1 FROM `bonk_ironman` m WHERE m.`guid` = c.`guid`)", guidLow, accountId))
        return;

    std::set<uint8> used;
    std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery("SELECT `slot` FROM `character_inventory` WHERE `guid` = %u AND `bag` = 0 AND `slot` >= %u AND `slot` < %u",
        guidLow, uint32(BANK_SLOT_ITEM_START), uint32(BANK_SLOT_BAG_END));
    if (result)
    {
        do
        {
            used.insert(result->Fetch()[0].GetUInt8());
        } while (result->NextRow());
    }

    std::vector<AccountBankItem> items;
    result = CharacterDatabase.PQuery("SELECT i.`item_guid`, i.`item_id`, i.`slot`, i.`guid` FROM `character_inventory` i JOIN `characters` c ON c.`guid` = i.`guid` "
        "WHERE c.`account` = %u AND i.`guid` <> %u AND i.`bag` = 0 AND i.`slot` >= %u AND i.`slot` < %u "
        "AND NOT EXISTS (SELECT 1 FROM `bonk_ironman` m WHERE m.`guid` = i.`guid`) "
        "UNION ALL SELECT `item_guid`, `item_id`, `slot`, 0 FROM `bonk_account_bank` WHERE `account` = %u AND `bag` = 0",
        accountId, guidLow, uint32(BANK_SLOT_ITEM_START), uint32(BANK_SLOT_BAG_END), accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            items.push_back({ fields[0].GetUInt32(), fields[1].GetUInt32(), fields[2].GetUInt8(), fields[3].GetUInt32() });
        } while (result->NextRow());
    }

    bool movedBag = false;
    for (AccountBankItem const& item : items)
    {
        if (item.holderGuid && sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, item.holderGuid)))
            continue;

        bool const isBag = item.slot >= BANK_SLOT_BAG_START;
        uint8 const slot = isBag
            ? TakeBankSlot(used, item.slot, BANK_SLOT_BAG_START, BANK_SLOT_BAG_END)
            : TakeBankSlot(used, item.slot, BANK_SLOT_ITEM_START, BANK_SLOT_ITEM_END);
        if (slot == NULL_SLOT)
            continue;

        if (item.holderGuid)
            MoveHeldBankItem(item, slot, guidLow);
        else
            MoveParkedBankItem(item, slot, guidLow);
        CharacterDatabase.DirectPExecute("UPDATE `item_instance` SET `owner_guid` = %u WHERE `guid` = %u "
            "OR `guid` IN (SELECT `item_guid` FROM `character_inventory` WHERE `bag` = %u)", guidLow, item.itemGuid, item.itemGuid);
        movedBag = movedBag || isBag;
    }

    uint32 const bankBagSlots = BANK_SLOT_BAG_END - BANK_SLOT_BAG_START;
    if (movedBag)
        CharacterDatabase.DirectPExecute("UPDATE `characters` SET `bank_bag_slots` = %u WHERE `guid` = %u AND `bank_bag_slots` < %u", bankBagSlots, guidLow, bankBagSlots);
}

void BonkPlayer::ParkAccountBank(uint32 guidLow)
{
    CharacterDatabase.PExecute("INSERT IGNORE INTO `bonk_account_bank` (`account`, `bag`, `slot`, `item_guid`, `item_id`) "
        "SELECT c.`account`, i.`bag`, i.`slot`, i.`item_guid`, i.`item_id` FROM `character_inventory` i JOIN `characters` c ON c.`guid` = i.`guid` "
        "WHERE i.`guid` = %u AND c.`account` <> 0 AND NOT EXISTS (SELECT 1 FROM `bonk_ironman` m WHERE m.`guid` = i.`guid`) "
        "AND ((i.`bag` = 0 AND i.`slot` >= %u AND i.`slot` < %u) OR i.`bag` IN "
        "(SELECT b.`item_guid` FROM `character_inventory` b WHERE b.`guid` = %u AND b.`bag` = 0 AND b.`slot` >= %u AND b.`slot` < %u))",
        guidLow, uint32(BANK_SLOT_ITEM_START), uint32(BANK_SLOT_BAG_END), guidLow, uint32(BANK_SLOT_BAG_START), uint32(BANK_SLOT_BAG_END));
    CharacterDatabase.PExecute("UPDATE `item_instance` i JOIN `bonk_account_bank` b ON b.`item_guid` = i.`guid` SET i.`owner_guid` = 0 WHERE i.`owner_guid` = %u", guidLow);
}

bool BonkPlayer::IsIronman() const
{
    if (m_ironman < 0)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT 1 FROM `bonk_ironman` WHERE `guid` = %u", m_player->GetGUIDLow()));
        m_ironman = result ? 1 : 0;
    }
    return m_ironman > 0;
}

uint32 BonkPlayer::GetProgressAccount() const
{
    return IsIronman() ? GetIronmanAccount(m_player->GetGUIDLow()) : m_player->GetSession()->GetAccountId();
}

void BonkPlayer::OnSave()
{
    SaveGold();
    sBonkQuests.Save(GetProgressAccount());
}

void BonkPlayer::SaveGold() const
{
    if (!m_goldLoaded)
        return;

    CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `gold`) VALUES (%u, %u) ON DUPLICATE KEY UPDATE `gold` = VALUES(`gold`)",
        GetProgressAccount(), m_player->GetMoney());
}

uint8 BonkPlayer::GetUpgradeRank(uint32 upgradeId) const
{
    auto itr = m_upgradeRanks.find(upgradeId);
    return itr != m_upgradeRanks.end() ? itr->second : 0;
}

float BonkPlayer::GetUpgradeUtility(BonkUpgradeUtility utility) const
{
    float total = 0.0f;
    for (auto const& itr : m_upgradeRanks)
        if (BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(itr.first))
            if (upgrade->utility == int8(utility))
                total += upgrade->valuePerRank * std::min(itr.second, upgrade->maxRank);
    return total;
}

uint8 BonkPlayer::GetTalentRank(uint32 talentId) const
{
    auto itr = m_talentRanks.find(talentId);
    return itr != m_talentRanks.end() ? itr->second : 0;
}

bool BonkPlayer::Spend(uint32 copper, std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    if (m_player->GetMoney() < copper)
    {
        message = "You need " + FormatBonkMoney(copper) + ".";
        return false;
    }

    m_player->ModifyMoney(-int32(copper));
    SaveGold();
    return true;
}

bool BonkPlayer::SpendShards(uint32 count, std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    if (m_shards < count)
    {
        message = "You need " + FormatBonkShards(count) + ". You have " + std::to_string(m_shards) + ".";
        return false;
    }

    m_shards -= count;
    SaveShards();
    SendWallet();
    return true;
}

void BonkPlayer::AddShards(uint32 count)
{
    m_shards += count;
    SaveShards();
    SendWallet();
}

void BonkPlayer::SendWallet() const
{
    SendAddon("wallet~" + std::to_string(m_shards));
}

void BonkPlayer::SaveShards() const
{
    CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `shards`) VALUES (%u, %u) ON DUPLICATE KEY UPDATE `shards` = VALUES(`shards`)",
        GetProgressAccount(), m_shards);
}

bool BonkPlayer::BuyUpgrade(uint32 upgradeId, std::string& message)
{
    BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(upgradeId);
    if (!upgrade)
        return false;

    uint8 const rank = GetUpgradeRank(upgradeId);
    if (rank >= upgrade->maxRank)
    {
        message = upgrade->name + " is already at its highest rank.";
        return false;
    }

    if (BonkQuest const* quest = GetLockingQuest(m_player, BONK_REWARD_UPGRADE, upgradeId))
    {
        message = "Finish the quest \"" + quest->name + "\" to unlock " + upgrade->name + ".";
        return false;
    }

    if (upgrade->layout.tree == BONK_TRAINING_TREE && rank >= GetTrainingRankCap())
    {
        message = GetTrainingCapHint();
        return false;
    }

    BonkUpgrade const* required = sBonkMgr.GetUpgrade(upgrade->layout.requiredId);
    if (required && GetUpgradeRank(required->id) < upgrade->layout.requiredRank)
    {
        message = RequirementText(required->name, upgrade->layout.requiredRank, required->maxRank);
        return false;
    }

    if (!Spend(upgrade->GetCost(rank), message))
        return false;

    m_upgradeRanks[upgradeId] = rank + 1;
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_upgrade` (`account`, `upgrade`, `rank`) VALUES (%u, %u, %u)",
        GetProgressAccount(), upgradeId, uint32(rank + 1));
    message = upgrade->name + " is now rank " + std::to_string(rank + 1) + ".";
    return true;
}

bool BonkPlayer::BuyTalent(uint32 talentId, std::string& message)
{
    BonkTalent const* talent = sBonkMgr.GetTalent(talentId);
    if (!talent)
        return false;

    uint8 const rank = GetTalentRank(talentId);
    if (rank >= talent->maxRank)
    {
        message = talent->name + " is already learned.";
        return false;
    }

    if (BonkQuest const* quest = GetLockingQuest(m_player, BONK_REWARD_TALENT, talentId))
    {
        message = "Finish the quest \"" + quest->name + "\" to unlock " + talent->name + ".";
        return false;
    }

    BonkTalent const* required = sBonkMgr.GetTalent(talent->layout.requiredId);
    if (required && GetTalentRank(required->id) < talent->layout.requiredRank)
    {
        message = RequirementText(required->name, talent->layout.requiredRank, required->maxRank);
        return false;
    }

    uint32 const gate = GetFortuneRowGate(talent->layout.row);
    uint32 const above = GetFortuneSpentAbove(talent->layout.row);
    if (above < gate)
    {
        message = "Spend " + FormatBonkShards(gate) + " on the rows above to unlock " + talent->name + ". You have spent " + std::to_string(above) + ".";
        return false;
    }

    if (rank < talent->rankQuests.size())
    {
        uint32 const needed = talent->rankQuests[rank];
        uint32 const done = sBonkQuests.GetCompletedCount(GetProgressAccount());
        if (done < needed)
        {
            message = "You must complete " + std::to_string(needed) + " quests to buy rank " + std::to_string(rank + 1) + " of " + talent->name +
                ". You have completed " + std::to_string(done) + ".";
            return false;
        }
    }

    if (!SpendShards(talent->GetCost(rank), message))
        return false;

    m_talentRanks[talentId] = rank + 1;
    if (talent->IsToggle())
        m_enabledTalents.insert(talentId);
    SaveTalent(talentId);
    message = talent->IsToggle() ? "You learned " + talent->name + " and switched it on." : talent->name + " is now rank " + std::to_string(rank + 1) + ".";
    return true;
}

bool BonkPlayer::ResetUpgrades(std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    if (m_upgradeRanks.empty())
    {
        message = "You have not bought any upgrades.";
        return false;
    }

    uint32 refunded = 0;
    for (auto const& itr : m_upgradeRanks)
        if (BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(itr.first))
            for (uint8 rank = 0; rank < itr.second; ++rank)
                refunded += upgrade->GetCost(rank);

    m_upgradeRanks.clear();
    CharacterDatabase.PExecute("DELETE FROM `bonk_account_upgrade` WHERE `account` = %u", GetProgressAccount());
    m_player->ModifyMoney(int32(refunded));
    SaveGold();
    message = "You refunded all your upgrades and got " + FormatBonkMoney(refunded) + " back.";
    return true;
}

bool BonkPlayer::ResetTalents(std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }
    if (m_talentRanks.empty())
    {
        message = "You have not bought any fortunes.";
        return false;
    }

    uint32 refunded = 0;
    for (auto const& itr : m_talentRanks)
        if (BonkTalent const* talent = sBonkMgr.GetTalent(itr.first))
            for (uint8 rank = 0; rank < itr.second; ++rank)
                refunded += talent->GetCost(rank);

    m_talentRanks.clear();
    m_enabledTalents.clear();
    CharacterDatabase.PExecute("DELETE FROM `bonk_account_talent` WHERE `account` = %u", GetProgressAccount());
    AddShards(refunded);
    message = "You refunded all your fortunes and got " + FormatBonkShards(refunded) + " back.";
    return true;
}

bool BonkPlayer::RefundUpgrade(uint32 upgradeId, std::string& message)
{
    BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(upgradeId);
    if (!upgrade)
        return false;

    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    uint8 const rank = GetUpgradeRank(upgradeId);
    if (!rank)
    {
        message = "You have not bought " + upgrade->name + ".";
        return false;
    }

    for (auto const& itr : m_upgradeRanks)
        if (BonkUpgrade const* other = sBonkMgr.GetUpgrade(itr.first))
            if (itr.second && other->layout.requiredId == upgradeId && rank - 1 < other->layout.requiredRank)
            {
                message = "Refund " + other->name + " first, because it requires this one.";
                return false;
            }

    uint32 const refunded = upgrade->GetCost(rank - 1);
    struct TrimAfter
    {
        BonkPlayer const* bonk;
        ~TrimAfter() { bonk->TrimPoolToggles(); }
    } const trimAfter{ this };
    if (rank > 1)
    {
        m_upgradeRanks[upgradeId] = rank - 1;
        CharacterDatabase.PExecute("REPLACE INTO `bonk_account_upgrade` (`account`, `upgrade`, `rank`) VALUES (%u, %u, %u)",
            GetProgressAccount(), upgradeId, uint32(rank - 1));
    }
    else
    {
        m_upgradeRanks.erase(upgradeId);
        CharacterDatabase.PExecute("DELETE FROM `bonk_account_upgrade` WHERE `account` = %u AND `upgrade` = %u",
            GetProgressAccount(), upgradeId);
    }
    m_player->ModifyMoney(int32(refunded));
    SaveGold();
    message = "You refunded " + (upgrade->maxRank > 1 ? "rank " + std::to_string(rank) + " of " : std::string()) + upgrade->name + " and got " +
        FormatBonkMoney(refunded) + " back.";
    return true;
}

uint32 BonkPlayer::GetFortuneSpentAbove(uint8 row, uint32 refundedId) const
{
    uint32 spent = 0;
    for (auto const& itr : m_talentRanks)
        if (BonkTalent const* talent = sBonkMgr.GetTalent(itr.first))
            if (talent->layout.tree && talent->layout.row < row && !talent->IsEndless())
            {
                uint8 const ranks = itr.first == refundedId && itr.second ? itr.second - 1 : itr.second;
                for (uint8 rank = 0; rank < ranks; ++rank)
                    spent += talent->GetCost(rank);
            }
    return spent;
}

uint32 BonkPlayer::GetFortuneSpent(uint32 tree) const
{
    uint32 spent = 0;
    for (auto const& itr : m_talentRanks)
        if (BonkTalent const* talent = sBonkMgr.GetTalent(itr.first))
            if (talent->layout.tree == tree)
                for (uint8 rank = 0; rank < itr.second; ++rank)
                    spent += talent->GetCost(rank);
    return spent;
}

bool BonkPlayer::RefundTalent(uint32 talentId, std::string& message)
{
    BonkTalent const* talent = sBonkMgr.GetTalent(talentId);
    if (!talent)
        return false;

    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    uint8 const rank = GetTalentRank(talentId);
    if (!rank)
    {
        message = "You have not bought " + talent->name + ".";
        return false;
    }

    for (auto const& itr : m_talentRanks)
        if (BonkTalent const* other = sBonkMgr.GetTalent(itr.first))
            if (itr.second && other->layout.requiredId == talentId && rank - 1 < other->layout.requiredRank)
            {
                message = "Refund " + other->name + " first, because it requires this one.";
                return false;
            }

    for (auto const& itr : m_talentRanks)
        if (BonkTalent const* other = sBonkMgr.GetTalent(itr.first))
            if (itr.second && other->layout.row > talent->layout.row &&
                GetFortuneSpentAbove(other->layout.row, talentId) < GetFortuneRowGate(other->layout.row))
            {
                message = "Refund " + other->name + " first, because it needs the tickets you spent here.";
                return false;
            }

    uint32 const refunded = talent->GetCost(rank - 1);
    if (rank > 1)
    {
        m_talentRanks[talentId] = rank - 1;
        SaveTalent(talentId);
    }
    else
    {
        m_talentRanks.erase(talentId);
        m_enabledTalents.erase(talentId);
        CharacterDatabase.PExecute("DELETE FROM `bonk_account_talent` WHERE `account` = %u AND `talent` = %u",
            GetProgressAccount(), talentId);
    }
    AddShards(refunded);
    message = "You refunded " + (talent->maxRank > 1 ? "rank " + std::to_string(rank) + " of " : std::string()) + talent->name + " and got " +
        FormatBonkShards(refunded) + " back.";
    return true;
}

bool BonkPlayer::ToggleTalent(uint32 talentId, std::string& message)
{
    BonkTalent const* talent = sBonkMgr.GetTalent(talentId);
    if (!talent || !talent->IsToggle() || !GetTalentRank(talentId))
        return false;

    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    bool const enable = !IsTalentEnabled(talentId);
    if (enable)
        m_enabledTalents.insert(talentId);
    else
        m_enabledTalents.erase(talentId);
    SaveTalent(talentId);
    message = talent->name + (enable ? " is now on." : " is now off.");
    return true;
}

void BonkPlayer::SaveTalent(uint32 talentId) const
{
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_talent` (`account`, `talent`, `rank`, `enabled`) VALUES (%u, %u, %u, %u)",
        GetProgressAccount(), talentId, uint32(GetTalentRank(talentId)), IsTalentEnabled(talentId) ? 1u : 0u);
}

void BonkPlayer::OpenShop(BonkShop shop, Creature const* npc)
{
    m_shop = shop;
    m_shopNpc = npc->GetObjectGuid();
    m_shopNpcName = npc->GetName();
    SendShop(true);
}

void BonkPlayer::VisitSpellbook(Creature const* keeper)
{
    m_spellbookKeeper = keeper->GetObjectGuid();
}

bool BonkPlayer::IsAtSpellbook() const
{
    if (m_spellbookKeeper.IsEmpty() || !m_player->IsInWorld())
        return false;

    Creature const* keeper = m_player->GetMap()->GetCreature(m_spellbookKeeper);
    return keeper && keeper->IsAlive() && m_player->IsWithinDistInMap(keeper, BONK_SHOP_RANGE);
}

void BonkPlayer::VisitDeck(GameObject const* table)
{
    m_deckTable = table->GetObjectGuid();
}

bool BonkPlayer::IsAtDeck() const
{
    if (m_deckTable.IsEmpty() || !m_player->IsInWorld())
        return false;

    GameObject const* table = m_player->GetMap()->GetGameObject(m_deckTable);
    return table && m_player->IsWithinDistInMap(table, BONK_SHOP_RANGE);
}

bool BonkPlayer::BuyBanSlot(BonkQuestReward type, std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }
    if (!IsAtDeck())
    {
        message = "Visit the Darkmoon Deck at the Inn to buy ban slots.";
        return false;
    }
    BonkTalentEffect const effect = type == BONK_REWARD_CARD ? BONK_TALENT_CARD_BANS : BONK_TALENT_GLYPH_BANS;
    for (auto const& itr : sBonkMgr.GetTalents())
        if (itr.second.effect == effect)
            return BuyTalent(itr.first, message);
    message = "Sayge has no ban slots for sale.";
    return false;
}

bool BonkPlayer::IsAtShop() const
{
    if (m_shop == BONK_SHOP_NONE || !m_player->IsInWorld())
        return false;

    Creature const* npc = m_player->GetMap()->GetCreature(m_shopNpc);
    return npc && npc->IsAlive() && m_player->IsWithinDistInMap(npc, BONK_SHOP_RANGE);
}

void BonkPlayer::ShopLearn(uint32 id)
{
    if (!IsAtShop())
    {
        Notify("You are too far away from " + (m_shopNpcName.empty() ? std::string("the shop") : m_shopNpcName) + ".");
        SendAddon("shopclose");
        return;
    }

    std::string message;
    if (m_shop == BONK_SHOP_UPGRADES)
        BuyUpgrade(id, message);
    else if (m_shop == BONK_SHOP_MASTERY)
        LearnMastery(id, message);
    else
        BuyTalent(id, message);
    if (!message.empty())
        Notify(message);
    SendShop(false);
    SendStats();
}

void BonkPlayer::ShopUnlearn(uint32 id)
{
    if (!IsAtShop())
    {
        Notify("You are too far away from " + (m_shopNpcName.empty() ? std::string("the shop") : m_shopNpcName) + ".");
        SendAddon("shopclose");
        return;
    }

    std::string message;
    if (m_shop == BONK_SHOP_UPGRADES)
        RefundUpgrade(id, message);
    else if (m_shop == BONK_SHOP_MASTERY)
        RefundMastery(id, message);
    else
        RefundTalent(id, message);
    if (!message.empty())
        Notify(message);
    SendShop(false);
    SendStats();
}

void BonkPlayer::ShopToggle(uint32 id)
{
    if (!IsAtShop() || m_shop != BONK_SHOP_TALENTS)
    {
        Notify("You are too far away from " + (m_shopNpcName.empty() ? std::string("the shop") : m_shopNpcName) + ".");
        SendAddon("shopclose");
        return;
    }

    std::string message;
    ToggleTalent(id, message);
    if (!message.empty())
        Notify(message);
    SendShop(false);
}

void BonkPlayer::ShopPreset(uint8 slot)
{
    if (!IsAtShop() || m_shop != BONK_SHOP_MASTERY)
    {
        Notify("You are too far away from " + (m_shopNpcName.empty() ? std::string("the shop") : m_shopNpcName) + ".");
        SendAddon("shopclose");
        return;
    }

    std::string message;
    LoadMasteryPreset(slot, message);
    if (!message.empty())
        Notify(message);
    SendShop(false);
    SendStats();
}

void BonkPlayer::ShopRespec()
{
    if (!IsAtShop())
    {
        Notify("You are too far away from " + (m_shopNpcName.empty() ? std::string("the shop") : m_shopNpcName) + ".");
        SendAddon("shopclose");
        return;
    }

    std::string message;
    if (m_shop == BONK_SHOP_UPGRADES)
        ResetUpgrades(message);
    else if (m_shop == BONK_SHOP_MASTERY)
        ResetMastery(message);
    else
        ResetTalents(message);
    if (!message.empty())
        Notify(message);
    SendShop(false);
    SendStats();
}

void BonkPlayer::SendMasteryShop(bool open, bool inspect) const
{
    uint8 const classId = m_player->GetClass();
    uint32 const level = GetMasteryLevel();
    uint32 const earned = GetMasteryPoints();
    uint32 const spent = GetMasterySpent();
    SendAddon("shop~" + std::to_string(uint32(BONK_SHOP_MASTERY)) + "~" + (open ? "1" : "0") + "~" + (inspect ? std::string() : m_shopNpcName) + "~points~" +
        std::to_string(earned > spent ? earned - spent : 0) + "~" + std::to_string(level) + "~" +
        std::to_string(BonkMastery::GetXpIntoLevel(m_masteryXp)) + "~" +
        std::to_string(level < BONK_MASTERY_MAX_LEVEL ? BonkMastery::GetXpForLevel(level + 1) : 0) + "~" +
        std::to_string(BONK_MASTERY_MAX_LEVEL) + "~" + std::to_string(earned) + "~" + std::to_string(earned - level) + "~" +
        GetClassName() + "~" + std::to_string(BONK_MASTERY_POINTS_PER_ROW) + (inspect ? "~1" : ""));

    for (BonkMasteryTree const* tree : sBonkMastery.GetTrees(classId))
    {
        std::string gates;
        for (uint8 row = 0; row < BONK_MASTERY_FREE_ROWS; ++row)
            gates += (row ? "," : "") + std::to_string(tree->IsFree() ? BONK_MASTERY_FREE_GATES[row] : uint32(std::max<uint8>(row, 1) - 1) * BONK_MASTERY_POINTS_PER_ROW);
        SendAddon("tree~" + std::to_string(tree->id) + "~" + tree->name + "~" + tree->background + "~" + std::to_string(uint32(tree->layout)) + "~" + gates +
            "~" + std::to_string(GetMasterySpentInTree(tree->id)));
    }

    if (!inspect)
    {
        SendAddon(DescribeMasteryPresets());
        SendMasteryPresetRanks();
    }

    for (auto const& itr : sBonkMastery.GetNodes())
    {
        BonkMasteryNode const& node = itr.second;
        if (node.classId != classId)
            continue;
        uint8 const rank = GetMasteryRank(node.id);
        std::ostringstream line;
        line << "node~" << node.tree << '~' << node.id << '~' << uint32(node.row) << '~' << uint32(node.column) << '~' << node.icon
             << '~' << uint32(rank) << '~' << uint32(node.maxRank) << '~' << (rank < node.maxRank ? 1 : 0)
             << '~' << node.requiredId << '~' << uint32(node.requiredRank) << "~0~0~" << node.name << '~' << node.description << "~0~~" << FormatStat(node.valuePerRank) << '~' << (rank ? 1 : 0);
        std::string parents;
        for (uint32 parentId : node.parents)
            parents += (parents.empty() ? "" : ",") + std::to_string(parentId);
        line << '~' << uint32(node.shape) << '~' << FormatStat(node.position) << '~' << parents << '~' << node.excludes;
        SendAddon(line.str());
    }

    SendAddon("shopend");
}

void BonkPlayer::SendShop(bool open) const
{
    if (m_shop == BONK_SHOP_MASTERY)
    {
        SendMasteryShop(open, false);
        return;
    }

    bool const shards = m_shop == BONK_SHOP_TALENTS;
    std::string gates;
    if (shards)
    {
        uint8 lastRow = 1;
        for (auto const& itr : sBonkMgr.GetTalents())
            lastRow = std::max(lastRow, itr.second.layout.row);
        for (uint8 row = 1; row <= lastRow; ++row)
            gates += (row > 1 ? "," : "") + std::to_string(GetFortuneRowGate(row)) + ":" + std::to_string(GetFortuneSpentAbove(row));
    }
    SendAddon("shop~" + std::to_string(uint32(m_shop)) + "~" + (open ? "1" : "0") + "~" + m_shopNpcName + "~" +
        (shards ? "shards~" + std::to_string(m_shards) + "~" + gates : "gold~" + std::to_string(m_player->GetMoney())));

    for (auto const& itr : sBonkMgr.GetShopTrees())
        if (itr.second.shop == m_shop)
            SendAddon("tree~" + std::to_string(itr.first) + "~" + itr.second.name + "~" + itr.second.background + "~" +
                std::to_string(shards ? GetFortuneSpent(itr.first) : 0));

    BonkQuestReward const lockType = m_shop == BONK_SHOP_UPGRADES ? BONK_REWARD_UPGRADE : BONK_REWARD_TALENT;
    auto sendNode = [this, lockType](uint32 id, BonkShopNode const& layout, uint8 rank, uint8 maxRank, uint32 nextCost, uint32 refund, bool toggle,
        bool enabled, std::string const& name, std::string const& description, float valuePerRank)
    {
        if (!layout.tree)
            return;

        BonkQuest const* lock = GetLockingQuest(m_player, lockType, id);

        rank = std::min(rank, maxRank);
        std::ostringstream line;
        line << "node~" << layout.tree << '~' << id << '~' << uint32(layout.row) << '~' << uint32(layout.column) << '~' << layout.icon
             << '~' << uint32(rank) << '~' << uint32(maxRank) << '~' << (rank < maxRank ? nextCost : 0)
             << '~' << layout.requiredId << '~' << uint32(layout.requiredRank) << '~' << (toggle ? 1 : 0) << '~' << (enabled ? 1 : 0)
             << '~' << name << '~' << description << '~' << (lock ? 1 : 0) << '~' << (lock ? lock->name : "") << '~' << FormatStat(valuePerRank)
             << '~' << (rank ? refund : 0) << '~' << (lock ? lock->description : "");
        SendAddon(line.str());
    };

    if (m_shop == BONK_SHOP_UPGRADES)
    {
        for (auto const& itr : sBonkMgr.GetUpgrades())
        {
            BonkUpgrade const& upgrade = itr.second;
            uint8 const rank = GetUpgradeRank(upgrade.id);
            std::string description = upgrade.description;
            if (upgrade.layout.tree == BONK_TRAINING_TREE && rank < upgrade.maxRank && rank >= GetTrainingRankCap())
                description += " " + GetTrainingCapHint();
            sendNode(upgrade.id, upgrade.layout, rank, upgrade.maxRank, upgrade.GetCost(rank), rank ? upgrade.GetCost(rank - 1) : 0, false, false,
                upgrade.name, description, upgrade.valuePerRank);
        }
    }
    else if (m_shop == BONK_SHOP_TALENTS)
    {
        for (auto const& itr : sBonkMgr.GetTalents())
        {
            BonkTalent const& talent = itr.second;
            uint8 const rank = GetTalentRank(talent.id);
            std::string description = talent.description;
            if (rank < talent.maxRank && rank < talent.rankQuests.size() &&
                sBonkQuests.GetCompletedCount(GetProgressAccount()) < talent.rankQuests[rank])
                description += " The next rank unlocks after you complete " + std::to_string(talent.rankQuests[rank]) + " quests.";
            sendNode(talent.id, talent.layout, rank, talent.maxRank, talent.GetCost(rank), rank ? talent.GetCost(rank - 1) : 0, talent.IsToggle(),
                IsTalentEnabled(talent.id), talent.name, description, talent.valuePerRank);
        }
    }

    SendAddon("shopend");
}

BonkRunBonuses BonkPlayer::GetRunBonuses() const
{
    BonkRunBonuses bonuses;

    for (auto const& itr : m_upgradeRanks)
        if (BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(itr.first))
        {
            float const value = upgrade->valuePerRank * std::min(itr.second, upgrade->maxRank);
            if (upgrade->utility >= 0)
                bonuses.utility[upgrade->utility] += value;
            else
                bonuses.passives[upgrade->effect] += value;
        }

    BonkMasteryBonuses const mastery = GetMasteryBonuses();
    for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
        bonuses.passives[effect] += mastery.passives[effect];

    float const determination = GetDetermination() * BONK_DETERMINATION_PER_STACK;
    bonuses.passives[BONK_PASSIVE_POWER] += determination;
    bonuses.damageTakenCut += determination;

    if (BonkTitle const* title = GetActiveTitle())
        if (title->value > 0.0f && title->stat < MAX_BONK_PASSIVE_EFFECT)
            bonuses.passives[title->stat] += title->value;

    for (auto const& itr : m_talentRanks)
    {
        BonkTalent const* talent = sBonkMgr.GetTalent(itr.first);
        if (!talent || !itr.second)
            continue;

        float const value = talent->valuePerRank * std::min(itr.second, talent->maxRank);
        if (talent->IsToggle())
        {
            if (!IsTalentEnabled(talent->id))
                continue;
            bonuses.goldBonus += talent->goldBonus;
        }

        switch (talent->effect)
        {
            case BONK_TALENT_ENEMY_HEALTH: bonuses.enemyHealth += value; break;
            case BONK_TALENT_ENEMY_DAMAGE: bonuses.enemyDamage += value; break;
            case BONK_TALENT_SPAWN_RATE:   bonuses.spawnRate += value; break;
            case BONK_TALENT_ELITE_RATE:   bonuses.eliteRate += value; break;
            case BONK_TALENT_REROLL:       bonuses.rerolls += uint32(value); break;
            case BONK_TALENT_BANISH:       bonuses.banishes += uint32(value); break;
            case BONK_TALENT_SKIP:         bonuses.skips += uint32(value); break;
            case BONK_TALENT_REVIVE:       bonuses.revives += uint32(value); break;
            case BONK_TALENT_START_COINS:  bonuses.startCoins += uint32(value); break;
            case BONK_TALENT_SPELL_SLOT:   bonuses.spellSlots += uint32(value); break;
            case BONK_TALENT_PASSIVE_SLOT: bonuses.passiveSlots += uint32(value); break;
            case BONK_TALENT_SECOND_SIGHT: bonuses.secondSight = bonuses.secondSight || value > 0.0f; break;
            case BONK_TALENT_EXPERIENCE:   bonuses.passives[BONK_PASSIVE_GROWTH] += value; break;
            case BONK_TALENT_PREMONITION:  bonuses.earlierEvolutions += uint32(value); break;
            case BONK_TALENT_CURSED_GROUND: bonuses.healingCut += value; break;
            case BONK_TALENT_DOOM_CLOCK:   bonuses.earlierBossSeconds += uint32(value); break;
            case BONK_TALENT_ELITE_AFFIX:  bonuses.eliteAffixes += uint32(value); break;
            case BONK_TALENT_MIGHT:
                bonuses.passives[BONK_PASSIVE_POWER] += value;
                bonuses.passives[BONK_PASSIVE_MAX_HEALTH] += value;
                break;
            case BONK_TALENT_FORTUNE:
                bonuses.goldBonus += value;
                bonuses.scrapBonus += value;
                break;
            default: break;
        }
    }

    return bonuses;
}

void BonkPlayer::PreviewTelegraph(BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs)
{
    m_previewTelegraphs.Add(m_player, shape, x, y, z, orientation, size, durationMs);
}

void BonkPlayer::Update(uint32 diff)
{
    if (m_campCheckPending && !m_player->IsBeingTeleported())
    {
        m_campCheckPending = false;
        if (!m_run && !m_player->IsGameMaster() && !IsAtCamp())
            TeleportToCamp();
    }

    m_previewTelegraphs.Update(m_player, diff);
    UpdateCampSeat(diff);
    UpdateReviveOutsideRun(diff);
    UpdateMailboxRefresh(diff);
    UpdateDash(diff);
    UpdateCampMailNotice(diff);
    UpdateMailboxGlow(diff);

    if (m_run)
    {
        m_run->Update(diff);
        if (m_run->IsFinished())
        {
            m_run.reset();
            SendStats();
            SendDash();
        }
        else
            BonkStats::UpdateBossBar(m_player, diff);
    }

    if (m_player->GetPowerType() == POWER_MANA)
    {
        uint32 const maxMana = m_player->GetMaxPower(POWER_MANA);
        if (m_player->GetPower(POWER_MANA) < maxMana)
            m_player->SetPower(POWER_MANA, maxMana);
    }
}

bool BonkPlayer::IsTierCleared(BonkTier const& tier) const
{
    return tier.stages && GetBestStage(tier.id) > tier.stages;
}

bool BonkPlayer::IsTierCleared(char const* tierKey) const
{
    BonkTier const* tier = sBonkMgr.FindTier(tierKey);
    return tier && IsTierCleared(*tier);
}

uint32 BonkPlayer::GetMythicFloorsCleared() const
{
    BonkTier const* mythic = sBonkMgr.FindTier("mythic");
    uint32 const reached = mythic ? GetBestStage(mythic->id) : 0;
    return reached ? reached - 1 : 0;
}

uint8 BonkPlayer::GetTrainingRankCap() const
{
    if (!IsTierCleared("heroic"))
        return BONK_TRAINING_OPEN_RANKS;
    uint32 const mythicRanks = std::min<uint32>(BONK_TRAINING_MYTHIC_RANKS, GetMythicFloorsCleared() / BONK_TRAINING_FLOORS_PER_RANK);
    return uint8(BONK_TRAINING_OPEN_RANKS + BONK_TRAINING_HEROIC_RANKS + mythicRanks);
}

std::string BonkPlayer::GetTrainingCapHint() const
{
    uint8 const cap = GetTrainingRankCap();
    if (!IsTierCleared("heroic"))
        return "Clear Heroic to unlock ranks " + std::to_string(cap + 1) + " to " + std::to_string(cap + BONK_TRAINING_HEROIC_RANKS) + ".";
    uint32 const floor = (cap - BONK_TRAINING_OPEN_RANKS - BONK_TRAINING_HEROIC_RANKS + 1) * BONK_TRAINING_FLOORS_PER_RANK;
    return "Clear Mythic floor " + std::to_string(floor) + " to unlock rank " + std::to_string(cap + 1) + ".";
}

uint8 BonkPlayer::GetDetermination() const
{
    return IsTierCleared("normal") ? 0 : m_determination;
}

void BonkPlayer::AddDetermination()
{
    if (IsTierCleared("normal") || m_determination >= BONK_DETERMINATION_MAX_STACKS)
        return;
    ++m_determination;
    CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `determination`) VALUES (%u, %u) ON DUPLICATE KEY UPDATE `determination` = VALUES(`determination`)",
        GetProgressAccount(), uint32(m_determination));
    SendDetermination();
}

void BonkPlayer::SendDetermination() const
{
    uint8 const stacks = GetDetermination();
    SendAddon("determination~" + std::to_string(stacks) + "~" + FormatStat(stacks * BONK_DETERMINATION_PER_STACK));
}

bool BonkPlayer::CanSwapGearInCombat() const
{
    return m_equippingGear || (m_run && (m_run->IsActive() || m_run->IsPortalOpen()));
}

bool BonkPlayer::StowTwoHander()
{
    Item* twoHander = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    if (!twoHander || !m_player->IsTwoHandUsed())
        return false;
    ItemPosCountVec dest;
    uint8 bagSlot = 0;
    if (m_player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, twoHander, bagSlot, false) != EQUIP_ERR_OK)
        return false;
    m_player->AutoUnequipItemFromSlot(EQUIPMENT_SLOT_MAINHAND);
    return true;
}

bool BonkPlayer::IsSlotUnusedByClass(uint8 slot) const
{
    uint8 const playerClass = m_player->GetClass();
    switch (slot)
    {
        case EQUIPMENT_SLOT_MAINHAND: return playerClass == CLASS_HUNTER;
        case EQUIPMENT_SLOT_OFFHAND:  return playerClass == CLASS_HUNTER || playerClass == CLASS_DRUID;
        case EQUIPMENT_SLOT_RANGED:   return playerClass != CLASS_HUNTER;
        default:                      return false;
    }
}

bool BonkPlayer::CanBuyGear(uint32 itemEntry) const
{
    BonkItemStat const* stat = sBonkMgr.GetItemStat(itemEntry);
    if (!stat || !stat->unlockTier)
        return true;
    BonkTier const* tier = sBonkMgr.GetTier(stat->unlockTier);
    return tier && IsTierCleared(*tier);
}

namespace
{
    uint32 GetGearGroup(uint32 itemEntry)
    {
        BonkItemStat const* stat = sBonkMgr.GetItemStat(itemEntry);
        return stat ? stat->unlockTier : 0;
    }

    BonkTier const* GetGearGroupTier(uint32 group, size_t* order = nullptr)
    {
        std::vector<BonkTier const*> const tiers = BonkPlayer::GetTiersInOrder();
        for (size_t i = 0; i < tiers.size(); ++i)
            if (tiers[i]->unlockTierId == group)
            {
                if (order)
                    *order = i;
                return tiers[i];
            }
        if (order)
            *order = tiers.size();
        return nullptr;
    }
}

std::vector<uint32> BonkPlayer::GetGearTabs(Creature const* vendor) const
{
    std::vector<uint32> tabs;
    VendorItemData const* items = vendor->GetVendorItems();
    if (!items)
        return tabs;
    for (uint32 i = 0; i < items->GetItemCount(); ++i)
    {
        VendorItem const* item = items->GetItem(i);
        if (!item || !CanBuyGear(item->item))
            continue;
        uint32 const group = GetGearGroup(item->item);
        if (std::find(tabs.begin(), tabs.end(), group) == tabs.end())
            tabs.push_back(group);
    }
    std::sort(tabs.begin(), tabs.end(), [](uint32 a, uint32 b)
    {
        size_t orderA, orderB;
        GetGearGroupTier(a, &orderA);
        GetGearGroupTier(b, &orderB);
        return orderA != orderB ? orderA < orderB : a < b;
    });
    return tabs;
}

uint32 BonkPlayer::GetShownGearTab(std::vector<uint32> const& tabs) const
{
    if (std::find(tabs.begin(), tabs.end(), m_gearTab) != tabs.end())
        return m_gearTab;
    return tabs.empty() ? 0 : tabs.front();
}

bool BonkPlayer::RecordGearFound(uint32 keepEntry)
{
    if (!keepEntry || !m_foundGear.insert(keepEntry).second)
        return false;
    CharacterDatabase.PExecute("INSERT IGNORE INTO `bonk_account_found` (`account`, `item`) VALUES (%u, %u)", GetProgressAccount(), keepEntry);
    return true;
}

float BonkPlayer::GetGearPriceFactor(Creature const* vendor, uint32 itemEntry) const
{
    return vendor && vendor->GetEntry() == BONK_GEAR_VENDOR && IsGearFound(itemEntry) ? BONK_FOUND_PRICE_FACTOR : 1.0f;
}

void BonkPlayer::SendFoundGear(Creature const* vendor) const
{
    if (vendor->GetEntry() != BONK_GEAR_VENDOR)
        return;
    SendAddon("gearfoundclear");
    VendorItemData const* items = vendor->GetVendorItems();
    std::string ids;
    for (uint32 i = 0; items && i < items->GetItemCount(); ++i)
    {
        VendorItem const* item = items->GetItem(i);
        if (!item || !IsGearFound(item->item))
            continue;
        ids += (ids.empty() ? "" : ",") + std::to_string(item->item);
        if (ids.size() > BONK_FOUND_LINE_LENGTH)
        {
            SendAddon("gearfound~" + ids);
            ids.clear();
        }
    }
    if (!ids.empty())
        SendAddon("gearfound~" + ids);
}

bool BonkPlayer::ShowsGearItem(Creature const* vendor, uint32 itemEntry) const
{
    if (vendor->GetEntry() != BONK_GEAR_VENDOR)
        return true;
    BonkGear const* runGear = sBonkMgr.GetGearByKeep(itemEntry);
    if (runGear && runGear->quality == ITEM_QUALITY_UNCOMMON && !IsGearFound(itemEntry))
        return false;
    std::vector<uint32> const tabs = GetGearTabs(vendor);
    return tabs.size() < 2 || GetGearGroup(itemEntry) == GetShownGearTab(tabs);
}

bool BonkPlayer::IsTierVendor(Creature const* vendor) const
{
    return vendor && vendor->GetEntry() == BONK_TIER_VENDOR;
}

uint32 BonkPlayer::GetStockTier() const
{
    uint32 const cleared = GetMythicFloorsCleared();
    uint32 const lagged = cleared > BONK_STOCK_TIER_LAG ? cleared - BONK_STOCK_TIER_LAG : 1u;
    return std::min(BONK_STOCK_TIER_MAX, std::max(1u, lagged));
}

std::vector<uint32> BonkPlayer::GetTierStock() const
{
    std::vector<uint32> stock;
    if (!IsTierCleared("heroic"))
        return stock;
    uint32 const tier = GetStockTier();
    for (uint32 slot = 0; slot < BONK_TIER_GEAR_SLOTS; ++slot)
        if (uint32 const entry = BonkMgr::GetTierGearEntry(m_player->GetClass(), tier, slot))
            stock.push_back(entry);
    std::vector<uint32> const mythicGear = BonkMgr::GetMythicGearFor(m_player->GetClassMask(), tier);
    stock.insert(stock.end(), mythicGear.begin(), mythicGear.end());
    return stock;
}

void BonkPlayer::AddScrap(uint32 points)
{
    if (!points)
        return;
    m_scrap += points;
    CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `scrap`) VALUES (%u, %u) ON DUPLICATE KEY UPDATE `scrap` = VALUES(`scrap`)",
        GetProgressAccount(), m_scrap);
}

void BonkPlayer::SetQuartermasterUpgrade(bool on)
{
    m_quartermasterUpgrade = on;
    DiscardFoundReroll();
    SendQuartermaster();
    SendFoundUpgrades();
}

void BonkPlayer::SendQuartermaster() const
{
    std::vector<Item*> const upgradable = GetUpgradableGear();
    SendAddon("quartermaster~" + std::to_string(GetStockTier()) + "~" + std::to_string(m_scrap) + "~" + std::to_string(GetUpgradeCeiling()) + "~" +
        (m_quartermasterUpgrade ? "1" : "0") + "~" + (upgradable.empty() ? "0" : "1"));
}

uint32 BonkPlayer::GetUpgradeCeiling() const
{
    return std::max(BONK_STOCK_TIER_MAX, GetMythicFloorsCleared());
}

std::vector<Item*> BonkPlayer::GetUpgradableGear() const
{
    std::vector<Item*> gear;
    auto consider = [&](Item* item)
    {
        if (item && BonkMgr::GetGearTier(item->GetEntry()) && sBonkFound.IsFound(item->GetGUIDLow()))
            gear.push_back(item);
    };
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        consider(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* container = dynamic_cast<Bag*>(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                consider(container->GetItemByPos(uint8(slot)));
    return gear;
}

void BonkPlayer::SendFoundUpgrades() const
{
    SendAddon("qmupclear");
    uint32 const ceiling = GetUpgradeCeiling();
    for (Item* item : GetUpgradableGear())
    {
        BonkFoundGear found;
        sBonkFound.Get(item->GetGUIDLow(), found);
        uint32 const tier = BonkFound::GetItemTier(item);
        uint32 const cost = BonkFound::GetUpgradeCost(tier);
        std::vector<BonkFoundLine> offers;
        if (item->GetGUIDLow() == m_rerollItem)
            for (auto const& offer : m_rerollOffers)
                offers.push_back({ offer.first, offer.second });
        SendAddon("qmup~" + std::to_string(item->GetGUIDLow()) + "~" + std::to_string(item->GetEntry()) + "~" + std::to_string(tier) + "~" +
            std::to_string(cost) + "~" + (tier < ceiling ? "1" : "0") + "~" + (item->IsEquipped() ? "1" : "0") + "~" +
            BonkFound::DescribeLines(found, tier) + "~" + BonkFound::DescribeExtra(item, tier) + "~" +
            std::to_string(BonkFound::GetRerollCost(found.rerolls)) + "~" + std::to_string(found.rerollLine) + "~" +
            (offers.empty() ? "~" : std::to_string(m_rerollLine) + "~" + BonkFound::DescribeOffers(offers, tier)) + "~" +
            BonkFound::DescribeRanges(tier));
    }
    SendAddon("qmupend~" + std::to_string(m_scrap) + "~" + std::to_string(ceiling));
}

Item* BonkPlayer::FindUpgradableGear(uint32 guidLow) const
{
    for (Item* item : GetUpgradableGear())
        if (item->GetGUIDLow() == guidLow)
            return item;
    return nullptr;
}

void BonkPlayer::DiscardFoundReroll()
{
    m_rerollItem = 0;
    m_rerollLine = 0;
    m_rerollOffers.clear();
}

bool BonkPlayer::RerollFoundGear(uint32 guidLow, uint8 lineIndex, std::string& error)
{
    if (!m_player->FindNearestCreature(BONK_TIER_VENDOR, BONK_GEAR_VENDOR_RANGE))
    {
        error = "Talk to the Mythic Quartermaster to reroll gear.";
        return false;
    }
    Item* item = FindUpgradableGear(guidLow);
    BonkFoundGear found;
    if (!item || !sBonkFound.Get(guidLow, found) || lineIndex >= found.lines.size())
    {
        error = "Only bonus stats on gear you found in Mythic runs can be rerolled.";
        return false;
    }
    if (found.rerollLine != BONK_FOUND_NO_LINE && found.rerollLine != lineIndex)
    {
        error = "You may only reroll one stat per item.";
        return false;
    }
    if (!Spend(BonkFound::GetRerollCost(found.rerolls), error))
        return false;

    ++found.rerolls;
    found.rerollLine = lineIndex;
    BonkFound::Remember(item, found);
    m_rerollItem = guidLow;
    m_rerollLine = lineIndex;
    m_rerollOffers.clear();
    for (BonkFoundLine const& offer : BonkFound::RollRerollOffers(found, lineIndex))
        m_rerollOffers.emplace_back(offer.effect, offer.roll);
    SendFoundUpgrades();
    return true;
}

bool BonkPlayer::PickFoundReroll(uint32 guidLow, uint8 choice, std::string& error)
{
    Item* item = FindUpgradableGear(guidLow);
    BonkFoundGear found;
    bool const valid = guidLow == m_rerollItem && item && sBonkFound.Get(guidLow, found) && m_rerollLine < found.lines.size() &&
        choice <= m_rerollOffers.size();
    if (valid && choice > 0)
    {
        found.lines[m_rerollLine].effect = m_rerollOffers[choice - 1].first;
        found.lines[m_rerollLine].roll = m_rerollOffers[choice - 1].second;
        BonkFound::Remember(item, found);
    }
    DiscardFoundReroll();
    SendFoundUpgrades();
    if (!valid)
    {
        error = "Reroll the stat again to get new choices.";
        return false;
    }
    if (choice > 0)
    {
        SendFoundItems();
        SendStats();
    }
    return true;
}

Item* BonkPlayer::ReplaceGearEntry(Item* item, uint32 entry)
{
    uint8 const bag = item->GetBagSlot();
    uint8 const slot = item->GetSlot();
    bool const equipped = item->IsEquipped();
    uint32 const oldEntry = item->GetEntry();
    auto create = [&](uint32 newEntry) -> Item*
    {
        if (equipped)
        {
            uint16 dest;
            if (m_player->CanEquipNewItem(slot, dest, newEntry, false) != EQUIP_ERR_OK)
                return nullptr;
            return m_player->EquipNewItem(dest, newEntry, true);
        }
        ItemPosCountVec dest;
        if (m_player->CanStoreNewItem(bag, slot, dest, newEntry, 1) != EQUIP_ERR_OK)
            return nullptr;
        return m_player->StoreNewItem(dest, newEntry, true);
    };
    m_player->DestroyItem(bag, slot, true);
    if (Item* fresh = create(entry))
        return fresh;
    create(oldEntry);
    return nullptr;
}

bool BonkPlayer::UpgradeFoundGear(uint32 guidLow, std::string& error)
{
    if (!m_player->FindNearestCreature(BONK_TIER_VENDOR, BONK_GEAR_VENDOR_RANGE))
    {
        error = "Talk to the Mythic Quartermaster to upgrade gear.";
        return false;
    }
    Item* item = FindUpgradableGear(guidLow);
    BonkFoundGear found;
    if (!item || !sBonkFound.Get(guidLow, found))
    {
        error = "Only gear you found in Mythic runs can be upgraded.";
        return false;
    }
    uint32 const tier = BonkFound::GetItemTier(item);
    uint32 const cost = BonkFound::GetUpgradeCost(tier);
    if (tier >= GetUpgradeCeiling())
    {
        error = "Clear Mythic floor " + std::to_string(tier + 1) + " to raise this to " + BonkFound::DescribeTier(tier + 1) + ".";
        return false;
    }
    if (m_scrap < cost)
    {
        error = "You need " + std::to_string(cost) + " Scrap. You have " + std::to_string(m_scrap) + ".";
        return false;
    }

    std::string const name = item->GetProto()->Name1;
    if (guidLow == m_rerollItem)
        DiscardFoundReroll();
    found.tier = tier + 1;
    if (found.tier <= BONK_TIER_GEAR_TIERS)
    {
        uint32 const entry = BonkMgr::GetGearAtTier(item->GetEntry(), found.tier);
        if (!entry)
        {
            error = name + " can't be upgraded.";
            return false;
        }
        item = ReplaceGearEntry(item, entry);
        if (!item)
        {
            error = "You have no room to upgrade " + name + ".";
            return false;
        }
        m_player->SendNewItem(item, 1, true, false);
        SendAddon("qmupselect~" + std::to_string(guidLow) + "~" + std::to_string(item->GetGUIDLow()));
    }
    BonkFound::Remember(item, found);

    m_scrap -= cost;
    CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `scrap`) VALUES (%u, %u) ON DUPLICATE KEY UPDATE `scrap` = VALUES(`scrap`)",
        GetProgressAccount(), m_scrap);
    Notify(name + " is now " + BonkFound::DescribeTier(found.tier) + ".");
    SendQuartermaster();
    SendFoundUpgrades();
    SendFoundItems();
    SendStats();
    return true;
}

bool BonkPlayer::GiveTestFoundGear(uint32 tier, uint32 lineCount, std::string& error)
{
    uint32 const entry = BonkMgr::GetTierGearEntry(m_player->GetClass(), std::min(BONK_TIER_GEAR_TIERS, std::max(1u, tier)), urand(0, BONK_TIER_GEAR_SLOTS - 1));
    ItemPosCountVec dest;
    if (!entry || m_player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, 1) != EQUIP_ERR_OK)
    {
        error = "No room for a test piece.";
        return false;
    }
    Item* item = m_player->StoreNewItem(dest, entry, true);
    if (!item)
    {
        error = "The test piece couldn't be created.";
        return false;
    }
    BonkFound::Remember(item, BonkFound::Make(tier, lineCount));
    m_player->SendNewItem(item, 1, true, false);
    SendFoundItems();
    return true;
}

void BonkPlayer::SendFoundItems() const
{
    if (!m_addonReady)
        return;
    SendAddon("foundclear");
    auto send = [&](Item const* item, std::string const& place)
    {
        BonkFoundGear found;
        if (!item || !sBonkFound.Get(item->GetGUIDLow(), found))
            return;
        uint32 const tier = BonkFound::GetItemTier(item);
        SendAddon("founditem~" + place + "~" + std::to_string(item->GetEntry()) + "~" + std::to_string(tier) + "~" +
            BonkFound::DescribeLines(found, tier) + "~" + BonkFound::DescribeExtra(item, tier));
    };
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        send(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot), "inv:" + std::to_string(slot + 1));
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        send(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot), "0:" + std::to_string(slot - INVENTORY_SLOT_ITEM_START + 1));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag const* container = dynamic_cast<Bag const*>(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                send(container->GetItemByPos(uint8(slot)), std::to_string(bag - INVENTORY_SLOT_BAG_START + 1) + ":" + std::to_string(slot + 1));
    for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; ++slot)
        send(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot), "inv:" + std::to_string(slot + 1));
    for (uint8 bag = BANK_SLOT_BAG_START; bag < BANK_SLOT_BAG_END; ++bag)
        if (Bag const* container = dynamic_cast<Bag const*>(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                send(container->GetItemByPos(uint8(slot)), std::to_string(bag - BANK_SLOT_BAG_START + 5) + ":" + std::to_string(slot + 1));
    SendAddon("foundend");
}

void BonkPlayer::SendUpdateNotices()
{
    if (m_gearNoticePending)
        SendAddon("notice~gear");
    if (!m_legendaryPromptPending)
        return;
    std::vector<Item*> const legendaries = GetOwnedLegendaries();
    if (legendaries.empty())
    {
        ClearLegendaryPrompt();
        return;
    }
    uint32 refund = 0;
    for (Item const* item : legendaries)
        refund += BonkFound::GetScrapValue(item);
    SendAddon("oldlegendaries~" + std::to_string(legendaries.size()) + "~" + std::to_string(refund));
}

void BonkPlayer::SeeGearNotice()
{
    if (!m_gearNoticePending)
        return;
    m_gearNoticePending = false;
    CharacterDatabase.PExecute("UPDATE `bonk_account` SET `gear_notice` = 0 WHERE `account` = %u", GetProgressAccount());
}

void BonkPlayer::ClearLegendaryPrompt()
{
    m_legendaryPromptPending = false;
    CharacterDatabase.PExecute("UPDATE `bonk_character_notice` SET `old_legendaries` = 0 WHERE `guid` = %u", m_player->GetGUIDLow());
}

std::vector<Item*> BonkPlayer::GetOwnedLegendaries() const
{
    std::vector<Item*> legendaries;
    auto consider = [&](Item* item)
    {
        uint32 kind, tier;
        if (item && BonkMgr::ReadLegendaryEntry(item->GetEntry(), kind, tier))
            legendaries.push_back(item);
    };
    auto considerBag = [&](uint8 bagSlot)
    {
        if (Bag* container = dynamic_cast<Bag*>(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot)))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                consider(container->GetItemByPos(uint8(slot)));
    };
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        consider(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        consider(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        considerBag(bag);
    for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; ++slot)
        consider(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bag = BANK_SLOT_BAG_START; bag < BANK_SLOT_BAG_END; ++bag)
        considerBag(bag);
    return legendaries;
}

bool BonkPlayer::ResolveOldLegendaries(bool remove)
{
    if (!m_legendaryPromptPending || GetRun())
        return false;
    if (remove)
    {
        uint32 refund = 0;
        std::vector<std::pair<uint8, uint8>> places;
        for (Item const* item : GetOwnedLegendaries())
        {
            refund += BonkFound::GetScrapValue(item);
            places.emplace_back(item->GetBagSlot(), item->GetSlot());
        }
        for (auto const& place : places)
            m_player->DestroyItem(place.first, place.second, true);
        AddScrap(refund);
        Notify("Your old legendaries are gone. You received " + std::to_string(refund) + " Scrap.");
    }
    ClearLegendaryPrompt();
    return true;
}

std::vector<Item*> BonkPlayer::GetForeignTierPieces() const
{
    std::vector<Item*> pieces;
    auto consider = [&](Item* item)
    {
        uint8 classId;
        uint32 tier, slot;
        if (item && BonkMgr::ReadTierGearEntry(item->GetEntry(), classId, tier, slot) && classId != m_player->GetClass())
            pieces.push_back(item);
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        consider(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* container = dynamic_cast<Bag*>(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                consider(container->GetItemByPos(uint8(slot)));
    return pieces;
}

uint32 BonkPlayer::GetTierExchangeFee(std::vector<Item*> const& pieces) const
{
    uint32 fee = 0;
    for (Item* piece : pieces)
        fee += piece->GetProto()->BuyPrice * BONK_TIER_EXCHANGE_FEE_PERCENT / 100;
    return fee;
}

void BonkPlayer::ExchangeTierPieces()
{
    uint32 exchanged = 0;
    for (Item* piece : GetForeignTierPieces())
    {
        uint8 classId;
        uint32 tier, slot;
        BonkMgr::ReadTierGearEntry(piece->GetEntry(), classId, tier, slot);
        uint32 const blue = BonkMgr::GetTierGearEntry(m_player->GetClass(), tier, slot);
        uint32 const entry = BonkMgr::IsEpicGear(piece->GetEntry()) ? BonkMgr::GetEpicGearEntry(blue) : blue;
        uint32 const fee = piece->GetProto()->BuyPrice * BONK_TIER_EXCHANGE_FEE_PERCENT / 100;
        if (!entry || m_player->GetMoney() < fee)
            break;
        uint8 const bag = piece->GetBagSlot();
        uint8 const slotInBag = piece->GetSlot();
        uint32 const oldEntry = piece->GetEntry();
        BonkFoundGear found;
        bool const wasFound = sBonkFound.Get(piece->GetGUIDLow(), found);
        m_player->DestroyItem(bag, slotInBag, true);
        ItemPosCountVec dest;
        if (m_player->CanStoreNewItem(bag, slotInBag, dest, entry, 1) != EQUIP_ERR_OK)
        {
            ItemPosCountVec back;
            if (m_player->CanStoreNewItem(bag, slotInBag, back, oldEntry, 1) == EQUIP_ERR_OK)
                m_player->StoreNewItem(back, oldEntry, true);
            break;
        }
        m_player->ModifyMoney(-int32(fee));
        if (Item* refit = m_player->StoreNewItem(dest, entry, true))
        {
            if (wasFound)
                BonkFound::Remember(refit, found);
            m_player->SendNewItem(refit, 1, true, false);
        }
        ++exchanged;
    }
    Notify(exchanged ? "Refitted " + std::to_string(exchanged) + (exchanged == 1 ? " set piece" : " set pieces") + " for your class." :
        "You have no set pieces from other classes in your bags, or not enough gold.");
}

void BonkPlayer::SendGearTabs(Creature const* vendor) const
{
    if (vendor->GetEntry() != BONK_GEAR_VENDOR)
        return;
    std::vector<uint32> const tabs = GetGearTabs(vendor);
    std::string line = "geartabs~" + std::to_string(GetShownGearTab(tabs));
    if (tabs.size() >= 2)
        for (uint32 group : tabs)
        {
            BonkTier const* tier = GetGearGroupTier(group);
            line += "~" + std::to_string(group) + "~" + (tier ? tier->name : std::string("Gear"));
        }
    SendAddon(line);
}

void BonkPlayer::SendRefitOffer() const
{
    std::vector<Item*> const foreign = GetForeignTierPieces();
    SendAddon("qmrefit~" + std::to_string(foreign.size()) + "~" + std::to_string(GetTierExchangeFee(foreign)));
}

void BonkPlayer::RefitAtQuartermaster()
{
    Creature* vendor = m_player->FindNearestCreature(BONK_TIER_VENDOR, BONK_GEAR_VENDOR_RANGE);
    if (!vendor)
        return;
    ExchangeTierPieces();
    m_player->GetSession()->SendListInventory(vendor->GetObjectGuid());
}

void BonkPlayer::SelectGearTab(uint32 group)
{
    Creature* vendor = m_player->FindNearestCreature(BONK_GEAR_VENDOR, BONK_GEAR_VENDOR_RANGE);
    if (!vendor)
        return;
    std::vector<uint32> const tabs = GetGearTabs(vendor);
    if (std::find(tabs.begin(), tabs.end(), group) == tabs.end())
        return;
    m_gearTab = group;
    m_player->GetSession()->SendListInventory(vendor->GetObjectGuid());
}

bool BonkPlayer::StartRun(BonkTier const& tier, std::string& error)
{
    return StartRun(tier, error, BonkRunChoice());
}

bool BonkPlayer::StartRun(BonkTier const& tier, std::string& error, BonkRunChoice const& choice)
{
    if (m_run)
    {
        error = "You are already in a run. Use .bonk stop to leave it.";
        return false;
    }

    if (!IsTierUnlocked(tier))
    {
        error = tier.name + " is locked. " + GetUnlockHint(tier) + ".";
        return false;
    }

    if (choice.dungeon && !IsTierCleared(tier))
    {
        error = "Clear " + tier.name + " first to unlock its challenges.";
        return false;
    }

    if (choice.startFloor > 1)
    {
        std::vector<uint32> const checkpoints = GetCheckpoints(tier);
        if (choice.dungeon || std::find(checkpoints.begin(), checkpoints.end(), choice.startFloor) == checkpoints.end())
        {
            error = "You can't start on floor " + std::to_string(choice.startFloor) + " yet. Checkpoints open " +
                std::to_string(BONK_CHECKPOINT_GAP) + " floors below your best Mythic floor for this class.";
            return false;
        }
    }

    if (m_player->IsInCombat())
    {
        error = "You can't start a run while in combat.";
        return false;
    }

    if (m_player->IsTaxiFlying())
    {
        error = "You can't start a run while flying.";
        return false;
    }

    if (!IsPlayableClass(m_player->GetClass()) || !sBonkMgr.HasStarterSpell(m_player->GetClassMask()))
    {
        error = "The " + GetClassName() + " is being reworked and can't start a run yet.";
        return false;
    }

    if (!choice.groupReady && BonkCoop::RequestGroupStart(m_player, tier, choice))
        return true;

    TrimPoolToggles();
    m_run.reset(new BonkRun(m_player, tier, choice));
    if (!m_run->Begin(error))
    {
        m_run.reset();
        return false;
    }
    return true;
}

bool BonkPlayer::StartGuestRun(BonkTier const& tier, BonkRunChoice const& choice, Player* host, std::string& error)
{
    if (m_run)
    {
        error = "You are already in a run.";
        return false;
    }
    if (m_player->IsTaxiFlying() || !IsPlayableClass(m_player->GetClass()) || !sBonkMgr.HasStarterSpell(m_player->GetClassMask()))
    {
        error = "You can't start a run right now.";
        return false;
    }
    TrimPoolToggles();
    m_run.reset(new BonkRun(m_player, tier, choice));
    m_run->FollowHost(host->GetObjectGuid());
    if (!m_run->Begin(error))
    {
        m_run.reset();
        return false;
    }
    return true;
}

void BonkPlayer::StopRun()
{
    if (m_run)
        m_run->Surrender();
}

std::vector<BonkTier const*> BonkPlayer::GetTiersInOrder()
{
    std::map<uint32, BonkTier> const& tiers = sBonkMgr.GetTiers();
    std::vector<BonkTier const*> ordered;
    std::set<uint32> placed;

    for (bool added = true; added;)
    {
        added = false;
        for (auto const& itr : tiers)
        {
            BonkTier const& tier = itr.second;
            if (placed.count(tier.id) || (tier.unlockTierId && !placed.count(tier.unlockTierId)))
                continue;
            ordered.push_back(&tier);
            placed.insert(tier.id);
            added = true;
        }
    }

    for (auto const& itr : tiers)
        if (!placed.count(itr.first))
            ordered.push_back(&itr.second);
    return ordered;
}

uint32 BonkPlayer::GetBestStage(uint32 tierId) const
{
    auto itr = m_bestStages.find(tierId);
    return itr != m_bestStages.end() ? itr->second : 0;
}

bool BonkPlayer::IsTierUnlocked(BonkTier const& tier) const
{
    return !tier.unlockTierId || GetBestStage(tier.unlockTierId) >= tier.unlockStage;
}

std::string BonkPlayer::GetUnlockHint(BonkTier const& tier) const
{
    BonkTier const* required = sBonkMgr.GetTier(tier.unlockTierId);
    if (!required)
        return "";
    if (required->stages && tier.unlockStage > required->stages)
        return "Clear " + required->name;
    return "Reach floor " + std::to_string(tier.unlockStage) + " on " + required->name;
}

void BonkPlayer::RecordStage(uint32 tierId, uint32 stage)
{
    sBonkQuests.OnStage(m_player, tierId, stage);

    if (stage <= GetBestStage(tierId))
        return;

    std::vector<BonkTier const*> locked;
    for (auto const& itr : sBonkMgr.GetTiers())
        if (!IsTierUnlocked(itr.second))
            locked.push_back(&itr.second);

    m_bestStages[tierId] = stage;
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_tier` (`account`, `tier`, `best_stage`) VALUES (%u, %u, %u)",
        GetProgressAccount(), tierId, stage);

    for (BonkTier const* tier : locked)
        if (IsTierUnlocked(*tier))
            Notify(GetString(BONK_STRING_TIER_UNLOCKED, { tier->name }));

    if (m_determination)
        SendDetermination();
}

void BonkPlayer::SendAddon(std::string const& line) const
{
    if (!m_addonReady)
        return;

    ChatHandler(m_player).SendSysMessage(("BONK~" + line).c_str());
}

std::vector<BonkPlayer::BanSource> BonkPlayer::GetBanSources(BonkQuestReward type) const
{
    bool const cards = type == BONK_REWARD_CARD;
    uint32 const account = GetProgressAccount();
    std::vector<BanSource> sources;

    for (BonkQuest const* quest : sBonkQuests.GetRewardQuests(cards ? BONK_REWARD_CARD_BAN : BONK_REWARD_GLYPH_BAN))
        for (uint32 i = 0; i < quest->rewardValue; ++i)
            sources.push_back({ "Quest: " + quest->name, quest->description, sBonkQuests.IsCompleted(account, quest->id), 0 });

    uint32 const cleared = GetMythicFloorsCleared();
    for (uint32 floor : cards ? std::vector<uint32>{ 5, 10 } : std::vector<uint32>{ 15, 20 })
        sources.push_back({ "Mythic floor " + std::to_string(floor), "Clear floor " + std::to_string(floor), cleared >= floor, 0 });

    BonkTalentEffect const effect = cards ? BONK_TALENT_CARD_BANS : BONK_TALENT_GLYPH_BANS;
    for (auto const& itr : sBonkMgr.GetTalents())
    {
        BonkTalent const& talent = itr.second;
        if (talent.effect != effect)
            continue;
        uint8 const owned = GetTalentRank(talent.id);
        for (uint8 rank = 0; rank < talent.maxRank; ++rank)
            sources.push_back({ "Sayge: " + talent.name, "", rank < owned, talent.GetCost(rank) });
    }
    return sources;
}

uint32 BonkPlayer::GetBanSlots(BonkQuestReward type) const
{
    uint32 slots = 0;
    for (BanSource const& source : GetBanSources(type))
        if (source.earned)
            ++slots;
    return slots;
}

void BonkPlayer::TrimPoolToggles() const
{
    sBonkQuests.TrimDisabled(GetProgressAccount(), BONK_REWARD_CARD, GetBanSlots(BONK_REWARD_CARD));
    sBonkQuests.TrimDisabled(GetProgressAccount(), BONK_REWARD_GLYPH, GetBanSlots(BONK_REWARD_GLYPH));
}

void BonkPlayer::SendGlyphInfo() const
{
    for (auto const& itr : sBonkMgr.GetGlyphs())
    {
        BonkGlyph const& glyph = itr.second;
        SendAddon("glyphinfo~" + std::to_string(glyph.id) + "~" + Localize(BONK_FIELD_GLYPH_NAME, glyph.id, glyph.name) + "~" + glyph.icon + "~" +
            std::to_string(uint32(glyph.rarity)) + "~" + Localize(BONK_FIELD_GLYPH_DESCRIPTION, glyph.id, glyph.description) + "~" +
            Localize(BONK_FIELD_GLYPH_DRAWBACK, glyph.id, glyph.drawback));
    }
}

std::string BonkPlayer::GetString(BonkString id, std::vector<std::string> const& args) const
{
    return sBonkLocale.GetString(id, m_locale, args);
}

std::string BonkPlayer::Localize(BonkTextField field, uint32 id, std::string const& english) const
{
    return sBonkLocale.Localize(field, id, english, m_locale);
}

float BonkPlayer::GetRunDodge() const
{
    return m_run ? m_run->GetDodgeChance() : 0.0f;
}

void BonkPlayer::ResetReworkedTrinkets()
{
    uint32 const guid = m_player->GetGUIDLow();
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT 1 FROM `bonk_trinket_reset` WHERE `guid` = %u", guid));
    if (!result)
        return;
    CharacterDatabase.PExecute("DELETE FROM `bonk_trinket_reset` WHERE `guid` = %u", guid);
    for (uint8 slot : { EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 })
        m_player->AutoUnequipItemFromSlot(slot);
    Notify("Trinkets have new effects, so yours were moved to your bags. Hover them to see what they do now.");
}

void BonkPlayer::Notify(std::string const& text) const
{
    if (m_addonReady)
        SendAddon("msg~" + text);
    else
        ChatHandler(m_player).SendSysMessage(text.c_str());
}

void BonkPlayer::NotifyFind(std::string const& text) const
{
    if (m_addonReady)
        SendAddon("find~" + text);
    else
        ChatHandler(m_player).SendSysMessage(text.c_str());
}

void BonkPlayer::SendGearInfo() const
{
    uint32 const tierGearEnd = BONK_TIER_GEAR_FIRST + BONK_TIER_GEAR_CLASS_COUNT * BONK_TIER_GEAR_TIERS * BONK_TIER_GEAR_SLOTS;
    auto const isSetPiece = [tierGearEnd](uint32 entry)
    {
        uint32 const blue = BonkMgr::GetBlueGearEntry(entry);
        return blue >= BONK_TIER_GEAR_FIRST && blue < tierGearEnd;
    };
    for (auto const& itr : sBonkMgr.GetItemStats())
        if (!isSetPiece(itr.first))
            SendAddon("gear~" + std::to_string(itr.first) + "~" + std::to_string(itr.second.setId) + "~" + FormatStatList(itr.second.stats));

    SendAddon("tiergear~" + std::to_string(BONK_TIER_GEAR_FIRST) + "~" + std::to_string(BONK_TIER_GEAR_TIERS) + "~" +
        std::to_string(BONK_TIER_GEAR_SLOTS) + "~" + std::to_string(BONK_TIER_GEAR_CLASS_COUNT) + "~" + std::to_string(BONK_EPIC_GEAR_OFFSET));
    for (uint32 tier = 1; tier <= BONK_TIER_GEAR_TIERS; ++tier)
    {
        uint32 const entry = BONK_TIER_GEAR_FIRST + (tier - 1) * BONK_TIER_GEAR_SLOTS;
        if (BonkItemStat const* stat = sBonkMgr.GetItemStat(entry))
            SendAddon("tierstats~" + std::to_string(tier) + "~" + FormatStatList(stat->stats));
        if (BonkItemStat const* epic = sBonkMgr.GetItemStat(BonkMgr::GetEpicGearEntry(entry)))
            SendAddon("tierstats~" + std::to_string(tier) + "~" + FormatStatList(epic->stats) + "~epic");
    }
    for (uint8 effect = 0; effect < MAX_BONK_TRINKET_EFFECT; ++effect)
        SendAddon("trinkettip~" + std::to_string(BONK_MYTHIC_GEAR_FIRST + (BONK_MYTHIC_GEAR_FIRST_TRINKET + effect) * BONK_TIER_GEAR_TIERS) + "~" +
            std::to_string(BONK_TIER_GEAR_TIERS) + "~" + BonkMgr::GetTrinket(BonkTrinketEffect(effect)).text);

    for (auto const& itr : sBonkMgr.GetItemSets())
    {
        BonkItemSet const& set = itr.second;
        std::string pieces;
        for (uint32 entry : set.items)
            if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry))
                pieces += (pieces.empty() ? "" : ",") + std::to_string(entry) + ":" + proto->Name1;
        SendAddon("set~" + std::to_string(set.id) + "~" + set.name + "~" + pieces);

        for (BonkItemSetBonus const& bonus : set.bonuses)
            SendAddon("setbonus~" + std::to_string(set.id) + "~" + std::to_string(bonus.pieces) + "~" + FormatStatList(bonus.stats) + "~" +
                bonus.description);
    }

    SendAddon("gearend");
    SendFoundItems();
    if (m_run)
        m_run->SendEnchants();
    else
    {
        SendAddon("enchantclear");
        SendAddon("enchantend");
    }
    SendStats();
}

void BonkPlayer::SendStats() const
{
    if (m_run)
    {
        m_run->SendStats();
        return;
    }

    BonkRunBonuses const bonuses = GetRunBonuses();
    SendStatTotals(bonuses.passives, false);
}

void BonkPlayer::SendStatTotals(float const* totals, bool inRun) const
{
    std::string line = "stats~" + std::string(inRun ? "1" : "0") + "~";
    for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
    {
        float value = totals[effect];
        if (effect == BONK_PASSIVE_CRIT)
            value += BONK_BASE_CRIT;
        line += (effect ? "," : "") + FormatStat(value);
    }
    SendAddon(line);
}

void BonkPlayer::SendTierList() const
{
    SendArena("");
}

void BonkPlayer::OpenArena(Creature const* npc) const
{
    SendCheckpoints();
    SendArena(npc->GetName());
}

void BonkPlayer::SendArena(std::string const& npc) const
{
    if (m_addonReady)
    {
        BonkRunBonuses const bonuses = GetRunBonuses();

        std::string challenges;
        for (auto const& itr : sBonkMgr.GetTalents())
            if (itr.second.IsToggle() && GetTalentRank(itr.first) && IsTalentEnabled(itr.first))
                challenges += (challenges.empty() ? "" : ", ") + itr.second.name;

        std::vector<std::string> perks;
        if (bonuses.rerolls)
            perks.push_back(std::to_string(bonuses.rerolls) + (bonuses.rerolls == 1 ? " reroll" : " rerolls"));
        if (bonuses.banishes)
            perks.push_back(std::to_string(bonuses.banishes) + (bonuses.banishes == 1 ? " banish" : " banishes"));
        if (bonuses.revives)
            perks.push_back(std::to_string(bonuses.revives) + (bonuses.revives == 1 ? " revive" : " revives"));
        if (bonuses.startCoins)
            perks.push_back("+" + std::to_string(bonuses.startCoins) + " Valor");
        std::string perkText;
        for (std::string const& perk : perks)
            perkText += (perkText.empty() ? "" : ", ") + perk;

        SendAddon("arena~" + std::string(npc.empty() ? "0" : "1") + "~" + npc + "~" + std::to_string(uint32(bonuses.goldBonus + 0.5f)) + "~" +
            challenges + "~" + perkText);
    }

    if (m_addonReady)
        SendMapInfo();

    for (BonkTier const* tierPtr : GetTiersInOrder())
    {
        BonkTier const& tier = *tierPtr;
        bool const unlocked = IsTierUnlocked(tier);
        uint32 const best = GetBestStage(tier.id);
        if (m_addonReady)
        {
            std::string maps;
            for (uint32 mapId : tier.mapIds)
                if (BonkMap const* map = sBonkMgr.GetMap(mapId))
                    maps += (maps.empty() ? "" : ",") + map->icon + ":" + map->name + ":" + std::to_string(uint32(map->type)) + ":" +
                        std::to_string(GetBonkMapRewardPercent(map->type)) + ":" + map->key;
            std::string dungeons;
            bool const cleared = IsTierCleared(tier);
            for (BonkMap const* dungeon : sBonkMgr.GetDungeons())
                dungeons += (dungeons.empty() ? "" : ",") + dungeon->key + ":" + dungeon->icon + ":" + dungeon->name + ":" + (cleared ? "1" : "0");
            SendAddon("tier~" + std::to_string(tier.id) + "~" + tier.key + "~" + tier.name + "~" + (unlocked ? "1" : "0") + "~" +
                (unlocked ? "" : GetUnlockHint(tier)) + "~" + std::to_string(best) + "~" + tier.description + "~" + tier.art + "~" + maps + "~" +
                std::to_string(tier.stages) + "~" + (tier.endless ? "1" : "0") + "~" + dungeons + "~" + tier.details);
            SendFloorPreview(tier, GetRunBonuses());
        }
        else if (unlocked)
            ChatHandler(m_player).PSendSysMessage("  %s (best: floor %u): .bonk start %s", tier.name.c_str(), best, tier.key.c_str());
        else
        {
            std::string hint = GetUnlockHint(tier);
            if (!hint.empty())
                hint[0] = char(tolower(hint[0]));
            ChatHandler(m_player).PSendSysMessage("  %s (locked: %s)", tier.name.c_str(), hint.c_str());
        }
    }
    if (m_addonReady)
        SendStarters();
    SendAddon("tierend");
}

namespace
{
    std::string Percent(float value)
    {
        return std::to_string(int32(std::floor(value + 0.5f)));
    }

    std::string Chance(float value)
    {
        char text[16];
        snprintf(text, sizeof(text), "%.1f", value);
        std::string result = text;
        if (result.size() > 2 && result.compare(result.size() - 2, 2, ".0") == 0)
            result.resize(result.size() - 2);
        return result;
    }

    std::string Ordinal(uint32 value)
    {
        uint32 const tens = value % 100;
        char const* suffix = (tens >= 11 && tens <= 13) ? "th" : value % 10 == 1 ? "st" : value % 10 == 2 ? "nd" : value % 10 == 3 ? "rd" : "th";
        return std::to_string(value) + suffix;
    }

    std::string FloorChangeKind(BonkTier const& tier, uint32 floor)
    {
        if (floor < 2)
            return "";
        if (BonkRun::MilestoneAffixes(tier, floor) > BonkRun::MilestoneAffixes(tier, floor - 1))
            return "affix";
        if (BonkRun::MilestoneVeteranChance(tier, floor) > BonkRun::MilestoneVeteranChance(tier, floor - 1))
            return "veteran";
        return "";
    }

    std::string DescribeFloorChange(BonkTier const& tier, uint32 floor)
    {
        std::string const kind = FloorChangeKind(tier, floor);
        if (kind == "affix")
            return "Elites gain a " + Ordinal(tier.eliteAffixes + BonkRun::MilestoneAffixes(tier, floor)) + " affix.";
        if (kind == "veteran")
            return "+" + Percent(BonkRun::MilestoneVeteranChance(tier, floor) - BonkRun::MilestoneVeteranChance(tier, floor - 1)) +
                "% Veteran chance.";
        return "";
    }

    uint32 FloorBand(BonkTier const& tier, uint32 floor)
    {
        if (tier.floorKnee2 && floor > tier.floorKnee2)
            return 3;
        if (tier.floorKnee && floor > tier.floorKnee)
            return 2;
        return 1;
    }

    std::string DescribeFirstClear(BonkTier const& tier, uint32 floor)
    {
        if (tier.key != "mythic")
            return "";
        std::vector<std::string> rewards;
        if (floor % BONK_FLOORS_PER_TALENT_POINT == 0 && floor / BONK_FLOORS_PER_TALENT_POINT <= BONK_FLOOR_TALENT_POINTS_MAX)
            rewards.push_back("+1 talent point");
        if (uint32 const tickets = GetFloorFirstClearTickets(floor))
            rewards.push_back(FormatBonkShards(tickets));
        if (floor % BONK_TRAINING_FLOORS_PER_RANK == 0 && floor / BONK_TRAINING_FLOORS_PER_RANK <= BONK_TRAINING_MYTHIC_RANKS)
            rewards.push_back("Account Training rank cap +1");
        std::string text;
        for (size_t i = 0; i < rewards.size(); ++i)
            text += (i == 0 ? "" : i + 1 == rewards.size() ? " and " : ", ") + rewards[i];
        return text;
    }
}

void BonkPlayer::SendFloorPreview(BonkTier const& tier, BonkRunBonuses const& bonuses) const
{
    static char const* const CASTER_AIM[] = { "Poor", "Good", "Precise" };
    static char const* const BOSS_ATTACKS[] = { "Normal", "More attacks", "Relentless" };
    uint32 const step = std::min(2u, tier.eliteAffixes ? tier.eliteAffixes - 1u : 0u);
    SendAddon("tierinfo~" + std::to_string(tier.id) + "~" + CASTER_AIM[step] + "~" + BOSS_ATTACKS[step] + "~" +
        std::to_string((tier.chargeIntervalMs + 500) / 1000) + "~" + std::to_string(tier.maxAlive));
    uint32 const cleared = GetMythicFloorsCleared();
    uint32 const floors = std::min(tier.stages, BONK_MYTHIC_MILESTONE_FLOORS * 4);
    for (uint32 floor = 1; floor <= floors; ++floor)
    {
        float const health = BonkRun::TierHealthScale(tier, floor) * (1.0f + bonuses.enemyHealth / 100.0f);
        float const damage = BonkRun::TierDamageScale(tier, floor) * (1.0f + bonuses.enemyDamage / 100.0f);
        float const veterans = tier.veteranChance > 0.0f ? tier.veteranChance + BonkRun::MilestoneVeteranChance(tier, floor) : 0.0f;
        uint32 const affixes = tier.eliteAffixes + bonuses.eliteAffixes + BonkRun::MilestoneAffixes(tier, floor);
        float const gold = (BonkRun::FloorRewardScale(tier, floor) - 1.0f) * 100.0f;
        float const legendary = BonkRun::BossLegendaryChance(tier, floor);
        float const epic = (100.0f - legendary) * BonkRun::BossEpicChance(tier, floor) / 100.0f;
        bool const mythic = tier.key == "mythic";
        bool const firstCleared = tier.key == "mythic" && floor <= cleared;
        SendAddon("floor~" + std::to_string(tier.id) + "~" + std::to_string(floor) + "~" + Percent((health - 1.0f) * 100.0f) + "~" +
            Percent((damage - 1.0f) * 100.0f) + "~" + Percent(veterans) + "~" + std::to_string(affixes) + "~" + Percent(gold) + "~" +
            (mythic ? Chance(legendary) : "") + "~" + DescribeFloorChange(tier, floor) + "~" + DescribeFirstClear(tier, floor) + "~" + (firstCleared ? "1" : "0") + "~" +
            std::to_string(FloorBand(tier, floor)) + "~" + FloorChangeKind(tier, floor) + "~" +
            (mythic ? Chance(epic) + "~" + Chance(100.0f - legendary - epic) : "~"));
    }
}

namespace
{
    std::string MapFraction(float value)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%.3f", std::max(0.0f, std::min(1.0f, value)));
        return text;
    }

    void PlayAreaCenter(BonkMap const& map, float& x, float& y)
    {
        std::vector<BonkMapStart> const starts = sBonkMgr.GetMapStarts(map.id);
        x = map.x;
        y = map.y;
        for (BonkMapStart const& start : starts)
        {
            x += start.x;
            y += start.y;
        }
        x /= float(starts.size() + 1);
        y /= float(starts.size() + 1);
    }
}

void BonkPlayer::SendZoneArt() const
{
    if (m_zoneArtSent)
        return;
    m_zoneArtSent = true;
    for (auto const& itr : sBonkMgr.GetZoneArts())
        for (BonkZoneOverlay const& overlay : itr.second.overlays)
            SendAddon("zoneart~" + itr.second.file + "~" + overlay.texture + "~" + std::to_string(overlay.width) + "~" +
                std::to_string(overlay.height) + "~" + std::to_string(overlay.x) + "~" + std::to_string(overlay.y));
}

void BonkPlayer::SendMapInfo() const
{
    SendZoneArt();
    for (auto const& itr : sBonkMgr.GetMaps())
    {
        BonkMap const& map = itr.second;
        std::string art = "~~~";
        if (BonkZoneArt const* zone = map.type == BONK_MAP_OPEN ? sBonkMgr.GetZoneArt(map.zoneId) : nullptr)
        {
            float x, y;
            PlayAreaCenter(map, x, y);
            float const u = (zone->left - y) / (zone->left - zone->right);
            float const v = (zone->top - x) / (zone->top - zone->bottom);
            art = "~" + zone->file + "~" + MapFraction(u) + "~" + MapFraction(v);
        }
        SendAddon("mapinfo~" + map.key + "~" + std::to_string(map.mapId) + "~" + map.summary + "~" + map.description + art);
    }
}

void BonkPlayer::ResetCharacter()
{
    if (!m_player->IsAlive())
    {
        m_player->ResurrectPlayer(1.0f);
        m_player->SpawnCorpseBones();
    }

    m_player->CombatStop();

    if (m_player->GetLevel() != 1)
        m_player->GiveLevel(1);
    m_player->SetUInt32Value(PLAYER_XP, 0);
    m_player->InitTalentForLevel();

    m_player->SetHealth(m_player->GetMaxHealth());
    m_player->SetPower(POWER_MANA, m_player->GetMaxPower(POWER_MANA));
}

void BonkPlayer::ClearAuras()
{
    std::set<uint32> spells;
    for (auto const& itr : m_player->GetSpellAuraHolderMap())
        if (!itr.second->IsPassive() && itr.second->GetCastItemGuid().IsEmpty())
            spells.insert(itr.second->GetId());
    for (uint32 spellId : spells)
        m_player->RemoveAurasDueToSpell(spellId);
}

bool BonkPlayer::IsAtCamp() const
{
    return m_player->GetMapId() == BONK_CAMP_LOCATION.mapId &&
        m_player->IsWithinDist2d(BONK_CAMP_LOCATION.x, BONK_CAMP_LOCATION.y, BONK_CAMP_RADIUS);
}

void BonkPlayer::NotePacket(uint16 opcode, size_t bytes)
{
    if (opcode < PACKET_KINDS)
        m_packetCounts[opcode].fetch_add(1, std::memory_order_relaxed);
    m_packetBytes.fetch_add(uint32(bytes), std::memory_order_relaxed);
}

BonkPlayer::PacketCounts BonkPlayer::TakePacketCounts()
{
    PacketCounts counts;
    counts.bytes = m_packetBytes.exchange(0, std::memory_order_relaxed);
    for (uint16 opcode = 0; opcode < PACKET_KINDS; ++opcode)
        if (uint32 const count = m_packetCounts[opcode].exchange(0, std::memory_order_relaxed))
        {
            counts.packets += count;
            counts.top.emplace_back(opcode, count);
        }
    std::sort(counts.top.begin(), counts.top.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
    if (counts.top.size() > BONK_PACKET_TOP)
        counts.top.resize(BONK_PACKET_TOP);
    return counts;
}

WorldLocation const& BonkPlayer::GetCampArrival() const
{
    return BONK_CAMP_SEATS[BonkCoop::CampSeat(m_player)].arrival;
}

void BonkPlayer::TeleportToCamp()
{
    m_player->TeleportTo(GetCampArrival());
    m_sitAtCampMs = BONK_CAMP_SIT_DELAY_MS;
    m_mailboxRefreshMs = BONK_MAILBOX_REFRESH_DELAY_MS;
}

void BonkPlayer::UpdateMailboxRefresh(uint32 diff)
{
    if (!m_mailboxRefreshMs || !m_player->IsInWorld() || m_player->IsBeingTeleported())
        return;
    if (diff < m_mailboxRefreshMs)
    {
        m_mailboxRefreshMs -= diff;
        return;
    }
    m_mailboxRefreshMs = 0;
    if (!m_run && IsAtCamp())
        RefreshMailboxBeacon();
}

void BonkPlayer::RefreshMailboxBeacon()
{
    m_mailboxGlowShown = HasUnreadMail();
    Creature* beacon = m_player->FindNearestCreature(BONK_MAILBOX_BEACON, BONK_MAILBOX_GLOW_RANGE);
    if (!beacon)
        return;
    if (m_mailboxGlowShown && m_player->IsInVisibleList(beacon))
    {
        beacon->DestroyForPlayer(m_player);
        std::unique_lock<std::shared_timed_mutex> lock(m_player->m_visibleGUIDs_lock);
        m_player->m_visibleGUIDs.erase(beacon->GetObjectGuid());
    }
    m_player->UpdateVisibilityOf(m_player, beacon);
}

void BonkPlayer::UpdateCampSeat(uint32 diff)
{
    if (!m_sitAtCampMs || !m_player->IsInWorld() || m_player->IsBeingTeleported())
        return;
    if (diff < m_sitAtCampMs)
    {
        m_sitAtCampMs -= diff;
        return;
    }
    m_sitAtCampMs = 0;
    SitAtCamp();
}

void BonkPlayer::UpdateReviveOutsideRun(uint32 diff)
{
    if (m_run || m_player->IsAlive() || !m_player->IsInWorld() || m_player->IsBeingTeleported())
    {
        m_deadOutsideRunMs = 0;
        return;
    }
    m_deadOutsideRunMs += diff;
    if (m_deadOutsideRunMs < BONK_REVIVE_OUTSIDE_RUN_MS)
        return;
    m_deadOutsideRunMs = 0;
    m_player->ResurrectPlayer(1.0f);
    m_player->SpawnCorpseBones();
    TeleportToCamp();
    Notify("You were brought back to the Inn.");
}

void BonkPlayer::SitAtCamp()
{
    if (m_run || !m_player->IsAlive() || !IsAtCamp())
        return;

    GameObject* chair = m_player->FindNearestGameObject(BONK_CAMP_SEATS[BonkCoop::CampSeat(m_player)].chair, BONK_CAMP_CHAIR_RANGE);
    if (!chair)
        return;
    for (auto const& ref : m_player->GetMap()->GetPlayers())
    {
        Player const* other = ref.getSource();
        if (other && other != m_player && other->GetStandState() != UNIT_STAND_STATE_STAND &&
            other->GetDistance2d(chair) <= BONK_CAMP_CHAIR_TAKEN)
            return;
    }
    chair->Use(m_player);
}

void BonkPlayer::UpdateCampMailNotice(uint32 diff)
{
    if (!m_campMailNoticeMs || m_run || m_player->IsBeingTeleported() || !IsAtCamp())
        return;

    if (diff < m_campMailNoticeMs)
    {
        m_campMailNoticeMs -= diff;
        return;
    }
    m_campMailNoticeMs = 0;
    Notify("You got mail!");
}

bool BonkPlayer::HasUnreadMail() const
{
    MasterPlayer const* master = m_player->GetSession()->GetMasterPlayer();
    return master && master->HasUnreadMail();
}

bool BonkPlayer::IsHiddenFrom(Creature const* creature, Player const* viewer)
{
    return creature->GetEntry() == BONK_MAILBOX_BEACON && !viewer->GetBonkPlayer()->HasUnreadMail();
}

void BonkPlayer::UpdateMailboxGlow(uint32 diff)
{
    m_mailboxGlowMs += diff;
    if (m_mailboxGlowMs < BONK_MAILBOX_CHECK_MS)
        return;
    m_mailboxGlowMs = 0;

    bool const unread = HasUnreadMail();
    if (unread == m_mailboxGlowShown || m_run || m_player->IsBeingTeleported() || !IsAtCamp())
        return;
    m_mailboxGlowShown = unread;
    if (Creature* beacon = m_player->FindNearestCreature(BONK_MAILBOX_BEACON, BONK_MAILBOX_GLOW_RANGE))
        m_player->UpdateVisibilityOf(m_player, beacon);
}

bool BonkPlayer::MoveToCampBeforeLogin(bool gameMaster)
{
    if (gameMaster || (IsAtCamp() && !BonkCoop::IsGuestAccount(m_player)))
        return false;

    WorldLocation const& arrival = GetCampArrival();
    m_player->SetLocationMapId(arrival.mapId);
    m_player->Relocate(arrival.x, arrival.y, arrival.z, arrival.o);
    m_sitAtCampMs = BONK_CAMP_SIT_DELAY_MS;
    return true;
}
