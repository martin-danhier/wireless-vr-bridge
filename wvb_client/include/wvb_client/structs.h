#pragma once

#ifdef __ANDROID__
#include <android_native_app_glue.h>
#endif

namespace wvb::client {
    /**
     * Create infos passed by the Android application to the shared client library,
     * allowing the use of Android-specific features.
    */
    struct ApplicationInfo {
#ifdef __ANDROID__
        struct android_app *android_app = nullptr;
#endif
    };
}