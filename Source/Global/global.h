// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include <httpClient/httpProvider.h>
#include "../HTTP/httpcall.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

namespace log
{
    class logger;
}

typedef struct http_retry_after_api_state
{
    http_retry_after_api_state() : statusCode(0)
    {
    }

    http_retry_after_api_state(
        _In_ const chrono_clock_t::time_point& _retryAfterTime,
        _In_ uint32_t _statusCode
        ) :
        retryAfterTime(_retryAfterTime),
        statusCode(_statusCode)
    {
    }

    chrono_clock_t::time_point retryAfterTime;
    uint32_t statusCode;
} http_retry_after_api_state;

typedef struct http_singleton
{
    http_singleton(IHCPlatformContext* context);
    ~http_singleton();

    std::mutex m_singletonLock;

    std::mutex m_retryAfterCacheLock;
    std::unordered_map<uint32_t, http_retry_after_api_state> m_retryAfterCache;
    void set_retry_state(_In_ uint32_t retryAfterCacheId, _In_ const http_retry_after_api_state& state);
    http_retry_after_api_state get_retry_state(_In_ uint32_t retryAfterCacheId);
    void clear_retry_state(_In_ uint32_t retryAfterCacheId);

    std::mutex m_callRoutedHandlersLock;
    std::atomic<int32_t> m_callRoutedHandlersContext;
    http_internal_unordered_map<int32_t, std::pair<HCCallRoutedHandler, void*>> m_callRoutedHandlers;

    // HTTP state
    std::atomic<std::uint64_t> m_lastId;
    HCCallPerformFunction m_performFunc;
    bool m_retryAllowed;
    uint32_t m_timeoutInSeconds;
    uint32_t m_timeoutWindowInSeconds;
    uint32_t m_retryDelayInSeconds;

    // Platform-specific context for calls
    std::unique_ptr<IHCPlatformContext> m_platformContext;

#if !HC_NOWEBSOCKETS
    // WebSocket state
    HCWebSocketMessageFunction m_websocketMessageFunc;
    HCWebSocketCloseEventFunction m_websocketCloseEventFunc;

    HCWebSocketConnectFunction m_websocketConnectFunc;
    HCWebSocketSendMessageFunction m_websocketSendMessageFunc;
    HCWebSocketDisconnectFunction m_websocketDisconnectFunc;
#endif

    // Mock state
    std::mutex m_mocksLock;
    http_internal_vector<HC_CALL*> m_mocks;
    HC_CALL* m_lastMatchingMock;
    bool m_mocksEnabled;

    std::mutex m_sharedPtrsLock;
    http_internal_unordered_map<void*, std::shared_ptr<void>> m_sharedPtrs;

} http_singleton;


std::shared_ptr<http_singleton> get_http_singleton(bool assertIfNull);
HRESULT init_http_singleton(HCInitArgs* args);
void cleanup_http_singleton();


class shared_ptr_cache
{
public:
    template<typename T>
    static void* store(std::shared_ptr<T> contextSharedPtr)
    {
        auto httpSingleton = get_http_singleton(false);
        if (nullptr == httpSingleton)
            return nullptr;
        std::lock_guard<std::mutex> lock(httpSingleton->m_sharedPtrsLock);

        void *rawVoidPtr = contextSharedPtr.get();
        std::shared_ptr<void> voidSharedPtr(contextSharedPtr, rawVoidPtr);
        httpSingleton->m_sharedPtrs.insert(std::make_pair(rawVoidPtr, voidSharedPtr));
        return rawVoidPtr;
    }

    template<typename T>
    static std::shared_ptr<T> fetch(void *rawContextPtr, bool deleteShared = true, bool assertIfNotFound = true)
    {
        auto httpSingleton = get_http_singleton(false);
        if (nullptr == httpSingleton)
            return nullptr;

        std::lock_guard<std::mutex> lock(httpSingleton->m_sharedPtrsLock);

        auto iter = httpSingleton->m_sharedPtrs.find(rawContextPtr);
        if (iter != httpSingleton->m_sharedPtrs.end())
        {
            auto returnPtr = std::shared_ptr<T>(iter->second, reinterpret_cast<T*>(iter->second.get()));
            if (deleteShared)
            {
                httpSingleton->m_sharedPtrs.erase(iter);
            }
            return returnPtr;
        }
        else
        {
            if (assertIfNotFound)
            {
                ASSERT(false && "Context not found!");
            }
            return std::shared_ptr<T>();
        }
    }

    template<typename T>
    static void remove(void *rawContextPtr)
    {
        auto httpSingleton = get_http_singleton(false);
        if (nullptr == httpSingleton)
            return;

        std::lock_guard<std::mutex> lock(httpSingleton->m_sharedPtrsLock);

        auto iter = httpSingleton->m_sharedPtrs.find(rawContextPtr);
        if (iter != httpSingleton->m_sharedPtrs.end())
        {
            httpSingleton->m_sharedPtrs.erase(iter);
        }
    }

    static void cleanup(_In_ std::shared_ptr<http_singleton> httpSingleton)
    {
        std::lock_guard<std::mutex> lock(httpSingleton->m_sharedPtrsLock);
        ASSERT(httpSingleton->m_sharedPtrs.size() == 0);
        httpSingleton->m_sharedPtrs.clear();
    }

private:
    shared_ptr_cache();
    shared_ptr_cache(const shared_ptr_cache&);
    shared_ptr_cache& operator=(const shared_ptr_cache&);
};

NAMESPACE_XBOX_HTTP_CLIENT_END
