// android_bridge.cpp - JNI entry points the Android app (com.shiinobu.basara) calls into libmain.so.
#if defined(__ANDROID__)

#include <aurora/aurora.h>

#include "cheats.h"
#include "display_settings.h"
#include "hle_stubs.h"
#include "host_suspend.h"
#include "graphics_info.h"
#include "runtime_config.h"
#include "settings_overlay.h"
#include "system_stats.h"
#include "thermal.h"
#include "virtual_remote.h"

#include "netplay/net_socket.h"
#include "netplay_session.h"
#include "netplay_start.h"
#include "netplay_ui.h"

#include <SDL3/SDL_system.h>
#include <jni.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace AndroidBridge {

// Network play: ask the activity to start the game again in a fresh process (GameActivity
// .restartForSession), then park this thread until Android ends the process.
bool RestartGame() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) {
        return false;
    }
    jclass type = env->GetObjectClass(activity);
    jmethodID restart = type != nullptr ? env->GetMethodID(type, "restartForSession", "()V") : nullptr;
    if (restart == nullptr) {
        env->ExceptionClear();
        return false;
    }
    env->CallVoidMethod(activity, restart);
    env->DeleteLocalRef(activity);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace AndroidBridge

extern "C" {

// GameActivity.nativeSetTouchState: the on-screen remote (VirtualRemote::TouchButton bits) and the
// phone's motion (shake, tilt -1..1) for player 1. Called from the UI thread on every change.
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeSetTouchState(
    JNIEnv*, jclass, jint buttons, jboolean shake, jfloat tilt)
{
    VirtualRemote::SetTouchState(static_cast<uint32_t>(buttons), shake == JNI_TRUE, tilt);
}

// GameActivity.nativeSetTouchSticks: the on-screen sticks, -1..1 with +y up.
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeSetTouchSticks(
    JNIEnv*, jclass, jfloat leftX, jfloat leftY, jfloat rightX, jfloat rightY)
{
    VirtualRemote::SetTouchSticks(leftX, leftY, rightX, rightY);
}

// GameActivity.nativeApplyDisplaySettings: the Display menu. The aspect (0 original 4:3, 1 16:9,
// 2 fill the screen) and the render scale (480 lines per 1x) apply at the next frame; 4:3 versus a
// widescreen mode only at the next start, since the game reads it at boot.
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeApplyDisplaySettings(
    JNIEnv*, jclass, jint aspect, jfloat renderScale, jboolean showFps, jboolean dynamicResolution,
    jint overlayItems, jboolean frameGraph, jboolean thermalControl, jint textureFilter)
{
    aurora_set_texture_filter(static_cast<int>(textureFilter));
    Thermal::SetEnabled(thermalControl == JNI_TRUE);
    settings_overlay::SetOverlayItems(static_cast<uint32_t>(overlayItems));
    settings_overlay::SetFrameGraph(frameGraph == JNI_TRUE);
    // Through the settings overlay, so the game's own settings window shows the same choice. A scale
    // of 0 is "match the screen".
    settings_overlay::SetAspect(aspect < 0 || aspect > 2 ? DisplaySettings::kAspectFill : static_cast<int32_t>(aspect));
    settings_overlay::SetRenderScale(renderScale >= 0.0f ? renderScale : 1.0f);
    settings_overlay::SetShowFps(showFps == JNI_TRUE);
    DisplaySettings::SetDynamicResolution(dynamicResolution == JNI_TRUE);
}

// GameActivity.nativeApplyLiveSettings: settings the in-game menu changes while playing. Upscaler
// 0 off, 1 FSR Light, 2 FSR Full, 3 SGSR; frameskip 0 off, 1 auto, 2 30 FPS lock.
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeApplyLiveSettings(
    JNIEnv*, jclass, jint upscaler, jint frameskip)
{
    if (upscaler >= 0 && upscaler <= 3 && aurora_get_fsr() != upscaler) {
        aurora_set_fsr(static_cast<int>(upscaler));
        RuntimeConfigFile::SetFsr(aurora_get_fsr());
    }
    BasaraFrameskip_SetMode(static_cast<int>(frameskip));
}

// GameActivity.nativeApplyCheats: Settings > Cheats (cheats.h). A Once request applies at the next frame.
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeApplyCheats(
    JNIEnv*, jclass, jint money, jint amount, jint onceSeq, jboolean infiniteHp, jboolean infiniteBasara)
{
    Cheats::SetFromLauncher(static_cast<int32_t>(money), static_cast<uint32_t>(amount < 0 ? 0 : amount),
                            static_cast<uint32_t>(onceSeq), infiniteHp == JNI_TRUE, infiniteBasara == JNI_TRUE);
}

// GameActivity.nativeSetCameraDistance: Settings > Display > Camera Distance (cheats.cpp).
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeSetCameraDistance(JNIEnv*, jclass, jfloat distance)
{
    Cheats::SetCameraDistance(static_cast<float>(distance));
}

JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeSetBattery(JNIEnv*, jclass, jint percent,
                                                                            jfloat tempC)
{
    SetHostBattery(percent, tempC);
}

// GameActivity.nativeGraphicsInfo: the GPU the game is drawing with and whether the GPU
// compatibility fixes are on, as "<name>\n<1|0>"; empty until the renderer has started.
JNIEXPORT jstring JNICALL Java_com_shiinobu_basara_GameActivity_nativeGraphicsInfo(JNIEnv* env, jclass)
{
    const std::string name = GraphicsInfo::Name();
    if (name.empty()) {
        return env->NewStringUTF("");
    }
    const std::string info = name + "\n" + (GraphicsInfo::CompatActive() ? "1" : "0");
    return env->NewStringUTF(info.c_str());
}

// GameActivity.nativeSetDeviceName: what this phone is called in room lists.
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeSetDeviceName(JNIEnv* env, jclass, jstring name)
{
    if (name == nullptr) {
        return;
    }
    const char* utf8 = env->GetStringUTFChars(name, nullptr);
    if (utf8 != nullptr) {
        Netplay::SetDeviceName(utf8);
        env->ReleaseStringUTFChars(name, utf8);
    }
}

// GameActivity.nativeSessionActive: whether this run is a LAN session (the menu offers to leave it).
JNIEXPORT jboolean JNICALL Java_com_shiinobu_basara_GameActivity_nativeSessionActive(JNIEnv*, jclass)
{
    return NetplaySession::Active() ? JNI_TRUE : JNI_FALSE;
}

// GameActivity.nativePrepareToLeave: the menu's "Leave LAN game", before the app restarts the game.
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativePrepareToLeave(JNIEnv*, jclass)
{
    NetplayStart::PrepareToLeave();
}

// GameActivity.nativeTap: a tap on the game's menus, as fractions of the screen (netplay_ui.h).
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativePointer(JNIEnv*, jclass, jfloat x, jfloat y,
                                                                      jboolean held)
{
    NetplayUi::Pointer(x, y, held != JNI_FALSE);
}

JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeTap(JNIEnv*, jclass, jfloat x, jfloat y)
{
    NetplayUi::Tap(x, y);
}

// GameActivity.nativeTakeSettings: {render scale, show FPS 1/0, aspect, spinning coins 1/0, frame graph 1/0,
// V-Sync 1/0} when the game's settings
// window changed them since the last call, else null; the app saves them for the next start.
JNIEXPORT jfloatArray JNICALL Java_com_shiinobu_basara_GameActivity_nativeTakeSettings(JNIEnv* env, jclass)
{
    float scale = 1.0f;
    bool showFps = false;
    int aspect = DisplaySettings::kAspectFill;
    bool spinningCoins = true;
    bool frameGraph = false;
    bool vsync = false;
    if (!NetplayUi::TakeSettingsChange(scale, showFps, aspect, spinningCoins, frameGraph, vsync)) {
        return nullptr;
    }
    jfloatArray result = env->NewFloatArray(6);
    const jfloat values[6] = {scale, showFps ? 1.0f : 0.0f, static_cast<jfloat>(aspect), spinningCoins ? 1.0f : 0.0f,
                              frameGraph ? 1.0f : 0.0f, vsync ? 1.0f : 0.0f};
    env->SetFloatArrayRegion(result, 0, 6, values);
    return result;
}

// GameActivity.nativeTapScreen: whether a menu that taps drive is up (the app hides its remote).
JNIEXPORT jboolean JNICALL Java_com_shiinobu_basara_GameActivity_nativeTapScreen(JNIEnv*, jclass)
{
    return NetplayUi::TapScreenUp() ? JNI_TRUE : JNI_FALSE;
}

// GameActivity.nativeSetBackground: the app left the screen (true) or came back (false). Leaving, the
// game writes a backup Quick Save and pauses (host_suspend.h); nativeGamePaused says when it has.
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeSetBackground(JNIEnv*, jclass, jboolean background)
{
    if (background) {
        HostSuspend::RequestPause(true);
    } else {
        HostSuspend::RequestResume();
    }
}

// GameActivity.nativeSetMenuPause: the Back menu is open (true) or closed (false). A pause without the
// backup save, since the app stays on screen.
JNIEXPORT void JNICALL Java_com_shiinobu_basara_GameActivity_nativeSetMenuPause(JNIEnv*, jclass, jboolean paused)
{
    if (paused) {
        HostSuspend::RequestPause(false);
    } else {
        HostSuspend::RequestResume();
    }
}

JNIEXPORT jboolean JNICALL Java_com_shiinobu_basara_GameActivity_nativeGamePaused(JNIEnv*, jclass)
{
    return HostSuspend::Paused() ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"

#endif // __ANDROID__
