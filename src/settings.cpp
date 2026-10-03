#include "PCH.h"
#include "settings.h"
#include "utils.h"

#include <SimpleIni.h>
#include <SKSEMenuFramework.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>


namespace settings {
    constexpr auto iniPath = "Data/SKSE/Plugins/PowerAttackInput.ini";
    constexpr auto general = "General";

    std::mutex configMutex;
    config activeConfig{};
    std::atomic_bool baseHoldRepeatEnabled{ false };

    std::atomic_bool unsavedChanges = false;

    namespace {
        enum class BindingTarget : std::uint8_t { none, modifier, altBlock, altPowerKey };
        std::atomic<BindingTarget> captureTarget = BindingTarget::none;
        std::atomic_bool waitingForCaptureRelease = false;
        SKSEMenuFramework::Model::InputEvent* bindingMenuInputEvent = nullptr;

        void SetKeyFromInput(const BindingTarget target, const int keyCode) {
            {
                std::scoped_lock lock(configMutex);
                switch (target) {
                case BindingTarget::modifier:
                    activeConfig.modifierKey = keyCode;
                    break;
                case BindingTarget::altBlock:
                    activeConfig.altBlockKey = keyCode;
                    break;
                case BindingTarget::altPowerKey:
                    activeConfig.altPowerKey = keyCode;
                    break;
                case BindingTarget::none:
                    return;
                }
            }
            unsavedChanges.store(true, std::memory_order_relaxed);
        }

        void StartBindingCapture(const BindingTarget target) {
            waitingForCaptureRelease.store(true, std::memory_order_release);
            captureTarget.store(target, std::memory_order_release);
        }

        void StopBindingCapture() {
            captureTarget.store(BindingTarget::none, std::memory_order_release);
            waitingForCaptureRelease.store(false, std::memory_order_release);
        }

        bool __stdcall OnBindingInput(RE::InputEvent* event) {
            const auto target = captureTarget.load(std::memory_order_acquire);
            if (target == BindingTarget::none) {
                return false;
            }

            if (!event) {
                return true;
            }

            auto* button = event->AsButtonEvent();
            if (!button) {
                return true;
            }

            // Ignore the click that started capture, then wait for a fresh input.
            if (waitingForCaptureRelease.load(std::memory_order_acquire)) {
                if (!button->IsDown()) {
                    waitingForCaptureRelease.store(false, std::memory_order_release);
                }
                return true;
            }

            if (button->IsDown()) {
                return true;
            }

            const int keyCode = utils::toKeyCode(*button);
            if (keyCode == 0) {
                return true;
            }

            // Escape cancels capture by unbinding, matching the menu hint.
            if (button->device.get() == RE::INPUT_DEVICE::kKeyboard && button->GetIDCode() == 0x01) {
                SetKeyFromInput(target, -1);
                SKSE::log::info("[settings] Binding target {} unbound", static_cast<int>(target));
                StopBindingCapture();
                return true;
            }

            SetKeyFromInput(target, keyCode);
            SKSE::log::info("[settings] Binding target {} bound to key code {}", static_cast<int>(target), keyCode);
            StopBindingCapture();
            return true;
        }
    }

    float readFloat(const CSimpleIniA& ini, const char* section, const char* key, const float fallback) {
        const float value = static_cast<float>(ini.GetDoubleValue(section, key, fallback));
        return std::isfinite(value) ? value : fallback;
    }

    bool readValue(const CSimpleIniA& ini, const char* section, const char* key, const bool fallback) {
        return ini.GetBoolValue(section, key, fallback);
    }

    int readValue(const CSimpleIniA& ini, const char* section, const char* key, const int fallback) {
        return static_cast<int>(ini.GetLongValue(section, key, fallback));
    }

    float readValue(const CSimpleIniA& ini, const char* section, const char* key, const float fallback) {
        return readFloat(ini, section, key, fallback);
    }

    void writeValue(CSimpleIniA& ini, const char* section, const char* key, const bool value) {
        ini.SetBoolValue(section, key, value);
    }

    void writeValue(CSimpleIniA& ini, const char* section, const char* key, const int value) {
        ini.SetLongValue(section, key, value);
    }

    void writeValue(CSimpleIniA& ini, const char* section, const char* key, const float value) {
        ini.SetDoubleValue(section, key, value, nullptr, false);
    }

    template <class T>
    void loadSetting(const CSimpleIniA& ini, settings::config& config, const setting_definition<T>& definition) {
        auto& value = config.*(definition.member);
        value = readValue(ini, general, definition.key, value);
    }

    template <class T>
    void saveSetting(CSimpleIniA& ini, const settings::config& config, const setting_definition<T>& definition) {
        writeValue(ini, general, definition.key, config.*(definition.member));
    }

    template <class Function>
    void forEachSetting(Function&& function) {
        std::apply(
            [&function](const auto&... definition) {
                (function(definition), ...);
            },
            setting_definitions);
    }

    config Get() {
        std::scoped_lock lock(configMutex);
        return activeConfig;
    }

    bool IsBaseHoldRepeatEnabled() {
        return baseHoldRepeatEnabled.load(std::memory_order_relaxed);
    }

    void Set(const config& value) {
        std::scoped_lock lock(configMutex);
        activeConfig = value;
        baseHoldRepeatEnabled.store(
            value.holdMode == 1 && value.currentMode == 0,
            std::memory_order_relaxed);
    }

    void Load() {
        StopBindingCapture();
        config loaded{};
        CSimpleIniA ini;
        ini.SetUnicode(false);

        std::error_code ec;
        if (!std::filesystem::exists(iniPath, ec) && !ec) {
            Set(loaded);
            if (Save()) {
                SKSE::log::info("[settings] Created {} with defaults", iniPath);
            }
            return;
        }
        if (ec || ini.LoadFile(iniPath) < 0) {
            SKSE::log::error("[settings] Could not read {}; using defaults without replacing it", iniPath);
            Set(loaded);
            return;
        }

        forEachSetting([&ini, &loaded](const auto& definition) {
            loadSetting(ini, loaded, definition);
        });

        // Accept the previous INI key name for existing installations.
        if (!ini.GetValue(general, "altBlockKey", nullptr)) {
            loaded.altBlockKey = static_cast<int>(ini.GetLongValue(general, "cancelBlockKey", loaded.altBlockKey));
        }

        // Migrate configs written before currentMode replaced the three base-mode booleans.
        if (!ini.GetValue(general, "currentMode", nullptr)) {
            if (ini.GetBoolValue(general, "leftAttackPA", false)) {
                loaded.currentMode = 3;
            } else if (ini.GetBoolValue(general, "useAltPowerKeyBind", false)) {
                loaded.currentMode = 2;
            } else if (ini.GetBoolValue(general, "modifierRightMode", false)) {
                loaded.currentMode = 1;
            }
        }
        if (loaded.currentMode < 0 || loaded.currentMode > 3) {
            loaded.currentMode = 0;
        }

        Set(loaded);
        SKSE::log::info("[settings] Loaded {}", iniPath);
    }

    bool Save() {
        const config current = Get();
        CSimpleIniA ini;
        ini.SetUnicode(false);
        (void)ini.LoadFile(iniPath);  // Preserve unrecognized keys from newer versions.

        forEachSetting([&ini, &current](const auto& definition) {
            saveSetting(ini, current, definition);
        });

        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(iniPath).parent_path(), ec);
        if (ec || ini.SaveFile(iniPath) < 0) {
            SKSE::log::error("[settings] Could not save {}: {}", iniPath, ec.message());
            return false;
        }

        SKSE::log::info("[settings] Saved {}", iniPath);
        return true;
    }

    static void FinishMenuPage(bool changed) {
        if (changed) {
            unsavedChanges.store(true, std::memory_order_relaxed);
        }
        ImGuiMCP::Separator();
        if (ImGuiMCP::Button("Save")) {
            if (Save()) {
                unsavedChanges.store(false, std::memory_order_relaxed);
            }
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Revert")) {
            Load();
            unsavedChanges.store(false, std::memory_order_relaxed);
        }
        if (unsavedChanges.load(std::memory_order_relaxed)) {
            ImGuiMCP::TextUnformatted("Unsaved changes");
        }
    }
    
    void __stdcall RenderMenuPage() {
        auto cfg = Get();
        bool changed = false;
        changed |= ImGuiMCP::Checkbox("Bashing requires block or alt block key held", &cfg.eldenCounterMode);

        static constexpr const char* attackInputModes[] = {
            "Hold RightAttack",
            "Modifier + RightAttack",
            "Alt Power Key",
            "Left Attack Power Attack"
        };
        if (cfg.currentMode < 0 || cfg.currentMode > 3) {
            cfg.currentMode = 0;
            changed = true;
        }
        changed |= ImGuiMCP::Combo("Power Attack Input Mode", &cfg.currentMode, attackInputModes, 4);

        if (cfg.currentMode == 0) {
            changed |= ImGuiMCP::SliderFloat("Power Attack Hold Duration", &cfg.HoldDuration, 0.01f, 0.5f, "%.2f");

            static constexpr const char* holdModes[] = {
                "No Repeats",
                "Repeat Power Attacks"
            };
            if (cfg.holdMode < 0 || cfg.holdMode >= 2) {
                cfg.holdMode = 0;
                changed = true;
            }
            changed |= ImGuiMCP::Combo("Hold Mode", &cfg.holdMode, holdModes, 2);
        }


        const auto activeCapture = captureTarget.load(std::memory_order_relaxed);
       
        const std::string altBlockKeyName = cfg.altBlockKey < 0 ? "Unbound" : std::to_string(cfg.altBlockKey);
        ImGuiMCP::Text("Alt Block Key Code: %s", altBlockKeyName.c_str());
        if (activeCapture == BindingTarget::altBlock) {
            ImGuiMCP::TextUnformatted("Listening for alt block key (ESC unbinds)");
        } else if (activeCapture == BindingTarget::none) {
            if (ImGuiMCP::Button("Bind Alt Block Key")) {
                StartBindingCapture(BindingTarget::altBlock);
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Unbind Alt Block Key")) {
                cfg.altBlockKey = -1;
                SetKeyFromInput(BindingTarget::altBlock, -1);
                Set(cfg);
                changed = true;
            }
        }

        const std::string modifierKeyName = cfg.modifierKey < 0 ? "Unbound" : std::to_string(cfg.modifierKey);
        ImGuiMCP::Text("Modifier Key Code: %s", modifierKeyName.c_str());
        if (activeCapture == BindingTarget::modifier) {
            ImGuiMCP::TextUnformatted("Listening for modifier key (ESC unbinds)");
        } else if (activeCapture == BindingTarget::none) {
            if (ImGuiMCP::Button("Bind Modifier Key")) {
                StartBindingCapture(BindingTarget::modifier);
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Unbind Modifier Key")) {
                cfg.modifierKey = -1;
                SetKeyFromInput(BindingTarget::modifier, -1);
                Set(cfg);
                changed = true;
            }
        }

        const std::string altPowerKeyName = cfg.altPowerKey < 0 ? "Unbound" : std::to_string(cfg.altPowerKey);
        ImGuiMCP::Text("Alt Power Attack Key Code: %s", altPowerKeyName.c_str());
        if (activeCapture == BindingTarget::altPowerKey) {
            ImGuiMCP::TextUnformatted("Listening for alt power key (ESC unbinds)");
        } else if (activeCapture == BindingTarget::none) {
            if (ImGuiMCP::Button("Bind Alt Power Key")) {
                StartBindingCapture(BindingTarget::altPowerKey);
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Unbind Alt Power Key")) {
                cfg.altPowerKey = -1;
                SetKeyFromInput(BindingTarget::altPowerKey, -1);
                Set(cfg);
                changed = true;
            }
        }

        ImGuiMCP::BeginDisabled(cfg.currentMode != 2);
        changed |= ImGuiMCP::Checkbox("Require Modifier for Alt Power Key", &cfg.useModifierAltPA);
        ImGuiMCP::EndDisabled();

        static constexpr const char* leftAttackModes[] = {
            "Moving: power attack; stationary: default block/cast",
            "Stationary: power attack; moving: default block/cast",
            "Spell: cast stationary, power attack moving; other: power attack",
            "Spell: cast moving, power attack stationary; other: power attack"
        };
        if (cfg.defaultBehaviorMode < 0 || cfg.defaultBehaviorMode >= 4) {
            cfg.defaultBehaviorMode = 0;
            changed = true;
        }
        static constexpr const char* modifierLeftAttackModes[] = {
            "Disabled",
            "Mod+Left: PA; Left: cast/block",
            "Mod+Left: PA; Left: cast/PA",
            "Mod+Left: cast/block; Left: PA",
            "Mod+Left: cast/PA; Left: PA"
        };
        if (cfg.modifierModeLeftPA < 0 || cfg.modifierModeLeftPA >= 5) {
            cfg.modifierModeLeftPA = 0;
            changed = true;
        }
        ImGuiMCP::BeginDisabled(cfg.currentMode != 3);
        changed |= ImGuiMCP::Combo("Modifier + Left Attack Mode", &cfg.modifierModeLeftPA, modifierLeftAttackModes, 5);
        ImGuiMCP::BeginDisabled(cfg.modifierModeLeftPA != 0);
        changed |= ImGuiMCP::Combo("Left Attack Movement Behavior", &cfg.defaultBehaviorMode, leftAttackModes, 4);
        changed |= ImGuiMCP::Checkbox("Treat Forward Movement as Neutral", &cfg.includeFWD);
        ImGuiMCP::EndDisabled();
        ImGuiMCP::EndDisabled();

        
        changed |= ImGuiMCP::Checkbox("Enable Log", &cfg.log);
        if (changed) {
            Set(cfg);
        }

        FinishMenuPage(changed);
    }

    void RegisterMenu() { 
        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::info("[settings] SKSE Menu Framework is not installed; INI settings remain available");
            return;
        }

        menuFramework = GetModuleHandleW(L"SKSEMenuFramework.dll");
        if (!menuFramework) {
            SKSE::log::warn("[settings] SKSE Menu Framework DLL exists but is not loaded");
            return;
        }
        if (!bindingMenuInputEvent) {
            bindingMenuInputEvent = SKSEMenuFramework::AddInputEvent(OnBindingInput);
        }
        SKSEMenuFramework::SetSection("Modern Power Attack Control");
        SKSEMenuFramework::AddSectionItem("Settings", RenderMenuPage);
        SKSE::log::info("[settings] Registered SKSE Menu Framework page");
    }
}
