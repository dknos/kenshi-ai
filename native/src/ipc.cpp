/**
 * ipc.cpp — Async WinHTTP client for sidecar communication.
 *
 * All public functions spawn a detached thread and return immediately.
 * Callbacks fire on the background thread; callers push results to a
 * mutex-guarded queue and drain on the game main thread.
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include "kenshi_ai.h"

#include <thread>
#include <string>

namespace KenshiAI
{
    struct HttpParams
    {
        std::string requestJson;
        std::string path;          // e.g. "/chat" or "/d2d"
        ResponseCallback cb;
        std::string serverUrl;
        int timeoutMs;
    };

    static void HttpThread(HttpParams p)
    {
        HINTERNET hSession = WinHttpOpen(
            L"kenshi-ai/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );
        if (!hSession) return;

        HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 9392, 0);
        std::string responseBody;

        if (hConnect)
        {
            std::wstring wpath(p.path.begin(), p.path.end());
            HINTERNET hRequest = WinHttpOpenRequest(
                hConnect, L"POST", wpath.c_str(),
                nullptr, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES, 0
            );

            if (hRequest)
            {
                WinHttpSetTimeouts(hRequest,
                    p.timeoutMs, p.timeoutMs, p.timeoutMs, p.timeoutMs);

                LPCWSTR headers = L"Content-Type: application/json\r\n";
                BOOL sent = WinHttpSendRequest(
                    hRequest, headers, (DWORD)-1L,
                    (LPVOID)p.requestJson.data(),
                    (DWORD)p.requestJson.size(),
                    (DWORD)p.requestJson.size(), 0
                );

                if (sent && WinHttpReceiveResponse(hRequest, nullptr))
                {
                    DWORD avail = 0;
                    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail)
                    {
                        std::string chunk(avail, '\0');
                        DWORD read = 0;
                        WinHttpReadData(hRequest, chunk.data(), avail, &read);
                        responseBody.append(chunk.data(), read);
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);

        if (!responseBody.empty())
            p.cb(responseBody);
    }

    static void PostRequest(const std::string& path,
                             const std::string& requestJson,
                             ResponseCallback cb)
    {
        HttpParams p;
        p.path        = path;
        p.requestJson = requestJson;
        p.cb          = std::move(cb);
        p.serverUrl   = g_config.serverUrl;
        p.timeoutMs   = g_config.timeoutMs;
        std::thread(HttpThread, std::move(p)).detach();
    }

    void PostChat(const std::string& requestJson, ResponseCallback cb)
    {
        PostRequest("/chat", requestJson, std::move(cb));
    }

    void PostD2D(const std::string& requestJson, ResponseCallback cb)
    {
        PostRequest("/d2d", requestJson, std::move(cb));
    }
}
