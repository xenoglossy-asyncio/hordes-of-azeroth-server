#include "BonkLeaderboard.h"
#include "BonkPlayer.h"
#include "GameObject.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <fstream>
#include <string>

namespace
{
    char const* const LEADERBOARD_FILE = "leaderboard.txt";
    size_t const MAX_LEADERBOARD_LINES = 400;
    size_t const MAX_LINE_LENGTH = 200;

    bool IsBoardLine(std::string const& line)
    {
        if (line.empty() || line.size() > MAX_LINE_LENGTH || line.find('|') != std::string::npos)
            return false;
        for (char c : line)
            if (static_cast<unsigned char>(c) < 0x20)
                return false;
        return line.compare(0, 8, "updated~") == 0 || line.compare(0, 6, "level~") == 0 || line.compare(0, 7, "finale~") == 0;
    }
}

namespace BonkLeaderboard
{
    void Send(Player* player)
    {
        BonkPlayer* bonk = player->GetBonkPlayer();
        bonk->SendAddon("boardopen");
        std::ifstream file(LEADERBOARD_FILE);
        std::string line;
        size_t sent = 0;
        while (file && sent < MAX_LEADERBOARD_LINES && std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!IsBoardLine(line))
                continue;
            bonk->SendAddon("board~" + line);
            ++sent;
        }
        bonk->SendAddon("boardend");
    }
}

bool GOHello_go_bonk_board(Player* player, GameObject* /*go*/)
{
    if (player->GetBonkPlayer()->GetRun())
        return true;
    BonkLeaderboard::Send(player);
    return true;
}

void AddSC_bonk_leaderboard()
{
    Script* newscript = new Script;
    newscript->Name = "go_bonk_board";
    newscript->pGOHello = &GOHello_go_bonk_board;
    newscript->RegisterSelf();
}
