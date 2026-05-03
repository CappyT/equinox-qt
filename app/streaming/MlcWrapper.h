/*
 * MlcWrapper -- typed C++ shim around moonlight-common-c when loaded via
 * dlmopen(LM_ID_NEWLM, ...).
 *
 * The Equinox V1 architecture runs two concurrent Moonlight sessions in a
 * single process. Each Session owns its own MlcWrapper instance, which owns
 * an independent dlmopen handle to libmoonlight-common-c.so. ELF namespace
 * isolation gives each handle its own private copy of the library's ~29
 * extern globals plus its own libssl/libcrypto/libc, so two Sessions can
 * call Li* simultaneously without colliding (validated in
 * docs/spikes/dlmopen-feasibility.md and tests/mlc-wrapper/).
 *
 * Coverage: every Li* symbol referenced anywhere under app/ is wrapped
 * (42 symbols at the time of writing). Adding new ones is mechanical:
 *   1. Add the function-pointer member in the // Function pointers block.
 *   2. Add the RESOLVE(NAME) line in MlcWrapper.cpp::load().
 *   3. Add the public method declaration here and its forward in the .cpp.
 *
 * Errors: the constructor throws std::runtime_error on dlmopen or any
 * dlsym failure, leaving no partially-constructed state. The destructor
 * dlcloses the handle (libssl/libcrypto are known not to fully unload
 * because of their global init state, but that is harmless for Equinox
 * where Sessions persist for the run of the app).
 *
 * Thread safety: a single MlcWrapper is intended to be driven by the
 * threads moonlight-common-c spawns internally for that one connection.
 * Two MlcWrapper instances are safe to use from separate threads in
 * parallel because their underlying global state is namespace-isolated.
 */

#pragma once

#include <Limelight.h>
#include <stdexcept>
#include <string>

class MlcWrapper {
public:
    explicit MlcWrapper(const std::string& libPath = "libmoonlight-common-c.so");
    ~MlcWrapper();

    MlcWrapper(const MlcWrapper&)            = delete;
    MlcWrapper& operator=(const MlcWrapper&) = delete;
    MlcWrapper(MlcWrapper&&)                 = delete;
    MlcWrapper& operator=(MlcWrapper&&)      = delete;

    // === Initialization ===
    void initializeStreamConfiguration(PSTREAM_CONFIGURATION cfg) const;
    void initializeVideoCallbacks(PDECODER_RENDERER_CALLBACKS cb) const;
    void initializeAudioCallbacks(PAUDIO_RENDERER_CALLBACKS cb) const;
    void initializeConnectionCallbacks(PCONNECTION_LISTENER_CALLBACKS cb) const;

    // === Connection lifecycle ===
    int  startConnection(PSERVER_INFORMATION serverInfo,
                         PSTREAM_CONFIGURATION streamConfig,
                         PCONNECTION_LISTENER_CALLBACKS clCallbacks,
                         PDECODER_RENDERER_CALLBACKS drCallbacks,
                         PAUDIO_RENDERER_CALLBACKS arCallbacks,
                         void* renderContext, int drFlags,
                         void* audioContext, int arFlags) const;
    void stopConnection() const;
    void interruptConnection() const;

    // === Connectivity / port helpers ===
    int          testClientConnectivity(const char* targetHostName,
                                        unsigned short referencePort,
                                        unsigned int testFlags) const;
    const char*  getStageName(int stage) const;
    unsigned int getPortFlagsFromStage(int stage) const;
    unsigned int getPortFlagsFromTerminationErrorCode(int errorCode) const;
    void         stringifyPortFlags(unsigned int portFlags, const char* separator,
                                    char* outputBuffer, int outputBufferLength) const;

    // === Time ===
    uint64_t getMicroseconds() const;

    // === Input -- keyboard ===
    int sendKeyboardEvent(short keyCode, char keyAction, char modifiers) const;
    int sendKeyboardEvent2(short keyCode, char keyAction, char modifiers, char flags) const;

    // === Input -- mouse ===
    int sendMouseButtonEvent(char action, int button) const;
    int sendMouseMoveEvent(short deltaX, short deltaY) const;
    int sendMousePositionEvent(short x, short y, short referenceWidth, short referenceHeight) const;
    int sendScrollEvent(signed char scrollClicks) const;
    int sendHighResScrollEvent(short scrollAmount) const;
    int sendHScrollEvent(signed char scrollClicks) const;
    int sendHighResHScrollEvent(short scrollAmount) const;

    // === Input -- touch / pen ===
    int sendTouchEvent(uint8_t eventType, uint32_t pointerId, float x, float y,
                       float pressureOrDistance, float contactAreaMajor,
                       float contactAreaMinor, uint16_t rotation) const;
    int sendPenEvent(uint8_t eventType, uint8_t toolType, uint8_t penButtons,
                     float x, float y, float pressureOrDistance,
                     float contactAreaMajor, float contactAreaMinor,
                     uint16_t rotation, uint8_t tilt) const;

    // === Input -- controller ===
    int sendMultiControllerEvent(short controllerNumber, short activeGamepadMask,
                                 int buttonFlags, unsigned char leftTrigger,
                                 unsigned char rightTrigger, short leftStickX,
                                 short leftStickY, short rightStickX,
                                 short rightStickY) const;
    int sendControllerArrivalEvent(uint8_t controllerNumber, uint16_t activeGamepadMask,
                                   uint8_t type, uint32_t supportedButtonFlags,
                                   uint16_t capabilities) const;
    int sendControllerBatteryEvent(uint8_t controllerNumber, uint8_t batteryState,
                                   uint8_t batteryPercentage) const;
    int sendControllerMotionEvent(uint8_t controllerNumber, uint8_t motionType,
                                  float x, float y, float z) const;
    int sendControllerTouchEvent(uint8_t controllerNumber, uint8_t eventType,
                                 uint32_t pointerId, float x, float y, float pressure) const;

    // === Input -- text ===
    int sendUtf8TextEvent(const char* text, unsigned int length) const;

    // === Video frame management ===
    bool waitForNextVideoFrame(VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit) const;
    bool pollNextVideoFrame(VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit) const;
    void completeVideoFrame(VIDEO_FRAME_HANDLE handle, int drStatus) const;
    void wakeWaitForVideoFrame() const;
    void requestIdrFrame() const;

    // === Stats / info ===
    bool         getEstimatedRttInfo(uint32_t* estimatedRtt, uint32_t* estimatedRttVariance) const;
    bool         getHdrMetadata(PSS_HDR_METADATA metadata) const;
    bool         getCurrentHostDisplayHdrMode() const;
    uint32_t     getHostFeatureFlags() const;
    int          findExternalAddressIP4(const char* stunServer, unsigned short stunPort,
                                        unsigned int* wanAddr) const;
    int          getPendingAudioDuration() const;
    int          getPendingAudioFrames() const;

    // === Misc ===
    const char*  getLaunchUrlQueryParameters() const;

    // Diagnostic accessor for tests / logs. Returns the dlmopen handle so test
    // code can verify ELF-namespace isolation by comparing addresses of extern
    // globals between two MlcWrapper instances.
    void* rawHandle() const noexcept { return m_handle; }

private:
    void  load(const std::string& libPath);
    void* resolveOrThrow(const char* symbol) const;

    void* m_handle;

    // Function pointers -- keep in the same order as the public methods above.
    // Naming convention: m_p + symbol name.

    // Init
    void     (*m_pLiInitializeStreamConfiguration)(PSTREAM_CONFIGURATION);
    void     (*m_pLiInitializeVideoCallbacks)(PDECODER_RENDERER_CALLBACKS);
    void     (*m_pLiInitializeAudioCallbacks)(PAUDIO_RENDERER_CALLBACKS);
    void     (*m_pLiInitializeConnectionCallbacks)(PCONNECTION_LISTENER_CALLBACKS);

    // Lifecycle
    int      (*m_pLiStartConnection)(PSERVER_INFORMATION, PSTREAM_CONFIGURATION,
                                     PCONNECTION_LISTENER_CALLBACKS,
                                     PDECODER_RENDERER_CALLBACKS,
                                     PAUDIO_RENDERER_CALLBACKS,
                                     void*, int, void*, int);
    void     (*m_pLiStopConnection)(void);
    void     (*m_pLiInterruptConnection)(void);

    // Connectivity / port helpers
    int          (*m_pLiTestClientConnectivity)(const char*, unsigned short, unsigned int);
    const char*  (*m_pLiGetStageName)(int);
    unsigned int (*m_pLiGetPortFlagsFromStage)(int);
    unsigned int (*m_pLiGetPortFlagsFromTerminationErrorCode)(int);
    void         (*m_pLiStringifyPortFlags)(unsigned int, const char*, char*, int);

    // Time
    uint64_t (*m_pLiGetMicroseconds)(void);

    // Input -- keyboard
    int (*m_pLiSendKeyboardEvent)(short, char, char);
    int (*m_pLiSendKeyboardEvent2)(short, char, char, char);

    // Input -- mouse
    int (*m_pLiSendMouseButtonEvent)(char, int);
    int (*m_pLiSendMouseMoveEvent)(short, short);
    int (*m_pLiSendMousePositionEvent)(short, short, short, short);
    int (*m_pLiSendScrollEvent)(signed char);
    int (*m_pLiSendHighResScrollEvent)(short);
    int (*m_pLiSendHScrollEvent)(signed char);
    int (*m_pLiSendHighResHScrollEvent)(short);

    // Input -- touch / pen
    int (*m_pLiSendTouchEvent)(uint8_t, uint32_t, float, float, float, float, float, uint16_t);
    int (*m_pLiSendPenEvent)(uint8_t, uint8_t, uint8_t, float, float, float, float, float, uint16_t, uint8_t);

    // Input -- controller
    int (*m_pLiSendMultiControllerEvent)(short, short, int, unsigned char, unsigned char,
                                         short, short, short, short);
    int (*m_pLiSendControllerArrivalEvent)(uint8_t, uint16_t, uint8_t, uint32_t, uint16_t);
    int (*m_pLiSendControllerBatteryEvent)(uint8_t, uint8_t, uint8_t);
    int (*m_pLiSendControllerMotionEvent)(uint8_t, uint8_t, float, float, float);
    int (*m_pLiSendControllerTouchEvent)(uint8_t, uint8_t, uint32_t, float, float, float);

    // Input -- text
    int (*m_pLiSendUtf8TextEvent)(const char*, unsigned int);

    // Video frame management
    bool (*m_pLiWaitForNextVideoFrame)(VIDEO_FRAME_HANDLE*, PDECODE_UNIT*);
    bool (*m_pLiPollNextVideoFrame)(VIDEO_FRAME_HANDLE*, PDECODE_UNIT*);
    void (*m_pLiCompleteVideoFrame)(VIDEO_FRAME_HANDLE, int);
    void (*m_pLiWakeWaitForVideoFrame)(void);
    void (*m_pLiRequestIdrFrame)(void);

    // Stats / info
    bool         (*m_pLiGetEstimatedRttInfo)(uint32_t*, uint32_t*);
    bool         (*m_pLiGetHdrMetadata)(PSS_HDR_METADATA);
    bool         (*m_pLiGetCurrentHostDisplayHdrMode)(void);
    uint32_t     (*m_pLiGetHostFeatureFlags)(void);
    int          (*m_pLiFindExternalAddressIP4)(const char*, unsigned short, unsigned int*);
    int          (*m_pLiGetPendingAudioDuration)(void);
    int          (*m_pLiGetPendingAudioFrames)(void);

    // Misc
    const char*  (*m_pLiGetLaunchUrlQueryParameters)(void);
};
