#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include "app/Application.h"
#include "app/ApplicationDesc.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    engine::app::ApplicationDesc desc;
    desc.windowTitle  = L"Engine v0.1";
    desc.windowWidth  = 1280;
    desc.windowHeight = 720;
    desc.vsync        = true;
    desc.game         = nullptr;

    // Parse CLI arguments for --host and --connect
    {
        int    argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                if (::wcscmp(argv[i], L"--host") == 0 && i + 1 < argc) {
                    ++i;
                    unsigned long port = ::wcstoul(argv[i], nullptr, 10);
                    if (port > 0 && port <= 65535) {
                        desc.hostPort = static_cast<uint16_t>(port);
                    }
                } else if (::wcscmp(argv[i], L"--connect") == 0 && i + 1 < argc) {
                    ++i;
                    // Convert the ASCII-safe "ip:port" argument to narrow string
                    const wchar_t* wArg = argv[i];
                    int len = ::WideCharToMultiByte(CP_ACP, 0, wArg, -1,
                                                    nullptr, 0, nullptr, nullptr);
                    if (len > 1) {
                        desc.connectAddr.resize(static_cast<size_t>(len - 1));
                        ::WideCharToMultiByte(CP_ACP, 0, wArg, -1,
                                              desc.connectAddr.data(), len,
                                              nullptr, nullptr);
                    }
                }
                // Unknown args are silently ignored
            }
            ::LocalFree(argv);
        }
    }

    engine::app::Application app;
    if (!app.init(desc)) return 1;
    app.run();
    app.shutdown();
    return 0;
}
