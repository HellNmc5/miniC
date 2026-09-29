// ============================================================================
//  gui_platform.cpp — окно Windows и DirectX 11 для Dear ImGui.
//  Стандартная обвязка по образцу examples/example_win32_directx11 из ImGui.
//  Своей логики здесь нет: каждый кадр вызывается guiFrame() из gui.cpp.
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <initializer_list>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "gui.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

ID3D11Device*           device = nullptr;
ID3D11DeviceContext*    context = nullptr;
IDXGISwapChain*         swapChain = nullptr;
ID3D11RenderTargetView* target = nullptr;
UINT resizeW = 0, resizeH = 0;

void createTarget() {
    ID3D11Texture2D* back = nullptr;
    swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        device->CreateRenderTargetView(back, nullptr, &target);
        back->Release();
    }
}

void releaseTarget() {
    if (target) { target->Release(); target = nullptr; }
}

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    for (D3D_DRIVER_TYPE type : { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP }) {   // WARP — программный
        if (D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                          &sd, &swapChain, &device, nullptr, &context) == S_OK) {
            createTarget();
            return true;
        }
    }
    return false;
}

void releaseDevice() {
    releaseTarget();
    if (swapChain) { swapChain->Release(); swapChain = nullptr; }
    if (context) { context->Release(); context = nullptr; }
    if (device) { device->Release(); device = nullptr; }
}

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;
    switch (m) {
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) { resizeW = LOWORD(l); resizeH = HIWORD(l); }
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_KEYMENU) return 0;   // Alt не уводит фокус в системное меню
        break;
    case WM_CLOSE:
        if (guiConfirmClose()) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MiniC";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"miniC", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              (int)(1280 * scale), (int)(800 * scale), nullptr, nullptr, hinst, nullptr);
    if (!hwnd || !createDevice(hwnd)) {
        releaseDevice();
        MessageBoxW(nullptr, L"Не удалось инициализировать DirectX 11.", L"miniC", MB_ICONERROR);
        return 1;
    }

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;   // не создавать imgui.ini рядом с программой
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context);
    guiInit(hwnd, scale);

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    bool occluded = false;
    for (;;) {
        MSG msg;
        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) quit = true;
        }
        if (quit) break;

        // свёрнутое окно не рисуем, чтобы не грузить процессор
        if (occluded && swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        if (resizeW && resizeH) {
            releaseTarget();
            swapChain->ResizeBuffers(0, resizeW, resizeH, DXGI_FORMAT_UNKNOWN, 0);
            resizeW = resizeH = 0;
            createTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        guiFrame();
        ImGui::Render();

        const ImVec4& bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float clear[4] = { bg.x, bg.y, bg.z, 1.0f };
        context->OMSetRenderTargets(1, &target, nullptr);
        context->ClearRenderTargetView(target, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        occluded = swapChain->Present(1, 0) == DXGI_STATUS_OCCLUDED;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    releaseDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hinst);
    return 0;
}
