#include "BannerDegradation.h"
#include "BannerRuntime.h"

namespace hdt {
    void BannerRuntime::UpdateBannerFireGlow(BannerDamagePart& part, float intensity) {
        intensity = std::clamp(intensity, 0.f, 1.f);
        const bool contact = intensity > 0;
        const unsigned glowWidth = BannerFireWidth(intensity);
        using Property = RE::BSShaderProperty;
        auto* property = netimmerse_cast<RE::BSLightingShaderProperty*>(part.originalShader.get());
        if (!property || (!contact && !part.fireGlowActive)) {
            return;
        }
        if (!contact) {
            property->SetMaterial(part.cooledMaterial.get(), true);
            part.cooledMaterial = RE::BSTSmartPointer<RE::BSShaderMaterial>(property->material);
            property->SetFlags(Property::EShaderPropertyFlag8::kOwnEmit, part.originalOwnEmit);
            property->SetFlags(Property::EShaderPropertyFlag8::kGlowMap, part.originalGlowMap);
            property->emissiveColor = part.originalEmissiveColor;
            property->emissiveMult = part.originalEmissiveMult;
            property->DoClearRenderPasses();
            part.fireGlowActive = false;
            return;
        }
        if (part.fireGlowActive) {
            property->emissiveMult = 5.f * intensity;
            if (part.glowCutoff != part.wearCutoff || part.glowWidth != glowWidth) {
                auto* hot = static_cast<RE::BSLightingShaderMaterialGlowmap*>(part.fireMaterial.get());
                auto* cooled = static_cast<RE::BSLightingShaderMaterialBase*>(part.cooledMaterial.get());
                if (hot && cooled &&
                    FillRuntimeBannerTexture(cooled->diffuseTexture.get(), hot->glowTexture.get(), part.stage,
                                             part.layout, true, false, 85, glowWidth)) {
                    part.glowCutoff = part.wearCutoff;
                    part.glowWidth = glowWidth;
                }
            }
            return;
        }
        if (!part.geometry || part.geometry->GetGeometryRuntimeData().shaderProperty != part.originalShader ||
            !part.cooledMaterial || property->material != part.cooledMaterial.get()) {
            return;
        }
        auto* cooled = static_cast<RE::BSLightingShaderMaterialBase*>(part.cooledMaterial.get());
        const auto feature = cooled->GetFeature();
        if (feature != RE::BSShaderMaterial::Feature::kDefault && feature != RE::BSShaderMaterial::Feature::kGlowMap) {

            return;
        }
        if (!part.fireMaterial) {
            auto* source = cooled->diffuseTexture.get();
            if (!source || !source->rendererTexture || !source->rendererTexture->resourceView) {
                return;
            }
            const std::string key = CanonicalBannerTexturePath(source->name.c_str());
            if (key.size() < 4 || key.find("slot_") == std::string::npos || key.substr(key.size() - 4) != ".dds") {
                return;
            }
            RE::BSTSmartPointer<RE::BSShaderMaterial> material(
                RE::BSLightingShaderMaterialBase::CreateMaterial<RE::BSLightingShaderMaterialGlowmap>());
            if (!material) {
                return;
            }
            auto* hot = static_cast<RE::BSLightingShaderMaterialGlowmap*>(material.get());
            auto cached = m_damageGlowTextures.find(key);
            if (cached == m_damageGlowTextures.end()) {
                const auto glowPath = key.substr(0, key.size() - 4) + "_g.dds";
                auto& donor = m_damageGlowModels[key];
                if (!donor) {
                    const auto slash = key.find_last_of('/');
                    const auto file = key.substr(slash == std::string::npos ? 0 : slash + 1);
                    const auto path =
                        "meshes\\bannerphysics\\fsmpdamage\\slots\\" + file.substr(0, file.size() - 4) + "_g.nif";
                    RE::BSModelDB::DBTraits::ArgsType args;
                    auto result = RE::BSModelDB::Demand(path.c_str() + 7, donor, args);
                    if (result != RE::BSResource::ErrorCode::kNone || !donor) {
                        result = RE::BSModelDB::Demand(path.c_str(), donor, args);
                    }
                    if (result != RE::BSResource::ErrorCode::kNone || !donor) {
                        return;
                    }
                }
                auto* object = donor->GetObjectByName("BP_AlphaDonor");
                auto* shape = object ? object->AsTriShape() : nullptr;
                auto* donorShader = shape ? netimmerse_cast<RE::BSLightingShaderProperty*>(
                                                shape->GetGeometryRuntimeData().shaderProperty.get())
                                          : nullptr;
                auto* donorMaterial = donorShader && donorShader->material
                                          ? static_cast<RE::BSLightingShaderMaterialBase*>(donorShader->material)
                                          : nullptr;
                if (!donorMaterial) {
                    return;
                }
                auto textures = hdt::make_nismart(RE::BSShaderTextureSet::Create());
                RE::BSTSmartPointer<RE::BSShaderMaterial> loader(donorMaterial->Create());
                if (!textures || !loader) {
                    return;
                }
                loader->CopyMembers(donorMaterial);
                std::string nativeGlowPath = glowPath;
                std::replace(nativeGlowPath.begin(), nativeGlowPath.end(), '/', '\\');
                textures->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, nativeGlowPath.c_str());
                auto* prepared = static_cast<RE::BSLightingShaderMaterialBase*>(loader.get());
                prepared->textureSet = textures;
                prepared->OnLoadTextureSet(0, textures.get());
                donorShader->SetMaterial(loader.get(), true);
                prepared = static_cast<RE::BSLightingShaderMaterialBase*>(donorShader->material);
                auto* glow = prepared->diffuseTexture.get();
                if (!glow || !glow->rendererTexture || !glow->rendererTexture->resourceView ||
                    CanonicalBannerTexturePath(glow->name.c_str()) != glowPath || glow == source ||
                    glow->rendererTexture == source->rendererTexture ||
                    (cooled->normalTexture && glow->rendererTexture == cooled->normalTexture->rendererTexture)) {

                    return;
                }
                for (const auto& [otherKey, other] : m_damageGlowTextures) {
                    if (other && other->rendererTexture == glow->rendererTexture) {
                        return;
                    }
                }
                cached = m_damageGlowTextures.emplace(key, prepared->diffuseTexture).first;
            }
            if (!FillRuntimeBannerTexture(source, cached->second.get(), part.stage, part.layout, true, false, 85,
                                          glowWidth)) {
                return;
            }
            hot->CopyBaseMembers(cooled);
            hot->glowTexture = cached->second;
            hot->hashKey = std::numeric_limits<uint32_t>::max();
            part.fireMaterial = std::move(material);
            part.glowCutoff = part.wearCutoff;
            part.glowWidth = glowWidth;
            part.fireColor = std::make_unique<RE::NiColor>(1.f, 1.f, 1.f);
        }
        part.originalOwnEmit = property->flags.any(Property::EShaderPropertyFlag::kOwnEmit);
        part.originalGlowMap = property->flags.any(Property::EShaderPropertyFlag::kGlowMap);
        part.originalEmissiveColor = property->emissiveColor;
        part.originalEmissiveMult = property->emissiveMult;
        property->SetMaterial(part.fireMaterial.get(), true);
        property->SetFlags(Property::EShaderPropertyFlag8::kOwnEmit, true);
        property->SetFlags(Property::EShaderPropertyFlag8::kGlowMap, true);
        property->emissiveColor = part.fireColor.get();
        property->emissiveMult = 5.f * intensity;
        property->DoClearRenderPasses();
        part.fireGlowActive = true;
    }

    bool BannerRuntime::ApplyBannerDamagePart(BannerDamageState& state, RE::NiNode* root, RE::BSTriShape* geometry,
                                              unsigned ordinal, unsigned layout, int desiredStage) {
        if (!geometry || !geometry->GetGeometryRuntimeData().skinInstance ||
            !geometry->GetGeometryRuntimeData().alphaProperty) {
            return false;
        }
        auto* original =
            netimmerse_cast<RE::BSLightingShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
        auto* material =
            original && original->material && original->material->GetType() == RE::BSShaderMaterial::Type::kLighting
                ? static_cast<RE::BSLightingShaderMaterialBase*>(original->material)
                : nullptr;
        if (!material || !material->diffuseTexture || !geometry->GetGeometryRuntimeData().alphaProperty) {
            return false;
        }
        const char* diffuse = material->diffuseTexture->name.c_str();
        if ((!diffuse || !*diffuse) && material->textureSet) {
            diffuse = material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
        }
        if (!diffuse || !*diffuse) {
            return false;
        }
        std::string texture = diffuse;
        std::replace(texture.begin(), texture.end(), '\\', '/');
        std::transform(texture.begin(), texture.end(), texture.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        const auto slash = texture.find_last_of('/');
        auto stem = texture.substr(slash == std::string::npos ? 0 : slash + 1);
        if (stem.size() < 5 || stem.substr(stem.size() - 4) != ".dds") {
            return false;
        }
        stem.resize(stem.size() - 4);
        const auto sourceAlpha = geometry->GetGeometryRuntimeData().alphaProperty;
        const unsigned reference = sourceAlpha->alphaThreshold;
        RE::NiPointer<RE::NiAlphaProperty> privateAlpha;
        if (sourceAlpha->GetAlphaTesting() && !sourceAlpha->GetAlphaBlending() &&
            ((sourceAlpha->alphaFlags >> 10) & 7) == 4 && BannerHasErosionMask(layout)) {
            privateAlpha = hdt::make_nismart(netimmerse_cast<RE::NiAlphaProperty*>(sourceAlpha->Clone()));
            if (privateAlpha == sourceAlpha) {
                privateAlpha.reset();
            }
        }
        const bool animate = privateAlpha != nullptr;
        if (!animate && state.hits == 0) {
            return false;
        }
        const auto cacheKey =
            texture + "|layout" + std::to_string(layout) + "|stage" + std::to_string(desiredStage) +
            (animate ? "|ramp-ref" + std::to_string(reference) + "|ref" +
                           std::to_string(state.reference.get() ? state.reference.get()->GetFormID() : 0)
                     : "|instant");
        auto& donor = m_damageTextureModels[cacheKey];
        if (!donor) {
            static std::unordered_map<std::string, size_t> slots;
            auto [slot, added] = slots.try_emplace(cacheKey, slots.size());
            if (slot->second >= 512) {
                logger::error("Runtime alpha slot pool full");
                return false;
            }
            const auto path = fmt::format("meshes\\bannerphysics\\fsmpdamage\\slots\\slot_{:03}.nif", slot->second);
            RE::NiPointer<RE::NiNode> loaded;
            RE::BSModelDB::DBTraits::ArgsType args;
            auto result = RE::BSModelDB::Demand(path.c_str() + 7, loaded, args);
            if (result != RE::BSResource::ErrorCode::kNone || !loaded) {
                result = RE::BSModelDB::Demand(path.c_str(), loaded, args);
            }
            if (result != RE::BSResource::ErrorCode::kNone || !loaded) {
                if (state.root.get() != root) {
                    logger::error("Runtime alpha model load failed: {} code={} diffuse={}", path,
                                  static_cast<uint32_t>(result), texture);
                }
                state.root = hdt::make_nismart(root);
                return false;
            }
            auto* slotObject = loaded->GetObjectByName("BP_AlphaDonor");
            auto* shape = slotObject ? slotObject->AsTriShape() : nullptr;
            auto* prop = shape ? netimmerse_cast<RE::BSLightingShaderProperty*>(
                                     shape->GetGeometryRuntimeData().shaderProperty.get())
                               : nullptr;
            auto* slotMaterial =
                prop && prop->material ? static_cast<RE::BSLightingShaderMaterialBase*>(prop->material) : nullptr;
            if (!slotMaterial) {
                return false;
            }
            auto slotSet = hdt::make_nismart(RE::BSShaderTextureSet::Create());
            RE::BSTSmartPointer<RE::BSShaderMaterial> slotPrivate(slotMaterial->Create());
            if (!slotSet || !slotPrivate) {
                return false;
            }
            slotPrivate->CopyMembers(slotMaterial);
            const auto slotPath =
                fmt::format("textures\\bannerphysics\\fsmpdamage\\slots\\slot_{:03}.dds", slot->second);
            slotSet->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, slotPath.c_str());
            auto* prepared = static_cast<RE::BSLightingShaderMaterialBase*>(slotPrivate.get());
            prepared->textureSet = slotSet;
            prepared->OnLoadTextureSet(0, slotSet.get());
            prop->SetMaterial(slotPrivate.get(), true);
            slotMaterial = static_cast<RE::BSLightingShaderMaterialBase*>(prop->material);
            const auto expected = fmt::format("slot_{:03}.dds", slot->second);
            const std::string actual =
                slotMaterial && slotMaterial->diffuseTexture ? slotMaterial->diffuseTexture->name.c_str() : "";
            if (actual.size() < expected.size() || actual.substr(actual.size() - expected.size()) != expected) {
                logger::error("Refused fallback texture for {}: loaded={}", slotPath, actual);
                return false;
            }
            for (const auto& [otherKey, otherRoot] : m_damageTextureModels) {
                if (otherRoot && otherKey != cacheKey) {
                    auto* otherObject = otherRoot->GetObjectByName("BP_AlphaDonor");
                    auto* otherShape = otherObject ? otherObject->AsTriShape() : nullptr;
                    auto* otherProp = otherShape ? netimmerse_cast<RE::BSLightingShaderProperty*>(
                                                       otherShape->GetGeometryRuntimeData().shaderProperty.get())
                                                 : nullptr;
                    auto* otherMaterial = otherProp && otherProp->material
                                              ? static_cast<RE::BSLightingShaderMaterialBase*>(otherProp->material)
                                              : nullptr;
                    if (otherMaterial && otherMaterial->diffuseTexture &&
                        (otherMaterial->diffuseTexture == slotMaterial->diffuseTexture ||
                         otherMaterial->diffuseTexture->rendererTexture ==
                             slotMaterial->diffuseTexture->rendererTexture)) {
                        logger::error("Refused shared GPU resource for {}", slotPath);
                        return false;
                    }
                }
            }
            if (!slotMaterial ||
                !FillRuntimeBannerTexture(material->diffuseTexture.get(), slotMaterial->diffuseTexture.get(),
                                          desiredStage, layout, false, animate, reference)) {
                state.root = hdt::make_nismart(root);
                return false;
            }
            donor = loaded;
        }
        auto* donorObject = donor->GetObjectByName("BP_AlphaDonor");
        auto* donorGeometry = donorObject ? donorObject->AsTriShape() : nullptr;
        auto* donorShader = donorGeometry ? netimmerse_cast<RE::BSLightingShaderProperty*>(
                                                donorGeometry->GetGeometryRuntimeData().shaderProperty.get())
                                          : nullptr;
        auto* donorMaterial = donorShader && donorShader->material
                                  ? static_cast<RE::BSLightingShaderMaterialBase*>(donorShader->material)
                                  : nullptr;
        if (donorMaterial && donorMaterial->textureSet &&
            (!donorMaterial->diffuseTexture || !donorMaterial->diffuseTexture->rendererTexture ||
             !donorMaterial->diffuseTexture->rendererTexture->resourceView)) {
            donorMaterial->OnLoadTextureSet(0, donorMaterial->textureSet.get());
        }
        if (!donorMaterial || !donorMaterial->diffuseTexture || !donorMaterial->diffuseTexture->rendererTexture ||
            !donorMaterial->diffuseTexture->rendererTexture->resourceView) {
            state.root = hdt::make_nismart(root);
            return false;
        }
        auto loadedDiffuse = donorMaterial->diffuseTexture;
        const auto* loadedGPU = loadedDiffuse->rendererTexture;
        ID3D11Resource* loadedResource = nullptr;
        ID3D11Texture2D* loadedImage = nullptr;
        D3D11_TEXTURE2D_DESC loadedDesc{};
        loadedGPU->resourceView->GetResource(&loadedResource);
        if (loadedResource) {
            if (SUCCEEDED(loadedResource->QueryInterface(__uuidof(ID3D11Texture2D),
                                                         reinterpret_cast<void**>(&loadedImage)))) {
                loadedImage->GetDesc(&loadedDesc);
                loadedImage->Release();
            }
            loadedResource->Release();
        }
        if (loadedDesc.Width < 64 || loadedDesc.Height < 64) {
            if (state.root.get() != root) {
                logger::error("Alpha donor GPU resource unavailable: {} name={} GPU={}x{} wrapper={}x{}", stem,
                              loadedDiffuse->name.c_str(), loadedDesc.Width, loadedDesc.Height, loadedGPU->width,
                              loadedGPU->height);
            }
            state.root = hdt::make_nismart(root);
            return false;
        }
        for (auto& other : m_bannerInstances) {
            auto* marker = FindBannerStringData(other.root.get(), "BP_BannerDamageShapes");
            if (!marker || !marker->value) {
                continue;
            }
            std::istringstream shapeLines(marker->value);
            std::string entry;
            while (std::getline(shapeLines, entry)) {
                unsigned ignored = 0;
                std::string_view shapeName;
                if (!ParseBannerDamageEntry(entry, shapeName, ignored)) {
                    continue;
                }
                auto* obj = other.root->GetObjectByName(std::string(shapeName).c_str());
                auto* candidate = obj ? obj->AsTriShape() : nullptr;
                if (candidate && candidate != geometry &&
                    candidate->GetGeometryRuntimeData().shaderProperty.get() == original) {
                    logger::error(
                        "Refused shared live shader on banner {:08X}; material change would affect another cloth part",
                        state.reference.get() ? state.reference.get()->GetFormID() : 0);
                    return false;
                }
            }
        }
        RE::BSTSmartPointer<RE::BSShaderMaterial> privateMaterial(material->Create());
        if (!privateMaterial) {
            return false;
        }
        privateMaterial->CopyMembers(material);
        auto* masked = static_cast<RE::BSLightingShaderMaterialBase*>(privateMaterial.get());
        masked->diffuseTexture = donorMaterial->diffuseTexture;
        auto textureSet = hdt::make_nismart(RE::BSShaderTextureSet::Create());
        if (!textureSet) {
            return false;
        }
        if (material->textureSet) {
            for (uint32_t slot = 0; slot < RE::BSTextureSet::Texture::kTotal; ++slot) {
                const auto path = material->textureSet->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(slot));
                if (path) {
                    textureSet->SetTexturePath(static_cast<RE::BSTextureSet::Texture>(slot), path);
                }
            }
        }
        const auto* maskedPath = donorMaterial->textureSet
                                     ? donorMaterial->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse)
                                     : nullptr;
        if (!maskedPath) {
            return false;
        }
        textureSet->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, maskedPath);
        masked->textureSet = textureSet;
        masked->hashKey = std::numeric_limits<uint32_t>::max();
        BannerDamagePart part;
        part.geometry = hdt::make_nismart(geometry);
        part.ordinal = ordinal;
        part.layout = layout;
        part.stage = desiredStage;
        part.originalShader = geometry->GetGeometryRuntimeData().shaderProperty;
        part.originalAlpha = geometry->GetGeometryRuntimeData().alphaProperty;
        part.animatedAlpha = privateAlpha;
        part.originalNoTransparencyMultiSample =
            original->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kNoTransparencyMultiSample);
        if (privateAlpha) {
            privateAlpha->SetAlphaBlending(false);
            privateAlpha->SetAlphaTesting(true);
            privateAlpha->alphaThreshold = uint8_t(reference);
            part.wearTexture = donorMaterial->diffuseTexture;
            auto& ranks = m_damageWearRanks[cacheKey];
            if (!ranks) {
                ranks = part.wearTexture->rendererTexture->resourceView;
            }
            part.wearRanks = ranks;
            const unsigned initialCutoff = state.completedStage >= desiredStage ? 254 : 0;
            if (!ApplyBannerWearCutoff(part.wearRanks.Get(), part.wearTexture.get(), initialCutoff)) {
                return false;
            }
            part.wearCutoff = int(initialCutoff);
            original->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kNoTransparencyMultiSample, true);
            geometry->GetGeometryRuntimeData().alphaProperty = privateAlpha;
        }
        part.originalMaterial = RE::BSTSmartPointer<RE::BSShaderMaterial>(material);
        original->SetMaterial(privateMaterial.get(), true);
        part.cooledMaterial = RE::BSTSmartPointer<RE::BSShaderMaterial>(original->material);
        original->DoClearRenderPasses();

        state.parts.push_back(std::move(part));

        return true;
    }

    void BannerRuntime::RestoreBannerDamage(BannerDamageState& state) {
        for (auto& part : state.parts) {
            UpdateBannerFireGlow(part, false);
            if (part.geometry) {
                auto& data = part.geometry->GetGeometryRuntimeData();
                if (part.animatedAlpha && data.alphaProperty == part.animatedAlpha) {
                    data.alphaProperty = part.originalAlpha;
                    if (data.shaderProperty == part.originalShader) {
                        part.originalShader->SetFlags(
                            RE::BSShaderProperty::EShaderPropertyFlag8::kNoTransparencyMultiSample,
                            part.originalNoTransparencyMultiSample);
                    }
                }
                if (data.shaderProperty == part.originalShader && part.originalMaterial) {
                    auto* live = netimmerse_cast<RE::BSLightingShaderProperty*>(data.shaderProperty.get());
                    if (live) {
                        live->SetMaterial(part.originalMaterial.get(), true);
                        live->DoClearRenderPasses();
                    }
                }
            }
        }
        state.parts.clear();
        state.applied = false;
        state.appliedStage = 0;
        state.root.reset();
    }

    void BannerRuntime::QueueBannerDamageReset() {
        std::lock_guard guard(m_damageEventLock);
        m_damageResetRequested = true;
    }

    void BannerRuntime::SyncBannerDamage() {
        std::vector<RE::FormID> detached;
        bool reset = false;
        {
            std::lock_guard guard(m_damageEventLock);
            detached.swap(m_detachedBannerReferences);
            reset = m_damageResetRequested;
            m_damageResetRequested = false;
        }
        if (!BP::Settings::bEnableDegradation) {
            for (auto& [id, state] : m_bannerDamage) {
                RestoreBannerDamage(state);
            }
            m_bannerDamage.clear();
            for (auto& instance : m_bannerInstances) {
                instance.damageApplied = false;
                instance.damageMask.clear();
            }
            return;
        }
        if (reset) {
            for (auto& [id, state] : m_bannerDamage) {
                RestoreBannerDamage(state);
            }
            m_bannerDamage.clear();
            m_bannerAttackCycles.clear();
        }
        for (auto id : detached) {
            if (auto it = m_bannerDamage.find(id); it != m_bannerDamage.end()) {
                RestoreBannerDamage(it->second);
                m_bannerDamage.erase(it);
            }
        }
        unsigned wearUpdateBudget = 1;
        for (auto it = m_bannerDamage.begin(); it != m_bannerDamage.end();) {
            auto& state = it->second;
            const float glowDt = !m_loading && RE::UI::GetSingleton() && !RE::UI::GetSingleton()->GameIsPaused()
                                     ? std::clamp(RE::GetSecondsSinceLastFrame(), 0.f, .1f)
                                     : 0.f;
            if (state.fireContactTime > 0) {
                state.fireGlowHeat = BannerFireHeat(state.fireGlowHeat, true, glowDt);
                state.fireGlowReleaseHeat = state.fireGlowHeat;
                state.fireGlowCooldownAge = 0;
            } else {
                state.fireGlowCooldownAge = std::min(3.5f, state.fireGlowCooldownAge + glowDt);
                state.fireGlowHeat = BannerFireCooldown(state.fireGlowReleaseHeat, state.fireGlowCooldownAge);
            }
            state.fireContactTime = std::max(0.f, state.fireContactTime - glowDt);
            const auto ref = state.reference.get();
            auto* cell = ref ? ref->GetParentCell() : nullptr;
            if (!ref || !cell || !cell->IsAttached() || cell->GetFormID() != state.cell) {
                RestoreBannerDamage(state);
                it = m_bannerDamage.erase(it);
                continue;
            }
            const int desiredStage = state.hits ? std::min(BannerWearTargetStage(state.hits), state.completedStage + 1)
                                     : state.fireGlowHeat > 0 ? 1
                                                              : 0;
            if (!desiredStage) {
                if (state.applied || !state.parts.empty()) {
                    RestoreBannerDamage(state);
                }
                ++it;
                continue;
            }
            if (m_loading) {
                ++it;
                continue;
            }
            auto* root = ref->Get3D() ? ref->Get3D()->AsNode() : nullptr;
            if (!state.parts.empty() && (state.appliedStage != desiredStage || state.root.get() != root)) {
                RestoreBannerDamage(state);
            }
            if (root && !state.applied) {
                auto* manifest = FindBannerStringData(root, "BP_BannerDamageShapes");
                if (!manifest || !manifest->value) {
                    ++it;
                    continue;
                }
                const bool firstTry = state.root.get() != root || state.appliedStage != desiredStage;
                state.root = hdt::make_nismart(root);
                if (state.appliedStage != desiredStage) {
                    state.transitionAge = state.completedStage >= desiredStage ? kBannerWearTransitionSeconds : 0;
                }
                state.appliedStage = desiredStage;
                std::istringstream lines(manifest->value);
                std::string line;
                unsigned ordinal = 0;
                bool complete = true;
                state.parts.reserve(16);
                while (std::getline(lines, line)) {
                    unsigned layout = 0;
                    std::string_view shape;
                    if (!ParseBannerDamageEntry(line, shape, layout)) {
                        if (firstTry) {
                            logger::error("Invalid damage manifest on {:08X}", ref->GetFormID());
                        }
                        complete = false;
                        break;
                    }
                    if (std::none_of(state.parts.begin(), state.parts.end(), [&](const auto& part) {
                            return part.ordinal == ordinal;
                        })) {
                        auto* object = root->GetObjectByName(std::string(shape).c_str());
                        complete = ApplyBannerDamagePart(state, root, object ? object->AsTriShape() : nullptr, ordinal,
                                                         layout, desiredStage) &&
                                   complete;
                    }
                    ++ordinal;
                }
                state.applied = complete && ordinal > 0;
            }
            if (state.applied) {
                const bool animated = std::any_of(state.parts.begin(), state.parts.end(), [](const auto& part) {
                    return part.animatedAlpha != nullptr;
                });
                state.transitionAge = animated ? std::min(BannerWearTargetAge(state.hits, state.appliedStage),
                                                          state.transitionAge + glowDt * .5f)
                                               : kBannerWearTransitionSeconds;
                const auto cutoff = BannerWearCutoff(state.transitionAge);
                for (auto& part : state.parts) {
                    const unsigned step = cutoff == 254 ? 254 : unsigned(cutoff);
                    if (part.wearRanks && part.wearTexture && part.wearCutoff != int(step) && wearUpdateBudget > 0) {
                        --wearUpdateBudget;
                        if (ApplyBannerWearCutoff(part.wearRanks.Get(), part.wearTexture.get(), step)) {
                            part.wearCutoff = int(step);
                        }
                    }
                    UpdateBannerFireGlow(part, BannerFireIntensity(state.fireGlowHeat));
                }
                if (cutoff == 254 && std::all_of(state.parts.begin(), state.parts.end(), [](const auto& part) {
                        return !part.wearRanks || part.wearCutoff == 254;
                    })) {
                    state.completedStage = state.appliedStage;
                }
            }
            ++it;
        }
        for (auto& instance : m_bannerInstances) {
            const auto ref = instance.reference.get();
            const auto it = ref ? m_bannerDamage.find(ref->GetFormID()) : m_bannerDamage.end();
            instance.damageApplied = it != m_bannerDamage.end() && it->second.applied && it->second.completedStage > 0;
            instance.damageMask =
                instance.damageApplied ? it->second.coverage[it->second.completedStage - 1] : std::string{};
        }
    }

    RE::BSEventNotifyControl BannerRuntime::ProcessEvent(const RE::TESCellAttachDetachEvent* event,
                                                         RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) {
        if (event && !event->attached && event->reference) {
            std::lock_guard guard(m_damageEventLock);
            m_detachedBannerReferences.push_back(event->reference->GetFormID());
        }
        return RE::BSEventNotifyControl::kContinue;
    }
}
