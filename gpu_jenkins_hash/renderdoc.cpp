#pragma once

#include "renderdoc.hpp"
#define RENDERDOC

#ifdef RENDERDOC
#include <renderdoc_app.h>
// #pragma comment(lib, "renderdoc.dll")
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace renderdoc
{
#ifdef RENDERDOC
    RENDERDOC_API_1_1_2 *rdoc_api = nullptr;

    void init()
    {
        if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
        {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI =
                (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
            RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&rdoc_api);
        }
    }
#endif

    void begin_frame() {
#ifdef RENDERDOC
        if (rdoc_api != nullptr)
            rdoc_api->StartFrameCapture(nullptr, nullptr);
#endif
    }

    void end_frame() {
#ifdef RENDERDOC
        if (rdoc_api != nullptr)
            rdoc_api->EndFrameCapture(nullptr, nullptr);
#endif
    }
}
