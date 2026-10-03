#pragma once

namespace settings {
    struct config {
        bool log = true;

        float HoldDuration = 0.18f;
        // bool queuePowerAttacks = false;

        bool eldenCounterMode = false; //while blocking, cancel block for an attack only when cancelBlockKey is held.
        int modifierKey = -1;
        int cancelBlockKey = -1; //if pressed and elden counter mode is enabled, the right attack input will cancel the block and do attack/power attack.
        bool modifierRightMode = false; //modifier+righthand input power attack mode. 

        int movementCancelMode = 0; //0 means disabled, 1 means if stationary (cancel block), 2 means if not stationary (cancel block).

        bool useAltPowerKeyBind = false;
        int altPowerKey = -1;
    };
    
    template <class T>
    struct setting_definition {
        const char* key;
        T settings::config::*member;
    };

    constexpr auto setting_definitions = std::tuple{
        setting_definition<bool>{ "log", &settings::config::log },
        setting_definition<float>{ "HoldDuration", &settings::config::HoldDuration },
        setting_definition<bool>{ "eldenCounterMode", &settings::config::eldenCounterMode },
        setting_definition<int>{ "modifierKey", &settings::config::modifierKey },
        setting_definition<int>{ "cancelBlockKey", &settings::config::cancelBlockKey },
        setting_definition<int>{ "movementCancelMode", &settings::config::movementCancelMode },
        setting_definition<bool>{ "modifierRightMode", &settings::config::modifierRightMode },

        setting_definition<bool>{ "useAltPowerKeyBind", &settings::config::useAltPowerKeyBind },
        setting_definition<int>{ "altPowerKey", &settings::config::altPowerKey },
    };

    config Get();
    void Set(const config& value);
    void Load();
    bool Save();

    void __stdcall RenderMenuPage();

    void RegisterMenu();
}
