#include "BonkCoop.h"

#include "AccountMgr.h"
#include "BonkFoundGear.h"
#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "Item.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    char const* const INBOX_FILE = "coop/ledger.in";
    char const* const INBOX_ANSWER_FILE = "coop/ledger.answer";
    size_t const MAX_GAME_LENGTH = 64;
    uint32 const GUEST_SYNC_MS = 5000;
    uint32 const HOME_MAIL_SENDER = 900009;

    struct GuestTotals
    {
        uint32 gold = 0;
        uint32 shards = 0;
        uint32 scrap = 0;
        uint32 determination = 0;
        uint32 masteryXp = 0;
        uint32 runs = 0;
        uint32 bestLevel = 0;
        uint32 bestSeconds = 0;
        uint32 kills = 0;
        uint32 goldEarned = 0;
        std::map<uint32, std::pair<uint32, bool>> quests;
    };

    struct HomeTarget
    {
        uint32 accountId = 0;
        uint32 characterGuid = 0;
    };

    std::mutex s_guestLock;
    std::map<ObjectGuid, GuestTotals> s_guests;
    uint32 s_guestTimer = 0;

    std::mutex s_outboxLock;
    std::map<std::string, uint32> s_lastSeq;

    std::string OutboxPath(std::string const& username)
    {
        return "coop/ledger-" + username + ".out";
    }

    std::string AnswersPath(std::string const& username)
    {
        return "coop/ledger-" + username + ".answers";
    }

    std::vector<std::string> SplitTabs(std::string const& line)
    {
        std::vector<std::string> fields;
        std::string field;
        for (char c : line)
        {
            if (c == '\t')
            {
                fields.push_back(field);
                field.clear();
            }
            else if (c != '\r')
                field += c;
        }
        fields.push_back(field);
        return fields;
    }

    uint32 LastSeqInFile(std::string const& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::string line;
        uint32 last = 0;
        while (std::getline(in, line))
            last = std::max(last, uint32(std::strtoul(line.c_str(), nullptr, 10)));
        return last;
    }

    bool ReadCount(std::vector<std::string> const& fields, size_t index, uint32& count)
    {
        if (fields.size() <= index || fields[index].empty() || fields[index].find_first_not_of("0123456789") != std::string::npos)
            return false;
        count = uint32(std::strtoul(fields[index].c_str(), nullptr, 10));
        return true;
    }

    bool ReadSigned(std::vector<std::string> const& fields, size_t index, int64& value)
    {
        if (fields.size() <= index || fields[index].empty())
            return false;
        std::string const& text = fields[index];
        size_t const start = text[0] == '-' ? 1 : 0;
        if (start == text.size() || text.find_first_not_of("0123456789", start) != std::string::npos)
            return false;
        value = std::strtoll(text.c_str(), nullptr, 10);
        return true;
    }

    std::string Escape(std::string const& value)
    {
        std::string out;
        for (char c : value)
        {
            if (c == '\\')
                out += "\\\\";
            else if (c == '\t')
                out += "\\t";
            else if (c == '\n')
                out += "\\n";
            else if (c != '\r')
                out += c;
        }
        return out;
    }

    std::string Unescape(std::string const& value)
    {
        std::string out;
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] == '\\' && i + 1 < value.size())
            {
                char const next = value[++i];
                out += next == 't' ? '\t' : next == 'n' ? '\n' : next;
            }
            else
                out += value[i];
        }
        return out;
    }

    std::string ApplyAccountColumn(HomeTarget const& home, std::vector<std::string> const& fields, char const* column, int64 cap)
    {
        int64 delta = 0;
        if (!ReadSigned(fields, 2, delta))
            return std::string("broken ") + column + " entry";
        CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `%s`) VALUES (%u, %u) "
            "ON DUPLICATE KEY UPDATE `%s` = LEAST(%lld, GREATEST(0, CAST(`%s` AS SIGNED) + %lld))",
            column, home.accountId, uint32(std::max<int64>(0, std::min<int64>(cap, delta))), column, (long long)cap, column, (long long)delta);
        return "";
    }

    std::string ApplyMastery(HomeTarget const& home, std::vector<std::string> const& fields)
    {
        uint32 classId = 0;
        int64 delta = 0;
        if (!ReadCount(fields, 2, classId) || !ReadSigned(fields, 3, delta))
            return "broken mastery entry";
        CharacterDatabase.PExecute("INSERT INTO `bonk_account_mastery` (`account`, `class`, `xp`) VALUES (%u, %u, %u) "
            "ON DUPLICATE KEY UPDATE `xp` = GREATEST(0, CAST(`xp` AS SIGNED) + %lld)",
            home.accountId, classId, uint32(std::max<int64>(0, delta)), (long long)delta);
        return "";
    }

    std::string ApplyQuest(HomeTarget const& home, std::vector<std::string> const& fields)
    {
        uint32 quest = 0;
        uint32 progress = 0;
        uint32 completed = 0;
        if (!ReadCount(fields, 2, quest) || !ReadCount(fields, 3, progress) || !ReadCount(fields, 4, completed))
            return "broken quest entry";
        CharacterDatabase.PExecute("INSERT INTO `bonk_account_quest` (`account`, `quest`, `progress`, `completed`) VALUES (%u, %u, %u, %u) "
            "ON DUPLICATE KEY UPDATE `progress` = GREATEST(`progress`, VALUES(`progress`)), `completed` = GREATEST(`completed`, VALUES(`completed`))",
            home.accountId, quest, progress, completed ? 1u : 0u);
        return "";
    }

    std::string ApplyRunStats(HomeTarget const& home, std::vector<std::string> const& fields)
    {
        uint32 values[5] = {};
        for (size_t i = 0; i < 5; ++i)
            if (!ReadCount(fields, 2 + i, values[i]))
                return "broken runstats entry";
        if (!home.characterGuid)
            return "no home character";
        CharacterDatabase.PExecute("INSERT INTO `bonk_character_stats` (`guid`, `runs`, `best_level`, `best_time_sec`, `kills`, `gold_earned`) "
            "VALUES (%u, %u, %u, %u, %u, %u) ON DUPLICATE KEY UPDATE `runs` = `runs` + VALUES(`runs`), `best_level` = GREATEST(`best_level`, VALUES(`best_level`)), "
            "`best_time_sec` = GREATEST(`best_time_sec`, VALUES(`best_time_sec`)), `kills` = `kills` + VALUES(`kills`), `gold_earned` = `gold_earned` + VALUES(`gold_earned`)",
            home.characterGuid, values[0], values[1], values[2], values[3], values[4]);
        return "";
    }

    bool ReadFound(std::string const& text, BonkFoundGear& found)
    {
        std::vector<std::string> parts;
        std::string part;
        for (char c : text)
        {
            if (c == ',')
            {
                parts.push_back(part);
                part.clear();
            }
            else
                part += c;
        }
        parts.push_back(part);
        if (parts.size() < 3)
            return false;
        found.tier = uint32(std::strtoul(parts[0].c_str(), nullptr, 10));
        found.rerolls = uint32(std::strtoul(parts[1].c_str(), nullptr, 10));
        found.rerollLine = uint8(std::strtoul(parts[2].c_str(), nullptr, 10));
        for (size_t i = 3; i + 1 < parts.size() && found.lines.size() < BONK_FOUND_MAX_LINES; i += 2)
        {
            BonkFoundLine line;
            line.effect = uint8(std::strtoul(parts[i].c_str(), nullptr, 10));
            line.roll = float(std::strtod(parts[i + 1].c_str(), nullptr));
            found.lines.push_back(line);
        }
        return found.tier > 0;
    }

    std::string WriteFound(BonkFoundGear const& found)
    {
        std::ostringstream out;
        out << found.tier << ',' << found.rerolls << ',' << uint32(found.rerollLine);
        for (BonkFoundLine const& line : found.lines)
            out << ',' << uint32(line.effect) << ',' << line.roll;
        return out.str();
    }

    std::string ApplyMail(HomeTarget const& home, std::vector<std::string> const& fields)
    {
        uint32 entry = 0;
        if (!ReadCount(fields, 2, entry) || fields.size() < 6)
            return "broken mail entry";
        if (!home.characterGuid)
            return "no home character";
        if (!sObjectMgr.GetItemPrototype(entry))
            return "unknown item " + std::to_string(entry);
        ObjectGuid const receiver(HIGHGUID_PLAYER, home.characterGuid);
        Item* item = Item::CreateItem(entry, 1, receiver);
        if (!item)
            return "item not created";
        item->SaveToDB();
        BonkFoundGear found;
        if (fields[5] != "-" && ReadFound(fields[5], found))
            BonkFound::Remember(item, found);
        MailDraft(Unescape(fields[3]), Unescape(fields[4]))
            .AddItem(item)
            .SendMailTo(MailReceiver(receiver), MailSender(MAIL_CREATURE, HOME_MAIL_SENDER));
        return "";
    }

    std::string ApplyEntry(HomeTarget const& home, std::vector<std::string> const& fields)
    {
        std::string const& kind = fields[1];
        if (kind == "gold")
            return ApplyAccountColumn(home, fields, "gold", 0xFFFFFFFFll);
        if (kind == "shards")
            return ApplyAccountColumn(home, fields, "shards", 0xFFFFFFFFll);
        if (kind == "scrap")
            return ApplyAccountColumn(home, fields, "scrap", 0xFFFFFFFFll);
        if (kind == "determination")
            return ApplyAccountColumn(home, fields, "determination", BONK_DETERMINATION_MAX_STACKS);
        if (kind == "mastery")
            return ApplyMastery(home, fields);
        if (kind == "quest")
            return ApplyQuest(home, fields);
        if (kind == "runstats")
            return ApplyRunStats(home, fields);
        if (kind == "mail")
            return ApplyMail(home, fields);
        return "unknown entry " + kind;
    }

    GuestTotals ReadTotals(Player const* guest)
    {
        BonkPlayer const* bonk = guest->GetBonkPlayer();
        GuestTotals totals;
        totals.gold = guest->GetMoney();
        totals.shards = bonk->GetShards();
        totals.scrap = bonk->GetScrap();
        totals.determination = bonk->GetDeterminationStacks();
        totals.masteryXp = bonk->GetMasteryXp();
        bonk->GetRunRecord(totals.runs, totals.bestLevel, totals.bestSeconds, totals.kills, totals.goldEarned);
        totals.quests = sBonkQuests.GetAllProgress(bonk->GetProgressAccount());
        return totals;
    }

    void RecordDelta(Player const* guest, char const* kind, uint32 before, uint32 now)
    {
        if (before != now)
            BonkCoop::Record(guest, std::string(kind) + "\t" + std::to_string(int64(now) - int64(before)));
    }

    void RecordChanges(Player const* guest, GuestTotals const& before, GuestTotals const& now)
    {
        RecordDelta(guest, "gold", before.gold, now.gold);
        RecordDelta(guest, "shards", before.shards, now.shards);
        RecordDelta(guest, "scrap", before.scrap, now.scrap);
        RecordDelta(guest, "determination", before.determination, now.determination);
        if (before.masteryXp != now.masteryXp)
            BonkCoop::Record(guest, "mastery\t" + std::to_string(uint32(guest->GetClass())) + "\t" + std::to_string(int64(now.masteryXp) - int64(before.masteryXp)));
        if (now.runs != before.runs || now.kills != before.kills || now.goldEarned != before.goldEarned || now.bestLevel != before.bestLevel || now.bestSeconds != before.bestSeconds)
            BonkCoop::Record(guest, "runstats\t" + std::to_string(now.runs - std::min(now.runs, before.runs)) + "\t" + std::to_string(now.bestLevel) + "\t" +
                std::to_string(now.bestSeconds) + "\t" + std::to_string(now.kills - std::min(now.kills, before.kills)) + "\t" +
                std::to_string(now.goldEarned - std::min(now.goldEarned, before.goldEarned)));
        for (auto const& quest : now.quests)
        {
            auto const old = before.quests.find(quest.first);
            if (old != before.quests.end() && old->second == quest.second)
                continue;
            BonkCoop::Record(guest, "quest\t" + std::to_string(quest.first) + "\t" + std::to_string(quest.second.first) + "\t" + (quest.second.second ? "1" : "0"));
        }
    }

    HomeTarget FindHome(std::string const& username, std::string const& character)
    {
        HomeTarget home;
        home.accountId = sAccountMgr.GetId(username);
        if (!home.accountId || character.empty())
            return home;
        std::string name = character;
        CharacterDatabase.escape_string(name);
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `guid` FROM `characters` WHERE `name` = '%s' AND `account` = %u", name.c_str(), home.accountId));
        if (result)
            home.characterGuid = result->Fetch()[0].GetUInt32();
        return home;
    }

    bool AlreadyApplied(uint32 accountId, std::string const& game, uint32 seq)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT 1 FROM `bonk_coop_applied` WHERE `account` = %u AND `game` = '%s' AND `seq` = %u", accountId, game.c_str(), seq));
        return result != nullptr;
    }

    std::string ApplyLine(HomeTarget const& home, std::string const& game, std::string const& line)
    {
        uint32 const accountId = home.accountId;
        std::vector<std::string> const fields = SplitTabs(line);
        uint32 seq = 0;
        if (fields.size() < 2 || !ReadCount(fields, 0, seq) || seq == 0)
            return "";
        std::string const prefix = std::to_string(seq) + "\t";
        if (AlreadyApplied(accountId, game, seq))
            return prefix + "ok\n";
        CharacterDatabase.BeginTransaction();
        std::string const problem = ApplyEntry(home, fields);
        if (!problem.empty())
        {
            CharacterDatabase.RollbackTransaction();
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op ledger: account %u refused entry %u (%s)", accountId, seq, problem.c_str());
            return prefix + "refused\t" + problem + "\n";
        }
        CharacterDatabase.PExecute("INSERT INTO `bonk_coop_applied` (`account`, `game`, `seq`) VALUES (%u, '%s', %u)", accountId, game.c_str(), seq);
        if (!CharacterDatabase.CommitTransactionDirect())
            return prefix + "retry\n";
        return prefix + "ok\n";
    }

    void WriteWhole(std::string const& path, std::string const& text)
    {
        std::string const temporary = path + ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << text;
        }
        std::remove(path.c_str());
        std::rename(temporary.c_str(), path.c_str());
    }

    std::string ReadWhole(std::string const& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    bool IsSafeGame(std::string const& game)
    {
        return !game.empty() && game.size() <= MAX_GAME_LENGTH && game.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") == std::string::npos;
    }

    std::string ApplyInbox(std::string const& text)
    {
        std::istringstream in(text);
        std::string header;
        std::getline(in, header);
        std::vector<std::string> const from = SplitTabs(header);
        if (from.size() < 3 || from[0] != "from" || !IsSafeGame(from[2]))
            return "error broken ledger\n";
        HomeTarget const home = FindHome(from[1], from.size() >= 4 ? from[3] : "");
        if (!home.accountId)
            return "error There is no account called " + from[1] + ".\n";
        std::string answers;
        std::string line;
        while (std::getline(in, line))
            if (!line.empty())
                answers += ApplyLine(home, from[2], line);
        return answers;
    }

    void ReadAnswers(std::string const& username)
    {
        std::string const path = AnswersPath(username);
        std::ifstream probe(path, std::ios::binary);
        if (!probe.good())
            return;
        probe.close();
        std::istringstream in(ReadWhole(path));
        std::remove(path.c_str());
        std::string line;
        while (std::getline(in, line))
        {
            std::vector<std::string> const fields = SplitTabs(line);
            if (fields.size() >= 2 && fields[1] == "refused")
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op ledger: %s's home refused entry %s (%s)", username.c_str(), fields[0].c_str(),
                    fields.size() >= 3 ? fields[2].c_str() : "");
        }
    }
}

void BonkCoop::Record(Player const* guest, std::string const& entry)
{
    if (!IsGuestAccount(guest))
        return;
    std::string const username = guest->GetSession()->GetUsername();
    std::string const path = OutboxPath(username);
    std::lock_guard<std::mutex> guard(s_outboxLock);
    auto found = s_lastSeq.find(username);
    uint32 const seq = (found == s_lastSeq.end() ? LastSeqInFile(path) : found->second) + 1;
    s_lastSeq[username] = seq;
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << seq << '\t' << entry << '\n';
}

void BonkCoop::SyncGuest(Player* guest)
{
    if (!IsGuestAccount(guest) || !guest->GetBonkPlayer() || !guest->GetBonkPlayer()->IsGoldLoaded())
        return;
    GuestTotals const now = ReadTotals(guest);
    GuestTotals before;
    {
        std::lock_guard<std::mutex> guard(s_guestLock);
        auto found = s_guests.find(guest->GetObjectGuid());
        if (found == s_guests.end())
        {
            s_guests[guest->GetObjectGuid()] = now;
            return;
        }
        before = found->second;
        found->second = now;
    }
    RecordChanges(guest, before, now);
}

void BonkCoop::ForgetGuest(Player const* guest)
{
    std::lock_guard<std::mutex> guard(s_guestLock);
    s_guests.erase(guest->GetObjectGuid());
}

void BonkCoop::RecordMail(Player const* guest, uint32 entry, std::string const& subject, std::string const& body, BonkFoundGear const* found)
{
    if (!IsGuestAccount(guest))
        return;
    Record(guest, "mail\t" + std::to_string(entry) + "\t" + Escape(subject) + "\t" + Escape(body) + "\t" + (found ? WriteFound(*found) : std::string("-")));
}

void BonkCoop::UpdateLedger()
{
    std::ifstream probe(INBOX_FILE, std::ios::binary);
    if (probe.good())
    {
        probe.close();
        std::string const text = ReadWhole(INBOX_FILE);
        std::remove(INBOX_FILE);
        WriteWhole(INBOX_ANSWER_FILE, ApplyInbox(text));
    }
    s_guestTimer += 1000;
    bool const syncGuests = s_guestTimer >= GUEST_SYNC_MS;
    if (syncGuests)
        s_guestTimer = 0;
    for (auto const& entry : sObjectAccessor.GetPlayers())
    {
        Player* player = entry.second;
        if (!player || !IsGuestAccount(player))
            continue;
        if (syncGuests && player->IsInWorld())
            SyncGuest(player);
        ReadAnswers(player->GetSession()->GetUsername());
    }
}
