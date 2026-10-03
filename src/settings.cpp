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

    std::atomic_bool unsavedChanges = false;

    namespace {
        enum class BindingTarget : std::uint8_t { none, modifier, cancelBlock };
        std::atomic<BindingTarget> captureTarget = BindingTarget::none;
        std::atomic_bool waitingForCaptureRelease = false;
        SKSEMenuFramework::Model::InputEvent* bindingMenuInputEvent = nullptr;

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
                if (target == BindingTarget::modifier) {
                    SetModifierKeyFromInput(-1);
                    SKSE::log::info("[settings] Modifier key unbound from capture");
                } else {
                    SetCancelBlockKeyFromInput(-1);
                    SKSE::log::info("[settings] Cancel block key unbound from capture");
                }
                StopBindingCapture();
                return true;
            }

            if (target == BindingTarget::modifier) {
                SetModifierKeyFromInput(keyCode);
                SKSE::log::info("[settings] Bound modifier input to key code {}", keyCode);
            } else {
                SetCancelBlockKeyFromInput(keyCode);
                SKSE::log::info("[settings] Bound cancel block input to key code {}", keyCode);
            }
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

    void Set(const config& value) {
        std::scoped_lock lock(configMutex);
        activeConfig = value;
    }

    void SetModifierKeyFromInput(const int keyCode) {
        {
            std::scoped_lock lock(configMutex);
            activeConfig.modifierKey = keyCode;
        }
        unsavedChanges = true;
    }

    void SetCancelBlockKeyFromInput(const int keyCode) {
        {
            std::scoped_lock lock(configMutex);
            activeConfig.cancelBlockKey = keyCode;
        }
        unsavedChanges = true;
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
        changed |= ImGuiMCP::SliderFloat("Power Attack Hold Duration", &cfg.HoldDuration, 0.01f, 0.5f, "%.2f");
        changed |= ImGuiMCP::Checkbox("Hold cancel block key to attack instead of bash during block", &cfg.eldenCounterMode);
        changed |= ImGuiMCP::Checkbox("Use modifier + RightAttack instead", &cfg.modifierRightMode);

        const auto activeCapture = captureTarget.load(std::memory_order_relaxed);
        const std::string modifierKeyName = cfg.modifierKey < 0 ? "Unbound" : std::to_string(cfg.modifierKey);
        
        const std::string cancelBlockKeyName = cfg.cancelBlockKey < 0 ? "Unbound" : std::to_string(cfg.cancelBlockKey);
        ImGuiMCP::Text("Cancel Block Key Code: %s", cancelBlockKeyName.c_str());
        if (activeCapture == BindingTarget::cancelBlock) {
            ImGuiMCP::TextUnformatted("Listening for cancel block key (ESC unbinds)");
        } else {
            if (activeCapture == BindingTarget::none) {
                if (ImGuiMCP::Button("Bind Cancel Block Key")) {
                    StartBindingCapture(BindingTarget::cancelBlock);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Unbind Cancel Block Key")) {
                    cfg.cancelBlockKey = -1;
                    Set(cfg);
                    changed = true;
                }
            }
        }

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
                Set(cfg);
                changed = true;
            }
        }

        
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
        SKSEMenuFramework::SetSection("Hold PowerAttack");
        SKSEMenuFramework::AddSectionItem("Settings", RenderMenuPage);
        SKSE::log::info("[settings] Registered SKSE Menu Framework page");
    }
}
