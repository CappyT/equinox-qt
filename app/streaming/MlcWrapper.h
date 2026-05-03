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
 * Only the subset of Li* symbols actually called by the app is exposed.
 * Add more methods here when Session needs them; the implementation pattern
 * in MlcWrapper.cpp is mechanical.
 *
 * Errors: the constructor throws std::runtime_error on dlmopen or dlsym
 * failure, leaving no partially-constructed state. The destructor dlclose's
 * the handle (whether it actually frees memory depends on the loaded libs;
 * libssl/libcrypto are known not to fully unload, but that is harmless for
 * Equinox's lifetime model where Sessions persist for the run of the app).
 *
 * Thread safety: a single MlcWrapper instance is intended to be driven by
 * the threads moonlight-common-c spawns internally for that one connection.
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

    // Initialization helpers (zero-and-prime the various callback / config structs).
    void initializeStreamConfiguration(PSTREAM_CONFIGURATION cfg) const;
    void initializeVideoCallbacks(PDECODER_RENDERER_CALLBACKS cb) const;
    void initializeAudioCallbacks(PAUDIO_RENDERER_CALLBACKS cb) const;
    void initializeConnectionCallbacks(PCONNECTION_LISTENER_CALLBACKS cb) const;

    // Connection lifecycle.
    int  startConnection(PSERVER_INFORMATION serverInfo,
                         PSTREAM_CONFIGURATION streamConfig,
                         PCONNECTION_LISTENER_CALLBACKS clCallbacks,
                         PDECODER_RENDERER_CALLBACKS drCallbacks,
                         PAUDIO_RENDERER_CALLBACKS arCallbacks,
                         void* renderContext, int drFlags,
                         void* audioContext, int arFlags) const;
    void stopConnection() const;
    void interruptConnection() const;

    // Diagnostic / port test helpers.
    int  testClientConnectivity(const char* targetHostName,
                                unsigned short referencePort,
                                unsigned int testFlags) const;
    const char*  getStageName(int stage) const;
    unsigned int getPortFlagsFromStage(int stage) const;
    unsigned int getPortFlagsFromTerminationErrorCode(int errorCode) const;
    void         stringifyPortFlags(unsigned int portFlags, const char* separator,
                                    char* outputBuffer, int outputBufferLength) const;

    // Utilities.
    uint64_t getMicroseconds() const;

    // Diagnostic accessor for tests / logs. Returns the dlmopen handle so test
    // code can verify ELF-namespace isolation by comparing addresses of extern
    // globals between two MlcWrapper instances.
    void* rawHandle() const noexcept { return m_handle; }

private:
    void  load(const std::string& libPath);
    void* resolveOrThrow(const char* symbol) const;

    void* m_handle;

    // Function pointer table. Naming: `m_p` + symbol name.
    void     (*m_pLiInitializeStreamConfiguration)(PSTREAM_CONFIGURATION);
    void     (*m_pLiInitializeVideoCallbacks)(PDECODER_RENDERER_CALLBACKS);
    void     (*m_pLiInitializeAudioCallbacks)(PAUDIO_RENDERER_CALLBACKS);
    void     (*m_pLiInitializeConnectionCallbacks)(PCONNECTION_LISTENER_CALLBACKS);
    int      (*m_pLiStartConnection)(PSERVER_INFORMATION, PSTREAM_CONFIGURATION,
                                     PCONNECTION_LISTENER_CALLBACKS,
                                     PDECODER_RENDERER_CALLBACKS,
                                     PAUDIO_RENDERER_CALLBACKS,
                                     void*, int, void*, int);
    void     (*m_pLiStopConnection)(void);
    void     (*m_pLiInterruptConnection)(void);
    int      (*m_pLiTestClientConnectivity)(const char*, unsigned short, unsigned int);
    const char* (*m_pLiGetStageName)(int);
    unsigned int (*m_pLiGetPortFlagsFromStage)(int);
    unsigned int (*m_pLiGetPortFlagsFromTerminationErrorCode)(int);
    void     (*m_pLiStringifyPortFlags)(unsigned int, const char*, char*, int);
    uint64_t (*m_pLiGetMicroseconds)(void);
};
