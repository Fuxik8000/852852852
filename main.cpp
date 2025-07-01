#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <iostream>
#include <chrono>
#include <queue>
#include <mutex>
#include <vector>
#include <mmsystem.h>  // Для высокоточного таймера
#pragma comment(lib, "winmm.lib")

#include "imgui.h"  
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "Gui.h"
#include "LuaEngine.h"
#include "Log.h"

// DirectX
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Таймеры
#define GUI_TIMER_ID 2

// Глобальные переменные
static BYTE g_prevKeyState[256] = {};   // Предыдущее состояние клавиш
static LuaEngine* g_pLuaEngine = nullptr;
static UINT g_timerResolution = 1;      // Разрешение таймера

// Буфер событий
struct InputEvent {
    std::string eventName;
    int argument;
    std::chrono::steady_clock::time_point timestamp;

    // Конструктор для прямой инициализации
    InputEvent(const std::string& name, int arg)
        : eventName(name), argument(arg), timestamp(std::chrono::steady_clock::now()) {
    }
};

static std::queue<InputEvent> g_eventQueue;
static std::mutex g_eventMutex;

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void CreateRenderTarget();
void CleanupRenderTarget();
bool CreateDeviceD3D(HWND);
void CleanupDeviceD3D();
void ProcessEventQueue();

// Глобальный HWND
HWND g_hwnd = nullptr;

// Глобальный указатель на GUI
Gui* g_pGui = nullptr;

// Оптимизированная функция для добавления события в очередь
void QueueInputEvent(const std::string& eventName, int arg) {
    std::lock_guard<std::mutex> lock(g_eventMutex);

    // Защита от переполнения очереди - более эффективная очистка
    const size_t maxQueueSize = 1000;
    const size_t clearToSize = 800;

    if (g_eventQueue.size() >= maxQueueSize) {
        // Удаляем сразу нужное количество элементов
        size_t toRemove = g_eventQueue.size() - clearToSize;
        for (size_t i = 0; i < toRemove; ++i) {
            g_eventQueue.pop();
        }
    }

    // Используем emplace для прямого конструирования в контейнере
    g_eventQueue.emplace(eventName, arg);
}

// Обработка всех событий из очереди
void ProcessEventQueue() {
    std::lock_guard<std::mutex> lock(g_eventMutex);

    const size_t maxProcessPerFrame = 100; // Ограничение на обработку за один кадр
    size_t processed = 0;

    while (!g_eventQueue.empty() && processed < maxProcessPerFrame) {
        const InputEvent& event = g_eventQueue.front();

        try {
            if (g_pLuaEngine) {
                g_pLuaEngine->CallOnEvent(event.eventName.c_str(), event.argument);
            }
        }
        catch (const sol::error& e) {
            std::cerr << "SOL ERROR in event processing: " << e.what() << std::endl;
            OutputLogMessage("[SOL ERROR] " + std::string(e.what()) + "\n");
        }
        catch (const std::exception& e) {
            std::cerr << "EXCEPTION in event processing: " << e.what() << std::endl;
            OutputLogMessage("[EXCEPTION] " + std::string(e.what()) + "\n");
        }

        g_eventQueue.pop();
        ++processed;
    }

    // Логирование при большой очереди - статические переменные для оптимизации
    static constexpr size_t logThreshold = 50;
    static constexpr auto logInterval = std::chrono::seconds(1);

    if (g_eventQueue.size() > logThreshold) {
        static auto lastLog = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - lastLog > logInterval) {
            OutputLogMessage("Queue size: " + std::to_string(g_eventQueue.size()) + "\n");
            lastLog = now;
        }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Настройка высокоточного таймера
    TIMECAPS tc;
    if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
        // Исправленная строка с более эффективной логикой:
        const UINT minRes = (tc.wPeriodMin > 1u) ? tc.wPeriodMin : 1u;
        g_timerResolution = (minRes < tc.wPeriodMax) ? minRes : tc.wPeriodMax;
        timeBeginPeriod(g_timerResolution);
    }

    // Установка максимального приоритета
 //   SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
 //   SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // Инициализация состояний - используем constexpr
    static_assert(sizeof(g_prevKeyState) == 256, "Key state array size mismatch");
    ZeroMemory(g_prevKeyState, sizeof(g_prevKeyState));

    // Оптимизированная инициализация структуры окна
    const WNDCLASSEX wc = {
        sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
        hInstance, NULL, NULL, NULL, NULL,
        _T("HelperApp"), NULL
    };
    ::RegisterClassEx(&wc);

    HWND hwnd = ::CreateWindow(wc.lpszClassName, _T("Helper Application"), WS_OVERLAPPEDWINDOW,
        100, 100, 1280, 720, NULL, NULL, wc.hInstance, NULL);

    g_hwnd = hwnd;

    // Регистрация Raw Input устройств - константные данные
    static constexpr RAWINPUTDEVICE rid[2] = {
        { 0x01, 0x06, RIDEV_INPUTSINK, nullptr }, // Keyboard - hwndTarget установим после
        { 0x01, 0x02, RIDEV_INPUTSINK, nullptr }  // Mouse
    };

    RAWINPUTDEVICE ridCopy[2];
    memcpy(ridCopy, rid, sizeof(rid));
    ridCopy[0].hwndTarget = hwnd;
    ridCopy[1].hwndTarget = hwnd;

    if (!RegisterRawInputDevices(ridCopy, 2, sizeof(RAWINPUTDEVICE))) {
        OutputDebugStringA("Failed to register raw input devices!\n");
    }

    if (!CreateDeviceD3D(hwnd)) return 1;
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    // Оптимизированная настройка шрифта
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = true;

    // Загрузка шрифта
    ImGui::GetIO().Fonts->AddFontFromFileTTF(
        "assets/fonts/JetBrainsMono-Regular.ttf", 18.0f, &config,
        ImGui::GetIO().Fonts->GetGlyphRangesCyrillic()
    );

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    LuaEngine luaEngine;
    g_pLuaEngine = &luaEngine;

    Gui gui(&luaEngine);
    g_pGui = &gui;
    gui.SetReloadCallback([&]() {
        luaEngine.ReloadScript();
        });

    // Запуск GUI таймера
    SetTimer(hwnd, GUI_TIMER_ID, 16, nullptr); // 60 FPS

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT) {
        // Обрабатываем сообщения Windows
        if (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        else {
            // Небольшая пауза для снижения нагрузки на CPU
            Sleep(1);
        }
    }

    // Восстановление настроек системы
    timeEndPeriod(g_timerResolution);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    KillTimer(hwnd, GUI_TIMER_ID);
    g_pLuaEngine = nullptr;
    g_pGui = nullptr;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    // Инициализация структуры с константными значениями
    const DXGI_SWAP_CHAIN_DESC sd = {
        .BufferDesc = { 0, 0, {0, 0}, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED, DXGI_MODE_SCALING_UNSPECIFIED },
        .SampleDesc = { 1, 0 },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .OutputWindow = hWnd,
        .Windowed = TRUE,
        .SwapEffect = DXGI_SWAP_EFFECT_DISCARD,
        .Flags = 0
    };

    constexpr UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    constexpr D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
        createDeviceFlags, featureLevelArray, 1,
        D3D11_SDK_VERSION, const_cast<DXGI_SWAP_CHAIN_DESC*>(&sd), &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

// ImGui WndProc
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
    case WM_TIMER:
        if (wParam == GUI_TIMER_ID) {
            ProcessEventQueue();

            // Обновление GUI
            if (g_pGui) {
                // Начинаем кадр
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                // Отрисовка GUI
                g_pGui->Render();

                // Рендеринг
                ImGui::Render();
                static constexpr float clear_color[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
                g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
                g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                g_pSwapChain->Present(1, 0); // VSync
            }
        }
        return 0;

    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;

    case WM_INPUT:
    {
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize == 0)
            break;

        // Используем thread_local для избежания частых аллокаций
        thread_local std::vector<BYTE> lpb;
        lpb.resize(dwSize);

        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.data(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
            break;
        }

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(lpb.data());

        if (raw->header.dwType == RIM_TYPEKEYBOARD) {
            const RAWKEYBOARD& rawKB = raw->data.keyboard;
            const bool isDown = !(rawKB.Flags & RI_KEY_BREAK);
            const int vk = rawKB.VKey;

            // Обработка NumPad (0-9) - константы для оптимизации
            if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
                const int arg = 10 + (vk - VK_NUMPAD0);  // 10-19
                const std::string& eventName = isDown ? "MOUSE_BUTTON_PRESSED" : "MOUSE_BUTTON_RELEASED";
                QueueInputEvent(eventName, arg);
            }
            // Обработка клавиши F
            else if (vk == 'F') {
                if (isDown) {
                    QueueInputEvent("G_PRESSED", 'F');
                }
            }
            // Другие клавиши при необходимости - группировка для оптимизации ветвления
            else if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) {
                const std::string& eventName = isDown ? "KEY_PRESSED" : "KEY_RELEASED";
                QueueInputEvent(eventName, vk);
            }
        }
        else if (raw->header.dwType == RIM_TYPEMOUSE) {
            const RAWMOUSE& rawMouse = raw->data.mouse;
            const USHORT buttonFlags = rawMouse.usButtonFlags;

            // Оптимизированная обработка флагов мыши - проверяем все сразу
            if (buttonFlags & RI_MOUSE_BUTTON_1_DOWN)
                QueueInputEvent("MOUSE_BUTTON_PRESSED", 1);
            if (buttonFlags & RI_MOUSE_BUTTON_1_UP)
                QueueInputEvent("MOUSE_BUTTON_RELEASED", 1);
            if (buttonFlags & RI_MOUSE_BUTTON_2_DOWN)
                QueueInputEvent("MOUSE_BUTTON_PRESSED", 2);
            if (buttonFlags & RI_MOUSE_BUTTON_2_UP)
                QueueInputEvent("MOUSE_BUTTON_RELEASED", 2);
            if (buttonFlags & RI_MOUSE_BUTTON_3_DOWN)
                QueueInputEvent("MOUSE_BUTTON_PRESSED", 3);
            if (buttonFlags & RI_MOUSE_BUTTON_3_UP)
                QueueInputEvent("MOUSE_BUTTON_RELEASED", 3);
            if (buttonFlags & RI_MOUSE_BUTTON_4_DOWN)
                QueueInputEvent("MOUSE_BUTTON_PRESSED", 4);
            if (buttonFlags & RI_MOUSE_BUTTON_4_UP)
                QueueInputEvent("MOUSE_BUTTON_RELEASED", 4);
            if (buttonFlags & RI_MOUSE_BUTTON_5_DOWN)
                QueueInputEvent("MOUSE_BUTTON_PRESSED", 5);
            if (buttonFlags & RI_MOUSE_BUTTON_5_UP)
                QueueInputEvent("MOUSE_BUTTON_RELEASED", 5);
        }
        return 0;
    }
    }

    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
