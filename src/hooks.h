#pragma once

#include "settings.h"
#include "utils.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>


class hooks {
    using ProcessButton_t = void (*)(RE::AttackBlockHandler*, RE::ButtonEvent*, RE::PlayerControlsData*);
    static inline ProcessButton_t _ProcessButton = nullptr;

    public:
        static inline std::atomic_bool modifierPressed = false;
        static inline std::atomic_bool cancelBlockKeyPressed = false;

        static inline RE::BGSAction* rightAttackAction = nullptr;
        static inline RE::BGSAction* rightPowerAttackAction = nullptr;

        static void Install() {
            SKSE::log::info("Installing Hooks...");
            REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_AttackBlockHandler[0]};
            const std::uintptr_t original = vtbl.write_vfunc(0x4, &ProcessButtonHook);
            _ProcessButton = reinterpret_cast<ProcessButton_t>(original);
            SKSE::log::info("AttackBlockHandler::ProcessButton Hooked.");
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
        static inline std::atomic_bool rightAttackUsesVanilla = false;
        static inline std::atomic_bool rightAttackCancelsBlock = false;
        static inline std::atomic_bool rightAttackUsesModifierMode = false;
        static inline std::atomic_int trackedModifierKey = -2;
        static inline std::atomic_int trackedCancelBlockKey = -2;
        static inline std::atomic_bool bindModifierKey = false;

        class ModifierInputSink final : public RE::BSTEventSink<RE::InputEvent*> {
        public:
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override {
                if (!a_events) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto cfg = settings::Get();
                const int modifierKey = cfg.modifierKey;
                const int cancelBlockKey = cfg.cancelBlockKey;
                if (trackedModifierKey.exchange(modifierKey, std::memory_order_relaxed) != modifierKey) {
                    modifierPressed.store(false, std::memory_order_relaxed);
                }
                if (trackedCancelBlockKey.exchange(cancelBlockKey, std::memory_order_relaxed) != cancelBlockKey) {
                    cancelBlockKeyPressed.store(false, std::memory_order_relaxed);
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

                    if (cancelBlockKey >= 0 && cancelBlockKey == keyCode) {
                        if (btn->IsPressed()) {
                            cancelBlockKeyPressed.store(true, std::memory_order_relaxed);
                        } else if (btn->IsUp()) {
                            cancelBlockKeyPressed.store(false, std::memory_order_relaxed);
                        }
                    }
                }

                return RE::BSEventNotifyControl::kContinue;
            }
        };

        static inline ModifierInputSink modifierInputSink{};
        
        static bool PerformAction(RE::BGSAction* action, RE::Actor* actor, bool cancelBlock = false) {
            if (!action || !actor) return false;

            //off hand key must be held
            if (actor->IsBlocking() && cancelBlock) {
                actor->AsActorState()->actorState2.wantBlocking = 0;
                actor->NotifyAnimationGraph("blockStop");
                SKSE::log::info("[PerformAction] cleared block state");
            }

            std::unique_ptr<RE::TESActionData> data(RE::TESActionData::Create());
            data->source = RE::NiPointer<RE::TESObjectREFR>(actor);
            data->action = action;

            using ProcessAction_t = bool (*)(RE::TESActionData*);
            REL::Relocation<ProcessAction_t> processAction{ RELOCATION_ID(40551, 41557) };
            bool processed = processAction(data.get());
            // if (processed) {
            //     SKSE::log::info("[PerformAction] Processed action");
            // } 
            return processed;
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
            // Defer short inputs until release; issue a power attack once the hold threshold is reached.
            if (a_event->QUserEvent() == userEvents->rightAttack) {
                if (a_event->IsDown()) {
                    alreadyPerformed.store(false, std::memory_order_relaxed);
                    const bool cancelBlock = cfg.eldenCounterMode
                        && player->IsBlocking()
                        && cancelBlockKeyPressed.load(std::memory_order_relaxed);
                    rightAttackUsesVanilla.store(player->IsBlocking() && !cancelBlock, std::memory_order_relaxed);
                    rightAttackCancelsBlock.store(cancelBlock, std::memory_order_relaxed);

                    // Modifier mode uses the same vanilla block/bash route as hold mode.
                    // Only take over the press when the normal attack route is selected.
                    const bool useVanilla = rightAttackUsesVanilla.load(std::memory_order_relaxed);
                    const bool useModifierMode = cfg.modifierRightMode && !useVanilla;
                    rightAttackUsesModifierMode.store(useModifierMode, std::memory_order_relaxed);
                    if (useModifierMode) {
                        const bool modifierHeld = modifierPressed.load(std::memory_order_relaxed);
                        PerformAction(modifierHeld ? rightPowerAttackAction : rightAttackAction, player,
                            rightAttackCancelsBlock.load(std::memory_order_relaxed));
                    }
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
                        }
                    }
                }
                return;
            }

            return _ProcessButton(a_this, a_event, a_data);
        }

        // Modifier-key input tracking is handled by ModifierInputSink.
};
