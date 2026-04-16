/**
 * ipc.cpp — Async WinHTTP client for sidecar communication.
 *
 * PostChat(json, callback) spawns a detached std::thread that:
 *   1. Opens a WinHTTP session
 *   2. POSTs to 127.0.0.1:9392/chat
 *   3. Reads the full response body
 *   4. Calls callback(responseBody)
 *
 * The callback is invoked on the background thread; hooks.cpp pushes the
 * result onto a mutex-guarded queue and drains it on the main thread.
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include "kenshi_ai.h"

#include <thread>
#include <string>
#include <sstream>

namespace KenshiAI
{
    static void HttpThread(std::string requestJson, ResponseCallback cb,
                            std::string serverUrl, int timeoutMs)
    {
        // Parse host/port/path from serverUrl (assumed http://host:port)
        // For our default "http://127.0.0.1:9392" this is trivial.
        HINTERNET hSession = WinHttpOpen(
            L"kenshi-ai/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );
        if (!hSession) return;

        HINTERNET hConnect = WinHttpConnect(
            hSession,
            L"127.0.0.1",
            9392,
            0
        );

        HINTERNET hRequest = nullptr;
        std::string responseBody;

        if (hConnect)
        {
            hRequest = WinHttpOpenRequest(
                hConnect,
                L"POST",
                L"/chat",
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                0
            );
        }

        if (hRequest)
        {
            WinHttpSetTimeouts(hRequest, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

            LPCWSTR headers = L"Content-Type: application/json\r\n";
            BOOL sent = WinHttpSendRequest(
                hRequest,
                headers,
                (DWORD)-1L,
                (LPVOID)requestJson.data(),
                (DWORD)requestJson.size(),
                (DWORD)requestJson.size(),
                0
            );

            if (sent && WinHttpReceiveResponse(hRequest, nullptr))
            {
                DWORD bytesAvail = 0;
                while (WinHttpQueryDataAvailable(hRequest, &bytesAvail) && bytesAvail)
                {
                    std::string chunk(bytesAvail, '\0');
                    DWORD bytesRead = 0;
                    WinHttpReadData(hRequest, chunk.data(), bytesAvail, &bytesRead);
                    responseBody.append(chunk.data(), bytesRead);
                }
            }

            WinHttpCloseHandle(hRequest);
        }

        if (hConnect) WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (!responseBody.empty())
            cb(responseBody);
    }

    void PostChat(const std::string& requestJson, ResponseCallback cb)
    {
        std::string url  = g_config.serverUrl;
        int         toms = g_config.timeoutMs;
        std::thread(HttpThread, requestJson, cb, url, toms).detach();
    }
}
