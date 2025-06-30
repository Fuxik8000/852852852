#include "LuaEngine.h"
#include "Log.h"
#include "Platform.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cmath>
#include <unordered_map>
#include <algorithm>
#include <Windows.h>
#include <thread>
#include <chrono>
#include <map>
#include <random>

LuaEngine::LuaEngine() {
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::math,
        sol::lib::string, sol::lib::table, sol::lib::os);
    BindAPI();
    ReloadScript();
}

LuaEngine::~LuaEngine() {
    // Очищаем Lua state
}

void LuaEngine::SetRandomizationEnabled(bool enabled) {
    randomizationEnabled = enabled;
}

void LuaEngine::BindAPI() {
    // Создаем таблицу GHUB для всех функций
    sol::table ghub = lua.create_named_table("GHUB");

    // Создаем метатаблицу для обработки неизвестных функций
    sol::table metatable = lua.create_table();
    metatable["__index"] = [](sol::table table, const std::string& key) -> sol::object {
        // Возвращаем функцию-заглушку для неизвестных методов
        return sol::make_object(table.lua_state(), [key](sol::variadic_args args) {
            std::string errorMsg = "Попытка вызова неизвестной функции: GHUB." + key;
            ::OutputLogMessage("[LUA WARNING] " + errorMsg + "\n");
            // Вместо исключения просто возвращаем nil
            return sol::nil;
            });
        };

    ghub[sol::metatable_key] = metatable;

    // Регистрируем все функции внутри таблицы GHUB
    ghub.set_function("PressKey", [this](const std::string& key) { this->PressKey(key); });
    ghub.set_function("ReleaseKey", [this](const std::string& key) { this->ReleaseKey(key); });
    ghub.set_function("PressAndReleaseKey", [this](const std::string& key) {
        this->PressKey(key);
        this->ReleaseKey(key);
        });

    ghub.set_function("PressMouseButton", [](int button) {
        ::PressMouseButton(button);
        });

    ghub.set_function("ReleaseMouseButton", [](int button) {
        ::ReleaseMouseButton(button);
        });

    ghub.set_function("MoveMouseRelative", [this](int dx, int dy) {
        if (randomizationEnabled) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(-1, 1); // от -1 до +1 пикселя

            int jitterX = dx != 0 ? dx + dist(gen) : 0;
            int jitterY = dy != 0 ? dy + dist(gen) : 0;
            ::MoveMouseRelative(jitterX, jitterY);
        }
        else {
            ::MoveMouseRelative(dx, dy);
        }
        });


    ghub.set_function("MoveMouseTo", [](int x, int y) {
        ::MoveMouseTo(x, y);
        });

    ghub.set_function("GetMousePosition", []() {
        POINT p;
        ::GetCursorPos(&p);
        return std::make_tuple(p.x, p.y);
        });

    ghub.set_function("hzCf681ZWWcx", [this]() {
        return !this->IsMouseButtonPressed(1);
        });

    ghub.set_function("EnablePrimaryMouseButtonEvents", [this](bool enable) {
        this->primary_mouse_enabled = enable;
        });

    ghub.set_function("IsModifierPressed", [this](const std::string& mod) {
        return this->IsModifierPressed(mod);
        });

    ghub.set_function("IsMouseButtonPressed", [this](int btn) {
        return this->IsMouseButtonPressed(btn);
        });

    ghub.set_function("GetRunningTime", []() {
        static auto start = std::chrono::steady_clock::now();
        return (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        });

    ghub.set_function("OutputLogMessage", [](const std::string& msg) {
        ::OutputLogMessage("[LUA] " + msg + "\n");
        });

    ghub.set_function("ClearLog", []() {
        ::ClearLogBuffer();
        });

    // Реализация Sleep (блокирующая, но безопасная)
    ghub.set_function("Sleep", [](int ms) {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed >= ms) break;

            // Позволяем обрабатывать события во время ожидания
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        });

    ghub.set_function("Sleep", [](int ms) {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed >= ms) break;

            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        });

    // Замените существующую функцию Sleep_extra в BindAPI() на эту:

    ghub.set_function("Sleep_extra", [this](int gun, int ms) {
        auto start = std::chrono::steady_clock::now();
        auto target = start + std::chrono::milliseconds(ms);
        bool wasPressed = this->IsMouseButtonPressed(gun);

        while (std::chrono::steady_clock::now() < target) {
            bool currentlyPressed = this->IsMouseButtonPressed(gun);

            // Если кнопка была отпущена
            if (wasPressed && !currentlyPressed) {
                ::OutputLogMessage("[SLEEP_EXTRA] Кнопка мыши " + std::to_string(gun) + " отпущена, добавляем задержку\n");

                // Добавляем универсальную задержку 300ms для предотвращения лишних выстрелов
                auto delayStart = std::chrono::steady_clock::now();
                auto delayTarget = delayStart + std::chrono::milliseconds(300);

                while (std::chrono::steady_clock::now() < delayTarget) {
                    // Обрабатываем события во время задержки
                    MSG msg;
                    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                ::OutputLogMessage("[SLEEP_EXTRA] Задержка 300ms завершена, прерывание макроса\n");
                break;
            }

            wasPressed = currentlyPressed;

            // Позволяем обрабатывать события во время ожидания
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            // Небольшая пауза чтобы не нагружать процессор
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        });

    ghub.set_function("AbortMacro", []() {
        throw std::runtime_error("Macro aborted by user");
        });

    ghub.set_function("IsKeyLockOn", [](const std::string& key) {
        if (key == "capslock") return (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        if (key == "numlock") return (::GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
        if (key == "scrolllock") return (::GetKeyState(VK_SCROLL) & 0x0001) != 0;
        return false;
        });

    ghub.set_function("PlaySound", [](const std::string& file) {
        ::OutputLogMessage("[SOUND] Play: " + file + "\n");
        return true;
        });

    ghub.set_function("SetBacklightColor", [](int r, int g, int b, const sol::object& device) {
        return true;
        });

    // =========================================================================
    // РЕГИСТРАЦИЯ ГЛОБАЛЬНЫХ ФУНКЦИЙ ДЛЯ СОВМЕСТИМОСТИ С GHUB-СКРИПТАМИ
// =========================================================================

// Sleep
    lua.set_function("Sleep", [ghub](int ms) {
        ghub["Sleep"](ms);
        });

    lua.set_function("Sleep_extra", [ghub](int gun, int ms) {
        ghub["Sleep_extra"](gun, ms);
        });

    // OutputLogMessage
    lua.set_function("OutputLogMessage", [ghub](const std::string& msg) {
        ghub["OutputLogMessage"](msg);
        });

    // ClearLog
    lua.set_function("ClearLog", [ghub]() {
        ghub["ClearLog"]();
        });

    // PressKey
    lua.set_function("PressKey", [ghub](const std::string& key) {
        ghub["PressKey"](key);
        });

    // ReleaseKey
    lua.set_function("ReleaseKey", [ghub](const std::string& key) {
        ghub["ReleaseKey"](key);
        });

    // PressAndReleaseKey
    lua.set_function("PressAndReleaseKey", [ghub](const std::string& key) {
        ghub["PressAndReleaseKey"](key);
        });

    // MoveMouseRelative
    lua.set_function("MoveMouseRelative", [ghub](int dx, int dy) {
        ghub["MoveMouseRelative"](dx, dy);
        });

    // MoveMouseTo
    lua.set_function("MoveMouseTo", [ghub](int x, int y) {
        ghub["MoveMouseTo"](x, y);
        });

    // GetMousePosition
    lua.set_function("GetMousePosition", [ghub]() {
        return ghub["GetMousePosition"]();
        });

    // AbortMacro
    lua.set_function("AbortMacro", [ghub]() {
        ghub["AbortMacro"]();
        });

    // IsKeyLockOn
    lua.set_function("IsKeyLockOn", [ghub](const std::string& key) {
        return ghub["IsKeyLockOn"](key);
        });

    // IsMouseButtonPressed - ДОБАВЛЕНО
    lua.set_function("IsMouseButtonPressed", [ghub](int button) {
        return ghub["IsMouseButtonPressed"](button);
        });

    // EnablePrimaryMouseButtonEvents - ДОБАВЛЕНО
    lua.set_function("EnablePrimaryMouseButtonEvents", [ghub](bool enable) {
        ghub["EnablePrimaryMouseButtonEvents"](enable);
        });

    // GetRunningTime - ДОБАВЛЕНО
    lua.set_function("GetRunningTime", [ghub]() {
        return ghub["GetRunningTime"]();
        });

    // PressMouseButton - ДОБАВЛЕНО
    lua.set_function("PressMouseButton", [ghub](int button) {
        ghub["PressMouseButton"](button);
        });

    // ReleaseMouseButton - ДОБАВЛЕНО
    lua.set_function("ReleaseMouseButton", [ghub](int button) {
        ghub["ReleaseMouseButton"](button);
        });

    // IsModifierPressed - ДОБАВЛЕНО
    lua.set_function("IsModifierPressed", [ghub](const std::string& mod) {
        return ghub["IsModifierPressed"](mod);
        });

    // PlaySound - ДОБАВЛЕНО
    lua.set_function("PlaySound", [ghub](const std::string& file) {
        return ghub["PlaySound"](file);
        });

    // SetBacklightColor - ДОБАВЛЕНО
    lua.set_function("SetBacklightColor", [ghub](int r, int g, int b, const sol::object& device) {
        return ghub["SetBacklightColor"](r, g, b, device);
        });

    // hzCf681ZWWcx - специальная функция, возможно нужна
    lua.set_function("hzCf681ZWWcx", [ghub]() {
        return ghub["hzCf681ZWWcx"]();
        });

    // Тестирование функций после регистрации
    std::cout << "API привязано. Проверка GHUB.OutputLogMessage: ";
    sol::function test = ghub["OutputLogMessage"];
    if (test.valid()) {
        test("Тестовое сообщение из BindAPI");
        std::cout << "OK\n";
    }
    else {
        std::cerr << "FAILED\n";
    }
}

// Остальные методы остаются без изменений (KeyStringToVk, PressKey, и т.д.)
// ...

int LuaEngine::KeyStringToVk(const std::string& key) {
    static const std::unordered_map<std::string, int> keyMap = {
        {"pause", VK_PAUSE},
        {"ctrl", VK_CONTROL},
        {"lctrl", VK_LCONTROL},
        {"rctrl", VK_RCONTROL},
        {"shift", VK_SHIFT},
        {"lshift", VK_LSHIFT},
        {"rshift", VK_RSHIFT},
        {"alt", VK_MENU},
        {"lalt", VK_LMENU},
        {"ralt", VK_RMENU},
        {"space", VK_SPACE},
        {"enter", VK_RETURN},
        {"return", VK_RETURN},
        {"escape", VK_ESCAPE},
        {"esc", VK_ESCAPE},
        {"tab", VK_TAB},
        {"backspace", VK_BACK},
        {"delete", VK_DELETE},
        {"insert", VK_INSERT},
        {"home", VK_HOME},
        {"end", VK_END},
        {"pageup", VK_PRIOR},
        {"pagedown", VK_NEXT},
        {"left", VK_LEFT},
        {"right", VK_RIGHT},
        {"up", VK_UP},
        {"down", VK_DOWN},
        {"f1", VK_F1},
        {"f2", VK_F2},
        {"f3", VK_F3},
        {"f4", VK_F4},
        {"f5", VK_F5},
        {"f6", VK_F6},
        {"f7", VK_F7},
        {"f8", VK_F8},
        {"f9", VK_F9},
        {"f10", VK_F10},
        {"f11", VK_F11},
        {"f12", VK_F12},
        {"numlock", VK_NUMLOCK},
        {"scrolllock", VK_SCROLL},
        {"capslock", VK_CAPITAL},
        {"lwin", VK_LWIN},
        {"rwin", VK_RWIN},
        {"app", VK_APPS}
    };

    std::string lowerKey = key;
    std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);

    auto it = keyMap.find(lowerKey);
    if (it != keyMap.end()) return it->second;

    if (lowerKey.size() == 1) {
        char c = lowerKey[0];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) {
            return toupper(c);
        }
    }

    return 0;
}

void LuaEngine::PressKey(const std::string& key) {
    int vk = KeyStringToVk(key);
    if (vk != 0) {
        ::PressKey(vk);
        OutputLogMessage("PressKey: " + key + "\n");
    }
    else {
        OutputLogMessage("PressKey: Unknown key '" + key + "'\n");
    }
}

void LuaEngine::ReleaseKey(const std::string& key) {
    int vk = KeyStringToVk(key);
    if (vk != 0) {
        ::ReleaseKey(vk);
        OutputLogMessage("ReleaseKey: " + key + "\n");
    }
    else {
        OutputLogMessage("ReleaseKey: Unknown key '" + key + "'\n");
    }
}

bool LuaEngine::hzCf681ZWWcx() {
    return !IsMouseButtonPressed(1);
}

void LuaEngine::ReloadScript() {
    lua = sol::state{};
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::os);
    BindAPI();

    const std::string script_path = "assets/user_script.lua";
    std::cout << "Попытка загрузки скрипта: " << script_path << std::endl;

    try {
        auto result = lua.safe_script_file(script_path, sol::script_pass_on_error);

        if (result.valid()) {
            std::cout << "[LUA] Скрипт успешно загружен" << std::endl;

            // Проверка наличия OnEvent
            sol::function on_event = lua["OnEvent"];
            if (on_event.valid()) {
                std::cout << "Функция OnEvent найдена в скрипте" << std::endl;
            }
            else {
                std::cerr << "Предупреждение: Функция OnEvent не найдена в скрипте!" << std::endl;
            }
        }
        else {
            sol::error err = result;
            std::cerr << "[LUA ERROR] Ошибка загрузки скрипта: " << err.what() << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[LUA FATAL] " << e.what() << std::endl;
    }
}

void LuaEngine::CallOnEvent(const std::string& event, int arg) {
    try {
        sol::function on_event = lua["OnEvent"];
        if (on_event.valid()) {
            // Используем pcall для безопасного вызова
            sol::protected_function_result result = on_event(event, arg);

            if (!result.valid()) {
                sol::error err = result;
                std::string errorMsg = err.what();

                // Убираем излишнюю информацию
                size_t pos = errorMsg.find("stack traceback");
                if (pos != std::string::npos) {
                    errorMsg = errorMsg.substr(0, pos);
                }

                // Фильтруем ошибки о неизвестных функциях
                if (errorMsg.find("attempt to call") == std::string::npos) {
                    std::cerr << "LUA ERROR: " << errorMsg << std::endl;
                    ::OutputLogMessage("[LUA ERROR] " + errorMsg + "\n");
                }
            }
        }
        else {
            // Не логируем отсутствие OnEvent для некоторых событий
            if (event != "PROFILE_ACTIVATED" && event != "PROFILE_DEACTIVATED") {
                std::cerr << "OnEvent not found for event: " << event << std::endl;
            }
        }
    }
    catch (const sol::error& e) {
        std::string errorMsg = e.what();
        if (errorMsg.find("unknown function") == std::string::npos) {
            std::cerr << "SOL ERROR: " << errorMsg << std::endl;
            ::OutputLogMessage("[SOL ERROR] " + errorMsg + "\n");
        }
    }
    catch (const std::exception& e) {
        std::cerr << "STD EXCEPTION: " << e.what() << std::endl;
        ::OutputLogMessage("[EXCEPTION] " + std::string(e.what()) + "\n");
    }
    catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in CallOnEvent" << std::endl;
        ::OutputLogMessage("[UNKNOWN EXCEPTION]\n");
    }
}

bool LuaEngine::IsModifierPressed(const std::string& mod) {
    return IsModifierKeyPressed(mod);
}

bool LuaEngine::IsMouseButtonPressed(int button) {
    return IsMouseButtonDown(button);
}