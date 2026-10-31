#include "BonkRun.h"
#include "BonkEnchant.h"
#include "BonkFoundGear.h"
#include "BonkPlayer.h"
#include "BonkStrings.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"

namespace
{
    float const BONK_ENCHANTER_RANGE        = 10.0f;
    float const BONK_ENCHANT_PRICE          = 0.75f;
    float const BONK_ENCHANT_PRICE_GROWTH   = 0.5f;
}

uint32 BonkRun::GetEnchantPrice() const
{
    return std::max(1u, uint32(GetChestPrice() * BONK_ENCHANT_PRICE * (1.0f + BONK_ENCHANT_PRICE_GROWTH * m_enchantRolls) + 0.5f));
}

void BonkRun::GetPieceStats(uint8 slot, float* stats) const
{
    std::fill(stats, stats + MAX_BONK_PASSIVE_EFFECT, 0.0f);
    Item const* item = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!item)
        return;
    if (BonkItemStat const* stat = sBonkMgr.GetItemStat(item->GetEntry()))
        std::copy(std::begin(stat->stats), std::end(stat->stats), stats);
    BonkFound::AddWornStats(item, stats);
}

std::string BonkRun::DescribeEnchants() const
{
    std::string text;
    float pieceStats[MAX_BONK_PASSIVE_EFFECT];
    for (auto const& itr : m_enchants)
        if (itr.second.IsSet())
        {
            GetPieceStats(itr.first, pieceStats);
            char entry[48];
            snprintf(entry, sizeof(entry), "%u:%u:%u:%.1f", uint32(itr.first), uint32(itr.second.effect), uint32(itr.second.grade),
                BonkEnchanting::GetValue(itr.second, pieceStats));
            text += (text.empty() ? "" : ",") + std::string(entry);
        }
    return text;
}

void BonkRun::AddEnchantStats(float* passives) const
{
    float pieceStats[MAX_BONK_PASSIVE_EFFECT];
    for (auto const& itr : m_enchants)
        if (itr.second.IsSet())
        {
            GetPieceStats(itr.first, pieceStats);
            passives[itr.second.effect] += BonkEnchanting::GetValue(itr.second, pieceStats);
        }
}

bool BonkRun::IsNearEnchanter() const
{
    if (m_enchanterGuid.IsEmpty() || (m_state != STATE_RUNNING && m_state != STATE_PORTAL))
        return false;
    Creature const* enchanter = m_player->GetMap()->GetCreature(m_enchanterGuid);
    return enchanter && m_player->IsWithinDistInMap(enchanter, BONK_ENCHANTER_RANGE);
}

void BonkRun::PlaceEnchanter(float x, float y, float z, float facing)
{
    DespawnEnchanter();
    m_enchantRolls = 0;
    m_enchantOffers.clear();
    float const ground = m_player->GetMap()->GetHeight(x, y, z + 5.0f, true, 10.0f);
    if (ground > INVALID_HEIGHT)
        z = ground;
    if (Creature* enchanter = m_player->SummonCreature(BONK_ENCHANTER_ENTRY, x, y, z, facing, TEMPSUMMON_MANUAL_DESPAWN, 0))
        m_enchanterGuid = enchanter->GetObjectGuid();
}

void BonkRun::DespawnEnchanter()
{
    if (!m_enchanterGuid.IsEmpty())
        if (Map* map = m_player->FindMap())
            if (Creature* enchanter = map->GetCreature(m_enchanterGuid))
                enchanter->DespawnOrUnsummon();
    m_enchanterGuid.Clear();
    m_enchantOffers.clear();
    if (m_player->IsInWorld())
        m_player->GetBonkPlayer()->SendAddon("enchantclose");
}

void BonkRun::OpenEnchanter()
{
    SendEnchants();
    m_player->GetBonkPlayer()->SendAddon("enchantopen");
}

bool BonkRun::RollEnchant(uint8 slot, std::string& error)
{
    if (!IsNearEnchanter())
    {
        error = GetString(BONK_STRING_TALK_TO_ENCHANTER);
        return false;
    }
    if (!BonkEnchanting::IsEnchantSlot(slot))
    {
        error = GetString(BONK_STRING_SLOT_CANT_BE_ENCHANTED);
        return false;
    }
    if (!m_enchantOffers.empty())
    {
        error = GetString(BONK_STRING_PICK_ENCHANT_FIRST);
        return false;
    }
    uint32 const price = GetEnchantPrice();
    if (m_coins < price)
    {
        error = GetString(BONK_STRING_ENCHANT_COSTS_VALOR_YOU_HAVE, { std::to_string(price), std::to_string(m_coins) });
        return false;
    }
    m_coins -= price;
    m_valorOnEnchants += price;
    ++m_enchantRolls;
    m_enchantOfferSlot = slot;
    float pieceStats[MAX_BONK_PASSIVE_EFFECT];
    GetPieceStats(slot, pieceStats);
    for (uint8 index = 0; index < BonkEnchanting::OFFER_COUNT; ++index)
        m_enchantOffers.push_back(BonkEnchanting::Roll(pieceStats));
    SendState();
    SendEnchants();
    return true;
}

bool BonkRun::PickEnchant(uint8 index, std::string& error)
{
    if (index >= m_enchantOffers.size())
    {
        error = GetString(BONK_STRING_NO_ENCHANT_TO_PICK);
        return false;
    }
    BonkEnchant const picked = m_enchantOffers[index];
    m_enchants[m_enchantOfferSlot] = picked;
    m_enchantOffers.clear();
    UpdateGear();
    m_player->GetBonkPlayer()->SendGearInfo();
    m_player->GetBonkPlayer()->Notify(BONK_STRING_ENCHANT_APPLIED, { GetString(BonkEnchanting::GetGradeName(picked.grade)) });
    return true;
}

void BonkRun::DiscardEnchantOffers()
{
    m_enchantOffers.clear();
    SendEnchants();
}

void BonkRun::SendEnchants() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (!bonk || !bonk->IsAddonReady())
        return;
    bonk->SendAddon("enchantclear");
    bonk->SendAddon("enchantinfo~" + std::to_string(GetEnchantPrice()) + "~" + std::to_string(m_coins));
    float pieceStats[MAX_BONK_PASSIVE_EFFECT];
    uint8 const* slots = BonkEnchanting::GetSlots();
    for (uint8 index = 0; index < BonkEnchanting::GetSlotCount(); ++index)
    {
        uint8 const slot = slots[index];
        auto itr = m_enchants.find(slot);
        BonkEnchant const enchant = itr != m_enchants.end() ? itr->second : BonkEnchant();
        GetPieceStats(slot, pieceStats);
        bonk->SendAddon("enchantslot~" + std::to_string(slot) + "~" + BonkEnchanting::Describe(enchant, pieceStats));
    }
    GetPieceStats(m_enchantOfferSlot, pieceStats);
    for (uint8 index = 0; index < m_enchantOffers.size(); ++index)
        bonk->SendAddon("enchantoffer~" + std::to_string(m_enchantOfferSlot) + "~" + std::to_string(index) + "~" +
            BonkEnchanting::Describe(m_enchantOffers[index], pieceStats));
    bonk->SendAddon("enchantend");
}
