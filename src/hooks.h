#pragma once

#include "settings.h"
#include "utils.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>


class hooks {
    using ProcessButton_t = void (*)(RE::AttackBlockHandler*, RE::ButtonEvent*, RE::PlayerControlsData*);
    static inline ProcessButton_t _ProcessButton = nullptr;

    public:
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

            if (!rightPowerAttackAction) {
                SKSE::log::error("Failed to BGSAction Forms: rightAttackAction={}, rightPowerAttackAction={}", 
                    static_cast<void*>(rightAttackAction), static_cast<void*>(rightPowerAttackAction));
                return false;
            }
            SKSE::log::info("Correctly loaded rightAttackAction={:08X} rightPowerAttackAction={:08X}",
                rightAttackAction->GetFormID(), rightPowerAttackAction->GetFormID());
            return true;
        }

    private:
        static inline bool alreadyPerformed = false;
        // static bool PerformAction(RE::BGSAction* action, RE::Actor* actor) {
        //     bool processed = false;
        //     if (auto* taskInterface = SKSE::GetTaskInterface()) {
        //         taskInterface->AddTask([action, actor]() {
        //             std::unique_ptr<RE::TESActionData> data(RE::TESActionData::Create());
        //             data->source = RE::NiPointer<RE::TESObjectREFR>(actor);
		//             data->action = action;

        //             using ProcessAction_t = bool (*)(RE::TESActionData*);
        //             REL::Relocation<ProcessAction_t> processAction{ RELOCATION_ID(40551, 41557) };
        //             bool processed = processAction(data.get());
        //             if (processed) {
        //                 SKSE::log::info("[PerformAction] Processed action");
        //             } else {
        //                 SKSE::log::info("[PerformAction] Failed to Process action");
        //             }
        //         });
        //     }
        //     return processed;
        // }

        static bool PerformAction(RE::BGSAction* action, RE::Actor* actor) {
            std::unique_ptr<RE::TESActionData> data(RE::TESActionData::Create());
            data->source = RE::NiPointer<RE::TESObjectREFR>(actor);
            data->action = action;

            using ProcessAction_t = bool (*)(RE::TESActionData*);
            REL::Relocation<ProcessAction_t> processAction{ RELOCATION_ID(40551, 41557) };
            bool processed = processAction(data.get());
            if (processed) {
                SKSE::log::info("[PerformAction] Processed action");
            } else {
                SKSE::log::info("[PerformAction] Failed to Process action");
            }
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
            //defer inputs that are sub held duration until release, but if pressed cross threshold -> power attack.  
            if (a_event->QUserEvent() == userEvents->rightAttack) {
                if (a_event->IsUp()) {
                    if (a_event->HeldDuration() < cfg.HoldDuration) {
                        //perform light attack
                        // SKSE::log::info("[ProcessButtonHook] Release light attack");
                        if (!alreadyPerformed) {
                            PerformAction(rightAttackAction, player);
                            alreadyPerformed = false;
                        }
                    } else {
                        //perform power attack
                        // SKSE::log::info("[ProcessButtonHook] Release power attack");
                        if (!alreadyPerformed) {
                            PerformAction(rightPowerAttackAction, player);
                        }
                    }
                    //on release already set this to false, as we're trying to gate pressed hold spam and double power attack on release >= pressed 
                    alreadyPerformed = false;
                } 
                if (a_event->IsDown()) {
                    alreadyPerformed = false;
                } 
                if (a_event->IsPressed()) {
                    if (a_event->HeldDuration() >= cfg.HoldDuration) {
                        // SKSE::log::info("[ProcessButtonHook] Pressed power attack");
                        if (!alreadyPerformed && PerformAction(rightPowerAttackAction, player)){
                            alreadyPerformed = true;
                        }
                    }
                }
                return;
            } else {
                return _ProcessButton(a_this, a_event, a_data);
            }
            return _ProcessButton(a_this, a_event, a_data);
        }
        // inline static REL::Relocation<decltype(ProcessButtonHook)> _ProcessButton;
};
