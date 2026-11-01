#include "BonkCoop.h"

#include "AccountMgr.h"
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

    std::string ApplyShards(uint32 accountId, std::vector<std::string> const& fields)
    {
        uint32 count = 0;
        if (!ReadCount(fields, 2, count))
            return "broken shards entry";
        CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `shards`) VALUES (%u, %u) "
            "ON DUPLICATE KEY UPDATE `shards` = `shards` + VALUES(`shards`)", accountId, count);
        return "";
    }

    std::string ApplyEntry(uint32 accountId, std::vector<std::string> const& fields)
    {
        std::string const& kind = fields[1];
        if (kind == "shards")
            return ApplyShards(accountId, fields);
        return "unknown entry " + kind;
    }

    bool AlreadyApplied(uint32 accountId, std::string const& game, uint32 seq)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT 1 FROM `bonk_coop_applied` WHERE `account` = %u AND `game` = '%s' AND `seq` = %u", accountId, game.c_str(), seq));
        return result != nullptr;
    }

    std::string ApplyLine(uint32 accountId, std::string const& game, std::string const& line)
    {
        std::vector<std::string> const fields = SplitTabs(line);
        uint32 seq = 0;
        if (fields.size() < 2 || !ReadCount(fields, 0, seq) || seq == 0)
            return "";
        std::string const prefix = std::to_string(seq) + "\t";
        if (AlreadyApplied(accountId, game, seq))
            return prefix + "ok\n";
        CharacterDatabase.BeginTransaction();
        std::string const problem = ApplyEntry(accountId, fields);
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
        uint32 const accountId = sAccountMgr.GetId(from[1]);
        if (!accountId)
            return "error There is no account called " + from[1] + ".\n";
        std::string answers;
        std::string line;
        while (std::getline(in, line))
            if (!line.empty())
                answers += ApplyLine(accountId, from[2], line);
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
    for (auto const& entry : sObjectAccessor.GetPlayers())
        if (Player* player = entry.second)
            if (IsGuestAccount(player))
                ReadAnswers(player->GetSession()->GetUsername());
}
