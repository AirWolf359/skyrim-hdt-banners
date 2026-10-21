#include "BannerCollision.h"
#include "BannerRuntime.h"

namespace hdt {
    void BannerRuntime::CollideBannerWithActors() {
        if (m_loading || m_bannerInstances.empty() || !RE::UI::GetSingleton() ||
            RE::UI::GetSingleton()->GameIsPaused()) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        static std::vector<RE::ObjectRefHandle> actors;
        static std::unordered_map<RE::FormID, std::vector<BannerActorShape>> previous;

        struct PoseNames {
            RE::NiPointer<RE::NiAVObject> root;
            std::array<RE::NiPointer<RE::NiAVObject>, 24> nodes;
            std::vector<RE::NiPointer<RE::NiAVObject>> headgear;
            unsigned generation = 0;
        };

        static std::unordered_map<RE::FormID, PoseNames> poseNames;
        static unsigned poseGeneration = 1;
        static float scan = 0;
        const float dt = std::clamp(RE::GetSecondsSinceLastFrame(), 0.001f, 0.25f);
        scan -= dt;
        if (scan <= 0) {
            actors.clear();
            scan = 0.25f;
            ++poseGeneration;
            RE::TES::GetSingleton()->ForEachReferenceInRange(
                player, kBannerActivationRadius, [&](RE::TESObjectREFR* ref) {
                    if (ref->As<RE::Actor>() && ref->Get3D2() && !ref->IsDisabled()) {
                        actors.push_back(ref->GetHandle());
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            if (std::none_of(actors.begin(), actors.end(), [&](const auto& handle) {
                    return handle.get().get() == player;
                })) {
                actors.push_back(player->GetHandle());
            }
        }

        struct Segment {
            const char* a;
            const char* b;
            float radius;
        };

        static const Segment segments[] = {{"NPC Pelvis [Pelv]", "NPC Spine1 [Spn1]", 11},
                                           {"NPC Spine1 [Spn1]", "NPC Neck [Neck]", 13},
                                           {"NPC Head [Head]", "NPC Head [Head]", 8},
                                           {"NPC Neck [Neck]", "NPC Head [Head]", 4.25f},
                                           {"NPC L UpperArm [LUar]", "NPC L Forearm [LLar]", 5},
                                           {"NPC L Forearm [LLar]", "NPC L Hand [LHnd]", 4},
                                           {"NPC R UpperArm [RUar]", "NPC R Forearm [RLar]", 5},
                                           {"NPC R Forearm [RLar]", "NPC R Hand [RHnd]", 4},
                                           {"NPC L Thigh [LThg]", "NPC L Calf [LClf]", 7},
                                           {"NPC L Calf [LClf]", "NPC L Foot [Lft ]", 5},
                                           {"NPC R Thigh [RThg]", "NPC R Calf [RClf]", 7},
                                           {"NPC R Calf [RClf]", "NPC R Foot [Rft ]", 5}};
        std::unordered_map<RE::FormID, std::vector<BannerActorShape>> next;
        for (const auto& handle : actors) {
            const auto ref = handle.get();
            if (!ref || ref->IsDisabled()) {
                continue;
            }
            auto* root = ref->Get3D2();
            if (!root) {
                continue;
            }
            const float scale = std::clamp(root->world.scale, 0.1f, 10.0f);
            std::vector<BannerActorShape> capsules;
            auto& pose = poseNames[ref->GetFormID()];
            if (pose.root.get() != root || pose.generation != poseGeneration) {
                pose.root = hdt::make_nismart(root);
                pose.generation = poseGeneration;
                for (size_t i = 0; i < std::size(segments); ++i) {
                    pose.nodes[i * 2] = hdt::make_nismart(root->GetObjectByName(segments[i].a));
                    pose.nodes[i * 2 + 1] = hdt::make_nismart(root->GetObjectByName(segments[i].b));
                }
                pose.headgear.clear();
                if (auto* actor = ref->As<RE::Actor>()) {
                    if (const auto& worn = actor->GetBiped(false); worn) {
                        std::unordered_set<RE::NiAVObject*> visited;
                        auto collect = [&](auto&& self, RE::NiAVObject* object) -> void {
                            if (!object || pose.headgear.size() >= 32 || !visited.insert(object).second) {
                                return;
                            }
                            if (object->AsGeometry()) {
                                pose.headgear.push_back(hdt::make_nismart(object));
                            }
                            if (auto* node = object->AsNode()) {
                                for (auto& child : node->GetChildren()) {
                                    self(self, child.get());
                                }
                            }
                        };
                        for (const auto& part : worn->objects) {
                            auto* armor = part.item ? part.item->As<RE::TESObjectARMO>() : nullptr;
                            using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
                            if (armor && armor->GetSlotMask().any(Slot::kHead, Slot::kHair, Slot::kLongHair,
                                                                  Slot::kCirclet, Slot::kEars)) {
                                collect(collect, part.partClone.get());
                            }
                        }
                    }
                }
            }
            size_t segmentIndex = 0;
            for (const auto& segment : segments) {
                auto* a = pose.nodes[segmentIndex++].get();
                auto* b = pose.nodes[segmentIndex++].get();
                if (!a || !b) {
                    continue;
                }
                const auto pa = a->world.translate, pb = b->world.translate;
                BannerActorCapsule contact{convertNi(pa), convertNi(pb), segment.radius * scale + 0.75f};
                if (std::string_view(segment.a) == "NPC Head [Head]") {
                    const auto* neck = pose.nodes[3].get();
                    contact =
                        MakeBannerHeadCapsule(convertNi(pa), neck ? convertNi(neck->world.translate) : convertNi(pa),
                                              convertNi(root->world.rotate * RE::NiPoint3(0, 0, 1)), scale);
                    for (const auto& gear : pose.headgear) {
                        const auto& bound = gear->worldBound;
                        ExpandBannerHeadForEquipment(contact, convertNi(pa), convertNi(bound.center), bound.radius,
                                                     scale);
                    }
                }
                capsules.push_back({contact, reinterpret_cast<std::uintptr_t>(a)});
            }
            const size_t bodyCapsuleCount = capsules.size();
            if (auto* actor = ref->As<RE::Actor>();
                actor && actor->AsActorState()->GetWeaponState() == RE::WEAPON_STATE::kDrawn) {
                const bool firstPerson = actor == player && RE::PlayerCamera::GetSingleton() &&
                                         RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
                const auto& biped = actor->GetBiped(firstPerson);
                if (biped) {
                    auto* left = actor->GetEquippedObject(true);
                    auto* right = actor->GetEquippedObject(false);
                    std::unordered_set<RE::NiAVObject*> visited;
                    for (const auto& part : biped->objects) {
                        auto* weapon = part.item ? part.item->As<RE::TESObjectWEAP>() : nullptr;
                        auto* model = part.partClone.get();
                        if (!weapon || !weapon->IsMelee() || (part.item != left && part.item != right) || !model ||
                            model->GetAppCulled() || !visited.insert(model).second) {
                            continue;
                        }
                        const auto& lower = weapon->boundData.boundMin;
                        const auto& upper = weapon->boundData.boundMax;
                        btTransform world;
                        world.setIdentity();
                        world.setOrigin(convertNi(model->world.translate));
                        const auto x = convertNi(model->world.rotate * RE::NiPoint3(1, 0, 0));
                        const auto y = convertNi(model->world.rotate * RE::NiPoint3(0, 1, 0));
                        const auto z = convertNi(model->world.rotate * RE::NiPoint3(0, 0, 1));
                        world.setBasis(btMatrix3x3(x.x(), y.x(), z.x(), x.y(), y.y(), z.y(), x.z(), y.z(), z.z()));
                        BannerActorCapsule capsule;
                        if (MakeBannerWeaponCapsule(btVector3(lower.x, lower.y, lower.z),
                                                    btVector3(upper.x, upper.y, upper.z), world, model->world.scale,
                                                    capsule)) {
                            const auto identity = reinterpret_cast<std::uintptr_t>(model) ^
                                                  (static_cast<std::uintptr_t>(weapon->GetFormID()) << 32);
                            const bool leftHand =
                                part.item == left &&
                                (part.item != right || &part == &biped->objects[RE::BIPED_OBJECT::kShield]);
                            auto* entry = actor->GetEquippedEntryData(leftHand);
                            auto* enchantment = entry ? entry->GetEnchantment() : nullptr;
                            const auto charge = entry ? entry->GetEnchantmentCharge() : std::nullopt;
                            if (charge && (!std::isfinite(*charge) || *charge <= 0)) {
                                enchantment = nullptr;
                            }
                            capsules.push_back({capsule, identity, enchantment});
                        }
                    }
                }
            }
            if (capsules.empty()) {
                continue;
            }
            const auto old = previous.find(ref->GetFormID());
            const auto* attacker = ref->As<RE::Actor>();
            const bool attacking = attacker && attacker->IsAttacking();
            auto& attackCycle = m_bannerAttackCycles[ref->GetFormID()];
            const auto attackState = attacker ? attacker->GetAttackState() : RE::ATTACK_STATE_ENUM::kNone;
            if (attacking && (!attackCycle.attacking ||
                              (attackState == RE::ATTACK_STATE_ENUM::kDraw && attackCycle.state != attackState) ||
                              (attackState == RE::ATTACK_STATE_ENUM::kSwing && attackCycle.state != attackState &&
                               attackCycle.state != RE::ATTACK_STATE_ENUM::kDraw))) {
                ++attackCycle.serial;
            }
            attackCycle.state = attackState;
            attackCycle.attacking = attacking;
            BannerBounds actorBounds;
            for (const auto& shape : capsules) {
                actorBounds.include(shape.capsule.a, shape.capsule.radius);
                actorBounds.include(shape.capsule.b, shape.capsule.radius);
            }
            if (old != previous.end()) {
                for (const auto& shape : old->second) {
                    actorBounds.include(shape.capsule.a, shape.capsule.radius);
                    actorBounds.include(shape.capsule.b, shape.capsule.radius);
                }
            }
            for (auto& banner : m_bannerInstances) {
                const unsigned gridStep = BannerGridStep(banner.bones);
                const auto owner = banner.reference.get();
                if (!owner) {
                    continue;
                }
                const auto delta = owner->GetPosition() - ref->GetPosition();
                if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z > 600.0f * 600.0f) {
                    continue;
                }
                std::array<SkinnedMeshBone*, 88> bones{};
                std::array<btVector3, 88> positions{}, starts{}, velocities{};
                for (size_t index = 0; index < bones.size(); ++index) {
                    bones[index] = banner.bones[index];
                    if (!bones[index]) {
                        continue;
                    }
                    positions[index] = bones[index]->m_rig.getWorldTransform().getOrigin();
                    starts[index] = banner.worldContactInitialized ? banner.previousPositions[index] : positions[index];
                    if ((positions[index] - starts[index]).length2() > 2048.0f * 2048.0f) {
                        starts[index] = positions[index];
                    }
                    velocities[index] = bones[index]->m_rig.getLinearVelocity();
                }
                BannerBounds clothBounds;
                for (size_t index = 0; index < bones.size(); ++index) {
                    if (bones[index]) {
                        clothBounds.include(positions[index], .1f);
                        clothBounds.include(starts[index], .1f);
                    }
                }

                if (!clothBounds.overlaps(actorBounds)) {
                    continue;
                }
                auto resolve = [&](const std::array<size_t, 3>& indices, const BannerActorCapsule& before,
                                   const BannerActorCapsule& current, float elapsed, bool releaseSlidingContact,
                                   RE::EnchantmentItem* enchantment) {
                    if (!bones[indices[0]] || !bones[indices[1]] || !bones[indices[2]]) {
                        return;
                    }
                    const std::array<btVector3, 3> start{starts[indices[0]], starts[indices[1]], starts[indices[2]]};
                    const std::array<btVector3, 3> original{positions[indices[0]], positions[indices[1]],
                                                            positions[indices[2]]};
                    auto end = original;
                    const std::array<btVector3, 3> originalVelocities{velocities[indices[0]], velocities[indices[1]],
                                                                      velocities[indices[2]]};
                    auto speed = originalVelocities;
                    const std::array<float, 3> inverseMass{bones[indices[0]]->m_rig.getInvMass(),
                                                           bones[indices[1]]->m_rig.getInvMass(),
                                                           bones[indices[2]]->m_rig.getInvMass()};
                    btVector3 weights(0, 0, 0);
                    const std::array<uint8_t, 3> ids{static_cast<uint8_t>(indices[0]), static_cast<uint8_t>(indices[1]),
                                                     static_cast<uint8_t>(indices[2])};
                    if (ResolveBannerActorTriangle(before, current, start, end, speed, inverseMass, elapsed,
                                                   releaseSlidingContact, &weights)) {
                        if (banner.damageApplied &&
                            !BannerDamageTrianglePointPresent(ids, {weights.x(), weights.y(), weights.z()},
                                                              banner.damageMask)) {
                            return;
                        }
                        for (size_t j = 0; j < 3; ++j) {
                            positions[indices[j]] += end[j] - original[j];
                            velocities[indices[j]] += speed[j] - originalVelocities[j];
                        }
                        if (BP::Settings::bEnableDegradation && !releaseSlidingContact && attacking &&
                            std::max((current.a - before.a).length2(), (current.b - before.b).length2()) >
                                625.0f * elapsed * elapsed) {
                            auto* eligible = FindBannerStringData(banner.root.get(), "BP_BannerDamageShapes");
                            if (eligible && eligible->value && *eligible->value) {
                                auto* cell = owner->GetParentCell();
                                auto& state = m_bannerDamage[owner->GetFormID()];
                                state.reference = banner.reference;
                                state.cell = cell ? cell->GetFormID() : 0;
                                if (state.coverage[0].empty()) {
                                    for (int stage = 1; stage <= 4; ++stage) {
                                        const auto name = "BP_BannerDamageMask" + std::to_string(stage);
                                        if (auto* mask = FindBannerStringData(banner.root.get(), name.c_str());
                                            mask && mask->value) {
                                            state.coverage[stage - 1] = mask->value;
                                        }
                                    }
                                }
                                const bool freshHit =
                                    state.lastHitActor != ref->GetFormID() || state.lastHitSwing != attackCycle.serial;
                                RegisterBannerWeaponHit(state.hits, state.lastHitActor, state.lastHitSwing,
                                                        ref->GetFormID(), attackCycle.serial);
                                if (freshHit) {
                                    state.lastHitActor = ref->GetFormID();
                                    state.lastHitSwing = attackCycle.serial;
                                    if (indices[0] < 75) {
                                        ApplyBannerEnchantmentHit(enchantment, banner.reference);
                                    }
                                }
                            }
                        }
                    }
                };
                for (int pass = 0; pass < 3; ++pass) {
                    for (size_t i = 0; i < capsules.size(); ++i) {
                        auto before = capsules[i].capsule;
                        if (old != previous.end() && i < old->second.size() &&
                            BannerActorCanSweep(old->second[i], capsules[i])) {
                            before = old->second[i].capsule;
                        }
                        for (size_t row = 0; row < 14; row += gridStep) {
                            for (size_t col = 0; col < 4; col += gridStep) {
                                const size_t a = row * 5 + col, b = a + gridStep, c = a + 5 * gridStep,
                                             d = c + gridStep;
                                resolve({a, b, c}, before, capsules[i].capsule, dt, i < bodyCapsuleCount,
                                        capsules[i].enchantment);
                                resolve({b, d, c}, before, capsules[i].capsule, dt, i < bodyCapsuleCount,
                                        capsules[i].enchantment);
                            }
                        }
                        for (size_t index = 75; index < 88; ++index) {
                            if (!bones[index] || bones[index]->m_rig.isStaticOrKinematicObject()) {
                                continue;
                            }
                            resolve({index, index, index}, before, capsules[i].capsule, dt, i < bodyCapsuleCount,
                                    capsules[i].enchantment);
                        }
                    }
                }
                for (size_t index = 0; index < bones.size(); ++index) {
                    if (!bones[index] || bones[index]->m_rig.isStaticOrKinematicObject()) {
                        continue;
                    }
                    auto& rig = bones[index]->m_rig;
                    auto transform = rig.getWorldTransform();
                    transform.setOrigin(positions[index]);
                    rig.setWorldTransform(transform);
                    rig.setInterpolationWorldTransform(transform);
                    rig.setLinearVelocity(velocities[index]);
                    rig.setInterpolationLinearVelocity(velocities[index]);
                }
            }
            next.emplace(ref->GetFormID(), std::move(capsules));
        }
        previous = std::move(next);
        for (auto it = poseNames.begin(); it != poseNames.end();) {
            if (!previous.contains(it->first)) {
                it = poseNames.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& [id, cycle] : m_bannerAttackCycles) {
            if (!previous.contains(id)) {
                cycle.attacking = false;
                cycle.state = RE::ATTACK_STATE_ENUM::kNone;
            }
        }
    }
}

namespace hdt {
    void BannerRuntime::HoldBannerAnchors() {
        for (auto& instance : m_bannerInstances) {
            for (int column = 0; column < 5; ++column) {
                auto* base = instance.bones[column];
                if (!base) {
                    continue;
                }
                auto* bone = static_cast<SkyrimBone*>(base);
                auto* node = bone->m_node.get();
                if (!node || !node->parent) {
                    continue;
                }
                node->world = node->parent->world * node->local;
                bone->m_currentTransform = convertNi(node->world);
                const auto transform = bone->m_currentTransform.asTransform() * bone->m_localToRig;
                bone->m_rig.setMassProps(0, {0, 0, 0});
                bone->m_rig.setCollisionFlags(btCollisionObject::CF_KINEMATIC_OBJECT);
                bone->m_rig.setWorldTransform(transform);
                bone->m_rig.setInterpolationWorldTransform(transform);
                bone->m_rig.clearForces();
                bone->m_rig.setLinearVelocity({0, 0, 0});
                bone->m_rig.setAngularVelocity({0, 0, 0});
                bone->m_rig.setInterpolationLinearVelocity({0, 0, 0});
                bone->m_rig.setInterpolationAngularVelocity({0, 0, 0});
                instance.previousPositions[column] = transform.getOrigin();
                instance.wallSupports[column].active = false;
            }
        }
    }

    void BannerRuntime::StabilizeBannerFabric() {
        if (m_loading || m_bannerInstances.empty()) {
            return;
        }
        const auto& fabric = m_fabric;
        for (int pass = 0; pass < 6; ++pass) {
            for (size_t i = 0; i < fabric.size(); ++i) {
                auto* constraint = fabric[pass % 2 ? fabric.size() - 1 - i : i];
                if (!constraint->isEnabled()) {
                    continue;
                }
                auto& a = constraint->getRigidBodyA();
                auto& b = constraint->getRigidBodyB();
                auto pa = a.getWorldTransform().getOrigin(), pb = b.getWorldTransform().getOrigin();
                auto va = a.getLinearVelocity(), vb = b.getLinearVelocity();
                if (!LimitBannerFabricLength(pa, pb, va, vb, a.getInvMass(), b.getInvMass(),
                                             constraint->m_maxDistance * 1.015f)) {
                    continue;
                }
                if (!a.isStaticOrKinematicObject()) {
                    auto tr = a.getWorldTransform();
                    tr.setOrigin(pa);
                    a.setWorldTransform(tr);
                    a.setInterpolationWorldTransform(tr);
                    a.setLinearVelocity(va);
                    a.setInterpolationLinearVelocity(va);
                }
                if (!b.isStaticOrKinematicObject()) {
                    auto tr = b.getWorldTransform();
                    tr.setOrigin(pb);
                    b.setWorldTransform(tr);
                    b.setInterpolationWorldTransform(tr);
                    b.setLinearVelocity(vb);
                    b.setInterpolationLinearVelocity(vb);
                }
            }
        }
    }

    void BannerRuntime::CollideBannerFaces(BannerInstance& instance, RE::bhkWorld* world,
                                           const std::array<btVector3, 88>& starts, int& queryBudget) {
        const unsigned gridStep = BannerGridStep(instance.bones);
        const unsigned faceCount = 112 / (gridStep * gridStep);
        const unsigned firstFace = 16 / (gridStep * gridStep);
        std::array<SkinnedMeshBone*, 75> bones{};
        std::array<btVector3, 75> positions{}, velocities{};
        for (size_t i = 0; i < 75; ++i) {
            bones[i] = instance.bones[i];
            if (!bones[i]) {
                continue;
            }
            positions[i] = bones[i]->m_rig.getWorldTransform().getOrigin();
            velocities[i] = bones[i]->m_rig.getLinearVelocity();
        }
        const float scale = instance.root->world.scale, margin = std::max(.25f, .75f * scale);
        const float dt = std::clamp(RE::GetSecondsSinceLastFrame(), 0.f, .1f);
        auto indices = [gridStep](unsigned face) {
            return BannerGridFace(face, gridStep);
        };
        auto present = [&](const auto& ids, const auto& weights) {
            return !instance.damageApplied ||
                   BannerDamageTrianglePointPresent({uint8_t(ids[0]), uint8_t(ids[1]), uint8_t(ids[2])},
                                                    {weights.x(), weights.y(), weights.z()}, instance.damageMask);
        };
        auto resolve = [&](unsigned face) {
            auto& support = instance.faceSupports[face];
            const auto ids = indices(face);
            if (!present(ids, support.weights)) {
                support.wall.active = false;
                return;
            }
            std::array<btVector3, 3> p{positions[ids[0]], positions[ids[1]], positions[ids[2]]},
                v{velocities[ids[0]], velocities[ids[1]], velocities[ids[2]]};
            const std::array<float, 3> mass{bones[ids[0]]->m_rig.getInvMass(), bones[ids[1]]->m_rig.getInvMass(),
                                            bones[ids[2]]->m_rig.getInvMass()};
            btVector3 point(0, 0, 0);
            for (int i = 0; i < 3; ++i) {
                point += p[i] * support.weights[i];
            }
            if (!BannerWallSupportValid(point, support.wall, margin, scale)) {
                support.wall.active = false;
                return;
            }
            if (ResolveBannerFacePlane(p, v, mass, support.weights, support.wall.point, support.wall.normal, margin)) {
                for (int i = 0; i < 3; ++i) {
                    if (BannerFaceConflictsWithNode(positions[ids[i]], p[i], support.wall.normal,
                                                    instance.wallSupports[ids[i]], margin, scale)) {
                        support.wall.active = false;
                        return;
                    }
                }
                for (int i = 0; i < 3; ++i) {
                    positions[ids[i]] = p[i];
                    velocities[ids[i]] = v[i];
                }
            }
        };
        for (unsigned face = firstFace; face < faceCount; ++face) {
            auto& support = instance.faceSupports[face];
            support.wall.age += dt;
            if (support.wall.active) {
                resolve(face);
            }
        }
        const btVector3 samples[]{{1.f / 3, 1.f / 3, 1.f / 3}, {.5f, .5f, 0}, {0, .5f, .5f}, {.5f, 0, .5f}};
        int queries = 0;
        const unsigned begin = instance.faceProbeCursor;
        for (unsigned scan = 0; scan < faceCount && queries < 48 && queryBudget > 0; ++scan) {
            const unsigned sequence = begin + scan, face = sequence % faceCount;
            instance.faceProbeCursor = sequence + 1;
            if (face < firstFace) {
                continue;
            }
            auto& support = instance.faceSupports[face];
            const auto ids = indices(face);
            if (support.wall.active && support.wall.age < (.20f + .10f * float((face * 37) % 101) / 100.f)) {
                continue;
            }
            auto weights = support.wall.active ? support.weights : samples[(sequence / faceCount) % 4];
            if (!present(ids, weights)) {
                continue;
            }
            btVector3 point(0, 0, 0), before(0, 0, 0);
            float inverse = 0;
            for (int i = 0; i < 3; ++i) {
                point += positions[ids[i]] * weights[i];
                before += starts[ids[i]] * weights[i];
                inverse += bones[ids[i]]->m_rig.getInvMass() * weights[i];
            }
            if (inverse <= 0) {
                continue;
            }
            const auto motion = point - before;
            const bool periodicRecovery = !support.wall.active && BannerFaceRecoveryVisit(sequence, faceCount);
            btVector3 rayStart, end;
            bool twoSided = false;
            if (support.wall.active) {
                rayStart = point + support.wall.normal * (margin + 1);
                end = point - support.wall.normal * (margin + 1);
            } else if (!periodicRecovery && instance.worldContactInitialized && motion.length2() > .0004f &&
                       motion.length2() < 40000) {
                rayStart = before;
                end = point + motion.normalized() * margin;
            } else {
                if (!periodicRecovery && support.wall.age < .5f) {
                    continue;
                }
                auto normal = (positions[ids[1]] - positions[ids[0]]).cross(positions[ids[2]] - positions[ids[0]]);
                if (normal.length2() < .0001f) {
                    continue;
                }
                normal.normalize();
                const float depth = std::clamp(48.f * scale, 24.f, 160.f);
                rayStart = point + normal * depth;
                end = point - normal * depth;
                twoSided = true;
            }
            if (!present(ids, weights)) {
                continue;
            }
            bool found = false;
            float closest = std::numeric_limits<float>::max();
            BannerWorldContact best{};
            auto probe = [&](const btVector3& from, const btVector3& to) {
                if (queryBudget <= 0 || queries >= 48) {
                    return;
                }
                --queryBudget;
                ++queries;
                RE::bhkPickData pick{};
                pick.rayOutput.Reset();
                const float hs = RE::bhkWorld::GetWorldScale();
                pick.rayInput.from = RE::hkVector4(convertBt(from * hs));
                pick.rayInput.to = RE::hkVector4(convertBt(to * hs));
                pick.ray = pick.rayInput.to - pick.rayInput.from;
                pick.rayInput.enableShapeCollectionFilter = true;
                pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kClutter);
                if (!BP::PickBannerSolid(world, pick)) {
                    return;
                }
                alignas(16) float values[4];
                _mm_store_ps(values, pick.rayOutput.normal.quad);
                const btVector3 normal(values[0], values[1], values[2]);
                if (!BannerWorldEntryHit(from, to, normal)) {
                    return;
                }
                const auto contact =
                    ProjectBannerWorldContact(from, to, point, normal, pick.rayOutput.hitFraction, margin, {0, 0, 0});
                const float distance = contact.position.distance2(point);
                if (twoSided && !BannerRecoveryCorrectionAllowed(distance, scale)) {
                    return;
                }
                if (distance < closest) {
                    closest = distance;
                    best = contact;
                    found = true;
                }
            };
            if (twoSided && (queryBudget < 2 || queries > 46)) {
                continue;
            }
            support.wall.age = 0;
            probe(rayStart, end);
            if (twoSided) {
                probe(end, rayStart);
            }
            if (found) {
                support.wall.point = best.surface;
                support.wall.normal = best.normal;
                support.wall.active = true;
                support.weights = weights;
                resolve(face);
            } else {
                support.wall.active = false;
            }
        }
        for (int pass = 0; pass < 3; ++pass) {
            for (unsigned face = firstFace; face < faceCount; ++face) {
                if (instance.faceSupports[face].wall.active) {
                    resolve(face);
                }
            }
            for (size_t i = 10; i < 75; ++i) {
                auto& wall = instance.wallSupports[i];
                if (!bones[i] || !wall.active || bones[i]->m_rig.isStaticOrKinematicObject()) {
                    continue;
                }
                if (!BannerWallSupportValid(positions[i], wall, margin, scale)) {
                    wall.active = false;
                    continue;
                }
                const auto held = HoldBannerWallContact(positions[i], wall.normal, wall.point, margin, velocities[i]);
                positions[i] = held.position;
                velocities[i] = held.velocity;
            }
        }
        for (size_t i = 0; i < 75; ++i) {
            if (bones[i] && !bones[i]->m_rig.isStaticOrKinematicObject()) {
                auto tr = bones[i]->m_rig.getWorldTransform();
                tr.setOrigin(positions[i]);
                bones[i]->m_rig.setWorldTransform(tr);
                bones[i]->m_rig.setInterpolationWorldTransform(tr);
                bones[i]->m_rig.setLinearVelocity(velocities[i]);
                bones[i]->m_rig.setInterpolationLinearVelocity(velocities[i]);
            }
            instance.previousPositions[i] = positions[i];
        }
    }

    void BannerRuntime::CollideBannerWithWorld() {
        auto* ui = RE::UI::GetSingleton();
        if (m_loading || m_bannerInstances.empty() || !ui || ui->GameIsPaused()) {
            return;
        }
        const float havokScale = RE::bhkWorld::GetWorldScale();
        int faceQueryBudget = 192;
        int recoveryBudget = 192;
        const size_t first = m_worldProbeCursor++ % m_bannerInstances.size();
        for (size_t ordinal = 0; ordinal < m_bannerInstances.size(); ++ordinal) {
            auto& instance = m_bannerInstances[(first + ordinal) % m_bannerInstances.size()];
            auto owner = instance.reference.get();
            if (!owner || !instance.system || !instance.root) {
                continue;
            }
            auto* cell = owner->GetParentCell();
            auto* world = cell ? cell->GetbhkWorld() : nullptr;
            if (!world) {
                continue;
            }
            if (instance.recoveryNormal.length2() < .5f) {
                auto* a = instance.bones[0];
                auto* b = instance.bones[4];
                auto* c = instance.bones[70];
                if (a && b && c) {
                    BannerRecoveryFrame(
                        a->m_rig.getWorldTransform().getOrigin(), b->m_rig.getWorldTransform().getOrigin(),
                        c->m_rig.getWorldTransform().getOrigin(), instance.recoveryNormal, instance.recoveryDown);
                }
            }
            const auto faceStarts = instance.previousPositions;
            std::array<bool, 88> checkedThisFrame{};
            int recoveryQueries = 0;
            bool recovered = false;
            const int recoveryAllowance = std::min(12, std::max(2, 192 / int(m_bannerInstances.size()))) & ~1;
            const unsigned recoveryBegin = instance.recoveryProbeCursor;
            instance.recoveryProbeCursor += 6;
            for (size_t scan = 0; scan < 88; ++scan) {
                const size_t index = (recoveryBegin + scan) % 88;
                const float margin = std::max(.25f, (index < 75 ? .75f : 1.25f) * instance.root->world.scale);
                auto* bone = instance.bones[index];
                if (!bone) {
                    continue;
                }
                if (index < 10) {
                    instance.wallSupports[index].active = false;
                    instance.previousPositions[index] = bone->m_rig.getWorldTransform().getOrigin();
                    continue;
                }
                auto position = bone->m_rig.getWorldTransform().getOrigin();
                auto& history = instance.worldSweeps[index];
                const bool checkMotion = history.due(position, m_worldProbeCursor, index + owner->GetFormID(),
                                                     instance.wallSupports[index].active);
                checkedThisFrame[index] = checkMotion || !instance.worldContactInitialized;
                const auto start = history.checked;
                const auto motion = position - start;
                auto& support = instance.wallSupports[index];
                support.age += std::clamp(RE::GetSecondsSinceLastFrame(), 0.0f, 0.1f);
                bool refreshSupport = false;
                if (support.active && !bone->m_rig.isStaticOrKinematicObject()) {
                    if (!BannerWallSupportValid(position, support, margin, instance.root->world.scale)) {
                        support.active = false;
                    } else {
                        const auto held = HoldBannerWallContact(position, support.normal, support.point, margin,
                                                                bone->m_rig.getLinearVelocity());
                        position = held.position;
                        auto transform = bone->m_rig.getWorldTransform();
                        transform.setOrigin(position);
                        bone->m_rig.setWorldTransform(transform);
                        bone->m_rig.setInterpolationWorldTransform(transform);
                        bone->m_rig.setLinearVelocity(held.velocity);
                        bone->m_rig.setInterpolationLinearVelocity(held.velocity);
                        refreshSupport = support.age >= (.20f + .10f * float((index * 37) % 101) / 100.f);
                    }
                }
                const bool sweep =
                    checkMotion && !support.active && motion.length2() > 0.0004f && motion.length2() < 40000.0f;
                if (instance.worldContactInitialized && !bone->m_rig.isStaticOrKinematicObject() &&
                    (sweep || refreshSupport)) {
                    const auto rayStart = refreshSupport ? position + support.normal * (margin + 1) : start;
                    const auto end = refreshSupport ? position - support.normal * (margin + 1)
                                                    : position + motion.normalized() * margin;
                    RE::bhkPickData pick{};
                    pick.rayOutput.Reset();
                    pick.rayInput.from = RE::hkVector4(convertBt(rayStart * havokScale));
                    pick.rayInput.to = RE::hkVector4(convertBt(end * havokScale));
                    pick.ray = pick.rayInput.to - pick.rayInput.from;
                    pick.rayInput.enableShapeCollectionFilter = true;
                    pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kClutter);

                    if (BP::PickBannerSolid(world, pick)) {
                        alignas(16) float components[4];
                        _mm_store_ps(components, pick.rayOutput.normal.quad);
                        btVector3 normal(components[0], components[1], components[2]);
                        if (BannerWorldEntryHit(rayStart, end, normal)) {
                            const auto contact =
                                ProjectBannerWorldContact(rayStart, end, position, normal, pick.rayOutput.hitFraction,
                                                          margin, bone->m_rig.getLinearVelocity());
                            position = contact.position;
                            support.point = contact.surface;
                            support.normal = contact.normal;
                            support.age = 0;
                            support.active = true;
                            auto transform = bone->m_rig.getWorldTransform();
                            transform.setOrigin(position);
                            bone->m_rig.setWorldTransform(transform);
                            bone->m_rig.setInterpolationWorldTransform(transform);
                            bone->m_rig.setLinearVelocity(contact.velocity);
                            bone->m_rig.setInterpolationLinearVelocity(contact.velocity);
                        } else {
                            if (refreshSupport) {
                                support.active = false;
                            }
                        }
                    } else if (refreshSupport) {
                        support.active = false;
                    }
                }
                if (index < 75 && !support.active && !bone->m_rig.isStaticOrKinematicObject() &&
                    (!instance.worldContactInitialized ||
                     support.age >= (.12f + .06f * float((index * 37) % 101) / 100.f)) &&
                    recoveryQueries + 2 <= recoveryAllowance && recoveryBudget >= 2) {
                    const unsigned axis = (recoveryBegin / 88) % 4;
                    auto direction = instance.recoveryNormal;
                    if (axis == 2) {
                        direction = instance.recoveryDown;
                    } else if (axis == 3) {
                        direction = instance.recoveryNormal.cross(instance.recoveryDown);
                    }
                    if (direction.length2() < .5f) {
                        continue;
                    }
                    direction.normalize();
                    const float depth = std::clamp(48.f * instance.root->world.scale, 24.f, 160.f);
                    bool found = false;
                    float closest = std::numeric_limits<float>::max();
                    BannerWorldContact best{};
                    std::array<BannerWorldContact, 2> enclosedContacts{};
                    std::array<const RE::hkpCollidable*, 2> enclosedSolids{};
                    for (int side : {-1, 1}) {
                        const auto from = position + direction * (depth * float(side));
                        const auto to = position - direction * (depth * float(side));
                        RE::bhkPickData recovery{};
                        recovery.rayOutput.Reset();
                        recovery.rayInput.from = RE::hkVector4(convertBt(from * havokScale));
                        recovery.rayInput.to = RE::hkVector4(convertBt(to * havokScale));
                        recovery.ray = recovery.rayInput.to - recovery.rayInput.from;
                        recovery.rayInput.enableShapeCollectionFilter = true;
                        recovery.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kClutter);
                        ++recoveryQueries;
                        --recoveryBudget;

                        if (BP::PickBannerSolid(world, recovery)) {
                            alignas(16) float components[4];
                            _mm_store_ps(components, recovery.rayOutput.normal.quad);
                            btVector3 normal(components[0], components[1], components[2]);
                            if (!BannerWorldEntryHit(from, to, normal)) {
                                continue;
                            }
                            const auto contact =
                                ProjectBannerWorldContact(from, to, position, normal, recovery.rayOutput.hitFraction,
                                                          margin, bone->m_rig.getLinearVelocity());
                            const float displacement = contact.position.distance2(position);
                            const size_t sideIndex = side < 0 ? 0 : 1;
                            enclosedContacts[sideIndex] = contact;
                            enclosedSolids[sideIndex] = recovery.rayOutput.rootCollidable;
                            if (!BannerRecoveryCorrectionAllowed(displacement, instance.root->world.scale)) {
                                continue;
                            }
                            if (displacement < closest) {
                                best = contact;
                                closest = displacement;
                                found = true;
                            }
                        }
                    }
                    if (enclosedSolids[0] && enclosedSolids[1] &&
                        BannerEnclosedRecoveryAllowed(position, enclosedContacts[0], enclosedContacts[1],
                                                      enclosedSolids[0] == enclosedSolids[1],
                                                      instance.root->world.scale)) {
                        for (const auto& contact : enclosedContacts) {
                            const float displacement = contact.position.distance2(position);
                            if (displacement < closest) {
                                best = contact;
                                closest = displacement;
                                found = true;
                            }
                        }
                    }
                    support.age = 0;
                    if (found && closest > .000001f) {
                        position = best.position;
                        support.point = best.surface;
                        support.normal = best.normal;
                        support.active = true;
                        auto transform = bone->m_rig.getWorldTransform();
                        transform.setOrigin(position);
                        bone->m_rig.setWorldTransform(transform);
                        bone->m_rig.setInterpolationWorldTransform(transform);
                        bone->m_rig.setLinearVelocity(best.velocity);
                        bone->m_rig.setInterpolationLinearVelocity(best.velocity);
                        recovered = true;
                    }
                }
                instance.previousPositions[index] = position;
                if (support.active) {
                    checkedThisFrame[index] = true;
                }
            }
            const auto beforeFaceCorrection = instance.previousPositions;
            if (recovered) {
                for (auto& face : instance.faceSupports) {
                    face.wall.active = false;
                }
            } else {
                const int allowance = std::min(faceQueryBudget, std::max(2, 192 / int(m_bannerInstances.size())));
                int localBudget = allowance;
                CollideBannerFaces(instance, world, faceStarts, localBudget);
                faceQueryBudget -= allowance - localBudget;
            }
            for (size_t i = 10; i < 88; ++i) {
                if (instance.bones[i]) {
                    const auto finalPosition = instance.bones[i]->m_rig.getWorldTransform().getOrigin();
                    if (checkedThisFrame[i] || instance.wallSupports[i].active) {
                        instance.worldSweeps[i].commit(finalPosition);
                    } else {
                        instance.worldSweeps[i].checked += finalPosition - beforeFaceCorrection[i];
                    }
                }
            }
            instance.worldContactInitialized = true;
        }
    }
}
