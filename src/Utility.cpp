#include "Utility.h"
#include <filesystem>
#include <spdlog/sinks/basic_file_sink.h>

bool SetupLog() {
    auto path = SKSE::log::log_directory();
    if (!path) {
        return false;
    }
    *path /= "BannerPhysics.log";
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
    auto log = std::make_shared<spdlog::logger>("BannerPhysics", std::move(sink));
    spdlog::set_default_logger(log);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
    return true;
}

namespace BP::Settings {
    void Load() {
        std::array<wchar_t, 32768> executable{};
        const auto size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        bEnableDegradation = true;
        bEnableMagicDegradation = true;
        increaseDegradationKeywords = {"MagicDamageFire", "MagicDamageShock"};
        triggerBurnKeywords = {"MagicDamageFire", "MagicDamageShock"};
        bEnableInterior = true;
        iMaxPhysicsBanners = 64;
        fBannerWeightMult = 1.f;
        if (!size || size >= executable.size()) {
            logger::warn("Could not locate BannerPhysics.ini; using defaults");
            return;
        }
        const auto path =
            std::filesystem::path(executable.data()).parent_path() / L"Data/SKSE/Plugins/BannerPhysics.ini";
        std::ifstream input(path);
        const bool loaded = input.is_open();
        if (loaded) {
            const auto settings = Read(input);
            bEnableDegradation = settings.degradation;
            bEnableMagicDegradation = settings.magicDegradation;
            increaseDegradationKeywords = settings.increaseDegradationKeywords;
            triggerBurnKeywords = settings.triggerBurnKeywords;
            bEnableInterior = settings.interior;
            iMaxPhysicsBanners = settings.maxPhysicsBanners;
            fBannerWeightMult = settings.bannerWeightMult;
        }
        logger::info("bEnableDegradation={}, bEnableMagicDegradation={}, bEnableInterior={}, iMaxPhysicsBanners={}, "
                     "fBannerWeightMult={} ({})",
                     bEnableDegradation ? 1 : 0, bEnableMagicDegradation ? 1 : 0, bEnableInterior ? 1 : 0,
                     iMaxPhysicsBanners, fBannerWeightMult, loaded ? "INI" : "default");
    }
}
