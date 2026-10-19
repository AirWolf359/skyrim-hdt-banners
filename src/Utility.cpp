#include "Utility.h"
#include <filesystem>
#include <dbghelp.h>
#include <mutex>
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

namespace BP::Offsets {
    namespace {
        struct FieldLayout {
            const char* name;
            ULONG tag;
            ULONG offset;
            ULONG64 size;
        };

        struct TypeLayout {
            const char* name;
            ULONG64 size;
            std::initializer_list<FieldLayout> fields;
        };

        struct FunctionLayout {
            const char* name;
            const char* shape;
        };

        const TypeLayout layouts[]{
            {"btVector3",
             16,
             {
                 {"mVec128", 7, 0, 16},
                 {"m_floats", 7, 0, 16},
             }},
            {"btTransform",
             64,
             {
                 {"m_basis", 7, 0, 48},
                 {"m_origin", 7, 48, 16},
             }},
            {"btTypedConstraint",
             80,
             {
                 {"btTypedObject", 18, 8, 4},
                 {"", 25, 0, 8},
                 {"m_userConstraintType", 7, 12, 4},
                 {"m_userConstraintId", 7, 16, 4},
                 {"m_userConstraintPtr", 7, 16, 8},
                 {"m_breakingImpulseThreshold", 7, 24, 4},
                 {"m_isEnabled", 7, 28, 1},
                 {"m_needsFeedback", 7, 29, 1},
                 {"m_overrideNumSolverIterations", 7, 32, 4},
                 {"m_rbA", 7, 40, 8},
                 {"m_rbB", 7, 48, 8},
                 {"m_appliedImpulse", 7, 56, 4},
                 {"m_dbgDrawSize", 7, 60, 4},
                 {"m_jointFeedback", 7, 64, 8},
             }},
            {"btRigidBody",
             784,
             {
                 {"btCollisionObject", 18, 0, 384},
                 {"m_invInertiaTensorWorld", 7, 384, 48},
                 {"m_linearVelocity", 7, 432, 16},
                 {"m_angularVelocity", 7, 448, 16},
                 {"m_inverseMass", 7, 464, 4},
                 {"m_linearFactor", 7, 480, 16},
                 {"m_gravity", 7, 496, 16},
                 {"m_gravity_acceleration", 7, 512, 16},
                 {"m_invInertiaLocal", 7, 528, 16},
                 {"m_totalForce", 7, 544, 16},
                 {"m_totalTorque", 7, 560, 16},
                 {"m_linearDamping", 7, 576, 4},
                 {"m_angularDamping", 7, 580, 4},
                 {"m_additionalDamping", 7, 584, 1},
                 {"m_additionalDampingFactor", 7, 588, 4},
                 {"m_additionalLinearDampingThresholdSqr", 7, 592, 4},
                 {"m_additionalAngularDampingThresholdSqr", 7, 596, 4},
                 {"m_additionalAngularDampingFactor", 7, 600, 4},
                 {"m_linearSleepingThreshold", 7, 604, 4},
                 {"m_angularSleepingThreshold", 7, 608, 4},
                 {"m_optionalMotionState", 7, 616, 8},
                 {"m_constraintRefs", 7, 624, 32},
                 {"m_rigidbodyFlags", 7, 656, 4},
                 {"m_debugBodyId", 7, 660, 4},
                 {"m_deltaLinearVelocity", 7, 672, 16},
                 {"m_deltaAngularVelocity", 7, 688, 16},
                 {"m_angularFactor", 7, 704, 16},
                 {"m_invMass", 7, 720, 16},
                 {"m_pushVelocity", 7, 736, 16},
                 {"m_turnVelocity", 7, 752, 16},
                 {"m_contactSolverType", 7, 768, 4},
                 {"m_frictionSolverType", 7, 772, 4},
             }},
            {"hdt::SkinnedMeshBone",
             1040,
             {
                 {"RE::BSIntrusiveRefCounted", 18, 16, 4},
                 {"", 25, 0, 8},
                 {"m_name", 7, 24, 8},
                 {"m_marginMultipler", 7, 32, 4},
                 {"m_boudingSphereMultipler", 7, 36, 4},
                 {"m_gravityFactor", 7, 40, 4},
                 {"m_windFactor", 7, 44, 4},
                 {"m_rig", 7, 48, 784},
                 {"m_localToRig", 7, 832, 64},
                 {"m_rigToLocal", 7, 896, 64},
                 {"m_currentTransform", 7, 960, 32},
                 {"m_canCollideWithBone", 7, 992, 24},
                 {"m_noCollideWithBone", 7, 1016, 24},
             }},
            {"hdt::BoneScaleConstraint",
             48,
             {
                 {"hdt::RefObject", 18, 0, 16},
                 {"m_scaleA", 7, 16, 4},
                 {"m_scaleB", 7, 20, 4},
                 {"m_boneA", 7, 24, 8},
                 {"m_boneB", 7, 32, 8},
                 {"m_constraint", 7, 40, 8},
             }},
            {"tbb::detail::d1::task_group_context::traits_type", 4, {}},
            {"tbb::detail::d1::task_group_context::kind_type", 4, {}},
            {"hdt::SkinnedMeshSystem",
             152,
             {
                 {"RE::BSIntrusiveRefCounted", 18, 8, 4},
                 {"", 25, 0, 8},
                 {"m_shapeRefs", 7, 16, 24},
                 {"m_world", 7, 40, 8},
                 {"block_resetting", 7, 48, 1},
                 {"m_bones", 7, 56, 24},
                 {"m_meshes", 7, 80, 24},
                 {"m_constraints", 7, 104, 24},
                 {"m_constraintGroups", 7, 128, 24},
             }},
            {"hdt::SkinnedMeshWorld",
             656,
             {
                 {"btDiscreteDynamicsWorldMt", 18, 0, 528},
                 {"m_timeSteps", 7, 528, 24},
                 {"m_systems", 7, 552, 24},
                 {"m_windSpeed", 7, 576, 16},
                 {"m_windTime", 7, 592, 4},
                 {"_bodies", 7, 600, 24},
                 {"_shapes", 7, 624, 24},
             }},
            {"hdt::StiffSpringConstraint",
             144,
             {
                 {"hdt::BoneScaleConstraint", 18, 0, 48},
                 {"btTypedConstraint", 18, 48, 80},
                 {"m_minDistance", 7, 120, 4},
                 {"m_maxDistance", 7, 124, 4},
                 {"m_stiffness", 7, 128, 4},
                 {"m_damping", 7, 132, 4},
                 {"m_equilibriumPoint", 7, 136, 4},
                 {"m_oldDiff", 7, 140, 4},
             }},
            {"hdt::SkyrimSystem",
             192,
             {
                 {"hdt::SkinnedMeshSystem", 18, 0, 152},
                 {"m_skeleton", 7, 152, 8},
                 {"m_oldRoot", 7, 160, 8},
                 {"m_initialized", 7, 168, 1},
                 {"m_windFactor", 7, 172, 4},
                 {"m_lastRootRotation", 7, 176, 16},
             }},
            {"hdt::SkyrimBone",
             1072,
             {
                 {"hdt::SkinnedMeshBone", 18, 0, 1040},
                 {"m_depth", 7, 1040, 4},
                 {"m_node", 7, 1048, 8},
                 {"m_skeleton", 7, 1056, 8},
                 {"m_forceUpdateType", 7, 1064, 4},
             }},
            {"hdt::SkyrimSystemCreator",
             560,
             {
                 {"m_boneIndex", 7, 0, 64},
                 {"m_deferredBuilds", 7, 64, 24},
                 {"m_mesh", 7, 88, 8},
                 {"m_skeleton", 7, 96, 8},
                 {"m_model", 7, 104, 8},
                 {"m_reader", 7, 112, 8},
                 {"m_renameMap", 7, 120, 64},
                 {"m_filePath", 7, 184, 32},
                 {"m_boneTemplates", 7, 216, 64},
                 {"m_genericConstraintTemplates", 7, 280, 64},
                 {"m_stiffSpringConstraintTemplates", 7, 344, 64},
                 {"m_coneTwistConstraintTemplates", 7, 408, 64},
                 {"m_shapes", 7, 472, 64},
                 {"m_shapeRefs", 7, 536, 24},
             }},
            {"hdt::ActorManager",
             0,
             {
                 {"activeSkeletons", 7, 48, 4},
             }},
            {"hdt::SkyrimPhysicsWorld",
             0,
             {
                 {"hdt::SkinnedMeshWorld", 18, 0, 656},
                 {"RE::BSTEventSink<Events::FrameEvent>", 18, 656, 8},
                 {"RE::BSTEventSink<Events::ShutdownEvent>", 18, 664, 8},
                 {"RE::BSTEventSink<SKSE::CameraEvent>", 18, 672, 8},
                 {"RE::BSTEventSink<Events::FrameSyncEvent>", 18, 680, 8},
                 {"m_tasks", 7, 688, 152},
                 {"m_pendingTransformUpdate", 7, 840, 1},
             }},
            {"tbb::detail::d1::task_group_context::state", 1, {}},
            {"tbb::detail::d1::task_group_context::task_group_context_version", 1, {}},
            {"tbb::detail::d1::task_group_context",
             128,
             {
                 {"tbb::detail::d0::no_copy", 18, 0, 1},
                 {"my_cpu_ctl_env", 7, 0, 8},
                 {"my_cancellation_requested", 7, 8, 4},
                 {"my_version", 7, 12, 1},
                 {"my_traits", 7, 13, 1},
                 {"my_may_have_children", 7, 14, 1},
                 {"my_state", 7, 15, 1},
                 {"my_parent", 7, 16, 8},
                 {"my_actual_context", 7, 16, 8},
                 {"my_context_list", 7, 24, 8},
                 {"my_node", 7, 32, 16},
                 {"my_exception", 7, 48, 8},
                 {"my_itt_caller", 7, 56, 8},
                 {"my_name", 7, 64, 8},
                 {"padding", 7, 72, 56},
             }},
            {"tbb::detail::d1::task_group_context::context_traits",
             1,
             {
                 {"fp_settings", 7, 0, 1},
                 {"concurrent_wait", 7, 0, 1},
                 {"bound", 7, 0, 1},
                 {"reserved1", 7, 0, 1},
                 {"reserved2", 7, 0, 1},
                 {"reserved3", 7, 0, 1},
                 {"reserved4", 7, 0, 1},
                 {"reserved5", 7, 0, 1},
             }},
            {"tbb::detail::d1::wait_context_vertex",
             24,
             {
                 {"tbb::detail::d1::wait_tree_vertex_interface", 18, 0, 8},
                 {"m_wait", 7, 8, 16},
             }},
            {"tbb::detail::d1::wait_context",
             16,
             {
                 {"m_version_and_traits", 7, 0, 8},
                 {"m_ref_count", 7, 8, 8},
             }},
        };

        const FunctionLayout functions[]{
            {"hdt::SkyrimPhysicsWorld::get", "F()->P(hdt::SkyrimPhysicsWorld:0)"},
            {"hdt::SkyrimPhysicsWorld::lockSimulation", "F()->std::unique_lock<std::mutex>:16"},
            {"hdt::DefaultBBP::instance", "F()->P(hdt::DefaultBBP:88)"},
            {"hdt::DefaultBBP::scanBBP",
             "F(P(RE::NiNode:272))->std::pair<std::basic_string<char,std::char_traits<char>,std::allocator<char> "
             ">,std::unordered_map<std::basic_string<char,std::char_traits<char>,std::allocator<char> "
             ">,std::unordered_set<std::basic_string<char,std::char_traits<char>,std::allocator<char> "
             ">,std::hash<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::equal_to<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::allocator<std::basic_string<char,std::char_traits<char>,std::allocator<char> > > "
             ">,std::hash<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::equal_to<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::allocator<std::pair<std::basic_string<char,std::char_traits<char>,std::allocator<char> > const "
             ",std::unordered_set<std::basic_string<char,std::char_traits<char>,std::allocator<char> "
             ">,std::hash<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::equal_to<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::allocator<std::basic_string<char,std::char_traits<char>,std::allocator<char> > > > > > > >:96"},
            {"hdt::SkyrimSystemCreator::createOrUpdateSystem",
             "F(P(RE::NiNode:272),P(RE::NiAVObject:272),P(std::pair<std::basic_string<char,std::char_traits<char>,std::"
             "allocator<char> >,std::unordered_map<std::basic_string<char,std::char_traits<char>,std::allocator<char> "
             ">,std::unordered_set<std::basic_string<char,std::char_traits<char>,std::allocator<char> "
             ">,std::hash<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::equal_to<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::allocator<std::basic_string<char,std::char_traits<char>,std::allocator<char> > > "
             ">,std::hash<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::equal_to<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::allocator<std::pair<std::basic_string<char,std::char_traits<char>,std::allocator<char> > const "
             ",std::unordered_set<std::basic_string<char,std::char_traits<char>,std::allocator<char> "
             ">,std::hash<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::equal_to<std::basic_string<char,std::char_traits<char>,std::allocator<char> > "
             ">,std::allocator<std::basic_string<char,std::char_traits<char>,std::allocator<char> > > > > > > "
             ">:96),P(std::unordered_map<RE::detail::BSFixedString<char>,RE::detail::BSFixedString<char>,std::hash<RE::"
             "detail::BSFixedString<char> >,std::equal_to<RE::detail::BSFixedString<char> "
             ">,std::allocator<std::pair<RE::detail::BSFixedString<char> const ,RE::detail::BSFixedString<char> > > "
             ">:64),P(hdt::SkyrimSystem:192))->RE::BSTSmartPointer<hdt::SkyrimSystem,RE::"
             "BSTSmartPointerIntrusiveRefCount>:8"},
            {"hdt::SkyrimSystem::findBone", "F(P(RE::detail::BSFixedString<char>:8))->P(hdt::SkinnedMeshBone:1040)"},
            {"Events::Sources::FrameEventSource::GetSingleton", "F()->P(Events::Sources::FrameEventSource:88)"},
            {"Events::Sources::FrameSyncEventSource::GetSingleton", "F()->P(Events::Sources::FrameSyncEventSource:88)"},
            {"Events::Sources::ShutdownEventEventSource::GetSingleton",
             "F()->P(Events::Sources::ShutdownEventEventSource:88)"},
            {"hdt::SkyrimPhysicsWorld::doUpdate", "F(B8:4)->B1:0"},
            {"hdt::ActorManager::instance", "F()->P(hdt::ActorManager:0)"},
            {"hdt::StiffSpringConstraint::getInfo2", "F(P(btTypedConstraint::btConstraintInfo2:88))->B1:0"},
            {"hdt::SkinnedMeshWorld::applyWind", "F(B8:4)->B1:0"},
        };

        struct SymbolDetection {
            const unsigned char* image;
            const IMAGE_NT_HEADERS64* headers;
            std::vector<unsigned char> file;
            HANDLE session;
            DWORD64 symbolBase = 0;
            bool initialized = false;
            bool valid = true;
            Build result{};
            std::array<bool, std::size(layouts)> checked{};
            std::array<uintptr_t, std::size(functions)> addresses{};
            uintptr_t springVtable = 0;
            std::string reason = "Matching local FSMP symbols are unavailable";

            explicit SymbolDetection(void* module)
                : image(static_cast<const unsigned char*>(module)),
                  headers(reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                      image + reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew)),
                  session(this) {}

            ~SymbolDetection() {
                if (initialized) {
                    SymCleanup(session);
                }
            }

            bool Contains(uintptr_t rva, size_t size) const {
                return rva < headers->OptionalHeader.SizeOfImage && size <= headers->OptionalHeader.SizeOfImage - rva;
            }

            bool Section(uintptr_t rva, size_t size, bool executable) const {
                const auto* sections = IMAGE_FIRST_SECTION(headers);
                for (unsigned i = 0; i < headers->FileHeader.NumberOfSections; ++i) {
                    const auto& section = sections[i];
                    if (rva >= section.VirtualAddress && rva - section.VirtualAddress < section.Misc.VirtualSize &&
                        size <= section.Misc.VirtualSize - (rva - section.VirtualAddress)) {
                        return Contains(rva, size) && (section.Characteristics & IMAGE_SCN_MEM_READ) &&
                               (executable
                                    ? (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0
                                    : (section.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_WRITE)) == 0);
                    }
                }
                return false;
            }

            template <class T> bool Info(ULONG type, IMAGEHLP_SYMBOL_TYPE_INFO query, T& value) const {
                return SymGetTypeInfo(session, symbolBase, type, query, &value) != FALSE;
            }

            std::string Name(ULONG type) const {
                wchar_t* name = nullptr;
                if (!Info(type, TI_GET_SYMNAME, name) || !name) {
                    return {};
                }
                const int length = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
                std::string utf8Name(length > 0 ? size_t(length) : 0, '\0');
                if (length > 0) {
                    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8Name.data(), length, nullptr, nullptr);
                    utf8Name.pop_back();
                }
                LocalFree(name);
                return utf8Name;
            }

            std::vector<ULONG> Children(ULONG type) const {
                ULONG count = 0;
                if (!Info(type, TI_GET_CHILDRENCOUNT, count) || count > 65536) {
                    return {};
                }
                std::vector<ULONG> storage(size_t(count) + 2);
                auto* params = reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(storage.data());
                params->Count = count;
                if (count && !Info(type, TI_FINDCHILDREN, *params)) {
                    return {};
                }
                return {storage.begin() + 2, storage.end()};
            }

            std::string Shape(ULONG type, unsigned depth = 0) const {
                ULONG tag = 0, next = 0;
                ULONG64 size = 0;
                if (depth > 12 || !Info(type, TI_GET_SYMTAG, tag)) {
                    return {};
                }
                if (tag == 14 || tag == 20) {
                    if (!Info(type, TI_GET_TYPEID, next)) {
                        return {};
                    }
                    const auto nested = Shape(next, depth + 1);
                    return nested.empty() ? nested : tag == 14 ? "P(" + nested + ")" : nested;
                }
                if (tag == 13) {
                    if (!Info(type, TI_GET_TYPEID, next)) {
                        return {};
                    }
                    const auto returned = Shape(next, depth + 1);
                    if (returned.empty()) {
                        return {};
                    }
                    std::string signature = "F(";
                    for (const auto child : Children(type)) {
                        ULONG argument = 0;
                        if (!Info(child, TI_GET_TYPEID, argument)) {
                            return {};
                        }
                        const auto shape = Shape(argument, depth + 1);
                        if (shape.empty()) {
                            return {};
                        }
                        if (signature.size() > 2) {
                            signature += ',';
                        }
                        signature += shape;
                    }
                    return signature + ")->" + returned;
                }
                if (!Info(type, TI_GET_LENGTH, size)) {
                    return {};
                }
                if (tag == 16) {
                    ULONG basic = 0;
                    return Info(type, TI_GET_BASETYPE, basic) ? "B" + std::to_string(basic) + ":" + std::to_string(size)
                                                              : "";
                }
                const auto name = Name(type);
                if (name.empty()) {
                    return {};
                }
                return name + ':' +
                       std::to_string(name == "hdt::SkyrimPhysicsWorld" || name == "hdt::ActorManager" ? 0 : size);
            }

            static BOOL CALLBACK Type(PSYMBOL_INFO symbol, ULONG, void* context) {
                auto& self = *static_cast<SymbolDetection*>(context);
                for (size_t index = 0; index < std::size(layouts); ++index) {
                    const auto& layout = layouts[index];
                    if (std::string_view(symbol->Name, symbol->NameLen) != layout.name) {
                        continue;
                    }
                    ULONG64 size = 0;
                    if (!self.Info(symbol->TypeIndex, TI_GET_LENGTH, size) || (layout.size && size != layout.size)) {
                        self.reason = "Incompatible FSMP type: " + std::string(layout.name);
                        self.valid = false;
                        return FALSE;
                    }
                    const auto children = self.Children(symbol->TypeIndex);
                    for (const auto& field : layout.fields) {
                        bool found = false;
                        for (const auto child : children) {
                            ULONG tag = 0, offset = 0, type = 0;
                            ULONG64 fieldSize = 0;
                            if (self.Name(child) != field.name || !self.Info(child, TI_GET_SYMTAG, tag) ||
                                tag != field.tag) {
                                continue;
                            }
                            found = self.Info(child, TI_GET_OFFSET, offset) && offset == field.offset;
                            if (field.size) {
                                found = found && self.Info(child, TI_GET_TYPEID, type) &&
                                        self.Info(type, TI_GET_LENGTH, fieldSize) && fieldSize == field.size;
                            }
                            if (found) {
                                break;
                            }
                        }
                        if (!found) {
                            self.reason = "Incompatible FSMP field: " + std::string(layout.name) + "::" + field.name;
                            self.valid = false;
                            return FALSE;
                        }
                    }
                    if (std::string_view(layout.name) == "hdt::SkyrimPhysicsWorld") {
                        bool found = false;
                        for (const auto child : children) {
                            if (self.Name(child) != "m_loading") {
                                continue;
                            }
                            ULONG offset = 0, type = 0;
                            ULONG64 length = 0;
                            found = self.Info(child, TI_GET_OFFSET, offset) && self.Info(child, TI_GET_TYPEID, type) &&
                                    self.Info(type, TI_GET_LENGTH, length) && length == 1 && offset < size;
                            if (found) {
                                if (self.checked[index] && self.result.loadingFlag != offset) {
                                    found = false;
                                }
                                self.result.loadingFlag = offset;
                            }
                            break;
                        }
                        if (!found) {
                            self.reason = "Incompatible FSMP loading flag";
                            self.valid = false;
                            return FALSE;
                        }
                    }
                    self.checked[index] = true;
                }
                return TRUE;
            }

            bool Function(uintptr_t rva) const {
                if (!Section(rva, 16, true)) {
                    return false;
                }
                const auto directory = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
                if (!Contains(directory.VirtualAddress, directory.Size)) {
                    return false;
                }
                const auto* entries = reinterpret_cast<const RUNTIME_FUNCTION*>(image + directory.VirtualAddress);
                bool entry = false;
                for (size_t i = 0; i < directory.Size / sizeof(RUNTIME_FUNCTION); ++i) {
                    if (entries[i].BeginAddress == rva && entries[i].EndAddress >= rva + 16) {
                        entry = true;
                        break;
                    }
                }
                if (!entry) {
                    return false;
                }
                const auto* sections = IMAGE_FIRST_SECTION(headers);
                for (unsigned i = 0; i < headers->FileHeader.NumberOfSections; ++i) {
                    const auto& section = sections[i];
                    if (rva >= section.VirtualAddress && rva - section.VirtualAddress <= section.SizeOfRawData &&
                        16 <= section.SizeOfRawData - (rva - section.VirtualAddress)) {
                        const auto offset = size_t(section.PointerToRawData) + rva - section.VirtualAddress;
                        return offset <= file.size() && 16 <= file.size() - offset &&
                               std::memcmp(image + rva, file.data() + offset, 16) == 0;
                    }
                }
                return false;
            }

            static BOOL CALLBACK Symbol(PSYMBOL_INFO symbol, ULONG, void* context) {
                auto& self = *static_cast<SymbolDetection*>(context);
                if (symbol->Address < self.symbolBase) {
                    return TRUE;
                }
                const auto rva = uintptr_t(symbol->Address - self.symbolBase);
                const std::string_view rawName(symbol->Name, symbol->NameLen);
                std::array<char, 4096> undecorated{};
                const bool decorated =
                    rawName.starts_with('?') && UnDecorateSymbolName(symbol->Name, undecorated.data(),
                                                                     DWORD(undecorated.size()), UNDNAME_NAME_ONLY) != 0;
                const std::string_view name = decorated ? std::string_view(undecorated.data()) : rawName;
                for (size_t i = 0; i < std::size(functions); ++i) {
                    if (name != functions[i].name) {
                        continue;
                    }
                    if (symbol->Tag != 5 || self.Shape(symbol->TypeIndex) != functions[i].shape) {
                        continue;
                    }
                    if (!self.Function(rva) || (self.addresses[i] && self.addresses[i] != rva)) {
                        self.reason = "Incompatible or ambiguous FSMP function: " + std::string(name);
                        self.valid = false;
                        return FALSE;
                    }
                    self.addresses[i] = rva;
                }
                if (name == "hdt::StiffSpringConstraint::`vftable'" && self.Section(rva, 24, false) && rva >= 8) {
                    uintptr_t locator = 0;
                    std::memcpy(&locator, self.image + rva - 8, sizeof(locator));
                    const auto imageBase = reinterpret_cast<uintptr_t>(self.image);
                    if (locator < imageBase || !self.Contains(locator - imageBase, 24)) {
                        return TRUE;
                    }
                    const auto* info = reinterpret_cast<const ULONG*>(locator);
                    constexpr char springName[] = ".?AVStiffSpringConstraint@hdt@@";
                    if (info[0] == 1 && info[1] == 0 && info[5] == locator - imageBase &&
                        self.Contains(info[3], 16 + sizeof(springName)) &&
                        std::memcmp(self.image + info[3] + 16, springName, sizeof(springName)) == 0) {
                        if (self.springVtable && self.springVtable != rva) {
                            self.valid = false;
                            self.reason = "Ambiguous FSMP spring vtable";
                            return FALSE;
                        }
                        self.springVtable = rva;
                    }
                }
                return TRUE;
            }

            bool Resolve(const wchar_t* path) {
                if (reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_magic != IMAGE_DOS_SIGNATURE ||
                    headers->Signature != IMAGE_NT_SIGNATURE ||
                    headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                    return false;
                }
                auto pdb = std::filesystem::path(path);
                pdb.replace_extension(L".pdb");
                std::error_code fileError;
                if (!std::filesystem::is_regular_file(pdb, fileError) || fileError) {
                    reason = "hdtsmp64.pdb is missing or unreadable";
                    return false;
                }
                std::ifstream input(std::filesystem::path(path), std::ios::binary);
                file.assign(std::istreambuf_iterator<char>(input), {});
                if (file.empty()) {
                    return false;
                }
                initialized = SymInitializeW(session, pdb.parent_path().c_str(), FALSE) != FALSE;
                if (!initialized) {
                    return false;
                }
                symbolBase = SymLoadModuleExW(session, nullptr, path, nullptr, reinterpret_cast<DWORD64>(image),
                                              headers->OptionalHeader.SizeOfImage, nullptr, 0);

                struct {
                    SYMBOL_INFO info{};
                    char name[256]{};
                } worldType;

                worldType.info.SizeOfStruct = sizeof(SYMBOL_INFO);
                worldType.info.MaxNameLen = 256;
                if (!symbolBase ||
                    !SymGetTypeFromName(session, symbolBase, "hdt::SkyrimPhysicsWorld", &worldType.info)) {
                    return false;
                }
                IMAGEHLP_MODULE64 module{};
                module.SizeOfStruct = sizeof(module);
                if (!symbolBase || !SymGetModuleInfo64(session, symbolBase, &module) || module.PdbUnmatched ||
                    !module.LoadedPdbName[0]) {
                    return false;
                }
                const auto debug = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
                bool matched = false;
                if (Contains(debug.VirtualAddress, debug.Size)) {
                    const auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(image + debug.VirtualAddress);
                    for (size_t i = 0; i < debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i) {
                        const auto& entry = entries[i];
                        if (entry.Type == IMAGE_DEBUG_TYPE_CODEVIEW && entry.SizeOfData >= 24 &&
                            Contains(entry.AddressOfRawData, entry.SizeOfData)) {
                            const auto* data = image + entry.AddressOfRawData;
                            ULONG signature = 0, age = 0;
                            std::memcpy(&signature, data, 4);
                            std::memcpy(&age, data + 20, 4);
                            matched = signature == 0x53445352 && std::memcmp(data + 4, &module.PdbSig70, 16) == 0 &&
                                      age == module.PdbAge;
                            if (matched) {
                                break;
                            }
                        }
                    }
                }
                if (!matched) {
                    reason = "FSMP PDB does not match the loaded binary";
                    return false;
                }
                if (!SymEnumTypes(session, symbolBase, Type, this) || !valid ||
                    !std::all_of(checked.begin(), checked.end(), [](bool value) {
                        return value;
                    })) {
                    if (valid) {
                        reason = "Required FSMP type information is missing";
                    }
                    return false;
                }
                if (!SymEnumSymbols(session, symbolBase, "*", Symbol, this) || !valid || !springVtable ||
                    !std::all_of(addresses.begin(), addresses.end(), [](uintptr_t value) {
                        return value != 0;
                    })) {
                    if (valid) {
                        reason = springVtable ? "Required FSMP function information is missing" : "Required FSMP spring vtable is missing";
                    }
                    return false;
                }
                result.sha256 = "detected";
                const std::array<uintptr_t*, 13> targets{
                    &result.world,    &result.lock,        &result.bbp,        &result.scan,           &result.create,
                    &result.findBone, &result.frameSource, &result.syncSource, &result.shutdownSource, &result.doUpdate,
                    &result.actors,   &result.springInfo,  &result.wind};
                for (size_t i = 0; i < targets.size(); ++i) {
                    *targets[i] = addresses[i];
                    result.signatures[i].rva = addresses[i];
                    std::memcpy(result.signatures[i].bytes, image + addresses[i], 16);
                }
                result.springVtable = springVtable;
                return true;
            }
        };
    }

    bool Detect(void* module, const wchar_t* path, std::string* error) {
        static std::mutex mutex;
        static Build detected{};
        std::lock_guard lock(mutex);
        if (!module || !path) {
            if (error) {
                *error = "FSMP module or path is missing";
            }
            return false;
        }
        SymbolDetection detection(module);
        if (!detection.Resolve(path)) {
            if (error) {
                *error = detection.reason;
            }
            return false;
        }
        detected = detection.result;
        selected = &detected;
        return true;
    }
}
