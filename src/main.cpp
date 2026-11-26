#include "BannerRuntime.h"
#include "FSMPBridge.h"
#include "Utility.h"
#include "BannerDegradation.h"

extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v{};
    v.PluginVersion(REL::Version(BP_VERSION_MAJOR, BP_VERSION_MINOR, BP_VERSION_PATCH, BP_VERSION_TWEAK));
    v.PluginName("BannerPhysics");
    v.AuthorName("Banner Physics");
    v.UsesAddressLibrary();
    v.UsesNoStructs();
    return v;
}();

extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* skse,
                                                               SKSE::PluginInfo* info) {
    info->infoVersion = SKSE::PluginInfo::kVersion;
    info->name = "BannerPhysics";
    info->version = REL::Version(BP_VERSION_MAJOR, BP_VERSION_MINOR, BP_VERSION_PATCH, BP_VERSION_TWEAK).pack();
    return !skse->IsEditor() && BP::supportsRuntime(skse->RuntimeVersion());
}

namespace {

    void FSMPMessageHandler(SKSE::MessagingInterface::Message* message) {
        if (message && message->type == hdt::PluginInterface::MSG_STARTUP) {
            BP::connect(static_cast<hdt::PluginInterface*>(message->data));
        }
    }

    void SKSEMessageHandler(SKSE::MessagingInterface::Message* message) {
        if (!message) {
            return;
        }
        switch (message->type) {
            case SKSE::MessagingInterface::kPostLoad:
                if (!SKSE::GetMessagingInterface()->RegisterListener("hdtsmp64", FSMPMessageHandler)) {
                    logger::critical("FSMP messaging unavailable; banners disabled");
                }
                if (REL::Module::IsVR() && !GetModuleHandleW(L"vrik.dll")) {
                    logger::warn("VRIK not detected; player body collision with banners will not follow the headset");
                }
                break;
            case SKSE::MessagingInterface::kDataLoaded:
                if (!hdt::BannerAlphaShaderBytecode()) {
                    logger::error("Banner alpha shader compilation failed");
                }
                if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                    holder->AddEventSink<RE::TESCellAttachDetachEvent>(&hdt::BannerRuntime::GetSingleton());
                    holder->AddEventSink<RE::TESSpellCastEvent>(&hdt::BannerRuntime::GetSingleton());
                }
                break;
            case SKSE::MessagingInterface::kNewGame:
            case SKSE::MessagingInterface::kPreLoadGame:
                hdt::BannerRuntime::GetSingleton().QueueBannerDamageReset();
                break;
            default:
                break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    if (!SetupLog()) {
        return false;
    }

    if (skse->IsEditor() || !BP::supportsRuntime(skse->RuntimeVersion())) {
        logger::critical("Unsupported runtime {}; targets are 1.5.97, 1.6.x, 1.7.104 and VR 1.4.15",
                         skse->RuntimeVersion().string());
        return false;
    }
    SKSE::Init(skse, false);
    BP::Settings::Load();
    logger::info("Banner Physics loaded (Skyrim {})", skse->RuntimeVersion().string());
    return SKSE::GetMessagingInterface()->RegisterListener(SKSEMessageHandler);
}
