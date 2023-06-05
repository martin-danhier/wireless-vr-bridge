#pragma once

#define WVB_APP_NAME      "Wireless VR Bridge"
#define WVB_ENGINE_NAME   "WVB Engine"
#define WVB_VERSION_MAJOR 0
#define WVB_VERSION_MINOR 1
#define WVB_VERSION_PATCH 0

#define WVB_MAKE_VERSION_32(major, minor, patch) (((major & 0xFF) << 24) | ((minor & 0xFF) << 16) | (patch & 0xFFFF))
#define WVB_MAKE_VERSION_8(major, minor)         (((major & 0xF) << 4) | (minor & 0xF))
#define WVB_VERSION_32                           WVB_MAKE_VERSION_32(WVB_VERSION_MAJOR, WVB_VERSION_MINOR, WVB_VERSION_PATCH)
#define WVB_VERSION_8                            WVB_MAKE_VERSION_8(WVB_VERSION_MAJOR, WVB_VERSION_MINOR)

namespace wvb
{

}