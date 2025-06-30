#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <iostream>
#include <chrono>
#include <queue>
#include <mutex>
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
#define INPUT_TIMER_ID 1
#define GUI_TIMER_ID 2
#define INPUT_POLL_INTERVAL 1  // 1мс = 1000Hz

// Глобальные переменные
static BYTE g_prevKeyState[256] = {};   // Предыдущее состояние клавиш
static LuaEngine* g_pLuaEngine = nullptr;
static UINT g_timerResolution = 1;      // Разрешение таймера

// Буфер событий
struct InputEvent {
    std::string eventName;
    int argument;
    std::chrono::steady_clock::time_point timestamp;
};

static std::queue<InputEvent> g_eventQueue;
static std::mutex g_eventMutex;

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void CreateRenderTarget();
void CleanupRenderTarget();
bool CreateDeviceD3D(HWND);
void CleanupDeviceD3D();
void ProcessInputPolling();
void ProcessEventQueue();

// Глобальный HWND
HWND g_hwnd = nullptr;

// Глобальный указатель на GUI
Gui* g_pGui = nullptr;

// Функция для добавления события в очередь
void QueueInputEvent(const std::string& eventName, int arg) {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    // Защита от переполнения очереди
    if (g_eventQueue.size() > 1000) {
        // Удаляем самые старые события при переполнении
        while (g_eventQueue.size() > 800) {
            g_eventQueue.pop();
        }
        return;
    }

    InputEvent event;
    event.eventName = eventName;
    event.argument = arg;
    event.timestamp = std::chrono::steady_clock::now();
    g_eventQueue.push(event);
}

// Обработка всех событий из очереди
void ProcessEventQueue() {
    std::lock_guard<std::mutex> lock(g_eventMutex);

    auto start = std::chrono::steady_clock::now();
    size_t processed = 0;
    const size_t maxProcessPerFrame = 100; // Ограничение на обработку за один кадр

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
        processed++;
    }

    // Логирование при большой очереди
    if (g_eventQueue.size() > 50) {
        static auto lastLog = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - lastLog > std::chrono::seconds(1)) {
            OutputLogMessage("Queue size: " + std::to_string(g_eventQueue.size()) + "\n");
            lastLog = now;
        }
    }
}

// Оптимизированная высокочастотная обработка ввода
void ProcessInputPolling() {
    // Статический массив для кнопок мыши
    static const struct MouseButton {
        int vk;
        int id;
    } buttons[] = {
        { VK_LBUTTON, 1 },
        { VK_RBUTTON, 2 },
        { VK_MBUTTON, 3 },
        { VK_XBUTTON1, 4 },
        { VK_XBUTTON2, 5 },
    };

    try {
        // Обработка кнопок мыши
        for (const auto& btn : buttons) {
            bool isDown = (GetAsyncKeyState(btn.vk) & 0x8000) != 0;
            bool wasDown = g_prevKeyState[btn.vk] & 0x80;

            if (isDown != wasDown) {
                if (isDown) {
                    QueueInputEvent("MOUSE_BUTTON_PRESSED", btn.id);
                }
                else {
                    QueueInputEvent("MOUSE_BUTTON_RELEASED", btn.id);
                }
                g_prevKeyState[btn.vk] = isDown ? 0x80 : 0;
            }
        }

        // Обработка NumPad (0-9)
        for (int vk = VK_NUMPAD0; vk <= VK_NUMPAD9; ++vk) {
            bool isDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
            bool wasDown = g_prevKeyState[vk] & 0x80;

            if (isDown != wasDown) {
                int arg = 10 + (vk - VK_NUMPAD0);
                if (isDown) {
                    QueueInputEvent("MOUSE_BUTTON_PRESSED", arg);
                }
                else {
                    QueueInputEvent("MOUSE_BUTTON_RELEASED", arg);
                }
                g_prevKeyState[vk] = isDown ? 0x80 : 0;
            }
        }

        // Обработка клавиши F
        bool fDown = (GetAsyncKeyState('F') & 0x8000) != 0;
        bool fWasDown = g_prevKeyState['F'] & 0x80;

        if (fDown && !fWasDown) {
            QueueInputEvent("G_PRESSED", 'F');
        }

        // Сохраняем состояние F
        g_prevKeyState['F'] = fDown ? 0x80 : 0;

    }
    catch (const std::exception& e) {
        OutputLogMessage("[INPUT ERROR] " + std::string(e.what()) + "\n");
    }
    catch (...) {
        OutputLogMessage("[UNKNOWN INPUT ERROR]\n");
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Настройка высокоточного таймера
    TIMECAPS tc;
    if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
        // Исправленная строка:
        UINT minRes = (tc.wPeriodMin > 1u) ? tc.wPeriodMin : 1u;
        g_timerResolution = (minRes < tc.wPeriodMax) ? minRes : tc.wPeriodMax;
        timeBeginPeriod(g_timerResolution);
    }

    // Установка максимального приоритета
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // Инициализация состояний
    ZeroMemory(g_prevKeyState, sizeof(g_prevKeyState));

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
                      hInstance, NULL, NULL, NULL, NULL,
                      _T("HelperApp"), NULL };
    ::RegisterClassEx(&wc);
    HWND hwnd = ::CreateWindow(wc.lpszClassName, _T("Helper Application"), WS_OVERLAPPEDWINDOW,
        100, 100, 1280, 720, NULL, NULL, wc.hInstance, NULL);

    g_hwnd = hwnd;

    if (!CreateDeviceD3D(hwnd)) return 1;
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

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

    // Запуск высокочастотного таймера для ввода
    SetTimer(hwnd, INPUT_TIMER_ID, INPUT_POLL_INTERVAL, nullptr);

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

    KillTimer(hwnd, INPUT_TIMER_ID);
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
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
        createDeviceFlags, featureLevelArray, 1,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain,
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
    case WM_LBUTTONDOWN:
        QueueInputEvent("MOUSE_BUTTON_PRESSED", 1);
        return 0;
    case WM_LBUTTONUP:
        QueueInputEvent("MOUSE_BUTTON_RELEASED", 1);
        return 0;
    case WM_RBUTTONDOWN:
        QueueInputEvent("MOUSE_BUTTON_PRESSED", 2);
        return 0;
    case WM_RBUTTONUP:
        QueueInputEvent("MOUSE_BUTTON_RELEASED", 2);
        return 0;
    case WM_MBUTTONDOWN:
        QueueInputEvent("MOUSE_BUTTON_PRESSED", 3);
        return 0;
    case WM_MBUTTONUP:
        QueueInputEvent("MOUSE_BUTTON_RELEASED", 3);
        return 0;
    case WM_XBUTTONDOWN:
        if (HIWORD(wParam) == XBUTTON1)
            QueueInputEvent("MOUSE_BUTTON_PRESSED", 4);
        else if (HIWORD(wParam) == XBUTTON2)
            QueueInputEvent("MOUSE_BUTTON_PRESSED", 5);
        return 0;
    case WM_XBUTTONUP:
        if (HIWORD(wParam) == XBUTTON1)
            QueueInputEvent("MOUSE_BUTTON_RELEASED", 4);
        else if (HIWORD(wParam) == XBUTTON2)
            QueueInputEvent("MOUSE_BUTTON_RELEASED", 5);
        return 0;

    case WM_TIMER:
        if (wParam == INPUT_TIMER_ID) {
            ProcessInputPolling();
        }
        else if (wParam == GUI_TIMER_ID) {
            try {
                ProcessEventQueue();

                // Start the ImGui frame
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                if (g_pGui) {
                    g_pGui->Render();
                }

                ImGui::Render();
                const float clear_color[4] = { 0.1f, 0.1f, 0.1f, 1.00f };
                g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
                g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                g_pSwapChain->Present(1, 0);
            }
            catch (const sol::error& e) {
                std::cerr << "SOL ERROR IN GUI TIMER: " << e.what() << std::endl;
                OutputLogMessage("[FATAL SOL ERROR] " + std::string(e.what()) + "\n");
            }
            catch (const std::exception& e) {
                std::cerr << "EXCEPTION IN GUI TIMER: " << e.what() << std::endl;
                OutputLogMessage("[FATAL EXCEPTION] " + std::string(e.what()) + "\n");
            }
            catch (...) {
                std::cerr << "UNKNOWN EXCEPTION IN GUI TIMER" << std::endl;
                OutputLogMessage("[FATAL UNKNOWN EXCEPTION]\n");
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

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
