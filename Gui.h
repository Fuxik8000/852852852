#pragma once
#include <string>
#include <functional>
#include <array>
#include <imgui.h>  // для ImVec2, ImVec4

class LuaEngine;

class Gui {
public:
    Gui(LuaEngine* engine);
    void Render();
    void LoadScriptFile();
    void SaveScriptFile();
    void SetReloadCallback(std::function<void()> callback);
    void SetRandomizationEnabled(bool enabled);

private:
    LuaEngine* luaEngine;
    std::string scriptText;
    std::function<void()> onReload;

    // COM-порт настройки
    int comPortNumber;
    int lastComPortAttempt;

    // UI состояние
    bool showSettings;
    bool showProfile;
    bool showAuth;
    bool showBinds;
    int selectedBind;

    // Настройки
    float mouseSensitivity;
    float aimSensitivity;
    float fov;
    bool randomEnabled;

    // Бинды (10 F-клавиш + 3 кнопки мыши)
    std::array<std::string, 13> binds;

    // Методы рендеринга
    void RenderMainWindow();
    void RenderSettingsWindow();
    void RenderProfileWindow();
    void RenderAuthWindow();
    void RenderBindsWindow();
    void RenderBindEditWindow();
    void RenderScriptWindow();
};