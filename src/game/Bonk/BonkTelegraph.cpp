#include "BonkTelegraph.h"
#include "BonkMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "Map.h"
#include "MotionMaster.h"
#include "NullCreatureAI.h"
#include "Player.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    float const BONK_STANDIN_SIZES[] = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 5.5f, 6.0f, 6.5f, 7.0f, 7.5f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f };
    uint32 const BONK_STANDIN_SIZE_COUNT = sizeof(BONK_STANDIN_SIZES) / sizeof(BONK_STANDIN_SIZES[0]);
    float const BONK_LINE_HALF_WIDTH = 1.5f;
    float const BONK_LINE_SHORTEST = 1.0f;
    float const BONK_LINE_LONGEST = 72.0f;

    uint32 const BONK_STANDIN_INVISIBLE   = 11686;
    uint32 const BONK_STANDIN_LOOK_FADE_MS = 3500;
    uint32 const BONK_STANDIN_LIFETIME_MS = 60 * MINUTE * IN_MILLISECONDS;

    float const BONK_TELEGRAPH_MAX_RISE = 1.0f;
    float const BONK_TELEGRAPH_LIFT     = 0.05f;

    float const BONK_CONE_HALF_ANGLE = M_PI_F / 4.0f;
    float const BONK_TELEGRAPH_GROUND_ABOVE = 8.0f;
    float const BONK_TELEGRAPH_GROUND_SEARCH = 20.0f;

    uint32 const BONK_TELEGRAPH_OUTLINE_ONLY = 1;
    uint32 const BONK_TELEGRAPH_GROUND_EFFECT = 4;
    uint32 const BONK_KIT_RAISED = 1;

    char const* const BONK_TELEGRAPH_SHAPE_NAMES[MAX_BONK_TELEGRAPH_SHAPE] = { "circle", "cone", "line" };

    uint32 s_nextFlightId = 0;
    uint32 s_nextAuraKey = 0;

    float GetLineLength(float size)
    {
        return std::min(BONK_LINE_LONGEST, std::max(BONK_LINE_SHORTEST, size));
    }

    float GetGround(Map* map, float x, float y, float z)
    {
        float const ground = map->GetHeight(x, y, z + BONK_TELEGRAPH_GROUND_ABOVE, true, BONK_TELEGRAPH_GROUND_SEARCH);
        return ground > INVALID_HEIGHT ? ground : z;
    }

    float GetGroundTop(Map* map, float x, float y, float z, float reach)
    {
        float const middle = GetGround(map, x, y, z);
        float top = middle;
        for (uint32 i = 0; i < 8; ++i)
        {
            float const angle = i * M_PI_F / 4.0f;
            top = std::max(top, GetGround(map, x + reach * std::cos(angle), y + reach * std::sin(angle), middle));
        }
        return std::min(top, middle + BONK_TELEGRAPH_MAX_RISE) + BONK_TELEGRAPH_LIFT;
    }

    float GetTelegraphGround(Map* map, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size)
    {
        switch (shape)
        {
            case BONK_TELEGRAPH_CONE:
                return GetGroundTop(map, x + size / 2.0f * std::cos(orientation), y + size / 2.0f * std::sin(orientation), z, size / 2.0f);
            case BONK_TELEGRAPH_LINE:
                return GetGround(map, x, y, z) + BONK_TELEGRAPH_LIFT;
            default:
                return GetGroundTop(map, x, y, z, size * 0.8f);
        }
    }

    void InstallStandInAI(Creature* creature)
    {
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
        creature->SetDisplayId(BONK_STANDIN_INVISIBLE);
        creature->SetFactionTemplateId(35);
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
    }

    std::string FormatPoint(float x, float y, float z)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.2f~%.2f~%.2f", x, y, z);
        return buffer;
    }

    std::string FormatFloat(float value)
    {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%.3f", value);
        return buffer;
    }
}

void SendBonkDraw(Player* player, std::string const& line)
{
    if (player && player->GetSession())
        ChatHandler(player).SendSysMessage(("HOA~" + line).c_str());
}

void SendBonkKitAt(Player* player, uint32 kit, float x, float y, float z, float orientation, uint32 holdMs, bool raised)
{
    if (!kit)
        return;
    SendBonkDraw(player, "kit~" + std::to_string(kit) + "~" + FormatPoint(x, y, z) + "~" + FormatFloat(orientation) + "~" +
        std::to_string(holdMs) + (raised ? "~" + std::to_string(BONK_KIT_RAISED) : ""));
}

void SendBonkFlight(Player* player, uint32 id, float fromX, float fromY, float fromZ, float toX, float toY, float toZ, uint32 flightMs,
    uint32 display, float scale)
{
    SendBonkDraw(player, "fly~" + std::to_string(id) + "~" + FormatPoint(fromX, fromY, fromZ) + "~" + FormatPoint(toX, toY, toZ) + "~" +
        std::to_string(flightMs) + "~" + std::to_string(display) + "~" + FormatFloat(scale));
}

void SendBonkLanding(Player* player, uint32 id)
{
    SendBonkDraw(player, "land~" + std::to_string(id));
}

void SendBonkMissile(Player* player, uint32 spellId, float fromX, float fromY, float fromZ, float toX, float toY, float toZ, uint32 holdMs)
{
    SendBonkDraw(player, "msl~" + std::to_string(spellId) + "~" + FormatPoint(fromX, fromY, fromZ) + "~" + FormatPoint(toX, toY, toZ) + "~" +
        std::to_string(holdMs));
}

void SendBonkMark(Player* player, std::string const& key, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size,
    uint32 durationMs, bool outlineOnly, uint8 style, ObjectGuid const& follow, std::string const& texture, uint32 doneMs, bool groundEffect)
{
    if (Map* map = player ? player->FindMap() : nullptr)
        z = GetTelegraphGround(map, shape, x, y, z, orientation, size);
    SendBonkDraw(player, "mark~" + key + "~" + std::to_string(uint32(shape)) + "~" + FormatPoint(x, y, z) + "~" + FormatFloat(orientation) + "~" +
        FormatFloat(size) + "~" + std::to_string(durationMs) + "~" + std::to_string((outlineOnly ? BONK_TELEGRAPH_OUTLINE_ONLY : 0) | (groundEffect ? BONK_TELEGRAPH_GROUND_EFFECT : 0)) + "~" +
        std::to_string(uint32(style)) + "~" + std::to_string(follow.GetRawValue()) + "~" + texture + "~" + std::to_string(doneMs));
}

void SendBonkUnmark(Player* player, std::string const& key)
{
    SendBonkDraw(player, "markx~" + key);
}

void SendBonkAuraAt(Player* player, uint32 key, uint32 spellId, float x, float y, float z, float scale)
{
    SendBonkDraw(player, "aura~" + std::to_string(key) + "~" + std::to_string(spellId) + "~" + FormatPoint(x, y, z) + "~" + FormatFloat(scale));
}

void SendBonkAuraGone(Player* player, uint32 key)
{
    SendBonkDraw(player, "aurax~" + std::to_string(key));
}

void SendBonkProp(Player* player, uint32 key, uint32 display, float x, float y, float z, float orientation, float scale)
{
    SendBonkDraw(player, "prop~" + std::to_string(key) + "~" + std::to_string(display) + "~" + FormatPoint(x, y, z) + "~" + FormatFloat(orientation) +
        "~" + FormatFloat(scale));
}

void SendBonkPropGone(Player* player, uint32 key)
{
    SendBonkDraw(player, "propx~" + std::to_string(key));
}

uint32 NextBonkAuraKey()
{
    return ++s_nextAuraKey;
}

uint32 NextBonkFlightId()
{
    return ++s_nextFlightId;
}

float GetBonkTelegraphGround(Map* map, float x, float y, float z, float reach)
{
    return GetGroundTop(map, x, y, z, reach);
}

char const* GetBonkTelegraphShapeName(BonkTelegraphShape shape)
{
    return shape < MAX_BONK_TELEGRAPH_SHAPE ? BONK_TELEGRAPH_SHAPE_NAMES[shape] : "";
}

bool ParseBonkTelegraphShape(std::string const& name, BonkTelegraphShape& shape)
{
    for (uint8 i = 0; i < MAX_BONK_TELEGRAPH_SHAPE; ++i)
        if (name == BONK_TELEGRAPH_SHAPE_NAMES[i])
        {
            shape = BonkTelegraphShape(i);
            return true;
        }
    return false;
}

uint32 GetBonkTelegraphLeadMs(BonkTelegraphShape)
{
    return 0;
}

uint32 GetBonkStandInSizeCount()
{
    return BONK_STANDIN_SIZE_COUNT;
}

uint32 GetBonkStandInSizeIndex(float size)
{
    uint32 nearest = 0;
    for (uint32 i = 1; i < BONK_STANDIN_SIZE_COUNT; ++i)
        if (std::fabs(BONK_STANDIN_SIZES[i] - size) < std::fabs(BONK_STANDIN_SIZES[nearest] - size))
            nearest = i;
    return nearest;
}

bool IsInBonkTelegraph(BonkTelegraphShape shape, float originX, float originY, float orientation, float size, float x, float y)
{
    float const dx = x - originX, dy = y - originY;
    float const distance = std::sqrt(dx * dx + dy * dy);
    switch (shape)
    {
        case BONK_TELEGRAPH_CONE:
        {
            if (distance > size)
                return false;
            if (distance < 0.01f)
                return true;
            float diff = std::fabs(std::atan2(dy, dx) - orientation);
            while (diff > M_PI_F)
                diff = std::fabs(diff - 2.0f * M_PI_F);
            return diff <= BONK_CONE_HALF_ANGLE;
        }
        case BONK_TELEGRAPH_LINE:
        {
            float const length = GetLineLength(size);
            float const along = dx * std::cos(orientation) + dy * std::sin(orientation);
            float const across = -dx * std::sin(orientation) + dy * std::cos(orientation);
            return along >= 0.0f && along <= length && std::fabs(across) <= BONK_LINE_HALF_WIDTH;
        }
        default:
            return distance <= size;
    }
}

void BonkTelegraphs::Add(Player* owner, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
    std::function<void()> onFire, bool lingers, ObjectGuid const& caster, uint32 windId, BonkTelegraphStyle style)
{
    if (shape >= MAX_BONK_TELEGRAPH_SHAPE || size <= 0.0f || !owner->IsInWorld())
        return;

    Telegraph telegraph;
    telegraph.id = ++m_nextId;
    telegraph.leftMs = int32(durationMs);
    telegraph.onFire = std::move(onFire);
    telegraph.caster = caster;
    telegraph.windId = windId;

    float const ground = GetTelegraphGround(owner->GetMap(), shape, x, y, z, orientation, size);
    SendBonkDraw(owner, "tg~" + std::to_string(telegraph.id) + "~" + std::to_string(uint32(shape)) + "~" + FormatPoint(x, y, ground) + "~" +
        FormatFloat(orientation) + "~" + FormatFloat(size) + "~" + std::to_string(durationMs) + "~" +
        std::to_string(lingers ? BONK_TELEGRAPH_OUTLINE_ONLY : 0) + "~" + std::to_string(uint32(style)));
    m_telegraphs.push_back(std::move(telegraph));
}

void BonkTelegraphs::Update(Player* owner, uint32 diff)
{
    if (!owner->IsInWorld() || owner->IsBeingTeleported())
        return;

    m_clockMs += diff;
    Tend(owner);

    if (m_telegraphs.empty())
        return;

    std::vector<Telegraph> due;
    for (auto itr = m_telegraphs.begin(); itr != m_telegraphs.end();)
    {
        itr->leftMs -= int32(diff);
        if (itr->leftMs > 0)
        {
            ++itr;
            continue;
        }
        due.push_back(std::move(*itr));
        itr = m_telegraphs.erase(itr);
    }

    for (Telegraph& telegraph : due)
        if (telegraph.onFire)
            telegraph.onFire();
}

void BonkTelegraphs::Clear(Player* owner)
{
    if (!m_telegraphs.empty())
        SendBonkDraw(owner, "tgc");
    m_telegraphs.clear();
}

void BonkTelegraphs::CancelFrom(Player* owner, ObjectGuid const& caster, uint32 windId)
{
    if (caster.IsEmpty())
        return;

    m_telegraphs.erase(std::remove_if(m_telegraphs.begin(), m_telegraphs.end(), [&](Telegraph const& telegraph)
    {
        if (telegraph.caster != caster || (windId && telegraph.windId != windId))
            return false;
        TakeOff(owner, telegraph);
        return true;
    }), m_telegraphs.end());
}

void BonkTelegraphs::Dismiss(Player* owner)
{
    Clear(owner);
    SendBonkDraw(owner, "clr");
    if (Map* map = owner->FindMap())
        for (StandIn const& standIn : m_standIns)
            if (Creature* creature = map->GetCreature(standIn.guid))
                creature->DespawnOrUnsummon();
    m_standIns.clear();
}

void BonkTelegraphs::TakeOff(Player* owner, Telegraph const& telegraph)
{
    SendBonkDraw(owner, "tgx~" + std::to_string(telegraph.id));
}

size_t BonkTelegraphs::CountBusyStandIns() const
{
    return std::count_if(m_standIns.begin(), m_standIns.end(), [](StandIn const& standIn) { return standIn.busy; });
}

ObjectGuid BonkTelegraphs::Borrow(Player* owner, float x, float y, float z, uint32 display)
{
    if (!owner->IsInWorld())
        return ObjectGuid();
    Creature* creature = owner->SummonCreature(BONK_PICKUP_MARKER, x, y, z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, BONK_STANDIN_LIFETIME_MS,
        false, 0, &InstallStandInAI);
    if (!creature)
        return ObjectGuid();
    if (display != BONK_STANDIN_INVISIBLE)
        creature->SetDisplayId(display);
    StandIn standIn;
    standIn.guid = creature->GetObjectGuid();
    standIn.baseScale = creature->GetObjectScale();
    m_standIns.push_back(standIn);
    return standIn.guid;
}

ObjectGuid BonkTelegraphs::BorrowInvisible(Player* owner, float x, float y, float z)
{
    return Borrow(owner, x, y, z, BONK_STANDIN_INVISIBLE);
}

void BonkTelegraphs::Dress(Player* owner, ObjectGuid const& guid, uint32 display)
{
    if (guid.IsEmpty())
        return;
    if (Creature* creature = owner->GetMap()->GetCreature(guid))
        creature->SetDisplayId(display);
}

void BonkTelegraphs::Resize(Player* owner, ObjectGuid const& guid, float stretch)
{
    Creature* creature = guid.IsEmpty() ? nullptr : owner->GetMap()->GetCreature(guid);
    if (!creature)
        return;
    for (StandIn const& standIn : m_standIns)
        if (standIn.guid == guid)
            creature->SetObjectScale(standIn.baseScale * stretch);
}

void BonkTelegraphs::Hide(Player* owner, ObjectGuid const& guid)
{
    Dress(owner, guid, BONK_STANDIN_INVISIBLE);
}

void BonkTelegraphs::Release(Player* owner, ObjectGuid const& guid)
{
    for (StandIn& standIn : m_standIns)
        if (standIn.guid == guid && standIn.busy)
        {
            Hide(owner, guid);
            standIn.busy = false;
            standIn.goneAtMs = m_clockMs + BONK_STANDIN_LOOK_FADE_MS;
            return;
        }
}

void BonkTelegraphs::Tend(Player* owner)
{
    Map* map = owner->GetMap();
    if (map->GetId() != m_mapId || map->GetInstanceId() != m_instanceId)
    {
        m_standIns.clear();
        m_mapId = map->GetId();
        m_instanceId = map->GetInstanceId();
    }
    m_standIns.erase(std::remove_if(m_standIns.begin(), m_standIns.end(), [this, map](StandIn const& standIn)
    {
        Creature* creature = map->GetCreature(standIn.guid);
        if (!creature)
            return true;
        if (standIn.busy || standIn.goneAtMs > m_clockMs)
            return false;
        creature->DespawnOrUnsummon();
        return true;
    }), m_standIns.end());
}
