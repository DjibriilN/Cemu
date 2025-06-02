#include "DiscordPresence.h"

#ifdef ENABLE_DISCORD_RPC

#include <discord_rpc.h>
#include "Common/version.h"
#include "Cafe/TitleList/TitleList.h"
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/wfstream.h>
#include <wx/imagpng.h>
#include <filesystem>
#include "util/helpers/helpers.h"
#include <wx/mstream.h>
#include "Cafe/CafeSystem.h"

namespace fs = std::filesystem;

static std::string ExportCurrentGameIconAsPng(uint64_t titleId) {
    TitleInfo titleInfo;
    if (!CafeTitleList::GetFirstByTitleId(titleId, titleInfo))
        return {};
    std::string tempMountPath = TitleInfo::GetUniqueTempMountingPath();
    if (!titleInfo.Mount(tempMountPath, "", FSC_PRIORITY_BASE))
        return {};
    auto tgaData = fsc_extractFile((tempMountPath + "/meta/iconTex.tga").c_str());
    if (!tgaData) {
        tgaData = fsc_extractFile((tempMountPath + "/meta/iconTex.tga.gz").c_str());
        if (tgaData) {
            auto decompressed = zlibDecompress(*tgaData, 70 * 1024);
            tgaData = std::move(decompressed);
        }
    }
    std::string outPath;
    if (tgaData && tgaData->size() > 16) {
        wxMemoryInputStream tmp_stream(tgaData->data(), tgaData->size());
        wxImage image(tmp_stream);
        if (image.IsOk()) {
            fs::path iconDir = ActiveSettings::GetUserDataPath("discord_icons");
            if (!fs::exists(iconDir)) fs::create_directories(iconDir);
            outPath = (iconDir / fmt::format("{:016x}.png", titleId)).string();
            wxFileOutputStream pngFileStream(outPath);
            wxPNGHandler pngHandler;
            pngHandler.SaveFile(&image, pngFileStream, false);
        }
    }
    titleInfo.Unmount(tempMountPath);
    return outPath;
}

DiscordPresence::DiscordPresence()
{
	DiscordEventHandlers handlers{};
	Discord_Initialize("460807638964371468", &handlers, 1, nullptr);
	UpdatePresence(Idling);
}

DiscordPresence::~DiscordPresence()
{
	ClearPresence();
	Discord_Shutdown();
}

void DiscordPresence::UpdatePresence(State state, const std::string& text) const
{
    DiscordRichPresence discord_presence{};

    std::string state_string, details_string;
    std::string largeImageKey = "logo_icon_big_png";
    std::string largeImageText = BUILD_VERSION_WITH_NAME_STRING;

    switch (state)
    {
    case Idling:
        details_string = "Idling";
        break;
    case Playing:
        details_string = "Ingame";
        state_string = "Playing " + text;
        {
            uint64_t titleId = CafeSystem::GetForegroundTitleId();
            std::string iconPath = ExportCurrentGameIconAsPng(titleId);
            if (!iconPath.empty()) {
                // Discord RPC: largeImageKey muss ein Key sein, der bei Discord hochgeladen wurde.
                // Workaround: Wir nutzen den Dateinamen als Key, falls das Asset im Discord-Dev-Portal hinterlegt ist.
                // Alternativ: Immer logo_icon_big_png, aber largeImageText auf Spielnamen setzen.
                largeImageKey = fmt::format("gameicon_{:016x}", titleId);
                TitleInfo titleInfo;
                if (CafeTitleList::GetFirstByTitleId(titleId, titleInfo))
                    largeImageText = titleInfo.GetMetaTitleName();
            }
        }
        break;
    default:
        assert(false);
        break;
    }

    discord_presence.details = details_string.c_str();
    discord_presence.state = state_string.c_str();
    discord_presence.startTimestamp = time(nullptr);
    discord_presence.largeImageText = largeImageText.c_str();
    discord_presence.largeImageKey = largeImageKey.c_str();
    Discord_UpdatePresence(&discord_presence);
}

void DiscordPresence::ClearPresence() const
{
	Discord_ClearPresence();
}

#endif
