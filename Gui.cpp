#include "Gui.h"
#include "Log.h"
#include "Platform.h"
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <Windows.h>
#include <array>
#include <string>
#include <cstring>  // для strcpy_s
#include "LuaEngine.h"

// Внешние функции из Platform.h
extern bool ConnectToComPort(int port);
extern void DisconnectComPort();
extern bool IsComPortConnected();
extern int GetCurrentComPort();

// Внешние переменные из Log.h
extern ImGuiTextBuffer g_LogBuffer;
extern bool g_ScrollToBottom;
extern void OutputLogMessage(const std::string& message);
extern void ClearLogBuffer();

Gui::Gui(LuaEngine* engine)
    : luaEngine(engine), comPortNumber(3), lastComPortAttempt(3),
    showSettings(false), showProfile(false), showAuth(false), showBinds(false),
    selectedBind(-1), mouseSensitivity(1.0f), aimSensitivity(0.8f), fov(90.0f),
    randomEnabled(false) {
    scriptText.reserve(4096);
    LoadScriptFile();

    // Инициализируем бинды
    for (int i = 0; i < 13; i++) {
        binds[i] = "Не назначен";
    }
}

void Gui::SetReloadCallback(std::function<void()> callback) {
    onReload = callback;
}

void Gui::LoadScriptFile() {
    std::ifstream file("assets/user_script.lua");
    if (file) {
        std::ostringstream ss;
        ss << file.rdbuf();
        scriptText = ss.str();
    }
    else {
        scriptText = R"(
-- user_script.lua не найден, создан шаблон
function OnEvent(event, arg)
    OutputLogMessage("Событие: "..event..", Аргумент: "..arg.."\n")
    
    if event == "MOUSE_BUTTON_PRESSED" and arg == 5 then
        OutputLogMessage("Нажата боковая кнопка мыши!\n")
        PressKey("lshift")
        Sleep(50)
        PressAndReleaseKey("a")
        Sleep(50)
        ReleaseKey("lshift")
    end
end
)";
    }
}

void Gui::SaveScriptFile() {
    std::ofstream file("assets/user_script.lua");
    if (file) {
        file << scriptText;
        OutputLogMessage("[GUI] Скрипт успешно сохранён.\n");
    }
    else {
        OutputLogMessage("[GUI] Ошибка: не удалось сохранить скрипт!\n");
    }
}

void Gui::SetRandomizationEnabled(bool enabled) {
    randomEnabled = enabled;
    if (luaEngine) {
        luaEngine->SetRandomizationEnabled(enabled);
    }
}

// Кастомная функция для рисования графитовой рамки с выступами сверху и скошенными углами сверху и снизу
void DrawKvTFrame(const ImVec2& min, const ImVec2& max) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    // Цвета рамок
    ImU32 outerColor = IM_COL32(60, 60, 60, 255);      // графитовый
    ImU32 innerColor = IM_COL32(255, 140, 0, 255);     // оранжевый

    // Толщина линий
    float outerThickness = 4.0f;
    float innerThickness = 2.0f;

    // Параметры выступов и скосов
    float edgeOffset = 150.0f;    // отступ от левого и правого края
    float notchWidth = 12.0f;     // ширина выступа
    float notchHeight = 16.0f;    // высота выступа

    // --- Внешняя рамка ---

    // Вершины внешней рамки с выступами (по часовой стрелке)
    ImVec2 ptsOuter[] = {
        // Левая нижняя точка
        ImVec2(min.x, max.y),
        // Левая верхняя точка со скошенным углом и выступом
        ImVec2(min.x, min.y + notchHeight),
        ImVec2(min.x + edgeOffset, min.y + notchHeight),
        ImVec2(min.x + edgeOffset + notchWidth, min.y),
        ImVec2(max.x - edgeOffset - notchWidth, min.y),
        ImVec2(max.x - edgeOffset, min.y + notchHeight),
        ImVec2(max.x, min.y + notchHeight),
        ImVec2(max.x, max.y)
    };

    // Рисуем внешнюю рамку с замкнутым контурами
    draw_list->AddPolyline(ptsOuter, IM_ARRAYSIZE(ptsOuter), outerColor, true, outerThickness);

    // --- Внутренняя рамка (с отступом внутрь) ---

    // Смещение внутрь для внутренней рамки
    float inset = outerThickness + 1.5f;

    ImVec2 minInner = ImVec2(min.x + inset, min.y + inset);
    ImVec2 maxInner = ImVec2(max.x - inset, max.y - inset);

    ImVec2 ptsInner[] = {
        ImVec2(minInner.x, maxInner.y),
        ImVec2(minInner.x, minInner.y + notchHeight * 0.6f),
        ImVec2(minInner.x + edgeOffset * 0.7f, minInner.y + notchHeight * 0.6f),
        ImVec2(minInner.x + edgeOffset * 0.7f + notchWidth * 0.6f, minInner.y),
        ImVec2(maxInner.x - edgeOffset * 0.7f - notchWidth * 0.6f, minInner.y),
        ImVec2(maxInner.x - edgeOffset * 0.7f, minInner.y + notchHeight * 0.6f),
        ImVec2(maxInner.x, minInner.y + notchHeight * 0.6f),
        ImVec2(maxInner.x, maxInner.y)
    };

    draw_list->AddPolyline(ptsInner, IM_ARRAYSIZE(ptsInner), innerColor, true, innerThickness);
}



void Gui::RenderMainWindow() {
    ImGui::SetNextWindowSize(ImVec2(500, 450), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

    // Убрали заголовок окна, добавили NoTitleBar
    if (ImGui::Begin("KvT", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();

        DrawKvTFrame(window_pos, ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y));

        // Центрируем заголовок внутри окна, заменяем текст — если хочешь, можно убрать
        ImGui::SetCursorPosX((window_size.x - ImGui::CalcTextSize("KvT").x) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.0f, 1.0f));
        ImGui::Text("KvT");
        ImGui::PopStyleColor();

        ImGui::Spacing();

        ImGui::SetCursorPos(ImVec2(20, 60));
        if (ImGui::Button("≡", ImVec2(30, 30))) {
            showSettings = !showSettings;
        }

        ImGui::SetCursorPos(ImVec2(window_size.x - 50, 60));
        if (ImGui::Button("●", ImVec2(30, 30))) {
            showProfile = !showProfile;
        }

        ImGui::SetCursorPos(ImVec2(20, 100));
        bool isConnected = IsComPortConnected();
        ImVec4 connectedColor = isConnected ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
        ImGui::TextColored(connectedColor, "●");
        ImGui::SameLine();
        ImGui::Text("Подключено");

        ImGui::SetCursorPos(ImVec2(150, 100));
        ImVec4 hidColor = isConnected ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        ImGui::TextColored(hidColor, "●");
        ImGui::SameLine();
        ImGui::Text("HID активен");

        ImGui::SetCursorPos(ImVec2(280, 100));
        ImVec4 errorColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
        ImGui::TextColored(errorColor, "●");
        ImGui::SameLine();
        ImGui::Text("Ошибка");

        ImGui::SetCursorPos(ImVec2(20, 140));
        ImVec4 randomColor = randomEnabled ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, randomColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, randomColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, randomColor);
        if (ImGui::Button("Random", ImVec2(100, 30))) {
            SetRandomizationEnabled(!randomEnabled);
        }
        ImGui::PopStyleColor(3);

        ImGui::SetCursorPos(ImVec2(80, 200));
        if (ImGui::Button("Бинды", ImVec2(120, 50))) {
            showBinds = !showBinds;
        }

        ImGui::SetCursorPos(ImVec2(250, 200));
        if (ImGui::Button("Настройки", ImVec2(120, 50))) {
            showSettings = !showSettings;
        }

        ImGui::SetCursorPos(ImVec2(20, window_size.y - 60));
        ImVec4 statusColor1 = isConnected ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        ImGui::TextColored(statusColor1, "●");
        ImGui::SameLine();
        ImGui::Text("Плата подключена");

        ImGui::SetCursorPos(ImVec2(200, window_size.y - 60));
        ImVec4 statusColor2 = isConnected ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        ImGui::TextColored(statusColor2, "●");
        ImGui::SameLine();
        ImGui::Text("Ввод через плату");
    }
    ImGui::End();
    ImGui::PopStyleColor();

    if (showSettings) RenderSettingsWindow();
    if (showProfile) RenderProfileWindow();
    if (showAuth) RenderAuthWindow();
    if (showBinds) RenderBindsWindow();
    if (selectedBind >= 0) RenderBindEditWindow();
}

void Gui::RenderSettingsWindow() {
    ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(200, 150), ImGuiCond_FirstUseEver);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

    if (ImGui::Begin("Настройки", &showSettings, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        DrawKvTFrame(window_pos, ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y));

        ImGui::Spacing();

        ImGui::Text("Чувствительность мыши:");
        ImGui::SliderFloat("##mouse_sens", &mouseSensitivity, 0.1f, 5.0f, "%.2f");

        ImGui::Spacing();

        ImGui::Text("Чувствительность в прицеле:");
        ImGui::SliderFloat("##aim_sens", &aimSensitivity, 0.1f, 3.0f, "%.2f");

        ImGui::Spacing();

        ImGui::Text("FOV (угол обзора):");
        ImGui::SliderFloat("##fov", &fov, 60.0f, 120.0f, "%.0f");

        ImGui::Separator();

        ImGui::Text("Настройки COM-порта:");
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("COM порт", &comPortNumber)) {
            if (comPortNumber < 1) comPortNumber = 1;
            if (comPortNumber > 256) comPortNumber = 256;
        }

        ImGui::SameLine();

        bool isConnected = IsComPortConnected();
        if (isConnected) {
            if (ImGui::Button("Отключить", ImVec2(100, 0))) {
                DisconnectComPort();
                OutputLogMessage("[GUI] COM-порт отключен.\n");
            }
        }
        else {
            if (ImGui::Button("Подключить", ImVec2(100, 0))) {
                if (ConnectToComPort(comPortNumber)) {
                    lastComPortAttempt = comPortNumber;
                    OutputLogMessage("[GUI] Подключение к COM" + std::to_string(comPortNumber) + " успешно.\n");
                }
                else {
                    OutputLogMessage("[GUI] Не удалось подключиться к COM" + std::to_string(comPortNumber) + ".\n");
                }
            }
        }




        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Закрыть", ImVec2(100, 30))) {
            showSettings = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void Gui::RenderProfileWindow() {
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_Always);
    ImVec2 main_pos = ImGui::GetMainViewport()->Pos;
    ImVec2 main_size = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(main_pos.x + main_size.x * 0.5f - 150, main_pos.y + main_size.y * 0.5f - 100), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

    if (ImGui::Begin("Профиль", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        DrawKvTFrame(window_pos, ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y));

        // Кнопка закрытия
        ImGui::SetCursorPos(ImVec2(window_size.x - 30, 10));
        if (ImGui::Button("×", ImVec2(20, 20))) {
            showProfile = false;
        }

        ImGui::SetCursorPos(ImVec2(20, 40));
        ImGui::Text("Профиль пользователя");

        ImGui::SetCursorPos(ImVec2(20, 80));
        if (ImGui::Button("Войти в систему", ImVec2(150, 30))) {
            showAuth = true;
            showProfile = false;
        }

        ImGui::SetCursorPos(ImVec2(20, 120));
        if (ImGui::Button("Настройки профиля", ImVec2(150, 30))) {
            // Здесь можно добавить логику настроек профиля
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void Gui::RenderAuthWindow() {
    ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_Always);
    ImVec2 main_pos = ImGui::GetMainViewport()->Pos;
    ImVec2 main_size = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(main_pos.x + main_size.x * 0.5f - 175, main_pos.y + main_size.y * 0.5f - 125), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

    if (ImGui::Begin("Авторизация", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        DrawKvTFrame(window_pos, ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y));

        // Кнопка закрытия
        ImGui::SetCursorPos(ImVec2(window_size.x - 30, 10));
        if (ImGui::Button("×", ImVec2(20, 20))) {
            showAuth = false;
        }

        ImGui::SetCursorPos(ImVec2(20, 40));
        ImGui::Text("Вход в систему");

        static char username[256] = "";
        static char password[256] = "";

        ImGui::SetCursorPos(ImVec2(20, 80));
        ImGui::Text("Логин:");
        ImGui::SetCursorPos(ImVec2(20, 100));
        ImGui::SetNextItemWidth(250);
        ImGui::InputText("##username", username, sizeof(username));

        ImGui::SetCursorPos(ImVec2(20, 130));
        ImGui::Text("Пароль:");
        ImGui::SetCursorPos(ImVec2(20, 150));
        ImGui::SetNextItemWidth(250);
        ImGui::InputText("##password", password, sizeof(password), ImGuiInputTextFlags_Password);

        ImGui::SetCursorPos(ImVec2(20, 190));
        if (ImGui::Button("Войти", ImVec2(100, 30))) {
            // Логика авторизации
            showAuth = false;
        }

        ImGui::SetCursorPos(ImVec2(140, 190));
        if (ImGui::Button("Отмена", ImVec2(100, 30))) {
            showAuth = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void Gui::RenderBindsWindow() {
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(150, 100), ImGuiCond_FirstUseEver);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

    if (ImGui::Begin("Бинды", &showBinds, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        DrawKvTFrame(window_pos, ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y));

        ImGui::Text("Настройка горячих клавиш");
        ImGui::Separator();

        // 10 кнопок слева вертикально
        for (int i = 0; i < 10; i++) {
            ImGui::SetCursorPos(ImVec2(20, 60 + i * 30));
            std::string buttonText = "F" + std::to_string(i + 1) + ": " + binds[i];
            if (ImGui::Button(buttonText.c_str(), ImVec2(200, 25))) {
                selectedBind = i;
            }
        }

        // 3 кнопки справа
        for (int i = 0; i < 3; i++) {
            ImGui::SetCursorPos(ImVec2(250, 60 + i * 35));
            std::string buttonText = "MB" + std::to_string(i + 4) + ": " + binds[10 + i];
            if (ImGui::Button(buttonText.c_str(), ImVec2(200, 25))) {
                selectedBind = 10 + i;
            }
        }

        ImGui::SetCursorPos(ImVec2(20, 360));
        if (ImGui::Button("Закрыть", ImVec2(100, 30))) {
            showBinds = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void Gui::RenderBindEditWindow() {
    ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_Always);
    ImVec2 main_pos = ImGui::GetMainViewport()->Pos;
    ImVec2 main_size = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(main_pos.x + main_size.x * 0.5f - 150, main_pos.y + main_size.y * 0.5f - 75), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

    if (ImGui::Begin("Редактор бинда", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        DrawKvTFrame(window_pos, ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y));

        ImGui::SetCursorPos(ImVec2(20, 20));
        ImGui::Text("Настройка бинда #%d", selectedBind + 1);

        static char bindText[256];
        strcpy_s(bindText, binds[selectedBind].c_str());

        ImGui::SetCursorPos(ImVec2(20, 50));
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##bind", bindText, sizeof(bindText));

        ImGui::SetCursorPos(ImVec2(20, 90));
        if (ImGui::Button("Сохранить", ImVec2(80, 25))) {
            binds[selectedBind] = std::string(bindText);
            selectedBind = -1;
        }

        ImGui::SetCursorPos(ImVec2(120, 90));
        if (ImGui::Button("Отмена", ImVec2(80, 25))) {
            selectedBind = -1;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void Gui::RenderScriptWindow() {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(600, 100), ImGuiCond_FirstUseEver);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

    if (ImGui::Begin("Lua Script Editor", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        DrawKvTFrame(window_pos, ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y));

        // Делим окно на две части: скрипт слева, лог справа
        float script_width = window_size.x * 0.6f - 20;
        float log_width = window_size.x * 0.4f - 20;

        // Скрипт слева
        ImGui::BeginChild("ScriptRegion", ImVec2(script_width, window_size.y - 100), true);

        ImGui::Text("Редактор GHUB-скриптов:");

        auto resizeCallback = [](ImGuiInputTextCallbackData* data) -> int {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                std::string* str = (std::string*)data->UserData;
                str->resize(data->BufTextLen);
                data->Buf = str->data();
            }
            return 0;
            };

        ImGui::InputTextMultiline(
            "##script",
            scriptText.data(),
            scriptText.capacity() + 1,
            ImVec2(-FLT_MIN, -30),
            ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize,
            resizeCallback,
            &scriptText
        );

        ImGui::EndChild();

        // Лог справа
        ImGui::SameLine();
        ImGui::BeginChild("LogRegion", ImVec2(log_width, window_size.y - 100), true);

        ImGui::Text("Консоль:");
        ImGui::Separator();

        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -30), false, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(g_LogBuffer.begin(), g_LogBuffer.end());

        if (g_ScrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
            g_ScrollToBottom = false;
        }
        ImGui::EndChild();

        if (ImGui::Button("Очистить")) {
            ClearLogBuffer();
        }

        ImGui::EndChild();

        // Кнопки управления внизу
        if (ImGui::Button("💾 Сохранить", ImVec2(120, 30))) {
            SaveScriptFile();
        }

        ImGui::SameLine();

        if (ImGui::Button("🔄 Перезагрузить", ImVec2(120, 30))) {
            SaveScriptFile();
            if (onReload) {
                try {
                    onReload();
                    OutputLogMessage("[GUI] Скрипт перезагружен.\n");
                }
                catch (const std::exception& e) {
                    OutputLogMessage("[GUI] Ошибка при перезагрузке: " + std::string(e.what()) + "\n");
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("❌ Закрыть", ImVec2(120, 30))) {
            if (IsComPortConnected()) {
                DisconnectComPort();
            }
            ::PostQuitMessage(0);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
}

void Gui::Render() {
    RenderMainWindow();
    RenderScriptWindow();
}