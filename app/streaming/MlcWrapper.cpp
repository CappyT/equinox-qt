/*
 * MlcWrapper.cpp -- see MlcWrapper.h for design notes.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "MlcWrapper.h"

#include <dlfcn.h>
#include <string>

// Resolve `m_p<NAME>` from the dlmopen handle and reinterpret_cast to its declared
// pointer type. Throws via resolveOrThrow on dlsym failure.
#define RESOLVE(NAME) \
    m_p##NAME = reinterpret_cast<decltype(m_p##NAME)>(resolveOrThrow(#NAME))

MlcWrapper::MlcWrapper(const std::string& libPath)
    : m_handle(nullptr)
{
    load(libPath);
}

MlcWrapper::~MlcWrapper()
{
    if (m_handle) {
        dlclose(m_handle);
        m_handle = nullptr;
    }
}

void MlcWrapper::load(const std::string& libPath)
{
    // LM_ID_NEWLM gives this load its own ELF namespace. All transitive
    // dependencies (libc, libssl, libcrypto) are also loaded fresh into the
    // namespace, so the ~29 extern globals in moonlight-common-c plus the
    // static state inside OpenSSL are isolated from any other MlcWrapper.
    m_handle = dlmopen(LM_ID_NEWLM, libPath.c_str(), RTLD_NOW);
    if (!m_handle) {
        const char* err = dlerror();
        throw std::runtime_error(
            std::string("MlcWrapper: dlmopen('") + libPath + "') failed: " +
            (err ? err : "(no dlerror)"));
    }

    // Resolve every symbol up front; failing later from a lazily-resolved
    // function pointer would be much harder to debug than failing here.
    try {
        // Init
        RESOLVE(LiInitializeStreamConfiguration);
        RESOLVE(LiInitializeVideoCallbacks);
        RESOLVE(LiInitializeAudioCallbacks);
        RESOLVE(LiInitializeConnectionCallbacks);

        // Lifecycle
        RESOLVE(LiStartConnection);
        RESOLVE(LiStopConnection);
        RESOLVE(LiInterruptConnection);

        // Connectivity / port helpers
        RESOLVE(LiTestClientConnectivity);
        RESOLVE(LiGetStageName);
        RESOLVE(LiGetPortFlagsFromStage);
        RESOLVE(LiGetPortFlagsFromTerminationErrorCode);
        RESOLVE(LiStringifyPortFlags);

        // Time
        RESOLVE(LiGetMicroseconds);

        // Input -- keyboard
        RESOLVE(LiSendKeyboardEvent);
        RESOLVE(LiSendKeyboardEvent2);

        // Input -- mouse
        RESOLVE(LiSendMouseButtonEvent);
        RESOLVE(LiSendMouseMoveEvent);
        RESOLVE(LiSendMousePositionEvent);
        RESOLVE(LiSendScrollEvent);
        RESOLVE(LiSendHighResScrollEvent);
        RESOLVE(LiSendHScrollEvent);
        RESOLVE(LiSendHighResHScrollEvent);

        // Input -- touch / pen
        RESOLVE(LiSendTouchEvent);
        RESOLVE(LiSendPenEvent);

        // Input -- controller
        RESOLVE(LiSendMultiControllerEvent);
        RESOLVE(LiSendControllerArrivalEvent);
        RESOLVE(LiSendControllerBatteryEvent);
        RESOLVE(LiSendControllerMotionEvent);
        RESOLVE(LiSendControllerTouchEvent);

        // Input -- text
        RESOLVE(LiSendUtf8TextEvent);

        // Video frame management
        RESOLVE(LiWaitForNextVideoFrame);
        RESOLVE(LiPollNextVideoFrame);
        RESOLVE(LiCompleteVideoFrame);
        RESOLVE(LiWakeWaitForVideoFrame);
        RESOLVE(LiRequestIdrFrame);

        // Stats / info
        RESOLVE(LiGetEstimatedRttInfo);
        RESOLVE(LiGetHdrMetadata);
        RESOLVE(LiGetCurrentHostDisplayHdrMode);
        RESOLVE(LiGetHostFeatureFlags);
        RESOLVE(LiFindExternalAddressIP4);
        RESOLVE(LiGetPendingAudioDuration);
        RESOLVE(LiGetPendingAudioFrames);

        // Misc
        RESOLVE(LiGetLaunchUrlQueryParameters);
    }
    catch (...) {
        dlclose(m_handle);
        m_handle = nullptr;
        throw;
    }
}

void* MlcWrapper::resolveOrThrow(const char* symbol) const
{
    dlerror(); // clear any prior error so dlerror() below is meaningful
    void* p = dlsym(m_handle, symbol);
    const char* err = dlerror();
    if (err) {
        throw std::runtime_error(
            std::string("MlcWrapper: dlsym('") + symbol + "') failed: " + err);
    }
    return p;
}

// =====================================================================
// Method bodies -- straight forwarders. Order matches the header section
// banners.
// =====================================================================

// --- Init ---
void MlcWrapper::initializeStreamConfiguration(PSTREAM_CONFIGURATION cfg) const
    { m_pLiInitializeStreamConfiguration(cfg); }
void MlcWrapper::initializeVideoCallbacks(PDECODER_RENDERER_CALLBACKS cb) const
    { m_pLiInitializeVideoCallbacks(cb); }
void MlcWrapper::initializeAudioCallbacks(PAUDIO_RENDERER_CALLBACKS cb) const
    { m_pLiInitializeAudioCallbacks(cb); }
void MlcWrapper::initializeConnectionCallbacks(PCONNECTION_LISTENER_CALLBACKS cb) const
    { m_pLiInitializeConnectionCallbacks(cb); }

// --- Lifecycle ---
int MlcWrapper::startConnection(PSERVER_INFORMATION serverInfo,
                                PSTREAM_CONFIGURATION streamConfig,
                                PCONNECTION_LISTENER_CALLBACKS clCallbacks,
                                PDECODER_RENDERER_CALLBACKS drCallbacks,
                                PAUDIO_RENDERER_CALLBACKS arCallbacks,
                                void* renderContext, int drFlags,
                                void* audioContext, int arFlags) const
{
    return m_pLiStartConnection(serverInfo, streamConfig, clCallbacks,
                                drCallbacks, arCallbacks, renderContext,
                                drFlags, audioContext, arFlags);
}
void MlcWrapper::stopConnection() const      { m_pLiStopConnection(); }
void MlcWrapper::interruptConnection() const { m_pLiInterruptConnection(); }

// --- Connectivity / port helpers ---
int MlcWrapper::testClientConnectivity(const char* targetHostName,
                                       unsigned short referencePort,
                                       unsigned int testFlags) const
    { return m_pLiTestClientConnectivity(targetHostName, referencePort, testFlags); }
const char*  MlcWrapper::getStageName(int stage) const
    { return m_pLiGetStageName(stage); }
unsigned int MlcWrapper::getPortFlagsFromStage(int stage) const
    { return m_pLiGetPortFlagsFromStage(stage); }
unsigned int MlcWrapper::getPortFlagsFromTerminationErrorCode(int errorCode) const
    { return m_pLiGetPortFlagsFromTerminationErrorCode(errorCode); }
void MlcWrapper::stringifyPortFlags(unsigned int portFlags, const char* separator,
                                    char* outputBuffer, int outputBufferLength) const
    { m_pLiStringifyPortFlags(portFlags, separator, outputBuffer, outputBufferLength); }

// --- Time ---
uint64_t MlcWrapper::getMicroseconds() const { return m_pLiGetMicroseconds(); }

// --- Input -- keyboard ---
int MlcWrapper::sendKeyboardEvent(short keyCode, char keyAction, char modifiers) const
    { return m_pLiSendKeyboardEvent(keyCode, keyAction, modifiers); }
int MlcWrapper::sendKeyboardEvent2(short keyCode, char keyAction, char modifiers, char flags) const
    { return m_pLiSendKeyboardEvent2(keyCode, keyAction, modifiers, flags); }

// --- Input -- mouse ---
int MlcWrapper::sendMouseButtonEvent(char action, int button) const
    { return m_pLiSendMouseButtonEvent(action, button); }
int MlcWrapper::sendMouseMoveEvent(short deltaX, short deltaY) const
    { return m_pLiSendMouseMoveEvent(deltaX, deltaY); }
int MlcWrapper::sendMousePositionEvent(short x, short y, short referenceWidth, short referenceHeight) const
    { return m_pLiSendMousePositionEvent(x, y, referenceWidth, referenceHeight); }
int MlcWrapper::sendScrollEvent(signed char scrollClicks) const
    { return m_pLiSendScrollEvent(scrollClicks); }
int MlcWrapper::sendHighResScrollEvent(short scrollAmount) const
    { return m_pLiSendHighResScrollEvent(scrollAmount); }
int MlcWrapper::sendHScrollEvent(signed char scrollClicks) const
    { return m_pLiSendHScrollEvent(scrollClicks); }
int MlcWrapper::sendHighResHScrollEvent(short scrollAmount) const
    { return m_pLiSendHighResHScrollEvent(scrollAmount); }

// --- Input -- touch / pen ---
int MlcWrapper::sendTouchEvent(uint8_t eventType, uint32_t pointerId, float x, float y,
                               float pressureOrDistance, float contactAreaMajor,
                               float contactAreaMinor, uint16_t rotation) const
{
    return m_pLiSendTouchEvent(eventType, pointerId, x, y, pressureOrDistance,
                               contactAreaMajor, contactAreaMinor, rotation);
}
int MlcWrapper::sendPenEvent(uint8_t eventType, uint8_t toolType, uint8_t penButtons,
                             float x, float y, float pressureOrDistance,
                             float contactAreaMajor, float contactAreaMinor,
                             uint16_t rotation, uint8_t tilt) const
{
    return m_pLiSendPenEvent(eventType, toolType, penButtons, x, y,
                             pressureOrDistance, contactAreaMajor,
                             contactAreaMinor, rotation, tilt);
}

// --- Input -- controller ---
int MlcWrapper::sendMultiControllerEvent(short controllerNumber, short activeGamepadMask,
                                         int buttonFlags, unsigned char leftTrigger,
                                         unsigned char rightTrigger, short leftStickX,
                                         short leftStickY, short rightStickX,
                                         short rightStickY) const
{
    return m_pLiSendMultiControllerEvent(controllerNumber, activeGamepadMask, buttonFlags,
                                         leftTrigger, rightTrigger, leftStickX, leftStickY,
                                         rightStickX, rightStickY);
}
int MlcWrapper::sendControllerArrivalEvent(uint8_t controllerNumber, uint16_t activeGamepadMask,
                                           uint8_t type, uint32_t supportedButtonFlags,
                                           uint16_t capabilities) const
{
    return m_pLiSendControllerArrivalEvent(controllerNumber, activeGamepadMask, type,
                                           supportedButtonFlags, capabilities);
}
int MlcWrapper::sendControllerBatteryEvent(uint8_t controllerNumber, uint8_t batteryState,
                                           uint8_t batteryPercentage) const
    { return m_pLiSendControllerBatteryEvent(controllerNumber, batteryState, batteryPercentage); }
int MlcWrapper::sendControllerMotionEvent(uint8_t controllerNumber, uint8_t motionType,
                                          float x, float y, float z) const
    { return m_pLiSendControllerMotionEvent(controllerNumber, motionType, x, y, z); }
int MlcWrapper::sendControllerTouchEvent(uint8_t controllerNumber, uint8_t eventType,
                                         uint32_t pointerId, float x, float y, float pressure) const
    { return m_pLiSendControllerTouchEvent(controllerNumber, eventType, pointerId, x, y, pressure); }

// --- Input -- text ---
int MlcWrapper::sendUtf8TextEvent(const char* text, unsigned int length) const
    { return m_pLiSendUtf8TextEvent(text, length); }

// --- Video frame management ---
bool MlcWrapper::waitForNextVideoFrame(VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit) const
    { return m_pLiWaitForNextVideoFrame(frameHandle, decodeUnit); }
bool MlcWrapper::pollNextVideoFrame(VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit) const
    { return m_pLiPollNextVideoFrame(frameHandle, decodeUnit); }
void MlcWrapper::completeVideoFrame(VIDEO_FRAME_HANDLE handle, int drStatus) const
    { m_pLiCompleteVideoFrame(handle, drStatus); }
void MlcWrapper::wakeWaitForVideoFrame() const { m_pLiWakeWaitForVideoFrame(); }
void MlcWrapper::requestIdrFrame() const       { m_pLiRequestIdrFrame(); }

// --- Stats / info ---
bool MlcWrapper::getEstimatedRttInfo(uint32_t* estimatedRtt, uint32_t* estimatedRttVariance) const
    { return m_pLiGetEstimatedRttInfo(estimatedRtt, estimatedRttVariance); }
bool MlcWrapper::getHdrMetadata(PSS_HDR_METADATA metadata) const
    { return m_pLiGetHdrMetadata(metadata); }
bool MlcWrapper::getCurrentHostDisplayHdrMode() const
    { return m_pLiGetCurrentHostDisplayHdrMode(); }
uint32_t MlcWrapper::getHostFeatureFlags() const
    { return m_pLiGetHostFeatureFlags(); }
int MlcWrapper::findExternalAddressIP4(const char* stunServer, unsigned short stunPort,
                                       unsigned int* wanAddr) const
    { return m_pLiFindExternalAddressIP4(stunServer, stunPort, wanAddr); }
int MlcWrapper::getPendingAudioDuration() const { return m_pLiGetPendingAudioDuration(); }
int MlcWrapper::getPendingAudioFrames() const   { return m_pLiGetPendingAudioFrames(); }

// --- Misc ---
const char* MlcWrapper::getLaunchUrlQueryParameters() const
    { return m_pLiGetLaunchUrlQueryParameters(); }
