#include "BannerRuntime.h"
#include "FSMPBridge.h"

namespace {
    // CommonLibSSE-NG declares BSMultiBoundRoom's own members at fixed offsets. In a build that targets several
    // runtimes, the base classes' runtime-specific members are left out, so `room->joinedMultiBoundList` reads the
    // wrong field on every runtime. Use the real offsets: 0x168 on SE/AE, 0x190 on VR.
    RE::NiTPointerList<RE::NiPointer<RE::BSMultiBound>>& JoinedBannerBounds(RE::BSMultiBoundRoom* room) {
        return REL::RelocateMember<RE::NiTPointerList<RE::NiPointer<RE::BSMultiBound>>>(room, 0x168, 0x190);
    }

    float BannerBoundDistanceSquared(RE::BSMultiBound* bound, const RE::NiPoint3& point) {
        auto* box = bound && bound->data ? netimmerse_cast<RE::BSMultiBoundAABB*>(bound->data.get()) : nullptr;
        if (!box || box->size.x < 0 || box->size.y < 0 || box->size.z < 0) {
            return std::numeric_limits<float>::infinity();
        }
        auto delta = point - box->center;
        if (auto* oriented = netimmerse_cast<RE::BSMultiBoundOBB*>(box)) {
            delta = oriented->rotation.Transpose() * delta;
        }
        delta.x = std::max(std::abs(delta.x) - box->size.x, 0.0f);
        delta.y = std::max(std::abs(delta.y) - box->size.y, 0.0f);
        delta.z = std::max(std::abs(delta.z) - box->size.z, 0.0f);
        return delta.SqrLength();
    }

    RE::BSMultiBoundRoom* NearestBannerRoom(const RE::BSTArray<RE::NiPointer<RE::BSMultiBoundRoom>>& rooms,
                                            const RE::NiPoint3& center, float range) {
        if (!std::isfinite(range) || range <= 0) {
            return nullptr;
        }
        RE::BSMultiBoundRoom* selected = nullptr;
        float nearest = range * range;
        bool tied = false;
        for (const auto& room : rooms) {
            if (!room) {
                continue;
            }
            float distance = BannerBoundDistanceSquared(room->GetRuntimeData().multiBound.get(), center);
            for (const auto& joined : JoinedBannerBounds(room.get())) {
                distance = std::min(distance, BannerBoundDistanceSquared(joined.get(), center));
            }
            if (!std::isfinite(distance)) {
                continue;
            }
            if (distance < nearest) {
                selected = room.get();
                nearest = distance;
                tied = false;
            } else if (distance == nearest && selected && selected != room.get()) {
                tied = true;
            }
        }
        return tied ? nullptr : selected;
    }

    bool BannerRoomContains(RE::BSMultiBoundRoom* room, const RE::NiPoint3& point) {
        auto* bound = room ? room->GetRuntimeData().multiBound.get() : nullptr;
        if (!bound) {
            return false;
        }
        if (bound->QWithinPoint(point)) {
            return true;
        }
        for (const auto& joined : JoinedBannerBounds(room)) {
            if (joined && joined->QWithinPoint(point)) {
                return true;
            }
        }
        return false;
    }

    RE::BSMultiBoundRoom* SelectBannerRoom(const RE::BSTArray<RE::NiPointer<RE::BSMultiBoundRoom>>& rooms,
                                           RE::BSMultiBoundRoom* current, const RE::NiPoint3& anchor,
                                           const RE::NiPoint3& center) {
        std::vector<RE::BSMultiBoundRoom*> centers, anchors;
        for (const auto& room : rooms) {
            auto* bound = room ? room->GetRuntimeData().multiBound.get() : nullptr;
            if (!bound) {
                continue;
            }
            if (BannerRoomContains(room.get(), center)) {
                centers.push_back(room.get());
            }
            if (BannerRoomContains(room.get(), anchor)) {
                anchors.push_back(room.get());
            }
        }
        if (std::find(centers.begin(), centers.end(), current) != centers.end()) {
            return current;
        }
        if (centers.size() == 1) {
            return centers.front();
        }
        if (centers.empty() && anchors.size() == 1) {
            return anchors.front();
        }
        const auto& candidates = centers.empty() ? anchors : centers;
        RE::BSMultiBoundRoom* selected = nullptr;
        float nearest = std::numeric_limits<float>::max();
        bool tied = false;
        for (auto* room : candidates) {
            auto* bound = room->GetRuntimeData().multiBound.get();
            if (!bound || !bound->data) {
                continue;
            }
            RE::NiBound roomBound;
            bound->data->GetNiBound(&roomBound);
            const auto delta = center - roomBound.center;
            const float distance = delta.SqrLength();
            if (!std::isfinite(distance)) {
                continue;
            }
            if (distance < nearest) {
                nearest = distance;
                selected = room;
                tied = false;
            } else if (distance == nearest && selected != room) {
                tied = true;
            }
        }
        return tied ? nullptr : selected;
    }
}

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
        {
            auto lock = world->lockSimulation();
        }
        m_loading = BP::loading();
        BP::BeginBannerWorldQueries();
        SyncAllBanners();
        ResolveBannerRooms();
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

    void BannerRuntime::ResolveBannerRooms() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        if (m_loading || !ui || ui->GameIsPaused() || !cell || !cell->IsInteriorCell() || !cell->IsAttached()) {
            return;
        }
        auto* loaded = cell->GetRuntimeData().loadedData;
        if (!loaded || !loaded->refsFullyLoaded || loaded->criticalQueuedRefCount || loaded->queuedRefCount) {
            return;
        }
        auto* graph = loaded->portalGraph.get();
        if (!graph || graph->cellID != cell->GetFormID() || graph->rooms.empty() || graph->rooms.size() > 4096 ||
            graph->alwaysRenderChildren.size() > 100000) {
            return;
        }
        for (auto& instance : m_bannerInstances) {
            if (instance.roomResolved) {
                continue;
            }
            auto ref = instance.reference.get();
            auto* root = instance.root.get();
            if (!ref || ref->IsDisabled() || !root || ref->Get3D() != root || ref->GetParentCell() != cell) {
                continue;
            }
            if (std::any_of(graph->alwaysRenderChildren.begin(), graph->alwaysRenderChildren.end(),
                            [root](const auto& child) {
                                return child.get() == root;
                            })) {
                instance.roomResolved = true;
                continue;
            }
            RE::BSMultiBoundRoom* current = nullptr;
            auto* ancestor = root->parent;
            for (unsigned depth = 0; ancestor && depth < 32; ++depth, ancestor = ancestor->parent) {
                current = netimmerse_cast<RE::BSMultiBoundRoom*>(ancestor);
                if (current) {
                    break;
                }
            }
            RE::NiPoint3 center{};
            unsigned count = 0;
            for (unsigned i = 0; i < 75; ++i) {
                auto* bone = instance.bones[i];
                auto* node = bone ? static_cast<SkyrimBone*>(bone)->m_node.get() : nullptr;
                if (node) {
                    center += node->world.translate;
                    ++count;
                }
            }
            center = count ? center / static_cast<float>(count) : root->worldBound.center;
            auto* selected = SelectBannerRoom(graph->rooms, current, root->world.translate, center);
            instance.roomResolved = true;
            unsigned centerRooms = 0, anchorRooms = 0;
            for (const auto& room : graph->rooms) {
                centerRooms += BannerRoomContains(room.get(), center);
                anchorRooms += BannerRoomContains(room.get(), root->world.translate);
            }
            if (!selected && !centerRooms && !anchorRooms) {
                float extentSquared = 0;
                for (unsigned i = 0; i < 75; ++i) {
                    auto* bone = instance.bones[i];
                    auto* node = bone ? static_cast<SkyrimBone*>(bone)->m_node.get() : nullptr;
                    if (node) {
                        extentSquared = std::max(extentSquared, (node->world.translate - center).SqrLength());
                    }
                }
                const float range = 2.0f * std::sqrt(extentSquared);
                selected = NearestBannerRoom(graph->rooms, center, range);
            }
            if (!selected || selected == current || selected->world.scale <= .0001f) {
                continue;
            }
            bool cycle = false;
            ancestor = selected;
            for (unsigned depth = 0; ancestor && depth < 32; ++depth, ancestor = ancestor->parent) {
                if (ancestor == root) {
                    cycle = true;
                    break;
                }
            }
            if (cycle) {
                continue;
            }
            const auto world = root->world;
            const auto& parent = selected->world;
            RE::NiTransform local;
            local.rotate = parent.rotate.Transpose() * world.rotate;
            local.translate = (parent.rotate.Transpose() * (world.translate - parent.translate)) / parent.scale;
            local.scale = world.scale / parent.scale;
            selected->AttachChild(root, true);
            if (root->parent == selected) {
                root->local = local;
            } else {
                logger::error("Banner room attachment failed for {:08X}", ref->GetFormID());
            }
        }
    }

    void BannerRuntime::Shutdown() {
        auto* world = SkyrimPhysicsWorld::get();
        {
            auto lock = world->lockSimulation();
        }
        for (auto& [id, state] : m_bannerDamage) {
            RestoreBannerDamage(state);
        }
        m_bannerDamage.clear();
        m_bannerAttackCycles.clear();
        for (auto& instance : m_bannerInstances) {
            RemoveSkinnedMeshSystem(instance.system.get());
        }
        m_bannerMagic.clear();
        m_areaHazards.clear();
        m_hazardScanAge = .5f;
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
        m_areaCasts.clear();
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
                    if (!BannerGridStep(cached.bones)) {
                        logger::error("Unsupported banner cloth grid; skipped");
                        m_bannerInstances.pop_back();
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
