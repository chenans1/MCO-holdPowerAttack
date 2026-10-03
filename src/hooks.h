#pragma once

#include "settings.h"
#include "utils.h"

#include <CLibUtilsQTR/Tasker.hpp>
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>


class hooks {
    using ProcessButton_t = void (*)(RE::AttackBlockHandler*, RE::ButtonEvent*, RE::PlayerControlsData*);
    static inline ProcessButton_t _ProcessButton = nullptr;
    enum class LeftAttackRoute : std::uint8_t { kVanilla, kPowerAttack };

    public:
        static inline std::atomic_bool modifierPressed = false;
        static inline std::atomic_bool leftBlockKeyPressed = false;
        static inline std::atomic_bool altBlockKeyPressed = false;

        static inline RE::BGSAction* rightAttackAction = nullptr;
        static inline RE::BGSAction* rightPowerAttackAction = nullptr;

        static void Install() {
            SKSE::log::info("Installing Hooks...");
            REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_AttackBlockHandler[0]};
            const std::uintptr_t original = vtbl.write_vfunc(0x4, &ProcessButtonHook);
            _ProcessButton = reinterpret_cast<ProcessButton_t>(original);
            SKSE::log::info("AttackBlockHandler::ProcessButton Hooked.");


            REL::Relocation<std::uintptr_t> vtblPC{RE::VTABLE_PlayerCharacter[2]};

            _originalPC = vtblPC.write_vfunc(0x1, ProcessEvent_PC);
            SKSE::log::info("Finished Installing Hooks.");
        }

        static bool Load() {
            rightAttackAction = RE::TESForm::LookupByID<RE::BGSAction>(0x13005);
            rightPowerAttackAction = RE::TESForm::LookupByID<RE::BGSAction>(0x13383);

            if (!rightAttackAction || !rightPowerAttackAction) {
                SKSE::log::error("Failed to BGSAction Forms: rightAttackAction={}, rightPowerAttackAction={}", 
                    static_cast<void*>(rightAttackAction), static_cast<void*>(rightPowerAttackAction));
                return false;
            }
            SKSE::log::info("Correctly loaded rightAttackAction={:08X} rightPowerAttackAction={:08X}",
                rightAttackAction->GetFormID(), rightPowerAttackAction->GetFormID());

            auto* inputMgr = RE::BSInputDeviceManager::GetSingleton();
            if (!inputMgr) {
                SKSE::log::warn("BSInputDeviceManager not available; modifier and cancel block input will not work");
            } else {
                inputMgr->AddEventSink(&modifierInputSink);
                SKSE::log::info("Modifier key input sink registered");
            }
            return true;
        }

    private:
        static inline std::atomic_bool alreadyPerformed = false;
        static inline std::atomic_bool rightAttackHeld = false;
        static inline std::atomic_bool holdRepeatArmed = false;
        static inline std::atomic_bool holdRepeatActionQueued = false;
        static inline std::atomic_bool rightAttackUsesVanilla = false;
        static inline std::atomic_bool rightAttackCancelsBlock = false;
        static inline std::atomic_bool rightAttackUsesModifierMode = false;
        static inline std::atomic<LeftAttackRoute> leftAttackRoute = LeftAttackRoute::kVanilla;
        static inline std::atomic_bool bindModifierKey = false;

        class ModifierInputSink final : public RE::BSTEventSink<RE::InputEvent*> {
        public:
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override {
                if (!a_events) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto cfg = settings::Get();
                const int modifierKey = cfg.modifierKey;
                const int altBlockKey = cfg.altBlockKey;
                const int altPowerKey = cfg.altPowerKey;
                if (modifierKey < 0) {
                    modifierPressed.store(false, std::memory_order_relaxed);
                }
                if (altBlockKey < 0) {
                    altBlockKeyPressed.store(false, std::memory_order_relaxed);
                }

                for (auto ev = *a_events; ev != nullptr; ev = ev->next) {
                    auto* btn = ev->AsButtonEvent();
                    if (!btn) continue;

                    const int keyCode = utils::toKeyCode(*btn);

                    if (modifierKey > 0 && modifierKey == keyCode) {
                        if (btn->IsPressed()) {
                            modifierPressed.store(true, std::memory_order_relaxed);
                        } else if (btn->IsUp()) {
                            modifierPressed.store(false, std::memory_order_relaxed);
                        }
                    }

                    if (altBlockKey > 0 && altBlockKey == keyCode) {
                        if (btn->IsPressed()) {
                            altBlockKeyPressed.store(true, std::memory_order_relaxed);
                        } else if (btn->IsUp()) {
                            altBlockKeyPressed.store(false, std::memory_order_relaxed);
                        }
                    }

                    if (cfg.currentMode == 2 && altPowerKey > 0 && altPowerKey == keyCode && btn->IsPressed()) {
                        const bool modifierHeld = modifierPressed.load(std::memory_order_relaxed);
                        if (!cfg.useModifierAltPA || modifierHeld) {
                            auto* player = RE::PlayerCharacter::GetSingleton();
                            const bool blockInputHeld =
                                leftBlockKeyPressed.load(std::memory_order_relaxed) ||
                                altBlockKeyPressed.load(std::memory_order_relaxed);
                            const bool cancelBlock = cfg.eldenCounterMode && player && player->IsBlocking() && !blockInputHeld;
                            PerformAction(rightPowerAttackAction, player, cancelBlock);
                        }
                    }
                }

                return RE::BSEventNotifyControl::kContinue;
            }
        };

        static inline ModifierInputSink modifierInputSink{};
        
        // static bool PerformAction(RE::BGSAction* action, RE::Actor* actor, bool cancelBlock = false) {
        //     if (!action || !actor) return false;

        //     //off hand key must be held
        //     if (cancelBlock) {
        //         actor->AsActorState()->actorState2.wantBlocking = 0;
        //         if (actor->IsBlocking()) {
        //             actor->NotifyAnimationGraph("blockStop");
        //         }
        //     }
        //     actor->AsActorState()->actorState1.meleeAttackState = RE::ATTACK_STATE_ENUM::kSwing;

        //     const bool isPowerAttack = action == rightPowerAttackAction;
        //     utils::forceUpdateAttackData(isPowerAttack);
        //     std::unique_ptr<RE::TESActionData> data(RE::TESActionData::Create());
        //     data->source = RE::NiPointer<RE::TESObjectREFR>(actor);
        //     data->action = action;
        //     using ProcessAction_t = bool (*)(RE::TESActionData*);
        //     REL::Relocation<ProcessAction_t> processAction{ RELOCATION_ID(40551, 41557) };
        //     bool processed = processAction(data.get());
        //     // if (processed) {
        //     //     //force update action attackData
        //     //     const bool isPowerAttack = action == rightPowerAttackAction;
        //     //     utils::forceUpdateAttackData(isPowerAttack);
        //     // }
        //     return processed;
        // }

        static bool PerformAction(RE::BGSAction* action, RE::Actor* actor, bool cancelBlock = false, std::function<void()> onComplete = {}) {
            if (!action || !actor) return false;

            if (auto taskInterface = SKSE::GetTaskInterface()) {
                taskInterface->AddTask([action, actor, cancelBlock, onComplete = std::move(onComplete)]() mutable {
                    //off hand key must be held
                    if (cancelBlock) {
                        if (actor->IsBlocking()) {
                            actor->NotifyAnimationGraph("blockStopInstant");
                        }
                        actor->AsActorState()->actorState2.wantBlocking = 0;
                    }
                    // actor->NotifyAnimationGraph("MCO_PreHitFrame");
                    // actor->AsActorState()->actorState1.meleeAttackState = RE::ATTACK_STATE_ENUM::kSwing;
                    
                    std::unique_ptr<RE::TESActionData> data(RE::TESActionData::Create());
                    data->source = RE::NiPointer<RE::TESObjectREFR>(actor);
                    data->action = action;
                    using ProcessAction_t = bool (*)(RE::TESActionData*);
                    REL::Relocation<ProcessAction_t> processAction{ RELOCATION_ID(40551, 41557) };
                    processAction(data.get());
                    if (onComplete) {
                        onComplete();
                    }
                });
                return true;
            }
            return false;
        }

        //basically the idea is tap release -> light attack, press release -> power attack.
        static void ProcessButtonHook(RE::AttackBlockHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data) {
            static auto* player = RE::PlayerCharacter::GetSingleton();
            static const auto* userEvents = RE::UserEvents::GetSingleton();
            if (!player || !userEvents) {
                return _ProcessButton(a_this, a_event, a_data);
            }
            auto cfg = settings::Get();
            if (!utils::isRightMelee(player)) return _ProcessButton(a_this, a_event, a_data);
            if (a_event->QUserEvent() == userEvents->leftAttack) {
                if (a_event->IsDown()) {
                    leftBlockKeyPressed.store(true, std::memory_order_relaxed);
                } else if (a_event->IsUp()) {
                    leftBlockKeyPressed.store(false, std::memory_order_relaxed);
                }
            }
            // Choose the left-input behavior on the initial press and keep it
            // fixed through release. Vanilla handling retains cast and block/bash behavior.
            if (a_event->QUserEvent() == userEvents->leftAttack) {
                if (a_event->IsDown()) {
                    bool usePowerAttack = false;
                    if (cfg.currentMode == 3) {
                        const bool leftSpell = utils::isLeftSpell(player);
                        const bool stationary = utils::isNeutral();
                        const bool modifierHeld = modifierPressed.load(std::memory_order_relaxed);

                        if (cfg.modifierModeLeftPA > 0) {
                            switch (cfg.modifierModeLeftPA) {
                            case 1: // Modifier forces PA; plain left input keeps cast/block behavior.
                                usePowerAttack = modifierHeld;
                                break;
                            case 2: // Modifier forces PA; plain left casts with a spell, otherwise PA.
                                usePowerAttack = modifierHeld || !leftSpell;
                                break;
                            case 3: // Modifier keeps cast/block behavior; plain left forces PA.
                                usePowerAttack = !modifierHeld;
                                break;
                            case 4: // Modifier casts with a spell, otherwise PA; plain left forces PA.
                                usePowerAttack = !modifierHeld || !leftSpell;
                                break;
                            default:
                                usePowerAttack = false;
                                break;
                            }
                        } else {
                            switch (cfg.defaultBehaviorMode) {
                            case 0: // Stationary uses the default block/cast; movement uses power attack.
                                usePowerAttack = !stationary;
                                break;
                            case 1: // Moving uses the default block/cast; stationary uses power attack.
                                usePowerAttack = stationary;
                                break;
                            case 2: // Stationary spell cast; non-spell input becomes power attack.
                                usePowerAttack = !leftSpell || !stationary;
                                break;
                            case 3: // Moving spell cast; non-spell input becomes power attack.
                                usePowerAttack = !leftSpell || stationary;
                                break;
                            default:
                                usePowerAttack = false;
                                break;
                            }
                        }
                    }

                    const auto route = usePowerAttack ? LeftAttackRoute::kPowerAttack : LeftAttackRoute::kVanilla;
                    leftAttackRoute.store(route, std::memory_order_relaxed);
                    if (route == LeftAttackRoute::kPowerAttack && !PerformAction(rightPowerAttackAction, player)) {
                        // If queuing failed, preserve normal input instead of swallowing it.
                        leftAttackRoute.store(LeftAttackRoute::kVanilla, std::memory_order_relaxed);
                    }
                }

                if (leftAttackRoute.load(std::memory_order_relaxed) == LeftAttackRoute::kPowerAttack) {
                    if (a_event->IsUp()) {
                        leftAttackRoute.store(LeftAttackRoute::kVanilla, std::memory_order_relaxed);
                    }
                    return;
                }

                if (a_event->IsUp()) {
                    leftAttackRoute.store(LeftAttackRoute::kVanilla, std::memory_order_relaxed);
                }
                return _ProcessButton(a_this, a_event, a_data);
            }
            // Defer short inputs until release; issue a power attack once the hold threshold is reached.
            if (a_event->QUserEvent() == userEvents->rightAttack) {
                if (a_event->IsDown()) {
                    rightAttackHeld.store(true, std::memory_order_relaxed);
                    holdRepeatArmed.store(false, std::memory_order_relaxed);
                    alreadyPerformed.store(false, std::memory_order_relaxed);
                    const bool blocking = player->IsBlocking();
                    const bool blockInputHeld =
                        leftBlockKeyPressed.load(std::memory_order_relaxed) ||
                        altBlockKeyPressed.load(std::memory_order_relaxed);
                    const bool cancelBlock = cfg.eldenCounterMode && blocking && !blockInputHeld;
                    // While blocking, either held block key keeps the vanilla bash route.
                    // With Elden Counter enabled and neither held, route to attack/PA instead.
                    rightAttackUsesVanilla.store(blocking && !cancelBlock, std::memory_order_relaxed);
                    rightAttackCancelsBlock.store(cancelBlock, std::memory_order_relaxed);

                    // Modifier mode uses the same vanilla block/bash route as hold mode.
                    // Only take over the press when the normal attack route is selected.
                    const bool useVanilla = rightAttackUsesVanilla.load(std::memory_order_relaxed);
                    const bool useModifierMode = cfg.currentMode == 1 && !useVanilla;
                    rightAttackUsesModifierMode.store(useModifierMode, std::memory_order_relaxed);
                    if (useModifierMode) {
                        const bool modifierHeld = modifierPressed.load(std::memory_order_relaxed);
                        PerformAction(modifierHeld ? rightPowerAttackAction : rightAttackAction, player, rightAttackCancelsBlock.load(std::memory_order_relaxed));
                    }
                }

                if (a_event->IsUp()) {
                    rightAttackHeld.store(false, std::memory_order_relaxed);
                    holdRepeatArmed.store(false, std::memory_order_relaxed);
                }

                // Modifier-right mode chooses an action on button-down and owns the custom
                // attack input through release. Vanilla block/bash routes pass through below.
                if (rightAttackUsesModifierMode.load(std::memory_order_relaxed)) {
                    if (a_event->IsUp()) {  
                        rightAttackUsesModifierMode.store(false, std::memory_order_relaxed);
                    }
                    return;
                }

                // Keep the route selected on button-down until this input is released.
                const bool useVanilla = rightAttackUsesVanilla.load(std::memory_order_relaxed);
                if (useVanilla) {
                    if (a_event->IsUp()) {
                        rightAttackUsesVanilla.store(false, std::memory_order_relaxed);
                        rightAttackCancelsBlock.store(false, std::memory_order_relaxed);
                    }
                    return _ProcessButton(a_this, a_event, a_data);
                }

                if (a_event->IsUp()) {
                    const bool performed = alreadyPerformed.exchange(false, std::memory_order_relaxed);
                    if (a_event->HeldDuration() < cfg.HoldDuration) {
                        if (!performed) {
                            PerformAction(rightAttackAction, player, rightAttackCancelsBlock.load(std::memory_order_relaxed));
                        }
                    } else {
                        if (!performed) {
                            PerformAction(rightPowerAttackAction, player, rightAttackCancelsBlock.load(std::memory_order_relaxed));
                        }
                    }
                    rightAttackUsesVanilla.store(false, std::memory_order_relaxed);
                    rightAttackCancelsBlock.store(false, std::memory_order_relaxed);
                } else if (a_event->IsPressed() && a_event->HeldDuration() >= cfg.HoldDuration) {
                    bool expected = false;
                    if (alreadyPerformed.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                        if (!PerformAction(rightPowerAttackAction, player, rightAttackCancelsBlock.load(std::memory_order_relaxed))) {
                            alreadyPerformed.store(false, std::memory_order_relaxed);
                        } else if (settings::IsBaseHoldRepeatEnabled()) {
                            holdRepeatArmed.store(true, std::memory_order_relaxed);
                        }
                        
                    }
                }
                return;
            }

            return _ProcessButton(a_this, a_event, a_data);
        }
        static RE::BSEventNotifyControl ProcessEvent_PC(
            RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_sink,
            RE::BSAnimationGraphEvent* a_event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource) {

            // Most graph events take this fast path: no config mutex/copy and no tag comparison.
            if (!a_event || !a_event->holder || !settings::IsBaseHoldRepeatEnabled() ||
                !rightAttackHeld.load(std::memory_order_relaxed) ||
                !holdRepeatArmed.load(std::memory_order_relaxed)) {
                return _originalPC(a_sink, a_event, a_eventSource);
            }

            static auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || a_event->holder != player) {
                return _originalPC(a_sink, a_event, a_eventSource);
            }

            // Cache interned names so matching uses BSFixedString pointer equality.
            static const RE::BSFixedString mcoPowerWindowOpen{ "MCO_PowerWinOpen" };
            static const RE::BSFixedString bfcoNextPowerWindowStart{ "BFCO_NextPowerWinStart" };
            if (a_event->tag == mcoPowerWindowOpen || a_event->tag == bfcoNextPowerWindowStart) {
                bool expected = false;
                if (holdRepeatActionQueued.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                    const bool queued = PerformAction(rightPowerAttackAction, player, false, [] {
                        holdRepeatActionQueued.store(false, std::memory_order_relaxed);
                    });
                    if (!queued) {
                        holdRepeatActionQueued.store(false, std::memory_order_relaxed);
                    }
                }
            }

            return _originalPC(a_sink, a_event, a_eventSource);
        }
        static inline REL::Relocation<decltype(ProcessEvent_PC)> _originalPC;
};
