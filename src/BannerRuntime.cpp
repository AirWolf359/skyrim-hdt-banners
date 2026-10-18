#include "BannerRuntime.h"
#include "FSMPBridge.h"

namespace hdt {
    BannerRuntime& BannerRuntime::GetSingleton() {
        static BannerRuntime runtime;
        return runtime;
    }

    void BannerRuntime::AddSkinnedMeshSystem(SkinnedMeshSystem* system) {
        auto* s = static_cast<SkyrimSystem*>(system);
        for (auto& base : s->bannerConstraints()) {
            if (BP::isSpring(base.get())) {
                auto* c = static_cast<StiffSpringConstraint*>(base.get());
                c->setOverrideNumSolverIterations(24);
                m_springs.insert(c);
                m_fabric.push_back(c);
                c->m_scaleA = c->m_boneA->m_currentTransform.getScale();
                c->m_scaleB = c->m_boneB->m_currentTransform.getScale();
            }
        }
        SkyrimPhysicsWorld::get()->addSkinnedMeshSystem(system);
    }

    void BannerRuntime::RemoveSkinnedMeshSystem(SkinnedMeshSystem* system) {
        SkyrimPhysicsWorld::get()->removeSkinnedMeshSystem(system);
        for (auto& base : system->bannerConstraints()) {
            if (BP::isSpring(base.get())) {
                auto* c = static_cast<StiffSpringConstraint*>(base.get());
                m_springs.erase(c);
                std::erase(m_fabric, c);
            }
        }
    }

    void BannerRuntime::RefreshConstraints() {
        m_springs.clear();
        m_fabric.clear();
        for (auto& instance : m_bannerInstances) {
            for (auto& base : instance.system->bannerConstraints()) {
                if (BP::isSpring(base.get())) {
                    auto* c = static_cast<StiffSpringConstraint*>(base.get());
                    m_springs.insert(c);
                    m_fabric.push_back(c);
                }
            }
        }
    }

    void BannerRuntime::BeforeFrame() {
        auto* world = SkyrimPhysicsWorld::get();
        world->m_tasks.wait();
        m_loading = BP::loading();
        BP::BeginBannerWorldQueries();
        SyncAllBanners();
        SyncBannerMagic();
        SyncBannerDamage();
        HoldBannerAnchors();
    }

    void BannerRuntime::BeforeWrite() {
        auto* world = SkyrimPhysicsWorld::get();
        auto lock = world->lockSimulation();
        m_loading = BP::loading();
        if (m_loading || !world->m_pendingTransformUpdate) {
            return;
        }
        BP::BeginBannerWorldQueries();
        HoldBannerAnchors();
        CollideBannerWithActors();
        StabilizeBannerFabric();
        ApplyBannerArrowPins();
        CollideBannerWithWorld();
        HoldBannerAnchors();
    }

    void BannerRuntime::AfterWrite() {
        if (BP::loading()) {
            return;
        }
        for (auto& instance : m_bannerInstances) {
            if (instance.system && instance.root) {
                for (auto& base : instance.system->getBones()) {
                    if (base->m_rig.isStaticOrKinematicObject()) {
                        continue;
                    }
                    auto* node = static_cast<SkyrimBone*>(base.get())->m_node.get();
                    if (!node || !node->parent || node->parent->world.scale <= 0) {
                        continue;
                    }
                    const auto& parent = node->parent->world;
                    node->local.rotate = parent.rotate.Transpose() * node->world.rotate;
                    node->local.translate =
                        (parent.rotate.Transpose() * (node->world.translate - parent.translate)) / parent.scale;
                    node->local.scale = node->world.scale / parent.scale;
                }
            }
        }
    }

    void BannerRuntime::Shutdown() {
        auto* world = SkyrimPhysicsWorld::get();
        world->m_tasks.wait();
        for (auto& [id, state] : m_bannerDamage) {
            RestoreBannerDamage(state);
        }
        m_bannerDamage.clear();
        m_bannerAttackCycles.clear();
        for (auto& instance : m_bannerInstances) {
            RemoveSkinnedMeshSystem(instance.system.get());
        }
        m_bannerMagic.clear();
        m_springs.clear();
        m_fabric.clear();
        m_bannerInstances.clear();
        m_bannerReference = {};
        m_bannerRoot.reset();
        m_bannerSystem.reset();
        m_damageTextureModels.clear();
        m_damageWearRanks.clear();
        m_damageGlowTextures.clear();
        m_damageGlowModels.clear();
        std::lock_guard guard(m_damageEventLock);
        m_detachedBannerReferences.clear();
        m_damageResetRequested = false;
    }

    std::vector<std::pair<SkinnedMeshBone*, btVector3>> BannerRuntime::CaptureWindForces() {
        std::vector<std::pair<SkinnedMeshBone*, btVector3>> forces;
        for (auto& instance : m_bannerInstances) {
            for (auto& bone : instance.system->getBones()) {
                if (!bone->m_rig.isStaticOrKinematicObject()) {
                    forces.emplace_back(bone.get(), bone->m_rig.getTotalForce());
                }
            }
        }
        return forces;
    }

    void BannerRuntime::ApplyWindTwist(const std::vector<std::pair<SkinnedMeshBone*, btVector3>>& forces) {
        for (auto& [bone, before] : forces) {
            const std::string_view name(bone->m_name.c_str());
            if (!name.starts_with("BP_FSMP_") || name.empty()) {
                continue;
            }
            const int column = name.back() - '0';
            if (column < 0 || column > 4) {
                continue;
            }
            auto& rig = bone->m_rig;
            const auto stockWind = rig.getTotalForce() - before;
            const float multiplier = BannerWindScaleFactor(bone->m_currentTransform.getScale());
            const auto wind = stockWind * multiplier;

            rig.applyCentralForce(stockWind * (multiplier - 1.0f));
            const auto edge = float(column - 2);
            rig.applyCentralForce(wind * (edge * .225f) +
                                  rig.getWorldTransform().getBasis().getColumn(0) * (wind.length() * edge * .2f));
        }
    }
}

namespace hdt {
    void BannerRuntime::SyncBannerInstance(RE::TESObjectREFR* candidate) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        const auto owner = m_bannerReference.get();
        bool valid = player && owner && !m_loading && !owner->IsDisabled() && IsBannerCellAllowed(owner.get()) &&
                     owner->Get3D() == m_bannerRoot.get() && m_bannerRoot;
        if (valid) {
            const auto d = owner->GetPosition() - player->GetPosition();
            valid = d.x * d.x + d.y * d.y + d.z * d.z <= kBannerActivationRadius * kBannerActivationRadius;
            if (valid && m_bannerRoot->world.scale > 0.0f) {
                auto* node = m_bannerRoot->GetObjectByName("BP_PlayerCollider");
                if (node) {
                    auto center = player->GetPosition();
                    center.z += 65.0f;
                    node->local.translate =
                        (m_bannerRoot->world.rotate.Transpose() * (center - m_bannerRoot->world.translate)) /
                        m_bannerRoot->world.scale;
                    RE::NiUpdateData updateData;
                    node->UpdateWorldData(&updateData);
                }
            }
        }
        if (m_bannerSystem && !valid) {
            RemoveSkinnedMeshSystem(m_bannerSystem.get());
            m_bannerSystem.reset();
            m_bannerRoot.reset();
            m_bannerReference = {};
        }
        if (m_bannerSystem || m_loading || !player || !ui || ui->GameIsPaused()) {
            return;
        }
        RE::TESObjectREFR* selected = candidate;
        if (!selected || !IsBannerCellAllowed(selected)) {
            return;
        }
        auto* root = selected->Get3D()->AsNode();
        if (root->world.scale > 0.0f) {
            if (auto* node = root->GetObjectByName("BP_PlayerCollider")) {
                auto center = player->GetPosition();
                center.z += 65.0f;
                node->local.translate =
                    (root->world.rotate.Transpose() * (center - root->world.translate)) / root->world.scale;
            }
        }
        auto file = DefaultBBP::instance()->scanBBP(root);
        if (file.first != "SKSE\\Plugins\\hdtSkinnedMeshConfigs\\BP_FSMPGeneric01.xml" &&
            file.first != "SKSE\\Plugins\\hdtSkinnedMeshConfigs\\BP_FSMPGeneric02.xml" &&
            file.first != "SKSE\\Plugins\\hdtSkinnedMeshConfigs\\BP_FSMPGeneric03.xml" &&
            file.first != "SKSE\\Plugins\\hdtSkinnedMeshConfigs\\BP_FSMPGeneric04.xml" &&
            file.first.find("SKSE\\Plugins\\hdtSkinnedMeshConfigs\\BP_FSMPAll_") != 0) {
            logger::warn("Marker found but physics path is invalid");
            return;
        }
        auto system = SkyrimSystemCreator().createOrUpdateSystem(root, root, &file, {}, nullptr);
        if (!system) {
            logger::error("Failed to create world cloth system");
            return;
        }
        for (const auto& bone : system->getBones()) {
            if (!bone->m_rig.isStaticOrKinematicObject()) {
                const std::string_view weightBone(bone->m_name.c_str());
                if (weightBone.starts_with("BP_FSMP_") || weightBone.starts_with("BP_RopeBone_")) {
                    BP::ApplyBannerWeight(bone->m_rig, BP::Settings::fBannerWeightMult);
                }
                bone->m_rig.setAngularFactor(btVector3(0, 0, 0));
                bone->m_rig.setAngularVelocity(btVector3(0, 0, 0));
            }
        }

        m_bannerReference = selected->GetHandle();
        m_bannerRoot = hdt::make_nismart(root);
        m_bannerSystem = system;
        AddSkinnedMeshSystem(system.get());
    }

    void BannerRuntime::SyncAllBanners() {
        for (auto it = m_bannerInstances.begin(); it != m_bannerInstances.end();) {
            m_bannerReference = it->reference;
            m_bannerRoot = it->root;
            m_bannerSystem = it->system;
            SyncBannerInstance(nullptr);
            if (!m_bannerSystem) {
                it = m_bannerInstances.erase(it);
            } else {
                ++it;
            }
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        m_bannerScanTimer -= std::clamp(RE::GetSecondsSinceLastFrame(), 0.0f, 0.1f);
        if (!m_loading && player && ui && !ui->GameIsPaused() && m_bannerScanTimer <= 0 &&
            BP::Settings::iMaxPhysicsBanners > 0) {
            m_bannerScanTimer = 1;
            std::vector<RE::ObjectRefHandle> candidates;
            if (auto* tes = RE::TES::GetSingleton()) {
                tes->ForEachReferenceInRange(player, kBannerActivationRadius, [&](RE::TESObjectREFR* ref) {
                    if (!ref || ref->IsDisabled() || !IsBannerCellAllowed(ref) || !ref->Get3D()) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    auto* root = ref->Get3D()->AsNode();
                    if (!root || !root->GetObjectByName("BP_FSMP_0_0")) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    for (const auto& i : m_bannerInstances) {
                        if (i.reference.get().get() == ref) {
                            return RE::BSContainer::ForEachResult::kContinue;
                        }
                    }
                    candidates.push_back(ref->GetHandle());
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            auto distanceToPlayer = [&](RE::ObjectRefHandle handle) {
                auto ref = handle.get();
                if (!ref) {
                    return std::numeric_limits<float>::max();
                }
                const auto d = ref->GetPosition() - player->GetPosition();
                return d.x * d.x + d.y * d.y + d.z * d.z;
            };
            std::sort(candidates.begin(), candidates.end(), [&](auto a, auto b) {
                return distanceToPlayer(a) < distanceToPlayer(b);
            });
            for (auto handle : candidates) {
                if (m_bannerInstances.size() >= static_cast<size_t>(BP::Settings::iMaxPhysicsBanners)) {
                    auto farthest = std::max_element(
                        m_bannerInstances.begin(), m_bannerInstances.end(), [&](const auto& a, const auto& b) {
                            return distanceToPlayer(a.reference) < distanceToPlayer(b.reference);
                        });
                    if (distanceToPlayer(handle) >= distanceToPlayer(farthest->reference) * .9f) {
                        break;
                    }
                    RemoveSkinnedMeshSystem(farthest->system.get());
                    m_bannerInstances.erase(farthest);
                }
                auto ref = handle.get();
                if (!ref) {
                    continue;
                }
                m_bannerReference = {};
                m_bannerRoot.reset();
                m_bannerSystem.reset();
                SyncBannerInstance(ref.get());
                if (m_bannerSystem) {
                    m_bannerInstances.push_back({m_bannerReference, m_bannerRoot, m_bannerSystem, {}});
                    auto& cached = m_bannerInstances.back();
                    for (size_t i = 0; i < cached.bones.size(); ++i) {
                        const auto name = i < 75 ? "BP_FSMP_" + std::to_string(i / 5) + "_" + std::to_string(i % 5)
                                                 : "BP_RopeBone_" + std::to_string(i - 75);
                        cached.bones[i] = cached.system->findBone(name.c_str());
                    }
                }
            }
        }
        std::vector<RE::ProjectileHandle> flying;
        if (!m_bannerInstances.empty()) {
            if (auto* manager = RE::Projectile::Manager::GetSingleton()) {
                std::vector<RE::ProjectileHandle> snapshot;
                {
                    RE::BSSpinLockGuard guard(manager->projectileLock);
                    for (auto h : manager->limited) {
                        snapshot.push_back(h);
                    }
                    for (auto h : manager->unlimited) {
                        snapshot.push_back(h);
                    }
                }
                for (auto h : snapshot) {
                    auto projectile = h.get();
                    if (projectile && projectile->As<RE::ArrowProjectile>()) {
                        flying.push_back(h);
                    }
                }
            }
        }
        for (auto& instance : m_bannerInstances) {
            m_bannerReference = instance.reference;
            m_bannerRoot = instance.root;
            m_bannerSystem = instance.system;
            SyncBannerArrows(instance.arrows, instance.bones, flying);
        }
        if (m_bannerInstances.empty()) {
            m_bannerReference = {};
            m_bannerRoot.reset();
            m_bannerSystem.reset();
        }
    }
}
