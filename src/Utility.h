#ifndef UTILITY_H
#define UTILITY_H

#include <istream>
#include <string>
#include <string_view>
#include <cctype>
#include <charconv>
#include <cmath>
#include <REL/Version.h>
#include <cstdint>
#include <LinearMath/btVector3.h>
#include <array>
#include <algorithm>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <vector>

bool SetupLog();

namespace BP::Settings {
    inline bool bEnableDegradation = true;
    inline bool bEnableMagicDegradation = true;
    inline std::vector<std::string> increaseDegradationKeywords{"MagicDamageFire", "MagicDamageShock"};
    inline std::vector<std::string> triggerBurnKeywords{"MagicDamageFire", "MagicDamageShock"};
    inline bool bEnableInterior = true;
    inline int iMaxPhysicsBanners = 64;
    inline float fBannerWeightMult = 1.f;

    struct Values {
        bool degradation = true;
        bool magicDegradation = true;
        std::vector<std::string> increaseDegradationKeywords{"MagicDamageFire", "MagicDamageShock"};
        std::vector<std::string> triggerBurnKeywords{"MagicDamageFire", "MagicDamageShock"};
        bool interior = true;
        int maxPhysicsBanners = 64;
        float bannerWeightMult = 1.f;
    };

    inline Values Read(std::istream& input) {
        Values settings;
        bool inGeneral = false, inMagic = false, magicOverride = false;
        std::string line;
        auto trim = [](std::string_view value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == value.npos) {
                return std::string_view{};
            }
            return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
        };
        auto equal = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        };
        while (std::getline(input, line)) {
            std::string_view value = line;
            if (value.starts_with("\xEF\xBB\xBF")) {
                value.remove_prefix(3);
            }
            value = trim(value.substr(0, value.find_first_of(";#")));
            if (value.empty()) {
                continue;
            }
            if (value.front() == '[' && value.back() == ']') {
                const auto section = trim(value.substr(1, value.size() - 2));
                inGeneral = equal(section, "General");
                inMagic = equal(section, "Magic");
                continue;
            }
            if (!inGeneral && !inMagic) {
                continue;
            }
            const auto split = value.find('=');
            if (split == value.npos) {
                continue;
            }
            const auto key = trim(value.substr(0, split)), setting = trim(value.substr(split + 1));
            if (inMagic) {
                if (equal(key, "bEnableMagicDegradation")) {
                    if (setting == "0" || setting == "1") {
                        settings.magicDegradation = setting == "1";
                        magicOverride = true;
                    }
                } else if (equal(key, "sIncreaseDegradation") || equal(key, "sTriggerBurnEffect")) {
                    auto& keywords = equal(key, "sIncreaseDegradation") ? settings.increaseDegradationKeywords
                                                                        : settings.triggerBurnKeywords;
                    keywords.clear();
                    auto remaining = setting;
                    while (!remaining.empty()) {
                        const auto separator = remaining.find('|');
                        const auto keyword = trim(remaining.substr(0, separator));
                        if (!keyword.empty() &&
                            std::none_of(keywords.begin(), keywords.end(), [&](const auto& existing) {
                                return equal(existing, keyword);
                            })) {
                            keywords.emplace_back(keyword);
                        }
                        if (separator == remaining.npos) {
                            break;
                        }
                        remaining.remove_prefix(separator + 1);
                    }
                }
                continue;
            }
            if (equal(key, "bEnableDegradation")) {
                if (setting == "0") {
                    settings.degradation = false;
                } else if (setting == "1") {
                    settings.degradation = true;
                }
            } else if (equal(key, "bEnableMagicDegradation") && !magicOverride) {
                if (setting == "0") {
                    settings.magicDegradation = false;
                } else if (setting == "1") {
                    settings.magicDegradation = true;
                }
            } else if (equal(key, "bEnableInterior")) {
                if (setting == "0") {
                    settings.interior = false;
                } else if (setting == "1") {
                    settings.interior = true;
                }
            } else if (equal(key, "iMaxPhysicsBanners")) {
                int count = 0;
                auto result = std::from_chars(setting.data(), setting.data() + setting.size(), count);
                if (result.ec == std::errc{} && result.ptr == setting.data() + setting.size() && count >= 0) {
                    settings.maxPhysicsBanners = count;
                }
            } else if (equal(key, "fBannerWeightMult")) {
                float multiplier = 1;
                auto result = std::from_chars(setting.data(), setting.data() + setting.size(), multiplier);
                if (result.ec == std::errc{} && result.ptr == setting.data() + setting.size() &&
                    std::isfinite(multiplier) && multiplier >= .01f && multiplier <= 100.f) {
                    settings.bannerWeightMult = multiplier;
                }
            }
        }
        return settings;
    }

    inline bool ReadDegradation(std::istream& input) {
        return Read(input).degradation;
    }

    void Load();
}

namespace BP {
    inline constexpr REL::Version runtimeAE{1, 6, 1170, 0};
    inline constexpr REL::Version runtimeCurrent{1, 7, 104, 0};

    inline constexpr bool supportsRuntime(REL::Version version) {
        return version == runtimeAE || version == runtimeCurrent;
    }
}

namespace BP::Offsets {
    inline constexpr char sha256[] = "ff9c7f3f78eca122ac0aa9bd58a0628d7200685f86ef63fbd18bfe7f2c6d667f";
    inline constexpr uintptr_t world = 0x96c40;
    inline constexpr uintptr_t lock = 0x96cc0;
    inline constexpr uintptr_t bbp = 0xa42c0;
    inline constexpr uintptr_t scan = 0xa5380;
    inline constexpr uintptr_t create = 0xb1690;
    inline constexpr uintptr_t findBone = 0xb43e0;
    inline constexpr uintptr_t frameSource = 0x3a9b0;
    inline constexpr uintptr_t syncSource = 0x3aa40;
    inline constexpr uintptr_t shutdownSource = 0x3ab60;
    inline constexpr uintptr_t doUpdate = 0x96700;
    inline constexpr uintptr_t actors = 0x8caa0;
    inline constexpr uintptr_t springInfo = 0x39980;
    inline constexpr uintptr_t wind = 0x37e90;
    inline constexpr uintptr_t springVtable = 0x2cf448;

    struct Signature {
        uintptr_t rva;
        unsigned char bytes[16];
    };

    inline constexpr Signature signatures[] = {
        {world, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x0d, 0xe8, 0xff, 0x34, 0x00, 0x65, 0x48, 0x8b, 0x04}},
        {lock, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x60, 0x48}},
        {bbp, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x0d, 0x68, 0x29, 0x34, 0x00, 0x65, 0x48, 0x8b, 0x04}},
        {scan, {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}},
        {create, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56}},
        {findBone, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57}},
        {frameSource, {0x48, 0x83, 0xec, 0x28, 0x8b, 0x0d, 0x7a, 0xc2, 0x3a, 0x00, 0x65, 0x48, 0x8b, 0x04, 0x25, 0x58}},
        {syncSource, {0x48, 0x83, 0xec, 0x28, 0x8b, 0x0d, 0xea, 0xc1, 0x3a, 0x00, 0x65, 0x48, 0x8b, 0x04, 0x25, 0x58}},
        {shutdownSource,
         {0x48, 0x83, 0xec, 0x28, 0x8b, 0x0d, 0xca, 0xc0, 0x3a, 0x00, 0x65, 0x48, 0x8b, 0x04, 0x25, 0x58}},
        {doUpdate, {0x4c, 0x8b, 0xdc, 0x56, 0x48, 0x81, 0xec, 0x10, 0x01, 0x00, 0x00, 0xc4, 0xc1, 0x78, 0x29, 0x73}},
        {actors, {0x48, 0x83, 0xec, 0x28, 0x8b, 0x0d, 0x8a, 0xa1, 0x35, 0x00, 0x65, 0x48, 0x8b, 0x04, 0x25, 0x58}},
        {springInfo, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x18, 0x57, 0x48, 0x81, 0xec, 0xc0, 0x00, 0x00, 0x00, 0x4c}},
        {wind, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55}},
    };
}

namespace BP {
    inline bool BannerCellContext(bool sameCell, bool bannerInterior, bool playerInterior, bool sameWorldspace) {
        return sameCell || (!bannerInterior && !playerInterior && sameWorldspace);
    }
}

namespace hdt {
    struct BannerBounds {
        btVector3 low{BT_LARGE_FLOAT, BT_LARGE_FLOAT, BT_LARGE_FLOAT},
            high{-BT_LARGE_FLOAT, -BT_LARGE_FLOAT, -BT_LARGE_FLOAT};

        void include(const btVector3& p, float radius = 0) {
            const btVector3 pad(radius, radius, radius);
            low.setMin(p - pad);
            high.setMax(p + pad);
        }

        bool overlaps(const BannerBounds& other) const {
            for (int axis = 0; axis < 3; ++axis) {
                if (low[axis] > other.high[axis] || high[axis] < other.low[axis]) {
                    return false;
                }
            }
            return true;
        }
    };
}

namespace hdt {
    inline bool BannerSegmentHit(const btVector3& origin, const btVector3& delta, const btVector3& a,
                                 const btVector3& b, const btVector3& c, float& fraction, float& u, float& v) {
        const auto e1 = b - a, e2 = c - a, p = delta.cross(e2);
        const float determinant = e1.dot(p);
        if (std::abs(determinant) < 0.00001f) {
            return false;
        }
        const auto t = origin - a;
        u = t.dot(p) / determinant;
        if (u < 0 || u > 1) {
            return false;
        }
        const auto q = t.cross(e1);
        v = delta.dot(q) / determinant;
        if (v < 0 || u + v > 1) {
            return false;
        }
        fraction = e2.dot(q) / determinant;
        return fraction >= 0 && fraction <= 1;
    }
}

namespace hdt {
    constexpr int kBannerDamageMaskSize = 128;

    inline bool BannerDamageMaskPresent(std::string_view mask, float u, float v) {
        if (mask.size() != kBannerDamageMaskSize * kBannerDamageMaskSize) {
            return true;
        }
        if (u < 0 || u > 1 || v < 0 || v > 1) {
            return false;
        }
        const auto x = std::clamp(static_cast<int>(u * kBannerDamageMaskSize), 0, kBannerDamageMaskSize - 1);
        const auto y = std::clamp(static_cast<int>(v * kBannerDamageMaskSize), 0, kBannerDamageMaskSize - 1);
        return mask[y * kBannerDamageMaskSize + x] == '1';
    }

    inline bool BannerDamageFabricPresent(float u, float v) {
        const float x = (u - .5f) / .27f;
        const float y = (v - .49f - .12f * (u - .5f)) / .065f;
        if (x * x + y * y < 1.0f) {
            return false;
        }
        return true;
    }

    inline bool BannerDamageTrianglePointPresent(const std::array<uint8_t, 3>& indices,
                                                 const std::array<float, 3>& weights, std::string_view mask = {}) {
        float u = 0, v = 0;
        for (size_t i = 0; i < 3; ++i) {
            u += (indices[i] % 5) * weights[i] / 4.0f;
            v += (indices[i] / 5) * weights[i] / 14.0f;
        }
        return mask.empty() ? BannerDamageFabricPresent(u, v) : BannerDamageMaskPresent(mask, u, v);
    }
}

namespace hdt {
    inline bool ParseBannerDamageEntry(std::string_view line, std::string_view& shape, unsigned& layout) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1);
        }
        const auto tab = line.find('\t');
        if (tab == std::string_view::npos || tab == 0) {
            return false;
        }
        shape = line.substr(0, tab);
        const auto id = line.substr(tab + 1);
        auto result = std::from_chars(id.data(), id.data() + id.size(), layout);
        return result.ec == std::errc{} && result.ptr == id.data() + id.size() && layout < 512;
    }
}

namespace hdt {
    inline bool RegisterBannerWeaponHit(uint8_t& hits, uint32_t& lastActor, uint64_t& lastSwing, uint32_t actor,
                                        uint64_t swing) {
        if (hits < 20 && (actor != lastActor || swing != lastSwing)) {
            lastActor = actor;
            lastSwing = swing;
            ++hits;
        }
        return hits >= 2;
    }

    inline int BannerDegradationStage(uint8_t hits) {
        return hits >= 11 ? 4 : (hits >= 8 ? 3 : (hits >= 5 ? 2 : (hits >= 2 ? 1 : 0)));
    }
}

namespace BP {
    inline bool ApplyBannerWeight(btRigidBody& body, float multiplier) {
        if (body.isStaticOrKinematicObject() || !std::isfinite(multiplier) || multiplier < .01f || multiplier > 100.f ||
            multiplier == 1.f) {
            return false;
        }
        const float mass = 1.f / body.getInvMass();
        body.setMassProps(mass * multiplier, body.getLocalInertia() * multiplier);
        body.updateInertiaTensor();
        return true;
    }
}

namespace hdt {
    inline float BannerWindScaleFactor(float scale) {
        if (!std::isfinite(scale) || scale <= 1.0f) {
            return 1.0f;
        }
        const float factor = scale * scale;
        return std::isfinite(factor) ? factor : 1.0f;
    }
}

namespace hdt {
    inline RE::NiStringExtraData* FindBannerStringData(RE::NiNode* node, const char* key) {
        if (!node) {
            return nullptr;
        }
        for (uint16_t i = 0; i < node->extraDataSize; ++i) {
            auto* value = netimmerse_cast<RE::NiStringExtraData*>(node->extra[i]);
            if (value && value->name == key) {
                return value;
            }
        }
        return nullptr;
    }

    inline bool IsBannerCellAllowed(RE::TESObjectREFR* ref) {
        auto* cell = ref ? ref->GetParentCell() : nullptr;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* current = player ? player->GetParentCell() : nullptr;
        if (!cell || !current || (!BP::Settings::bEnableInterior && cell->IsInteriorCell())) {
            return false;
        }
        const auto world = cell->GetRuntimeData().worldSpace;
        return BP::BannerCellContext(cell == current, cell->IsInteriorCell(), current->IsInteriorCell(),
                                     world && world == current->GetRuntimeData().worldSpace);
    }

    inline constexpr float kBannerActivationRadius = 10000.0f;

    inline std::string CanonicalBannerTexturePath(std::string_view path) {
        std::string result(path);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return c == '\\' ? '/' : static_cast<char>(std::tolower(c));
        });
        if (result.starts_with("data/")) {
            result.erase(0, 5);
        }
        if (!result.empty() && !result.starts_with("textures/")) {
            result.insert(0, "textures/");
        }
        return result;
    }
}

namespace hdt {
    inline constexpr float kBannerWearTransitionSeconds = .75f;

    inline float BannerFireHeat(float heat, bool contact, float dt) {
        if (!std::isfinite(heat)) {
            heat = 0;
        }
        heat = std::clamp(heat, 0.f, 1.f);
        if (!std::isfinite(dt) || dt <= 0) {
            return heat;
        }
        return std::clamp(heat + (contact ? dt / 1.5f : -dt / 3.5f), 0.f, 1.f);
    }

    inline float BannerFireCooldown(float releaseHeat, float elapsed) {
        if (!std::isfinite(releaseHeat) || !std::isfinite(elapsed)) {
            return 0;
        }
        return std::clamp(releaseHeat, 0.f, 1.f) * (1.f - std::clamp(elapsed / 3.5f, 0.f, 1.f));
    }

    inline unsigned BannerFireWidth(float intensity) {
        return intensity >= .85f ? 5u : intensity >= .5f ? 4u : intensity >= .2f ? 3u : 2u;
    }

    inline float BannerFireIntensity(float heat) {
        heat = std::isfinite(heat) ? std::clamp(heat, 0.f, 1.f) : 0;
        return heat * heat * (3.f - 2.f * heat);
    }

    inline int BannerWearTargetStage(uint8_t hits) {
        return hits ? std::min((int(hits) + 4) / 5, 4) : 0;
    }

    inline float BannerWearTargetAge(uint8_t hits, int stage) {
        if (stage < 1 || stage > 4) {
            return 0;
        }
        return kBannerWearTransitionSeconds * std::clamp((int(hits) - (stage - 1) * 5) / 5.f, 0.f, 1.f);
    }

    inline uint8_t BannerWearCutoff(float elapsed) {
        if (!std::isfinite(elapsed) || elapsed <= 0) {
            return 0;
        }
        if (elapsed >= kBannerWearTransitionSeconds) {
            return 254;
        }
        const float t = elapsed / kBannerWearTransitionSeconds;
        return static_cast<uint8_t>(std::lround(254.f * t));
    }
}

#endif // UTILITY_H
