#pragma once

#include "settings.h"
#include "utils.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>

using ProcessButton = void (*)(RE::AttackBlockHandler*, RE::ButtonEvent*, RE::PlayerControlsData*);

class hooks {
    public:
        static inline RE::BGSAction* rightPowerAttackAction = nullptr;

        static void Install() {
            SKSE::log::info("Installing Hooks...");
            REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_AttackBlockHandler[0]};
            _ProcessButton = vtbl.write_vfunc(0x4, &ProcessButtonHook);
            SKSE::log::info("AttackBlockHandlder::ProcessButton Hooked.");
            SKSE::log::info("Finished Installing Hooks.");
        }

        static bool Load() {
            // auto* dataHandler = RE::TESDataHandler::GetSingleton();
            rightPowerAttackAction = TESForm::LookupByID<BGSAction>(0x13383);
            

            if (!rightPowerAttackAction) {
                SKSE::log::error("Failed to load effect forms: rightPowerAttackAction={}", 
                    static_cast<void*>(rightPowerAttackAction));
                return false;
            }
            SKSE::log::info("Correctly loaded forms: rightPowerAttackAction={:08X}", 
                    rightPowerAttackAction->GetFormID());
            return true;
        }

    private:
        static void ProcessButtonHook(RE::AttackBlockHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data) {

        }
        inline static REL::Relocation<decltype(ProcessButtonHook)> _ProcessButton;
};
