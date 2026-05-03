/*
 * MlcWrapper.cpp -- see MlcWrapper.h for design notes.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "MlcWrapper.h"

#include <dlfcn.h>
#include <string>

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
        m_pLiInitializeStreamConfiguration =
            reinterpret_cast<decltype(m_pLiInitializeStreamConfiguration)>(
                resolveOrThrow("LiInitializeStreamConfiguration"));
        m_pLiInitializeVideoCallbacks =
            reinterpret_cast<decltype(m_pLiInitializeVideoCallbacks)>(
                resolveOrThrow("LiInitializeVideoCallbacks"));
        m_pLiInitializeAudioCallbacks =
            reinterpret_cast<decltype(m_pLiInitializeAudioCallbacks)>(
                resolveOrThrow("LiInitializeAudioCallbacks"));
        m_pLiInitializeConnectionCallbacks =
            reinterpret_cast<decltype(m_pLiInitializeConnectionCallbacks)>(
                resolveOrThrow("LiInitializeConnectionCallbacks"));
        m_pLiStartConnection =
            reinterpret_cast<decltype(m_pLiStartConnection)>(
                resolveOrThrow("LiStartConnection"));
        m_pLiStopConnection =
            reinterpret_cast<decltype(m_pLiStopConnection)>(
                resolveOrThrow("LiStopConnection"));
        m_pLiInterruptConnection =
            reinterpret_cast<decltype(m_pLiInterruptConnection)>(
                resolveOrThrow("LiInterruptConnection"));
        m_pLiTestClientConnectivity =
            reinterpret_cast<decltype(m_pLiTestClientConnectivity)>(
                resolveOrThrow("LiTestClientConnectivity"));
        m_pLiGetStageName =
            reinterpret_cast<decltype(m_pLiGetStageName)>(
                resolveOrThrow("LiGetStageName"));
        m_pLiGetPortFlagsFromStage =
            reinterpret_cast<decltype(m_pLiGetPortFlagsFromStage)>(
                resolveOrThrow("LiGetPortFlagsFromStage"));
        m_pLiGetPortFlagsFromTerminationErrorCode =
            reinterpret_cast<decltype(m_pLiGetPortFlagsFromTerminationErrorCode)>(
                resolveOrThrow("LiGetPortFlagsFromTerminationErrorCode"));
        m_pLiStringifyPortFlags =
            reinterpret_cast<decltype(m_pLiStringifyPortFlags)>(
                resolveOrThrow("LiStringifyPortFlags"));
        m_pLiGetMicroseconds =
            reinterpret_cast<decltype(m_pLiGetMicroseconds)>(
                resolveOrThrow("LiGetMicroseconds"));
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

void MlcWrapper::initializeStreamConfiguration(PSTREAM_CONFIGURATION cfg) const
    { m_pLiInitializeStreamConfiguration(cfg); }
void MlcWrapper::initializeVideoCallbacks(PDECODER_RENDERER_CALLBACKS cb) const
    { m_pLiInitializeVideoCallbacks(cb); }
void MlcWrapper::initializeAudioCallbacks(PAUDIO_RENDERER_CALLBACKS cb) const
    { m_pLiInitializeAudioCallbacks(cb); }
void MlcWrapper::initializeConnectionCallbacks(PCONNECTION_LISTENER_CALLBACKS cb) const
    { m_pLiInitializeConnectionCallbacks(cb); }

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

void MlcWrapper::stopConnection() const           { m_pLiStopConnection(); }
void MlcWrapper::interruptConnection() const      { m_pLiInterruptConnection(); }

int MlcWrapper::testClientConnectivity(const char* targetHostName,
                                       unsigned short referencePort,
                                       unsigned int testFlags) const
{
    return m_pLiTestClientConnectivity(targetHostName, referencePort, testFlags);
}

const char*  MlcWrapper::getStageName(int stage) const
    { return m_pLiGetStageName(stage); }
unsigned int MlcWrapper::getPortFlagsFromStage(int stage) const
    { return m_pLiGetPortFlagsFromStage(stage); }
unsigned int MlcWrapper::getPortFlagsFromTerminationErrorCode(int errorCode) const
    { return m_pLiGetPortFlagsFromTerminationErrorCode(errorCode); }
void MlcWrapper::stringifyPortFlags(unsigned int portFlags, const char* separator,
                                    char* outputBuffer, int outputBufferLength) const
    { m_pLiStringifyPortFlags(portFlags, separator, outputBuffer, outputBufferLength); }

uint64_t MlcWrapper::getMicroseconds() const
    { return m_pLiGetMicroseconds(); }
