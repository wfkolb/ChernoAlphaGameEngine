#pragma once

#include <core/diag/Assert.h>
#include <cstdint>
#include <memory>
#include <string_view>

namespace engine::rendering {

    class Window {
    public:
        struct Desc {
            uint32_t         width  { 1280 };
            uint32_t         height { 720 };
            std::wstring_view title;
        };

        static Window create(const Desc& desc);

        // Returns the underlying HWND as void*. Cast to HWND only inside rendering internals.
        void*    nativeHandle()  const noexcept;
        uint32_t clientWidth()   const noexcept;
        uint32_t clientHeight()  const noexcept;
        // Returns true once WM_QUIT has been processed by the message pump.
        bool     wantsClose()    const noexcept;

        ENGINE_NO_COPY(Window);

        Window(Window&&) noexcept;
        Window& operator=(Window&&) noexcept;
        ~Window();

        // Impl is forward-declared public so internal .cpp files can name it.
        // The definition lives in internal/WindowImpl.h and is never shipped.
        struct Impl;

    private:
        std::unique_ptr<Impl> impl_;

        explicit Window(std::unique_ptr<Impl> impl) noexcept;
    };

} // namespace engine::rendering
