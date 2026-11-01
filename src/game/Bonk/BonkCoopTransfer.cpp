#include "BonkCoop.h"

#include "AccountMgr.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerDump.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    char const* const REQUEST_FILE = "coop/transfer.req";
    char const* const ANSWER_FILE = "coop/transfer.done";
    char const* const DUMP_FILE = "coop/transfer.dump";
    char const* const ACCOUNT_FILE = "coop/transfer.account";
    uint32 const SCAN_INTERVAL_MS = 1000;
    size_t const MAX_NAME_LENGTH = 12;

    char const* const ACCOUNT_TABLES[] = {
        "bonk_account", "bonk_account_disabled", "bonk_account_board", "bonk_account_epic_luck", "bonk_account_kept_talents", "bonk_account_level", "bonk_account_level_best", "bonk_account_mastery",
        "bonk_account_mastery_node", "bonk_account_mastery_preset", "bonk_account_mastery_preset_active", "bonk_account_quest",
        "bonk_account_starter", "bonk_account_talent", "bonk_account_unlock", "bonk_account_upgrade",
    };

    uint32 const SAVE_SETTLE_MS = 2000;
    uint32 const DELETE_WAIT_MS = 20000;

    uint32 s_scanTimer = 0;

    struct Pending
    {
        bool active = false;
        bool import = false;
        std::string username;
        std::string name;
        uint32 accountId = 0;
        uint32 guid = 0;
        uint32 waitedMs = 0;
    };

    Pending s_pending;

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
            else
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

    std::vector<std::string> SplitTabs(std::string const& line)
    {
        std::vector<std::string> fields;
        std::string field;
        for (size_t i = 0; i < line.size(); ++i)
        {
            if (line[i] == '\\' && i + 1 < line.size())
            {
                field += line[i];
                field += line[++i];
            }
            else if (line[i] == '\t')
            {
                fields.push_back(field);
                field.clear();
            }
            else
                field += line[i];
        }
        fields.push_back(field);
        return fields;
    }

    bool IsAccountTable(std::string const& table)
    {
        for (char const* name : ACCOUNT_TABLES)
            if (table == name)
                return true;
        return false;
    }

    std::string ExportAccountRows(uint32 accountId)
    {
        std::ostringstream out;
        for (char const* table : ACCOUNT_TABLES)
        {
            std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT * FROM `%s` WHERE `account` = %u", table, accountId));
            if (!result)
                continue;
            uint32 const columns = result->GetFieldCount();
            do
            {
                Field* fields = result->Fetch();
                out << table;
                for (uint32 i = 1; i < columns; ++i)
                    out << '\t' << (fields[i].IsNULL() ? std::string("\\N") : Escape(fields[i].GetCppString()));
                out << '\n';
            } while (result->NextRow());
        }
        return out.str();
    }

    bool ImportAccountRows(std::string const& text, uint32 accountId)
    {
        std::istringstream in(text);
        std::string line;
        std::vector<std::string> statements;
        while (std::getline(in, line))
        {
            if (line.empty())
                continue;
            std::vector<std::string> fields = SplitTabs(line);
            if (!IsAccountTable(fields[0]))
                return false;
            std::string values = std::to_string(accountId);
            for (size_t i = 1; i < fields.size(); ++i)
            {
                if (fields[i] == "\\N")
                {
                    values += ",NULL";
                    continue;
                }
                std::string value = Unescape(fields[i]);
                CharacterDatabase.escape_string(value);
                values += ",'" + value + "'";
            }
            statements.push_back("INSERT INTO `" + fields[0] + "` VALUES (" + values + ")");
        }
        CharacterDatabase.BeginTransaction();
        for (char const* table : ACCOUNT_TABLES)
            CharacterDatabase.PExecute("DELETE FROM `%s` WHERE `account` = %u", table, accountId);
        for (std::string const& statement : statements)
            CharacterDatabase.Execute(statement.c_str());
        return CharacterDatabase.CommitTransaction();
    }

    std::string ReadFile(std::string const& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    void WriteFile(std::string const& path, std::string const& text)
    {
        std::string const temporary = path + ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << text;
        }
        std::remove(path.c_str());
        std::rename(temporary.c_str(), path.c_str());
    }

    uint32 AccountOfCharacter(uint32 guid)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `account` FROM `characters` WHERE `guid` = %u", guid));
        return result ? result->Fetch()[0].GetUInt32() : 0;
    }

    bool IsIronman(uint32 guid)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT 1 FROM `bonk_ironman` WHERE `guid` = %u", guid));
        return result != nullptr;
    }

    uint32 GuidOfName(std::string name)
    {
        CharacterDatabase.escape_string(name);
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `guid` FROM `characters` WHERE `name` = '%s'", name.c_str()));
        return result ? result->Fetch()[0].GetUInt32() : 0;
    }

    std::string FreeName(std::string const& wanted)
    {
        if (!GuidOfName(wanted))
            return wanted;
        std::string base = wanted.substr(0, MAX_NAME_LENGTH - 1);
        for (char suffix = 'a'; suffix <= 'z'; ++suffix)
        {
            std::string candidate = base + suffix;
            if (!GuidOfName(candidate))
                return candidate;
        }
        return "";
    }

    void DiscardHeldBank(uint32 guid)
    {
        std::string bagList;
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `item_guid` FROM `character_inventory` WHERE `guid` = %u AND `bag` = 0 AND `slot` >= %u AND `slot` < %u",
            guid, uint32(BANK_SLOT_BAG_START), uint32(BANK_SLOT_BAG_END)));
        if (result)
        {
            do
                bagList += (bagList.empty() ? "" : ",") + std::to_string(result->Fetch()[0].GetUInt32());
            while (result->NextRow());
        }
        std::string itemList;
        result = CharacterDatabase.PQuery(
            "SELECT `item_guid` FROM `character_inventory` WHERE `guid` = %u AND ((`bag` = 0 AND `slot` >= %u AND `slot` < %u)%s%s%s)",
            guid, uint32(BANK_SLOT_ITEM_START), uint32(BANK_SLOT_BAG_END),
            bagList.empty() ? "" : " OR `bag` IN (", bagList.c_str(), bagList.empty() ? "" : ")");
        if (result)
        {
            do
                itemList += (itemList.empty() ? "" : ",") + std::to_string(result->Fetch()[0].GetUInt32());
            while (result->NextRow());
        }
        if (itemList.empty())
            return;
        CharacterDatabase.DirectPExecute("DELETE FROM `item_instance` WHERE `guid` IN (%s)", itemList.c_str());
        CharacterDatabase.DirectPExecute("DELETE FROM `character_inventory` WHERE `item_guid` IN (%s)", itemList.c_str());
    }

    std::vector<std::string> DumpValues(std::string const& line)
    {
        std::vector<std::string> values;
        size_t start = line.find("VALUES (");
        if (start == std::string::npos)
            return values;
        start += 8;
        while (start < line.size() && line[start] == '\'')
        {
            size_t const end = line.find('\'', start + 1);
            if (end == std::string::npos)
                break;
            values.push_back(line.substr(start + 1, end - start - 1));
            start = end + 3;
        }
        return values;
    }

    bool IsDumpOf(std::string const& line, char const* table)
    {
        return line.compare(0, 13 + strlen(table) + 1, std::string("INSERT INTO `") + table + "`") == 0;
    }

    std::string StripBank(std::string const& dump)
    {
        std::set<std::string> bankBags;
        std::set<std::string> bankItems;
        std::istringstream first(dump);
        std::string line;
        std::vector<std::vector<std::string>> inventory;
        while (std::getline(first, line))
        {
            if (!IsDumpOf(line, "character_inventory"))
                continue;
            std::vector<std::string> const values = DumpValues(line);
            if (values.size() < 4 || values[1] != "0")
                continue;
            uint32 const slot = uint32(std::strtoul(values[2].c_str(), nullptr, 10));
            if (slot >= BANK_SLOT_ITEM_START && slot < BANK_SLOT_BAG_END)
                bankItems.insert(values[3]);
            if (slot >= BANK_SLOT_BAG_START && slot < BANK_SLOT_BAG_END)
                bankBags.insert(values[3]);
        }
        std::istringstream second(dump);
        while (std::getline(second, line))
        {
            if (!IsDumpOf(line, "character_inventory"))
                continue;
            std::vector<std::string> const values = DumpValues(line);
            if (values.size() >= 4 && bankBags.count(values[1]))
                bankItems.insert(values[3]);
        }
        std::ostringstream kept;
        std::istringstream third(dump);
        while (std::getline(third, line))
        {
            std::vector<std::string> const values = DumpValues(line);
            bool drop = false;
            if (IsDumpOf(line, "character_inventory"))
                drop = values.size() >= 4 && bankItems.count(values[3]);
            else if (IsDumpOf(line, "item_instance") || IsDumpOf(line, "bonk_item_found"))
                drop = !values.empty() && bankItems.count(values[0]);
            if (!drop)
                kept << line << '\n';
        }
        return kept.str();
    }

    bool CharacterRowExists(uint32 guid)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT 1 FROM `characters` WHERE `guid` = %u", guid));
        return result != nullptr;
    }

    std::string WriteExport(uint32 low)
    {
        uint32 const accountId = AccountOfCharacter(low);
        PlayerDumpWriter writer;
        WriteFile(DUMP_FILE, writer.GetDump(low));
        WriteFile(ACCOUNT_FILE, ExportAccountRows(accountId));
        return "ok " + std::to_string(low) + " " + std::to_string(accountId);
    }

    std::string Export(std::string const& name)
    {
        uint32 const low = GuidOfName(name);
        if (!low)
            return "error There is no character called " + name + ".";
        if (IsIronman(low))
            return "error Ironman characters play solo only.";
        if (Player* online = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, low)))
        {
            online->SaveToDB();
            s_pending = Pending();
            s_pending.active = true;
            s_pending.guid = low;
            return "";
        }
        return WriteExport(low);
    }

    std::string Load(uint32 accountId, std::string const& name, uint32 guid)
    {
        WriteFile(DUMP_FILE, StripBank(ReadFile(DUMP_FILE)));
        PlayerDumpReader reader;
        DumpReturn const loaded = reader.LoadDump(DUMP_FILE, accountId, name, guid);
        if (loaded != DUMP_SUCCESS)
            return "error The character couldn't be loaded (" + std::to_string(uint32(loaded)) + ").";
        if (!ImportAccountRows(ReadFile(ACCOUNT_FILE), accountId))
            return "error The account progress couldn't be loaded.";
        return "ok 0 " + name;
    }

    std::string Import(std::string const& username, std::string const& name, uint32 replaceGuid)
    {
        uint32 const accountId = sAccountMgr.GetId(username);
        if (!accountId)
            return "error There is no account called " + username + ".";
        if (!replaceGuid)
        {
            std::string const useName = FreeName(name);
            if (useName.empty())
                return "error No free name for " + name + ".";
            return Load(accountId, useName, 0);
        }
        ObjectGuid const old = ObjectGuid(HIGHGUID_PLAYER, replaceGuid);
        if (sObjectMgr.GetPlayer(old))
            return "error That character is online.";
        if (CharacterRowExists(replaceGuid))
        {
            if (AccountOfCharacter(replaceGuid) != accountId)
                return "error That character belongs to another account.";
            DiscardHeldBank(replaceGuid);
            Player::DeleteFromDB(old, accountId, true, true);
        }
        s_pending = Pending();
        s_pending.active = true;
        s_pending.import = true;
        s_pending.accountId = accountId;
        s_pending.name = name;
        s_pending.guid = replaceGuid;
        return "";
    }

    std::string ContinuePending(uint32 diff)
    {
        s_pending.waitedMs += diff;
        if (!s_pending.import)
        {
            if (s_pending.waitedMs < SAVE_SETTLE_MS)
                return "";
            s_pending.active = false;
            return WriteExport(s_pending.guid);
        }
        if (CharacterRowExists(s_pending.guid) || GuidOfName(s_pending.name))
        {
            if (s_pending.waitedMs < DELETE_WAIT_MS)
                return "";
            s_pending.active = false;
            return "error The old copy of the character couldn't be removed.";
        }
        s_pending.active = false;
        return Load(s_pending.accountId, s_pending.name, 0);
    }

    std::string Handle(std::string const& request)
    {
        std::istringstream in(request);
        std::string verb;
        in >> verb;
        if (verb == "export")
        {
            std::string name;
            in >> name;
            return Export(name);
        }
        if (verb == "import")
        {
            std::string username, name;
            uint32 replaceGuid = 0;
            in >> username >> name >> replaceGuid;
            return Import(username, name, replaceGuid);
        }
        return "error Unknown request.";
    }
}

void BonkCoop::Update(uint32 diff)
{
    UpdateReadyChecks(diff);
    UpdateLeash(diff);
    UpdateDowned(diff);
    UpdateSharedPause(diff);
    s_scanTimer += diff;
    if (s_scanTimer < SCAN_INTERVAL_MS)
        return;
    uint32 const elapsed = s_scanTimer;
    s_scanTimer = 0;
    UpdateLedger();

    if (s_pending.active)
    {
        std::string const answer = ContinuePending(elapsed);
        if (!answer.empty())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op transfer: %s", answer.c_str());
            WriteFile(ANSWER_FILE, answer);
        }
        return;
    }

    std::ifstream probe(REQUEST_FILE, std::ios::binary);
    if (!probe.good())
        return;
    probe.close();
    std::string const request = ReadFile(REQUEST_FILE);
    std::remove(REQUEST_FILE);
    std::string const answer = Handle(request);
    if (answer.empty())
        return;
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Co-op transfer: %s", answer.c_str());
    WriteFile(ANSWER_FILE, answer);
}
