
#include <wvb_client/client.h>
#include <wvb_common/macros.h>

#include <cstdint>
#include <cstdlib>
#include <sys/socket.h>
#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/in.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <thread>
#include <unistd.h>

#define XR_USE_PLATFORM_ANDROID 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <wvb_common/module.h>
#include <sys/prctl.h>


void app_handle_cmd(struct android_app *android_app, int32_t cmd)
{
    auto *client = static_cast<wvb::client::Client *>(android_app->userData);

    if (cmd == APP_CMD_INIT_WINDOW)
    {
        wvb::client::ApplicationInfo info {
            .android_app = android_app,
        };
        client->init(info);
    }
    else if (cmd == APP_CMD_TERM_WINDOW)
    {
//        client->shutdown();
    }
}

void requestPermission(JNIEnv* env, jobject activityObject, const char* permissionName, int requestCode) {
    jclass activityClass = env->GetObjectClass(activityObject);
    jmethodID requestPermissionsMethod = env->GetMethodID(activityClass, "requestPermissions", "([Ljava/lang/String;I)V");

    jobjectArray permissionArray = env->NewObjectArray(1, env->FindClass("java/lang/String"), nullptr);
    jstring permissionString = env->NewStringUTF(permissionName);
    env->SetObjectArrayElement(permissionArray, 0, permissionString);

    env->CallVoidMethod(activityObject, requestPermissionsMethod, permissionArray, requestCode);
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(permissionArray);
    env->DeleteLocalRef(permissionString);
}

class OutputRedirector {
private:
    volatile bool m_running = false;
    std::thread m_thread;
    bool m_is_stdout = true;

    void redirect_thread()
    {
        // https://stackoverflow.com/questions/9192749/capturing-stdout-stderr-with-ndk
        int pipes[2]{};
        auto ret = pipe(pipes);
        if (ret != 0) {
            LOGE("Failed to create pipe");
            return;
        }
        ret = dup2(pipes[1], m_is_stdout ? STDOUT_FILENO : STDERR_FILENO);
        if (errno != 0) {
            LOGE("Failed to dup2");
            return;
        }
        ret = close (pipes[1]);
        if (ret != 0) {
            LOGE("Failed to close");
            return;
        }
        FILE *inputFile = fdopen(pipes[0], "r");
        if (inputFile == nullptr) {
            LOGE("Failed to fdopen");
            return;
        }
        char readBuffer[256] {};
        m_running = true;
        while (m_running) {
            fgets(readBuffer, sizeof(readBuffer), inputFile);
            __android_log_write(m_is_stdout ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR , m_is_stdout ? "stdout" : "stderr", readBuffer);


        }
    }

public:
    explicit OutputRedirector(bool is_stdout): m_is_stdout(is_stdout) {
        m_thread = std::thread(&OutputRedirector::redirect_thread, this);
    }

    ~OutputRedirector() {
        m_running = false;
        m_thread.join();
    }
};




void android_main(struct android_app *app)
{
    LOG("-------------- Starting WVB Client --------------");

    JNIEnv *env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);
    prctl(PR_SET_NAME, "wvb_client", 0, 0, 0);

    // Create pipes for stdout and stderr
    OutputRedirector stdout_redirector(true);
    OutputRedirector stderr_redirector(false);

    wvb::IO io(app->activity->assetManager);


    // Init OpenXR loader
    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *) &xrInitializeLoaderKHR);
    if (xrInitializeLoaderKHR != nullptr)
    {
        XrLoaderInitInfoAndroidKHR loaderInitializeInfoAndroid;
        memset(&loaderInitializeInfoAndroid, 0, sizeof(loaderInitializeInfoAndroid));
        loaderInitializeInfoAndroid.type               = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
        loaderInitializeInfoAndroid.next               = nullptr;
        loaderInitializeInfoAndroid.applicationVM      = app->activity->vm;
        loaderInitializeInfoAndroid.applicationContext = app->activity->clazz;
        xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR *) &loaderInitializeInfoAndroid);
    }

#ifdef TEST_MODE

    LOG("--- Launching tests ---");

    const auto modules = wvb::load_modules();
    for (const auto &module : modules) {
        LOG("Got module \"%s\"\n", module.name.c_str());
        if (module.test_function != nullptr) {
            LOG("Running tests for module \"%s\"\n", module.name.c_str());
            module.test_function(io);
        }
    }

    LOG("--- Tests complete ---");

#else
    wvb::client::Client client;
    app->userData = &client;
    app->onAppCmd = app_handle_cmd;

    uint64_t frame_count = 0;

    // Main loop
    while (app->destroyRequested == 0)
    {
        frame_count++;

        // Poll android events
        {
            int                         ident;
            int                         events;
            struct android_poll_source *source;

            while ((ident = ALooper_pollAll(0, nullptr, &events, (void **) &source)) >= 0)
            {
                if (source != nullptr)
                {
                    source->process(app, source);
                }
            }
        }

        client.update();
    }


    LOG("-------------- Exiting WVB Client --------------");

    client.shutdown();
#endif
    (*app->activity->vm).DetachCurrentThread();
}