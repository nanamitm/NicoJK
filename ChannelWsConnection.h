#pragma once

#include <winhttp.h>
#include <atomic>
#include <string>

// WinHTTP の非同期処理をワーカースレッドで待つ。停止時のハンドル解放も
// 同じスレッドで行い、最後のコールバックまでコンテキストと受信バッファを保持する。
class ChannelWsConnection
{
public:
	ChannelWsConnection() = default;
	~ChannelWsConnection() {
		CloseRequest();
		if (webSocket_) {
			WinHttpCloseHandle(webSocket_);
			WaitForSingleObject(receive_.closed, INFINITE);
		}
		if (connect_) WinHttpCloseHandle(connect_);
		if (session_) WinHttpCloseHandle(session_);
	}
	ChannelWsConnection(const ChannelWsConnection&) = delete;
	ChannelWsConnection& operator=(const ChannelWsConnection&) = delete;

	bool Connect(const std::wstring& url, bool secure, HANDLE quit) {
		if (!requestState_.IsValid() || !receive_.IsValid()) return false;
		URL_COMPONENTS components = {};
		components.dwStructSize = sizeof(components);
		wchar_t host[256] = {}, path[1024] = {}, extra[1024] = {};
		components.lpszHostName = host; components.dwHostNameLength = _countof(host);
		components.lpszUrlPath = path; components.dwUrlPathLength = _countof(path);
		components.lpszExtraInfo = extra; components.dwExtraInfoLength = _countof(extra);
		if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components)) return false;
		session_ = WinHttpOpen(L"NicoJK", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
		if (!session_ || WinHttpSetStatusCallback(session_, StatusCallback,
		    WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) return false;
		connect_ = WinHttpConnect(session_, host, components.nPort, 0);
		std::wstring target = std::wstring(path) + extra;
		request_ = connect_ ? WinHttpOpenRequest(connect_, L"GET", target.c_str(), nullptr,
		    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;
		if (!request_) return false;
		DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&requestState_);
		if (!WinHttpSetOption(request_, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) return false;
		requestHasContext_ = true;
		if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) return false;
		requestState_.Prepare();
		if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, context) ||
		    !requestState_.Wait(quit, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)) return false;
		requestState_.Prepare();
		if (!WinHttpReceiveResponse(request_, nullptr) ||
		    !requestState_.Wait(quit, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)) return false;
		DWORD status = 0, size = sizeof(status);
		if (!WinHttpQueryHeaders(request_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 101) return false;
		webSocket_ = WinHttpWebSocketCompleteUpgrade(request_, reinterpret_cast<DWORD_PTR>(&receive_));
		CloseRequest();
		return webSocket_ != nullptr;
	}

	bool Receive(std::string& data, WINHTTP_WEB_SOCKET_BUFFER_TYPE& type, HANDLE quit) {
		if (WaitForSingleObject(quit, 0) == WAIT_OBJECT_0) return false;
		receive_.Prepare();
		if (WinHttpWebSocketReceive(webSocket_, buffer_, sizeof(buffer_), nullptr, nullptr) != ERROR_SUCCESS ||
		    !receive_.Wait(quit, WINHTTP_CALLBACK_STATUS_READ_COMPLETE)) return false;
		if (receive_.bytes > sizeof(buffer_)) return false;
		data.append(buffer_, receive_.bytes);
		type = receive_.type;
		return true;
	}

private:
	struct State {
		HANDLE complete = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		HANDLE closed = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		std::atomic<DWORD> status {0};
		DWORD bytes = 0;
		WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE;
		~State() { if (complete) CloseHandle(complete); if (closed) CloseHandle(closed); }
		bool IsValid() const { return complete && closed; }
		void Prepare() { ResetEvent(complete); status = 0; }
		bool Wait(HANDLE quit, DWORD expected) {
			HANDLE events[] = {quit, complete};
			return WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1 && status == expected;
		}
	};
	static void CALLBACK StatusCallback(HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD length) {
		State* state = reinterpret_cast<State*>(context);
		if (!state) return;
		if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
			SetEvent(state->closed);
		} else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
		           status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
		           status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE ||
		           status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
			if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE && length == sizeof(WINHTTP_WEB_SOCKET_STATUS)) {
				const auto* result = static_cast<const WINHTTP_WEB_SOCKET_STATUS*>(info);
				state->bytes = result->dwBytesTransferred;
				state->type = result->eBufferType;
			}
			state->status = status;
			SetEvent(state->complete);
		}
	}
	void CloseRequest() {
		if (request_) {
			WinHttpCloseHandle(request_);
			if (requestHasContext_) WaitForSingleObject(requestState_.closed, INFINITE);
			request_ = nullptr;
		}
	}
	char buffer_[4096];
	State requestState_, receive_;
	HINTERNET session_ = nullptr, connect_ = nullptr, request_ = nullptr, webSocket_ = nullptr;
	bool requestHasContext_ = false;
};
