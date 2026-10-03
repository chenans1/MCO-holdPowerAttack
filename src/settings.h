#pragma once

namespace settings {
    struct config {
        bool log = true;

        // 0 - hold right, 1 - mod + right, 2 - alt power, 3 - leftPA
        int currentMode = 0;
        float HoldDuration = 0.18f;
        // 0 - disabled, 1 - repeat power attacks. window resets on MCO/BFCO_WinOpen
        int holdMode = 0;
        bool eldenCounterMode = false; //while blocking, cancel block for an attack only when cancelBlockKey is held.
        int modifierKey = -1;
        int cancelBlockKey = -1; //if pressed and elden counter mode is enabled, the right attack input will cancel the block and do attack/power attack.

        int movementCancelMode = 0; //0 means disabled, 1 means if stationary (cancel block), 2 means if not stationary (cancel block).

        int altPowerKey = -1;
        bool useModifierAltPA = false;

        // 0 stationary for left spell/staff/block, movement is power attack.
        // 1 movement for left spell/staff/block, stationary is power attack.
        // 2 stationary for left spell/staff cast only. Block, movement is power attack
        // 3 movement for left spell/staff cast only. Block, stationary is power attack
        int defaultBehaviorMode = 0;
        //for: isNeutral also accepts forward vec
        bool includeFWD = false;
        // 0 - disabled.
        // 1 - Mod+Left = PA | Left = Cast/Block
        // 2 - Mod+Left = PA | Left = Cast/PA
        // 3 - Mod+Left -> cast/block | Left = PA
        // 4 - Mod+Left -> cast/PA | Left = PA
        int modifierModeLeftPA = 0;
    };
    
    template <class T>
    struct setting_definition {
        const char* key;
        T settings::config::*member;
    };

    constexpr auto setting_definitions = std::tuple{
        setting_definition<bool>{ "log", &settings::config::log },
        setting_definition<int>{ "currentMode", &settings::config::currentMode },
        setting_definition<float>{ "HoldDuration", &settings::config::HoldDuration },
        setting_definition<int>{ "holdMode", &settings::config::holdMode },
        // setting_definition<bool>{ "eldenCounterMode", &settings::config::eldenCounterMode },
        setting_definition<int>{ "modifierKey", &settings::config::modifierKey },
        // setting_definition<int>{ "cancelBlockKey", &settings::config::cancelBlockKey },
        setting_definition<int>{ "movementCancelMode", &settings::config::movementCancelMode },

        setting_definition<int>{ "altPowerKey", &settings::config::altPowerKey },
        setting_definition<bool>{ "useModifierAltPA", &settings::config::useModifierAltPA },
        setting_definition<int>{ "defaultBehaviorMode", &settings::config::defaultBehaviorMode },
        setting_definition<int>{ "modifierModeLeftPA", &settings::config::modifierModeLeftPA },
        setting_definition<bool>{ "includeFWD", &settings::config::includeFWD },
    };

    config Get();
    bool IsBaseHoldRepeatEnabled();
    void Set(const config& value);
    void Load();
    bool Save();

    void __stdcall RenderMenuPage();    

    void RegisterMenu();
}
