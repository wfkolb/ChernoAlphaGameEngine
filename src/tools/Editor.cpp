#ifdef ENGINE_DEVREL

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "tools/editor/Editor.h"
#include "tools/internal/editor/EntityListPanel.h"
#include "tools/internal/editor/InspectorPanel.h"
#include "core/log.h"

namespace engine::tools {

Editor::Editor() = default;
Editor::~Editor() { if (initialized_) shutdown(); }

void Editor::init(void* device, uint32_t numFramesInFlight, uint32_t backBufferFormat,
                  void* heap, uint64_t cpuHandle, uint64_t gpuHandle)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    // Note: ImGui_ImplWin32_Init is called separately by the Window that owns the HWND.
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{ cpuHandle };
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{ gpuHandle };
    ImGui_ImplDX12_Init(
        static_cast<ID3D12Device*>(device),
        static_cast<int>(numFramesInFlight),
        static_cast<DXGI_FORMAT>(backBufferFormat),
        static_cast<ID3D12DescriptorHeap*>(heap),
        cpu, gpu);

    initialized_ = true;
    LOG_INFO("Editor initialized (DevRel)");
}

void Editor::shutdown()
{
    ImGui_ImplDX12_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void Editor::update(core::ecs::World& world)
{
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();

    drawEntityListPanel(world);
    drawInspectorPanel(world);

    ImGui::Render();
}

void Editor::drawEntityListPanel(core::ecs::World& world)
{
    selectedEntity_ = internal::drawEntityListPanel(world, selectedEntity_);
}

void Editor::drawInspectorPanel(core::ecs::World& world)
{
    internal::drawInspectorPanel(world, selectedEntity_);
}

} // namespace engine::tools

#endif // ENGINE_DEVREL
