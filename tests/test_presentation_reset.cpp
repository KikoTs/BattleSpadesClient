// Regression: changing Antialiasing in Settings killed the client.
//
// On Direct3D 11/12 bgfx::reset replaces the swap chain to change the MSAA
// sample count. Since the Steam overlay is attached before bgfx creates the
// device (2026-09-30), the overlay hooks Present and holds the chain; the
// replacement is refused and bgfx raises Fatal::UnableToInitialize ("Failed
// to create swap chain.", renderer_d3d11.cpp:2524), which the client's bgfx
// callback turns into std::abort().
//
// This drives the real BgfxUiRenderer on a never-shown window with a DXGI
// Present hook that holds the chain exactly as an overlay does. If the renderer
// ever forwards a sample-count change to bgfx on Direct3D again, the process
// aborts here and the test fails.

#include "battlespades/render/bgfx_ui_renderer.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#endif

namespace {

namespace render = battlespades::render;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void pure_policy() {
    expect(!render::multisample_change_is_live(render::GraphicsBackend::direct3d11),
           "Direct3D 11 replaces the swap chain for MSAA and must defer it");
    expect(!render::multisample_change_is_live(render::GraphicsBackend::direct3d12),
           "Direct3D 12 replaces the swap chain for MSAA and must defer it");
    expect(!render::multisample_change_is_live(render::GraphicsBackend::automatic),
           "an unresolved backend must be treated conservatively");
    expect(render::multisample_change_is_live(render::GraphicsBackend::vulkan) &&
               render::multisample_change_is_live(render::GraphicsBackend::metal) &&
               render::multisample_change_is_live(render::GraphicsBackend::opengl),
           "non-DXGI backends keep live MSAA changes");
}

#if defined(_WIN32)

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
PresentFn original_present = nullptr;
IDXGISwapChain* held_chain = nullptr;
bool hold_references{true};

/** Overlay stand-in: keeps a reference to whatever chain it last presented. */
HRESULT STDMETHODCALLTYPE holding_present(IDXGISwapChain* chain, UINT interval, UINT flags) {
    if (hold_references && held_chain != chain) {
        if (held_chain != nullptr) {
            held_chain->Release();
        }
        chain->AddRef();
        held_chain = chain;
    }
    return original_present(chain, interval, flags);
}

HWND hidden_window(const wchar_t* name, int width, int height) {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = name;
    RegisterClassW(&window_class);
    // No WS_VISIBLE: nothing ever appears on the desktop.
    return CreateWindowExW(0, name, name, WS_OVERLAPPEDWINDOW, 0, 0, width, height, nullptr,
                           nullptr, window_class.hInstance, nullptr);
}

/** Patches IDXGISwapChain::Present in dxgi's shared vtable. False: no device. */
bool install_present_hook() {
    const HWND window = hidden_window(L"aos_presentation_hook", 64, 64);
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2U;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1U;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0U,
                                             nullptr, 0U, D3D11_SDK_VERSION, &description,
                                             &chain, &device, nullptr, &context))) {
        return false;
    }
    constexpr std::size_t present_slot{8U};
    void** table = *reinterpret_cast<void***>(chain);
    DWORD protection{};
    VirtualProtect(&table[present_slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &protection);
    original_present = reinterpret_cast<PresentFn>(table[present_slot]);
    table[present_slot] = reinterpret_cast<void*>(&holding_present);
    VirtualProtect(&table[present_slot], sizeof(void*), protection, &protection);
    chain->Release();
    context->Release();
    device->Release();
    return true;
}

void present_frames(render::BgfxUiRenderer& renderer, int count) {
    for (int frame{}; frame < count; ++frame) {
        expect(renderer.begin_frame(), std::string{renderer.last_error()});
        expect(renderer.end_frame(), std::string{renderer.last_error()});
    }
}

[[nodiscard]] bool start(render::BgfxUiRenderer& renderer,
                         HWND window,
                         render::GraphicsBackend backend) {
    render::BgfxUiRendererConfig config;
    config.native_window.window = window;
    config.drawable_extent = {1'152U, 864U};
    config.asset_root = AOS_TEST_ASSET_ROOT;
    config.shader_root = AOS_SHADER_BIN_ROOT;
    config.backend = backend;
    config.vertical_sync = false;
    config.multisample_samples = 0U;
    if (!renderer.initialize(config)) {
        return false;
    }
    if (renderer.active_backend() != backend) {
        renderer.shutdown();
        return false;
    }
    return true;
}

void direct3d_keeps_its_swap_chain(render::GraphicsBackend backend, std::string_view name) {
    hold_references = true;
    const HWND window = hidden_window(L"aos_presentation_reset", 1'152, 864);
    render::BgfxUiRenderer renderer;
    if (!start(renderer, window, backend)) {
        std::cout << "SKIP: " << name << " unavailable\n";
        DestroyWindow(window);
        return;
    }
    present_frames(renderer, 3);
    expect(held_chain != nullptr,
           std::string{name} + ": the overlay stand-in never saw a Present; the test is vacuous");
    // Every reset carries BGFX_RESET_MAXANISOTROPY from init on (the Texture
    // Filtering setting switches per-draw sampler flags instead), so a resize
    // or VSync toggle is a ResizeBuffers on the SAME chain the overlay holds.
    const auto* const startup_chain = held_chain;

    // The crash: Antialiasing OFF -> 4x. Before the fix this aborted inside
    // bgfx's render thread on the next frame.
    expect(renderer.set_presentation_options(false, 4U), std::string{renderer.last_error()});
    present_frames(renderer, 3);
    expect(renderer.active_multisample_samples() == 0U,
           std::string{name} + ": the running swap chain must keep its startup sample count");
    expect(renderer.multisample_restart_pending(),
           std::string{name} + ": the deferred sample count must be reported for a restart notice");

    // VSync still applies live (ResizeBuffers path), under the same hook,
    // without dropping the pending MSAA request.
    expect(renderer.set_presentation_options(true, 4U), std::string{renderer.last_error()});
    present_frames(renderer, 3);
    expect(renderer.set_presentation_options(false, 4U), std::string{renderer.last_error()});
    present_frames(renderer, 3);
    expect(renderer.multisample_restart_pending(), "a VSync toggle must not lose the request");

    // Resizing (resolution / fullscreen) is a ResizeBuffers too.
    expect(renderer.resize({1'280U, 720U}), std::string{renderer.last_error()});
    present_frames(renderer, 3);
    expect(held_chain == startup_chain,
           std::string{name} + ": VSync and resize resets must keep the overlay's swap chain");

    // Choosing the startup value again clears the pending notice.
    expect(renderer.set_presentation_options(false, 0U), std::string{renderer.last_error()});
    expect(!renderer.multisample_restart_pending(),
           "returning to the running sample count needs no restart");

    hold_references = false;
    // Let go before bgfx destroys its device, as the overlay does when the
    // chain is torn down; a reference outliving the device is not legal.
    if (held_chain != nullptr) {
        held_chain->Release();
        held_chain = nullptr;
    }
    renderer.shutdown();
    DestroyWindow(window);
    std::cout << name << ": MSAA change deferred, VSync and resize applied live\n";
}

void vulkan_applies_msaa_live() {
    const HWND window = hidden_window(L"aos_presentation_vulkan", 1'152, 864);
    render::BgfxUiRenderer renderer;
    if (!start(renderer, window, render::GraphicsBackend::vulkan)) {
        std::cout << "SKIP: Vulkan unavailable\n";
        DestroyWindow(window);
        return;
    }
    present_frames(renderer, 3);
    expect(renderer.set_presentation_options(false, 4U), std::string{renderer.last_error()});
    present_frames(renderer, 3);
    expect(renderer.active_multisample_samples() == 4U && !renderer.multisample_restart_pending(),
           "Vulkan changes the sample count in place");
    renderer.shutdown();
    DestroyWindow(window);
    std::cout << "Vulkan: MSAA change applied live\n";
}

#endif

} // namespace

int main() {
    try {
        pure_policy();
#if defined(_WIN32)
        if (!std::filesystem::is_directory(AOS_TEST_ASSET_ROOT)) {
            std::cout << "SKIP: asset root unavailable; GPU checks not run\n";
        } else if (!install_present_hook()) {
            std::cout << "SKIP: no Direct3D 11 device; GPU checks not run\n";
        } else {
            direct3d_keeps_its_swap_chain(render::GraphicsBackend::direct3d11, "Direct3D 11");
            direct3d_keeps_its_swap_chain(render::GraphicsBackend::direct3d12, "Direct3D 12");
            vulkan_applies_msaa_live();
        }
#endif
        std::cout << "presentation reset tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "presentation reset tests failed: " << error.what() << '\n';
        return 1;
    }
}
