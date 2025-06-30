#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <iostream>
#include <chrono>
#include <queue>
#include <mutex>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "Gui.h"
#include "LuaEngine.h"
#include "Log.h"
#include "InputThread.h"

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
static bool g_keyState[256] = {};
static bool g_mouseState[5] = {};
static LuaEngine* g_pLuaEngine = nullptr;

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
    InputEvent event;
    event.eventName = eventName;
    event.argument = arg;
    event.timestamp = std::chrono::steady_clock::now();
    g_eventQueue.push(event);
}

// Обработка всех событий из очереди
void ProcessEventQueue() {
    std::lock_guard<std::mutex> lock(g_eventMutex);

    while (!g_eventQueue.empty()) {
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
    }
}

// Высокочастотная обработка ввода
void ProcessInputPolling() {
    try {
        // Перехват NumPad (0-9)
        for (int vk = VK_NUMPAD0; vk <= VK_NUMPAD9; ++vk) {
            int arg = 10 + (vk - VK_NUMPAD0);
            SHORT state = GetAsyncKeyState(vk);
            bool isDown = (state & 0x8000) != 0;

            if (isDown && !g_keyState[vk]) {
                QueueInputEvent("MOUSE_BUTTON_PRESSED", arg);
#ifdef _DEBUG
                std::cout << "NumPad pressed: " << (vk - VK_NUMPAD0)
                    << " (arg: " << arg << ")\n";
#endif
            }
            else if (!isDown && g_keyState[vk]) {
                QueueInputEvent("MOUSE_BUTTON_RELEASED", arg);
#ifdef _DEBUG
                std::cout << "NumPad released: " << (vk - VK_NUMPAD0)
                    << " (arg: " << arg << ")\n";
#endif
            }
            g_keyState[vk] = isDown;
        }

        // Обработка кнопок мыши
        struct MouseButton {
            int vk;
            int id;
            int index;
        } buttons[] = {
            { VK_LBUTTON, 1, 0 },
            { VK_RBUTTON, 2, 1 },
            { VK_MBUTTON, 3, 2 },
            { VK_XBUTTON1, 4, 3 },
            { VK_XBUTTON2, 5, 4 },
        };

        for (auto& btn : buttons) {
            SHORT state = GetAsyncKeyState(btn.vk);
            bool isDown = (state & 0x8000) != 0;

            if (isDown && !g_mouseState[btn.index]) {
                QueueInputEvent("MOUSE_BUTTON_PRESSED", btn.id);
#ifdef _DEBUG
                std::cout << "Mouse button PRESSED: "
                    << "VK=0x" << std::hex << btn.vk << std::dec
                    << ", ID=" << btn.id
                    << ", Index=" << btn.index << std::endl;
#endif
            }
            else if (!isDown && g_mouseState[btn.index]) {
                QueueInputEvent("MOUSE_BUTTON_RELEASED", btn.id);
#ifdef _DEBUG
                std::cout << "Mouse button RELEASED: "
                    << "VK=0x" << std::hex << btn.vk << std::dec
                    << ", ID=" << btn.id
                    << ", Index=" << btn.index << std::endl;
#endif
            }
            g_mouseState[btn.index] = isDown;
        }

        // Проверка клавиши F
        static bool lastFState = false;
        SHORT fState = GetAsyncKeyState('F');
        bool fDown = (fState & 0x8000) != 0;
        if (fDown && !lastFState) {
            QueueInputEvent("G_PRESSED", 'F');
#ifdef _DEBUG
            std::cout << "Клавиша F нажата\n";
#endif
        }
        lastFState = fDown;

    }
    catch (const std::exception& e) {
        std::cerr << "EXCEPTION in input polling: " << e.what() << std::endl;
        OutputLogMessage("[INPUT POLLING ERROR] " + std::string(e.what()) + "\n");
    }
    catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in input polling" << std::endl;
        OutputLogMessage("[INPUT POLLING UNKNOWN ERROR]\n");
    }
}
static InputThread g_InputThread;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Включаем консоль для отладки
    //AllocConsole();
    //FILE* fDummy;
    //freopen_s(&fDummy, "CONOUT$", "w", stdout);
    //freopen_s(&fDummy, "CONOUT$", "w", stderr);
    //SetConsoleTitleA("Debug Console");
    //std::cout << "Приложение запущено. Отладка включена.\n";

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

    // Загрузка TTF с поддержкой кириллицы
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
    SetTimer(hwnd, GUI_TIMER_ID, 7, nullptr);

    std::cout << "Таймеры запущены:\n";
    std::cout << "- Input polling: " << INPUT_POLL_INTERVAL << "мс (" << (1000 / INPUT_POLL_INTERVAL) << "Hz)\n";
    std::cout << "- GUI refresh: 7мс (~142Hz)\n";

    // Запуск потока ввода
    g_InputThread.SetCallback([](int vk) {
        if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) {
            int arg = 9 + (vk - VK_NUMPAD1 + 1); // 10-18
            OutputLogMessage("InputThread: Нажат Num" + std::to_string(vk - VK_NUMPAD0) + "\n");
            if (g_pLuaEngine) {
                g_pLuaEngine->CallOnEvent("MOUSE_BUTTON_PRESSED", arg);
            }
        }
        else if (vk == VK_NUMPAD0) {
            OutputLogMessage("InputThread: Нажат Num0\n");
            if (g_pLuaEngine) {
                g_pLuaEngine->CallOnEvent("MOUSE_BUTTON_PRESSED", 19);
            }
        }
        });
    g_InputThread.Start();

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT) {
        // Обрабатываем сообщения Windows
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }

        // Легкая пауза для снижения нагрузки на CPU
        Sleep(1);
    }

    // Остановка потока перед выходом
    g_InputThread.Stop();

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
