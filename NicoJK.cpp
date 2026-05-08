/*
	NicoJK
		TVTest ニコニコ実況プラグイン
*/

#include "stdafx.h"
#include "ImportLogUtil.h"
#include "Util.h"
#include "JKStream.h"
#include "JKTransfer.h"
#include "LogReader.h"
#include "CommentWindow.h"
#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "TVTestPlugin.h"
#include "resource.h"
#include "NetworkServiceIDTable.h"
#include "JKIDNameTable.h"
#include "NicoJK.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <winhttp.h>
#include <wrl/event.h>
#include <oleidl.h>
#include <ole2.h>
#pragma comment(lib, "winhttp.lib")
using Microsoft::WRL::Callback;

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

// ビジュアルスタイルの有効化
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
// NicoJKから他プラグインに情報提供するメッセージ
const int NICOJK_CURRENT_MSGVER = 1;
const UINT WM_NICOJK_GET_MSGVER = WM_APP + 50;
const UINT WM_NICOJK_GET_JKID = WM_APP + 51;
const UINT WM_NICOJK_GET_JKID_TO_GET = WM_APP + 52;
const UINT WM_NICOJK_OPEN_LOGFILE = WM_APP + 53;
const int NICOJK_OPEN_FLAG_TXT = 0x01000000;
const int NICOJK_OPEN_FLAG_JKL = 0x02000000;
const int NICOJK_OPEN_FLAG_XML = 0x04000000;
const int NICOJK_OPEN_FLAG_RELATIVE = 0x10000000;
const int NICOJK_OPEN_FLAG_ABSOLUTE = 0x20000000;

#if 0 // NicoJKのウィンドウを探す関数(改変流用自由)
BOOL CALLBACK FindNicoJKEnumProc(HWND hwnd, LPARAM lParam)
{
	TCHAR className[32];
	if (GetClassName(hwnd, className, _countof(className)) && !_tcscmp(className, TEXT("ru.jk.force"))) {
		*reinterpret_cast<HWND*>(lParam) = hwnd;
		return FALSE;
	}
	return TRUE;
}

BOOL CALLBACK FindNicoJKTopEnumProc(HWND hwnd, LPARAM lParam)
{
	std::pair<HWND, DWORD> &params = *reinterpret_cast<std::pair<HWND, DWORD>*>(lParam);
	DWORD processID = 0;
	GetWindowThreadProcessId(hwnd, &processID);
	if (processID == params.second && FindNicoJKEnumProc(hwnd, reinterpret_cast<LPARAM>(&params.first))) {
		EnumChildWindows(hwnd, FindNicoJKEnumProc, reinterpret_cast<LPARAM>(&params.first));
	}
	return params.first == nullptr;
}

BOOL CALLBACK FindNicoJKThreadTopEnumProc(HWND hwnd, LPARAM lParam)
{
	if (FindNicoJKEnumProc(hwnd, lParam)) {
		EnumChildWindows(hwnd, FindNicoJKEnumProc, lParam);
	}
	return *reinterpret_cast<HWND*>(lParam) == nullptr;
}

HWND FindNicoJKWindow(DWORD processID)
{
	std::pair<HWND, DWORD> params(nullptr, processID);
	EnumWindows(FindNicoJKTopEnumProc, reinterpret_cast<LPARAM>(&params)); // Call from another process
	return params.first;
	//HWND hwnd = nullptr;
	//EnumThreadWindows(GetCurrentThreadId(), FindNicoJKThreadTopEnumProc, reinterpret_cast<LPARAM>(&hwnd)); // Call from plugin
	//return hwnd;
}
#endif

// 通信用
const UINT WMS_FORCE = WM_APP + 101;
const UINT WMS_JK = WM_APP + 102;
const UINT WMS_TRANSFER = WM_APP + 103;
const UINT WMS_LOGIN = WM_APP + 104;

const UINT WM_RESET_STREAM = WM_APP + 105;
const UINT WM_UPDATE_LIST = WM_APP + 106;
const UINT WM_SET_ZORDER = WM_APP + 107;
const UINT WM_POST_COMMENT = WM_APP + 108;
const UINT WM_TOGGLE_LOG_LIST_NG = WM_APP + 109;
const UINT WM_GET_LOG_LIST_NG_STATE = WM_APP + 110;
const UINT WMS_LOGIN_SETTINGS = WM_APP + 111;
const UINT WMS_CHANNEL_WS    = WM_APP + 113;
const UINT WMS_FORCE_LIST_SEL  = WM_APP + 116;

const UINT ID_FORCE_LIST_COPY = 1;
const UINT ID_FORCE_LIST_TOGGLE_NG = 2;

enum {
	IDC_COMMENT_EDIT = 3001,
	IDC_COMMENT_SEND = 3002,
};

enum {
	IDC_LOGIN_MAIL = 2001,
	IDC_LOGIN_PASSWORD,
	IDC_LOGIN_OTP,
	IDC_LOGIN_STATUS,
	IDC_LOGIN_LAST_LOGIN,
	IDC_LOGIN_BUTTON_START,
	IDC_LOGIN_BUTTON_OTP,
	IDC_LOGIN_BUTTON_CANCEL,
	IDC_LOGIN_LABEL_MAIL,
	IDC_LOGIN_LABEL_PASSWORD,
	IDC_LOGIN_LABEL_OTP,
};

enum {
	TIMER_UPDATE = 1,
	TIMER_JK_WATCHDOG,
	TIMER_FORWARD,
	TIMER_SETUP_CURJK,
	TIMER_OPEN_DROPFILE,
	TIMER_DONE_MOVE,
	TIMER_DONE_SIZE,
	TIMER_DONE_POSCHANGE,
	TIMER_UPDATE_LIST,
};

enum {
	COMMAND_HIDE_FORCE,
	COMMAND_HIDE_COMMENT,
	COMMAND_FORWARD_A,
};

enum {
	LOGIN_STATE_IDLE,
	LOGIN_STATE_SET_MAIL,
	LOGIN_STATE_SET_PASSWORD,
	LOGIN_STATE_LOGIN,
	LOGIN_STATE_WAIT_2FA,
};

} // anonymous namespace

CNicoJKPanelColor::CNicoJKPanelColor()
	: crPanelText_(0)
	, crPanelBack_(0)
	, crPanelCurTabText_(0)
	, crPanelCurTabBack_(0)
	, hbrPanelBack_(nullptr)
	, hbrPanelCurTabBack_(nullptr)
	, bDelaySetColor_(false)
{
}

CNicoJKPanelColor::~CNicoJKPanelColor()
{
	if (hbrPanelBack_) {
		DeleteBrush(hbrPanelBack_);
	}
	if (hbrPanelCurTabBack_) {
		DeleteBrush(hbrPanelCurTabBack_);
	}
}

bool CNicoJKPanelColor::SetColor(TVTest::CTVTestApp *pApp)
{
	if (!pApp) {
		return false;
	}
	crPanelText_ = pApp->GetColor(L"PanelText");
	crPanelBack_ = pApp->GetColor(L"PanelBack");
	crPanelCurTabText_ = pApp->GetColor(L"PanelCurTabText");
	crPanelCurTabBack_ = pApp->GetColor(L"PanelCurTabBack");
	if (hbrPanelBack_) {
		DeleteBrush(hbrPanelBack_);
	}
	hbrPanelBack_ = CreateSolidBrush(crPanelBack_);
	if (hbrPanelCurTabBack_) {
		DeleteBrush(hbrPanelCurTabBack_);
	}
	hbrPanelCurTabBack_ = CreateSolidBrush(crPanelCurTabBack_);
	bDelaySetColor_ = false;
	return hbrPanelBack_ && hbrPanelCurTabBack_;
}

bool CNicoJKPanelColor::DelaySetColor(TVTest::CTVTestApp *pApp)
{
	if (!bDelaySetColor_) {
		return false;
	}
	return SetColor(pApp);
}

bool CNicoJKPanelColor::IsDark() const
{
	return 3 * GetRValue(crPanelBack_) + 6 * GetGValue(crPanelBack_) + GetBValue(crPanelBack_) < 1280;
}

void CNicoJK::RPL_ELEM::SetEnabled(bool b)
{
	if (!pattern.empty()) {
		if (b && TEXT('A') <= pattern[0] && pattern[0] <= TEXT('Z')) {
			pattern[0] = pattern[0] - TEXT('A') + TEXT('a');
		} else if (!b && TEXT('a') <= pattern[0] && pattern[0] <= TEXT('z')) {
			pattern[0] = pattern[0] - TEXT('a') + TEXT('A');
		}
	}
}

bool CNicoJK::RPL_ELEM::SetPattern(LPCTSTR patt)
{
	// 入力パターンはsedコマンド等の形式をまねたもの
	// ただし今のところ's/{regex}/{replace}/g'のみ対応(拡張可能)
	// 先頭文字が大文字の場合はそのパターンが無効状態であることを示す
	static const std::regex reBrace("[Ss](.)(.+?)\\1(.*?)\\1g");
	std::vector<char> utf8(WideCharToMultiByte(CP_UTF8, 0, patt, -1, nullptr, 0, nullptr, nullptr));
	if (utf8.empty() || WideCharToMultiByte(CP_UTF8, 0, patt, -1, utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr) == 0) {
		return false;
	}
	std::cmatch m;
	if (!std::regex_match(utf8.data(), m, reBrace)) {
		return false;
	}
	try {
		re.assign(m[2].first, m[2].length());
	} catch (std::regex_error&) {
		return false;
	}
	pattern = patt;
	fmt.assign(m[3].first, m[3].length());
	return true;
}

CNicoJK::CNicoJK()
	: bDragAcceptFiles_(false)
	, hPanel_(nullptr)
	, hPanelPopup_(nullptr)
	, hForce_(nullptr)
	, hForceTooltip_(nullptr)
	, hHelpWindow_(nullptr)
	, hHelpEdit_(nullptr)
	, hLoginWindow_(nullptr)
	, hLoginMailEdit_(nullptr)
	, hLoginPasswordEdit_(nullptr)
	, hLoginOtpEdit_(nullptr)
	, hLoginStatus_(nullptr)
	, hLoginLastLogin_(nullptr)
	, hForceFont_(nullptr)
	, bDisplayLogList_(false)
	, logListDisplayedSize_(0)
	, bPendingTimerUpdateList_(false)
	, lastUpdateListTick_(0)
	, lastCalcLeftWidth_(0)
	, lastCalcMiddleWidth_(0)
	, lastCalcLeftWidthD2D_(0)
	, lastCalcMiddleWidthD2D_(0)
	, forwardTick_(0)
	, bQuitSyncThread_(false)
	, bPendingTimerForward_(false)
	, bHalfSkip_(false)
	, bFlipFlop_(false)
	, forwardOffset_(0)
	, forwardOffsetDelta_(0)
	, loginState_(LOGIN_STATE_IDLE)
	, bLoginSettingsQuerying_(false)
	, currentJKToGet_(-1)
	, currentJK_(-1)
	, currentJKChatCount_(0)
	, currentJKForceByChatCount_(-1)
	, currentJKForceByChatCountTick_(0)
	, lastPostTick_(0)
	, bPostToRefuge_(false)
	, bPostToRefugeInverted_(false)
	, bRecording_(false)
	, hQuitCheckRecordingEvent_(nullptr)
	, bUsingLogfileDriver_(false)
	, bSetStreamCallback_(false)
	, bResyncComment_(false)
	, bNicoReceivingPastChat_(false)
	, bRefugeReceivingPastChat_(false)
	, currentLogfileJK_(-1)
	, hLogfile_(INVALID_HANDLE_VALUE)
	, hLogfileLock_(INVALID_HANDLE_VALUE)
	, llftTot_(-1)
	, pcr_(0)
	, pcrTick_(0)
	, pcrPid_(-1)
	, bSpecFile_(false)
	, dropFileTimeout_(0)
{
	cookie_[0] = '\0';
	lastPostComm_[0] = TEXT('\0');
	logReader_.SetCheckIntervalMsec(READ_LOG_FOLDER_INTERVAL);
	SETTINGS s = {};
	s_ = s;
	pcrPids_[0] = -1;
}

bool CNicoJK::GetPluginInfo(TVTest::PluginInfo *pInfo)
{
	// プラグインの情報を返す
	pInfo->Type           = TVTest::PLUGIN_TYPE_NORMAL;
	pInfo->Flags          = 0;
	pInfo->pszPluginName  = L"NicoJK";
	pInfo->pszCopyright   = L"Public Domain";
	pInfo->pszDescription = L"ニコニコ実況をSDKで表示";
	return true;
}

bool CNicoJK::Initialize()
{
	OleInitialize(nullptr);
	// ウィンドウクラスを登録
	WNDCLASSEX wcPanel = {};
	wcPanel.cbSize = sizeof(wcPanel);
	wcPanel.style = 0;
	wcPanel.lpfnWndProc = PanelWindowProc;
	wcPanel.hInstance = g_hinstDLL;
	wcPanel.lpszClassName = TEXT("ru.jk.panel");
	if (RegisterClassEx(&wcPanel) == 0) {
		return false;
	}
	WNDCLASSEX wcPanelPopup = {};
	wcPanelPopup.cbSize = sizeof(wcPanelPopup);
	wcPanelPopup.style = 0;
	wcPanelPopup.lpfnWndProc = PanelPopupWindowProc;
	wcPanelPopup.hInstance = g_hinstDLL;
	wcPanelPopup.lpszClassName = TEXT("ru.jk.panelpopup");
	if (RegisterClassEx(&wcPanelPopup) == 0) {
		return false;
	}
	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(wc);
	wc.style = CS_VREDRAW | CS_HREDRAW;
	wc.lpfnWndProc = ForceWindowProc;
	wc.hInstance = g_hinstDLL;
	wc.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	wc.lpszClassName = TEXT("ru.jk.force");
	if (RegisterClassEx(&wc) == 0) {
		return false;
	}
	WNDCLASSEX wcHelp = {};
	wcHelp.cbSize = sizeof(wcHelp);
	wcHelp.style = CS_HREDRAW | CS_VREDRAW;
	wcHelp.lpfnWndProc = HelpWindowProc;
	wcHelp.hInstance = g_hinstDLL;
	wcHelp.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcHelp.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcHelp.lpszClassName = TEXT("ru.jk.help");
	if (RegisterClassEx(&wcHelp) == 0) {
		return false;
	}
	WNDCLASSEX wcLogin = {};
	wcLogin.cbSize = sizeof(wcLogin);
	wcLogin.style = CS_HREDRAW | CS_VREDRAW;
	wcLogin.lpfnWndProc = LoginWindowProc;
	wcLogin.hInstance = g_hinstDLL;
	wcLogin.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcLogin.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcLogin.lpszClassName = TEXT("ru.jk.login");
	if (RegisterClassEx(&wcLogin) == 0) {
		return false;
	}
	// 初期化処理
	TCHAR path[MAX_PATH];
	iniFileName_.clear();
	tmpSpecFileName_.clear();
	if (GetLongModuleFileName(g_hinstDLL, path, _countof(path))) {
		iniFileName_ = path;
		size_t lastSep = iniFileName_.find_last_of(TEXT("/\\."));
		if (lastSep != tstring::npos && iniFileName_[lastSep] == TEXT('.')) {
			iniFileName_.erase(lastSep);
		}
		tmpSpecFileName_ = iniFileName_;
		iniFileName_ += TEXT(".ini");
		TCHAR ext[32];
		_stprintf_s(ext, TEXT("_%u.tmp"), GetCurrentProcessId());
		tmpSpecFileName_ += ext;
		logReader_.SetJK0LogfilePath(tmpSpecFileName_.c_str());
	}
	// OsdCompositorは他プラグインと共用することがあるので、有効にするならFinalize()まで破棄しない
	bool bEnableOsdCompositor = GetPrivateProfileInt(TEXT("Setting"), TEXT("enableOsdCompositor"), 0, iniFileName_.c_str()) != 0;
	// フィルタグラフを取得できないバージョンではAPIフックを使う
	bool bSetHookOsdCompositor = m_pApp->GetVersion() < TVTest::MakeVersion(0, 9, 0);
	if (!commentWindow_.Initialize(g_hinstDLL, &bEnableOsdCompositor, bSetHookOsdCompositor)) {
		return false;
	}
	if (bEnableOsdCompositor) {
		m_pApp->AddLog(L"OsdCompositorを初期化しました。");
	}
	int dpi = m_pApp->GetDPIFromWindow(m_pApp->GetAppWindow());
	int iconWidth = 16 * dpi / 96;
	int iconHeight = 16 * dpi / 96;
	m_pApp->GetStyleValuePixels(L"side-bar.item.icon.width", dpi, &iconWidth);
	m_pApp->GetStyleValuePixels(L"side-bar.item.icon.height", dpi, &iconHeight);
	bool bSmallIcon = iconWidth <= 16 && iconHeight <= 16;
	// アイコンを登録
	m_pApp->RegisterPluginIconFromResource(g_hinstDLL, MAKEINTRESOURCE(IDB_ICON));

	// パネル項目を登録
	s_.bUsePanel = GetPrivateProfileInt(TEXT("Setting"), TEXT("usePanel"), 1, iniFileName_.c_str()) != 0;
	if (s_.bUsePanel) {
		TVTest::PanelItemInfo pi;
		pi.Size = sizeof(pi);
		pi.Flags = 0;
		pi.Style = TVTest::PANEL_ITEM_STYLE_NEEDFOCUS;
		pi.ID = 1;
		pi.pszIDText = L"NicoJK";
		pi.pszTitle = L"NicoJK";
		pi.hbmIcon = static_cast<HBITMAP>(LoadImage(g_hinstDLL, MAKEINTRESOURCE(IDB_ICON), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
		s_.bUsePanel = m_pApp->RegisterPanelItem(&pi);
		DeleteObject(pi.hbmIcon);
	}
	// コマンドを登録
	TVTest::PluginCommandInfo ci;
	ci.Size = sizeof(ci);
	ci.Flags = TVTest::PLUGIN_COMMAND_FLAG_ICONIZE;
	ci.State = TVTest::PLUGIN_COMMAND_STATE_DISABLED;

	ci.ID = COMMAND_HIDE_FORCE;
	ci.pszText = L"HideForce";
	ci.pszDescription = ci.pszName = L"勢いウィンドウの表示切替";
	ci.hbmIcon = static_cast<HBITMAP>(LoadImage(g_hinstDLL, MAKEINTRESOURCE(bSmallIcon ? IDB_FORCE16 : IDB_FORCE), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
	if (!s_.bUsePanel && !m_pApp->RegisterPluginCommand(&ci)) {
		m_pApp->RegisterCommand(ci.ID, ci.pszText, ci.pszName);
	}
	DeleteObject(ci.hbmIcon);

	ci.ID = COMMAND_HIDE_COMMENT;
	ci.pszText = L"HideComment";
	ci.pszDescription = ci.pszName = L"実況コメントの表示切替";
	ci.hbmIcon = static_cast<HBITMAP>(LoadImage(g_hinstDLL, MAKEINTRESOURCE(bSmallIcon ? IDB_COMMENT16 : IDB_COMMENT), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
	if (!m_pApp->RegisterPluginCommand(&ci)) {
		m_pApp->RegisterCommand(ci.ID, ci.pszText, ci.pszName);
	}
	DeleteObject(ci.hbmIcon);

	memset(s_.forwardList, 0, sizeof(s_.forwardList));
	for (int i = 0; i < _countof(s_.forwardList); ++i) {
		TCHAR key[16], name[32];
		_stprintf_s(key, TEXT("Forward%c"), TEXT('A') + i);
		_stprintf_s(name, TEXT("実況コメントの前進:%c"), TEXT('A') + i);
		if ((s_.forwardList[i] = GetPrivateProfileInt(TEXT("Setting"), key, INT_MAX, iniFileName_.c_str())) == INT_MAX) {
			break;
		}
		m_pApp->RegisterCommand(COMMAND_FORWARD_A + i, key, name);
	}
	// イベントコールバック関数を登録
	m_pApp->SetEventCallback(EventCallback, this);
	return true;
}

bool CNicoJK::Finalize()
{
	OleUninitialize();
	// 終了処理
	TogglePlugin(false);
	// パネルウィンドウを破棄
	if (hPanel_) {
		DestroyWindow(hPanel_);
	}
	// 本体や他プラグインとの干渉を防ぐため、一旦有効にしたD&Dは最後まで維持する
	if (bDragAcceptFiles_) {
		DragAcceptFiles(m_pApp->GetAppWindow(), FALSE);
		bDragAcceptFiles_ = false;
	}
	commentWindow_.Finalize();
	return true;
}

bool CNicoJK::TogglePlugin(bool bEnabled)
{
	if (bEnabled) {
		if ((!s_.bUsePanel || hPanel_) && !hForce_) {
			LoadFromIni();
			LoadForceListFromIni(s_.logfileFolder);

			// 必要ならサーバに渡すCookieを取得
			cookie_[0] = '\0';
			if (!s_.execGetCookie.empty()) {
				// 投稿欄を表示するだけ
				std::string strCookie = ";";
				// 避難所のみに接続するときは実行しない
				if ((s_.refugeUri.empty() || s_.bRefugeMixing) && _tcsicmp(s_.execGetCookie.c_str(), TEXT("cmd /c echo ;"))) {
					strCookie = GetCookieString(s_.execGetCookie.c_str(), s_.execGetV10Key.c_str(), cookie_, _countof(cookie_), 10000);
				}
				if (strCookie.empty()) {
					m_pApp->AddLog(L"execGetCookieの実行に失敗しました。", TVTest::LOG_TYPE_ERROR);
				}
				strncpy_s(cookie_, strCookie.c_str(), _TRUNCATE);
			}

			// 勢い窓作成
			if (hPanel_) {
				hForce_ = CreateWindowEx(0, TEXT("ru.jk.force"), TEXT("NicoJK - ニコニコ実況勢い"),
				                         WS_CHILD, 0, 0, 320, 240, hPanel_, nullptr, g_hinstDLL, this);
			} else {
				hForce_ = CreateWindowEx(WS_EX_WINDOWEDGE | WS_EX_TOOLWINDOW, TEXT("ru.jk.force"), TEXT("NicoJK - ニコニコ実況勢い"),
				                         WS_CAPTION | WS_POPUP | WS_THICKFRAME | WS_SYSMENU,
				                         CW_USEDEFAULT, CW_USEDEFAULT, 320, 240, nullptr, nullptr, g_hinstDLL, this);
			}
			if (hForce_) {
				// ウィンドウコールバック関数を登録
				m_pApp->SetWindowMessageCallback(WindowMsgCallback, this);
				// ストリームコールバック関数を登録(指定ファイル再生機能のために常に登録)
				ToggleStreamCallback(true);
				// DWMの更新タイミングでTIMER_FORWARDを呼ぶスレッドを開始(Vista以降)
				if (s_.timerInterval < 0) {
					BOOL bCompEnabled;
					if (SUCCEEDED(DwmIsCompositionEnabled(&bCompEnabled)) && bCompEnabled) {
						bQuitSyncThread_ = false;
						syncThread_ = std::thread([this]() { SyncThread(); });
						SetThreadPriority(syncThread_.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
					}
					if (!syncThread_.joinable()) {
						m_pApp->AddLog(L"Aeroが無効のため設定timerIntervalのリフレッシュ同期機能はオフになります。");
						SetTimer(hForce_, TIMER_FORWARD, 166667 / -s_.timerInterval, nullptr);
					}
				}
				if (s_.dropLogfileMode != 0) {
					DragAcceptFiles(m_pApp->GetAppWindow(), TRUE);
					bDragAcceptFiles_ = true;
				}
			}
		}
		return hForce_ != nullptr;
	} else {
		if (hForce_) {
			DestroyWindow(hForce_);
		}
		if (hPanelPopup_) {
			DestroyWindow(hPanelPopup_);
		}
		if (hForceFont_) {
			DeleteFont(hForceFont_);
			hForceFont_ = nullptr;
		}
		return true;
	}
}

void CNicoJK::TogglePanelPopup()
{
	if (hPanelPopup_) {
		// パネルに戻す
		SendMessage(hPanelPopup_, WM_CLOSE, 0, 0);
	} else if (hPanel_) {
		// パネル項目をポップアップウィンドウ化
		hPanelPopup_ = CreateWindowEx(WS_EX_WINDOWEDGE | WS_EX_TOOLWINDOW, TEXT("ru.jk.panelpopup"), TEXT("NicoJK - パネル"),
		                              WS_CAPTION | WS_POPUP | WS_THICKFRAME | WS_SYSMENU,
		                              CW_USEDEFAULT, CW_USEDEFAULT, 320, 240, nullptr, nullptr, g_hinstDLL, this);
	}
}

void CNicoJK::SyncThread()
{
	DWORD count = 0;
	int timeout = 0;
	while (!bQuitSyncThread_) {
		if (FAILED(DwmFlush())) {
			// ビジーに陥らないように
			Sleep(500);
		}
		if (count >= 10000) {
			// 捌き切れない量のメッセージを送らない
			if (bPendingTimerForward_ && --timeout >= 0) {
				continue;
			}
			count -= 10000;
			timeout = 30;
			bPendingTimerForward_ = true;
			SendNotifyMessage(hForce_, WM_TIMER, TIMER_FORWARD, 0);
		}
		count += bHalfSkip_ ? -s_.timerInterval / 2 : -s_.timerInterval;
	}
}

void CNicoJK::CheckRecordingThread(DWORD processID)
{
	const DWORD CMD_SUCCESS = TRUE;
	const DWORD VIEW_APP_ST_REC = 2;
	const DWORD cmdViewAppGetStatus[] = {207, 0};
	TCHAR pipeName[64];
	_stprintf_s(pipeName, TEXT("\\\\.\\pipe\\View_Ctrl_BonNoWaitPipe_%d"), processID);

	while (WaitForSingleObject(hQuitCheckRecordingEvent_, 2000) == WAIT_TIMEOUT) {
		// EDCBのCtrlCmdインタフェースにアクセスしてその録画状態を調べる
		HANDLE pipe = CreateFile(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (pipe != INVALID_HANDLE_VALUE) {
			DWORD n;
			if (WriteFile(pipe, cmdViewAppGetStatus, sizeof(cmdViewAppGetStatus), &n, nullptr) && n == sizeof(cmdViewAppGetStatus)) {
				DWORD res[3];
				n = 0;
				for (DWORD m; n < sizeof(res) && ReadFile(pipe, reinterpret_cast<BYTE*>(res) + n, sizeof(res) - n, &m, nullptr); n += m);
				if (n == sizeof(res) && res[0] == CMD_SUCCESS) {
					bRecording_ = res[2] == VIEW_APP_ST_REC;
				}
			}
			CloseHandle(pipe);
		} else if (GetLastError() != ERROR_PIPE_BUSY) {
			break;
		}
	}
	bRecording_ = false;
}

void CNicoJK::ToggleStreamCallback(bool bSet)
{
	if (bSet) {
		if (!bSetStreamCallback_) {
			bSetStreamCallback_ = true;
			pcrPid_ = -1;
			pcrPids_[0] = -1;
			m_pApp->SetStreamCallback(0, StreamCallback, this);
		}
	} else {
		if (bSetStreamCallback_) {
			m_pApp->SetStreamCallback(TVTest::STREAM_CALLBACK_REMOVE, StreamCallback);
			bSetStreamCallback_ = false;
		}
	}
}

std::vector<NETWORK_SERVICE_ID_ELEM>::iterator CNicoJK::LowerBoundNetworkServiceID(std::vector<NETWORK_SERVICE_ID_ELEM>::iterator first,
                                                                                   std::vector<NETWORK_SERVICE_ID_ELEM>::iterator last, DWORD ntsID)
{
	NETWORK_SERVICE_ID_ELEM e;
	e.ntsID = ntsID;
	return std::lower_bound(first, last, e, [](const NETWORK_SERVICE_ID_ELEM &a, const NETWORK_SERVICE_ID_ELEM &b) { return a.ntsID < b.ntsID; });
}

std::vector<CNicoJK::FORCE_ELEM>::iterator CNicoJK::LowerBoundJKID(std::vector<FORCE_ELEM>::iterator first,
                                                                   std::vector<FORCE_ELEM>::iterator last, int jkID)
{
	FORCE_ELEM e;
	e.jkID = jkID;
	return std::lower_bound(first, last, e, [](const FORCE_ELEM &a, const FORCE_ELEM &b) { return a.jkID < b.jkID; });
}

void CNicoJK::LoadFromIni()
{
	// iniはセクション単位で読むと非常に速い。起動時は処理が混み合うのでとくに有利
	std::vector<TCHAR> buf = GetPrivateProfileSectionBuffer(TEXT("Setting"), iniFileName_.c_str());
	s_.hideForceWindow		= GetBufferedProfileInt(buf.data(), TEXT("hideForceWindow"), 0);
	s_.forceFontSize		= GetBufferedProfileInt(buf.data(), TEXT("forceFontSize"), 10);
	GetBufferedProfileString(buf.data(), TEXT("forceFontName"), TEXT("Meiryo UI"), s_.forceFontName, _countof(s_.forceFontName));
	s_.timerInterval		= GetBufferedProfileInt(buf.data(), TEXT("timerInterval"), -10000);
	s_.halfSkipThreshold	= GetBufferedProfileInt(buf.data(), TEXT("halfSkipThreshold"), 9999);
	s_.commentLineMargin	= GetBufferedProfileInt(buf.data(), TEXT("commentLineMargin"), 125);
	s_.commentFontOutline	= GetBufferedProfileInt(buf.data(), TEXT("commentFontOutline"), 0);
	s_.commentSize			= GetBufferedProfileInt(buf.data(), TEXT("commentSize"), 100);
	s_.commentSizeMin		= GetBufferedProfileInt(buf.data(), TEXT("commentSizeMin"), 16);
	s_.commentSizeMax		= GetBufferedProfileInt(buf.data(), TEXT("commentSizeMax"), 9999);
	GetBufferedProfileString(buf.data(), TEXT("commentFontName"), TEXT("メイリオ"), s_.commentFontName, _countof(s_.commentFontName));
	GetBufferedProfileString(buf.data(), TEXT("commentFontNameMulti"), TEXT("メイリオ"), s_.commentFontNameMulti, _countof(s_.commentFontNameMulti));
	GetBufferedProfileString(buf.data(), TEXT("commentFontNameEmoji"), TEXT(""), s_.commentFontNameEmoji, _countof(s_.commentFontNameEmoji));
	s_.bCommentFontBold		= GetBufferedProfileInt(buf.data(), TEXT("commentFontBold"), 1) != 0;
	s_.bCommentFontAntiAlias = GetBufferedProfileInt(buf.data(), TEXT("commentFontAntiAlias"), 1) != 0;
	s_.commentDuration		= GetBufferedProfileInt(buf.data(), TEXT("commentDuration"), CCommentWindow::DISPLAY_DURATION);
	s_.commentDrawLineCount = GetBufferedProfileInt(buf.data(), TEXT("commentDrawLineCount"), CCommentWindow::DEFAULT_LINE_DRAW_COUNT);
	s_.commentShareMode		= GetBufferedProfileInt(buf.data(), TEXT("commentShareMode"), 0);
	s_.logfileMode			= GetBufferedProfileInt(buf.data(), TEXT("logfileMode"), 0);
	s_.bCheckProcessRecording = GetBufferedProfileInt(buf.data(), TEXT("checkProcessRecording"), 1) != 0;
	s_.logfileDrivers		= GetBufferedProfileToString(buf.data(), TEXT("logfileDrivers"),
							                             TEXT("BonDriver_UDP.dll:BonDriver_TCP.dll:BonDriver_File.dll:BonDriver_RecTask.dll:BonDriver_TsTask.dll:")
							                             TEXT("BonDriver_NetworkPipe.dll:BonDriver_Pipe.dll:BonDriver_Pipe2.dll"));
	s_.nonTunerDrivers		= GetBufferedProfileToString(buf.data(), TEXT("nonTunerDrivers"),
							                             TEXT("BonDriver_UDP.dll:BonDriver_TCP.dll:BonDriver_File.dll:BonDriver_RecTask.dll:BonDriver_TsTask.dll:")
							                             TEXT("BonDriver_NetworkPipe.dll:BonDriver_Pipe.dll:BonDriver_Pipe2.dll"));
	s_.execGetCookie		= GetBufferedProfileToString(buf.data(), TEXT("execGetCookie"), TEXT("cmd /c echo ;"));
	s_.execGetV10Key		= GetBufferedProfileToString(buf.data(), TEXT("execGetV10Key"), TEXT(""));
	s_.channelsUri			= TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("channelsUri"), TEXT("")).c_str());
	s_.channelsWsUri		= TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("channelsWsUri"), TEXT("")).c_str());
	s_.refugeUri			= TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("refugeUri"), TEXT("")).c_str());
	s_.bDropForwardedComment = GetBufferedProfileInt(buf.data(), TEXT("dropForwardedComment"), 0) != 0;
	s_.bRefugeMixing		= !s_.refugeUri.empty() && GetBufferedProfileInt(buf.data(), TEXT("refugeMixing"), 0) != 0;
	s_.bPostToRefuge		= s_.bRefugeMixing ? GetBufferedProfileInt(buf.data(), TEXT("postToRefuge"), 0) != 0 : !s_.refugeUri.empty();
	s_.crNicoEditBox		= GetColor(TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("nicoEditBoxColor"), TEXT("#bbbbff")).c_str()).c_str());
	s_.crRefugeEditBox		= GetColor(TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("refugeEditBoxColor"), TEXT("#ffbbbb")).c_str()).c_str());
	s_.crNicoMarker			= GetColor(TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("nicoMarkerColor"), TEXT("#0000aa")).c_str()).c_str());
	s_.crRefugeMarker		= GetColor(TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("refugeMarkerColor"), TEXT("#990000")).c_str()).c_str());
	s_.crNicoLightShadow	= GetColor(TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("nicoLightShadowColor"), TEXT("white")).c_str()).c_str());
	s_.crRefugeLightShadow	= GetColor(TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("refugeLightShadowColor"), TEXT("pink")).c_str()).c_str());
	s_.crNicoDarkShadow		= GetColor(TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("nicoDarkShadowColor"), TEXT("black")).c_str()).c_str());
	s_.crRefugeDarkShadow	= GetColor(TStringToPrintableAsciiString(GetBufferedProfileToString(buf.data(), TEXT("refugeDarkShadowColor"), TEXT("#990000")).c_str()).c_str());
	s_.mailDecorations		= GetBufferedProfileToString(buf.data(), TEXT("mailDecorations"), TEXT("[cyan]:[red]:[green small]:[orange]::"));
	s_.bAnonymity			= GetBufferedProfileInt(buf.data(), TEXT("anonymity"), 1) != 0;
	s_.bUseOsdCompositor	= GetBufferedProfileInt(buf.data(), TEXT("useOsdCompositor"), 0) != 0;
	s_.bUseTexture			= GetBufferedProfileInt(buf.data(), TEXT("useTexture"), 1) != 0;
	s_.bUseDrawingThread	= GetBufferedProfileInt(buf.data(), TEXT("useDrawingThread"), 1) != 0;
	s_.bSetChannel			= GetBufferedProfileInt(buf.data(), TEXT("setChannel"), 1) != 0;
	s_.maxAutoReplace		= GetBufferedProfileInt(buf.data(), TEXT("maxAutoReplace"), 20);
	s_.abone				= GetBufferedProfileToString(buf.data(), TEXT("abone"), TEXT("### NG ### &"));
	s_.dropLogfileMode		= GetBufferedProfileInt(buf.data(), TEXT("dropLogfileMode"), 0);
	s_.defaultPlaybackDelay	= GetBufferedProfileInt(buf.data(), TEXT("defaultPlaybackDelay"), 500);
	// 実況ログフォルダのパスを作成
	TCHAR path[MAX_PATH];
	GetBufferedProfileString(buf.data(), TEXT("logfileFolder"), TEXT("Plugins\\NicoJK"), path, _countof(path));
	if (path[0] && !_tcschr(TEXT("/\\"), path[0]) && path[1] != TEXT(':')) {
		// 相対パス
		TCHAR dir[MAX_PATH];
		if (GetLongModuleFileName(nullptr, dir, _countof(dir))) {
			s_.logfileFolder = dir;
			size_t lastSep = s_.logfileFolder.find_last_of(TEXT("/\\"));
			if (lastSep != tstring::npos) {
				s_.logfileFolder.erase(lastSep + 1);
			}
			s_.logfileFolder += path;
		} else {
			s_.logfileFolder.clear();
		}
	} else {
		s_.logfileFolder = path;
	}
	DWORD attr = GetFileAttributes(s_.logfileFolder.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		s_.logfileFolder.clear();
	}
	logReader_.SetLogDirectory(s_.logfileFolder.c_str());

	buf = GetPrivateProfileSectionBuffer(TEXT("Window"), iniFileName_.c_str());
	s_.rcForce.left			= GetBufferedProfileInt(buf.data(), TEXT("ForceX"), 0);
	s_.rcForce.top			= GetBufferedProfileInt(buf.data(), TEXT("ForceY"), 0);
	s_.rcForce.right		= GetBufferedProfileInt(buf.data(), TEXT("ForceWidth"), 0) + s_.rcForce.left;
	s_.rcForce.bottom		= GetBufferedProfileInt(buf.data(), TEXT("ForceHeight"), 0) + s_.rcForce.top;
	s_.forceOpacity			= GetBufferedProfileInt(buf.data(), TEXT("ForceOpacity"), 255);
	s_.commentOpacity		= GetBufferedProfileInt(buf.data(), TEXT("CommentOpacity"), 255);
	s_.headerMask			= GetBufferedProfileInt(buf.data(), TEXT("HeaderMask"), 0);
	s_.bSetRelative			= GetBufferedProfileInt(buf.data(), TEXT("SetRelative"), 0) != 0;

	ntsIDList_.assign(DEFAULT_NTSID_TABLE, DEFAULT_NTSID_TABLE + _countof(DEFAULT_NTSID_TABLE));
	// 設定ファイルのネットワーク/サービスID-実況ID対照表を、ソートを維持しながらマージ
	buf = GetPrivateProfileSectionBuffer(TEXT("Channels"), iniFileName_.c_str());
	for (LPCTSTR p = buf.data(); *p; p += _tcslen(p) + 1) {
		NETWORK_SERVICE_ID_ELEM e;
		bool bPrior = _stscanf_s(p, TEXT("0x%x=+%d"), &e.ntsID, &e.jkID) == 2;
		if (bPrior) {
			e.jkID |= NETWORK_SERVICE_ID_ELEM::JKID_PRIOR;
		}
		if (bPrior || _stscanf_s(p, TEXT("0x%x=%d"), &e.ntsID, &e.jkID) == 2) {
			// 設定ファイルの定義では上位と下位をひっくり返しているので補正
			e.ntsID = (e.ntsID<<16) | (e.ntsID>>16);
			std::vector<NETWORK_SERVICE_ID_ELEM>::iterator it = LowerBoundNetworkServiceID(ntsIDList_.begin(), ntsIDList_.end(), e.ntsID);
			if (it != ntsIDList_.end() && it->ntsID == e.ntsID) {
				*it = e;
			} else {
				ntsIDList_.insert(it, e);
			}
		}
	}

	rplList_.clear();
	LoadRplListFromIni(TEXT("AutoReplace"), &rplList_);
	LoadRplListFromIni(TEXT("CustomReplace"), &rplList_);
}

void CNicoJK::SaveToIni()
{
	WritePrivateProfileInt(TEXT("Window"), TEXT("ForceX"), s_.rcForce.left, iniFileName_.c_str());
	WritePrivateProfileInt(TEXT("Window"), TEXT("ForceY"), s_.rcForce.top, iniFileName_.c_str());
	WritePrivateProfileInt(TEXT("Window"), TEXT("ForceWidth"), s_.rcForce.right - s_.rcForce.left, iniFileName_.c_str());
	WritePrivateProfileInt(TEXT("Window"), TEXT("ForceHeight"), s_.rcForce.bottom - s_.rcForce.top, iniFileName_.c_str());
	WritePrivateProfileInt(TEXT("Window"), TEXT("ForceOpacity"), s_.forceOpacity, iniFileName_.c_str());
	WritePrivateProfileInt(TEXT("Window"), TEXT("CommentOpacity"), s_.commentOpacity, iniFileName_.c_str());
	WritePrivateProfileInt(TEXT("Window"), TEXT("HeaderMask"), s_.headerMask, iniFileName_.c_str());
	WritePrivateProfileInt(TEXT("Window"), TEXT("SetRelative"), s_.bSetRelative, iniFileName_.c_str());
}

void CNicoJK::LoadForceListFromIni(const tstring &logfileFolder)
{
	// chatStreamIDをもつチャンネルのみ追加
	forceList_.clear();
	for (size_t i = 0; i < _countof(DEFAULT_JKID_NAME_TABLE); ++i) {
		if (DEFAULT_JKID_NAME_TABLE[i].chatStreamID) {
			FORCE_ELEM e;
			e.jkID = DEFAULT_JKID_NAME_TABLE[i].jkID;
			e.chatStreamID = DEFAULT_JKID_NAME_TABLE[i].chatStreamID;
			e.refugeChatStreamID = e.chatStreamID;
			e.force = -1;
			e.bFixedName = false;
			forceList_.push_back(e);
		}
	}

	// chatStreamIDの追加の対照表をマージ
	std::vector<TCHAR> buf = GetPrivateProfileSectionBuffer(TEXT("ChatStreams"), iniFileName_.c_str());
	for (LPCTSTR p = buf.data(); *p; p += _tcslen(p) + 1) {
		FORCE_ELEM e;
		e.jkID = _tcstol(p, nullptr, 10);
		if (e.jkID > 0) {
			TCHAR key[16];
			_stprintf_s(key, TEXT("%d"), e.jkID);
			tstring val = GetBufferedProfileToString(buf.data(), key, TEXT("!"));
			if (val != TEXT("!")) {
				bool bFirstVal = true;
				for (size_t i = 0; i < val.size(); ++i) {
					if ((TEXT('0') <= val[i] && val[i] <= TEXT('9')) ||
					    (TEXT('A') <= val[i] && val[i] <= TEXT('Z')) ||
					    (TEXT('a') <= val[i] && val[i] <= TEXT('z'))) {
						char c = static_cast<char>(val[i]);
						e.refugeChatStreamID += c;
						if (bFirstVal) {
							e.chatStreamID += c;
						}
					} else if (val[i] == TEXT(',') && bFirstVal) {
						// カンマ区切りがあれば後者が避難所の値
						e.refugeChatStreamID.clear();
						bFirstVal = false;
					} else {
						e.chatStreamID.clear();
						e.refugeChatStreamID.clear();
						TCHAR text[64];
						_stprintf_s(text, TEXT("[ChatStreams]のキー%sの値が不正です。"), key);
						m_pApp->AddLog(text, TVTest::LOG_TYPE_ERROR);
						break;
					}
				}
				e.force = -1;
				e.bFixedName = false;
				// まだなければ追加
				std::vector<FORCE_ELEM>::iterator it = LowerBoundJKID(forceList_.begin(), forceList_.end(), e.jkID);
				if (it == forceList_.end() || it->jkID != e.jkID) {
					if (!e.chatStreamID.empty() || !e.refugeChatStreamID.empty()) {
						forceList_.insert(it, e);
					}
				} else if (e.chatStreamID.empty() && e.refugeChatStreamID.empty()) {
					forceList_.erase(it);
				} else {
					*it = e;
				}
			}
		}
	}

	// ログフォルダにあるチャンネルを追加
	if (!logfileFolder.empty()) {
		EnumFindFile((logfileFolder + TEXT("\\jk*")).c_str(), [this](const WIN32_FIND_DATA &fd) {
			FORCE_ELEM e;
			if (!_tcsnicmp(fd.cFileName, TEXT("jk"), 2) && (e.jkID = _tcstol(&fd.cFileName[2], nullptr, 10)) > 0) {
				e.force = -1;
				e.bFixedName = false;
				// まだなければ追加
				std::vector<FORCE_ELEM>::iterator it = LowerBoundJKID(forceList_.begin(), forceList_.end(), e.jkID);
				if (it == forceList_.end() || it->jkID != e.jkID) {
					forceList_.insert(it, e);
				}
			}
		});
	}

	// とりあえず組み込みのチャンネル名をセットしておく
	for (auto it = forceList_.begin(); it != forceList_.end(); ++it) {
		JKID_NAME_ELEM e;
		e.jkID = it->jkID;
		const JKID_NAME_ELEM *p = std::lower_bound(
			DEFAULT_JKID_NAME_TABLE, DEFAULT_JKID_NAME_TABLE + _countof(DEFAULT_JKID_NAME_TABLE), e,
			[](const JKID_NAME_ELEM &a, const JKID_NAME_ELEM &b) { return a.jkID < b.jkID; });
		if (p && p->jkID == e.jkID) {
			it->name = p->name;
		}
	}

	// チャンネル名が指定されていれば上書き
	buf = GetPrivateProfileSectionBuffer(TEXT("ChannelNames"), iniFileName_.c_str());
	for (LPCTSTR p = buf.data(); *p; p += _tcslen(p) + 1) {
		int jkID = _tcstol(p, nullptr, 10);
		std::vector<FORCE_ELEM>::iterator it = LowerBoundJKID(forceList_.begin(), forceList_.end(), jkID);
		if (it != forceList_.end() && it->jkID == jkID) {
			TCHAR key[16];
			_stprintf_s(key, TEXT("%d"), jkID);
			it->name = GetBufferedProfileToString(buf.data(), key, TEXT(""));
			it->bFixedName = true;
		}
	}

	UpdateForceListEpgInfo();
}

void CNicoJK::UpdateForceListEpgInfo()
{
#if TVTEST_PLUGIN_VERSION >= TVTEST_PLUGIN_VERSION_(0, 0, 12)
	int numSpaces = 0;
	m_pApp->GetTuningSpace(&numSpaces);
	for (int space = 0; space < numSpaces; ++space) {
		for (int ch = 0; ; ++ch) {
			TVTest::ChannelInfo ci = {};
			ci.Size = sizeof(ci);
			if (!m_pApp->GetChannelInfo(space, ch, &ci)) {
				break;
			}
			if (!ci.NetworkID || !ci.TransportStreamID || !ci.ServiceID) {
				continue;
			}

			DWORD ntsID = 0;
			if (0x7880 <= ci.NetworkID && ci.NetworkID <= 0x7FEF) {
				// 地上波のサービス種別とサービス番号はマスクする
				ntsID = (static_cast<DWORD>(ci.ServiceID&~0x0187) << 16) | 0x000F;
			} else {
				ntsID = (static_cast<DWORD>(ci.ServiceID) << 16) | ci.NetworkID;
			}
			std::vector<NETWORK_SERVICE_ID_ELEM>::const_iterator itNts =
				LowerBoundNetworkServiceID(ntsIDList_.begin(), ntsIDList_.end(), ntsID);
			if (itNts == ntsIDList_.end() || itNts->ntsID != ntsID || itNts->jkID <= 0) {
				continue;
			}

			int jkID = itNts->jkID & ~NETWORK_SERVICE_ID_ELEM::JKID_PRIOR;
			std::vector<FORCE_ELEM>::iterator itForce = LowerBoundJKID(forceList_.begin(), forceList_.end(), jkID);
			if (itForce == forceList_.end() || itForce->jkID != jkID) {
				continue;
			}
			if (!itForce->bHasEpgInfo || (itNts->jkID & NETWORK_SERVICE_ID_ELEM::JKID_PRIOR)) {
				itForce->bHasEpgInfo = true;
				itForce->networkID = ci.NetworkID;
				itForce->transportStreamID = ci.TransportStreamID;
				itForce->serviceID = ci.ServiceID;
				itForce->eventName.clear();
				itForce->eventNameUpdateTick = 0;
			}
		}
	}
#endif
}

void CNicoJK::ResetForceListEpgInfo()
{
#if TVTEST_PLUGIN_VERSION >= TVTEST_PLUGIN_VERSION_(0, 0, 12)
	for (auto &elem : forceList_) {
		elem.bHasEpgInfo = false;
		elem.networkID = 0;
		elem.transportStreamID = 0;
		elem.serviceID = 0;
		elem.eventName.clear();
		elem.eventNameUpdateTick = 0;
	}
	UpdateForceListEpgInfo();
	if (hForce_) {
		SendMessage(hForce_, WM_UPDATE_LIST, TRUE, 0);
	}
#endif
}

void CNicoJK::UpdateForceElemEventName(FORCE_ELEM *pElem, ULONGLONG nowTick)
{
#if TVTEST_PLUGIN_VERSION >= TVTEST_PLUGIN_VERSION_(0, 0, 12)
	if (!pElem->bHasEpgInfo || nowTick < pElem->eventNameUpdateTick) {
		return;
	}

	pElem->eventName.clear();
	pElem->eventNameUpdateTick = nowTick + 10000;

	TVTest::EpgEventQueryInfo query = {};
	query.NetworkID = pElem->networkID;
	query.TransportStreamID = pElem->transportStreamID;
	query.ServiceID = pElem->serviceID;
	query.Type = TVTest::EPG_EVENT_QUERY_TIME;
	query.Flags = 0;
	GetSystemTimeAsFileTime(&query.Time);
	const ULONGLONG filetimeSecond = 10000000ULL;
	const ULONGLONG eventNameLeadTime = 30ULL * filetimeSecond;
	ULONGLONG queryTime = (static_cast<ULONGLONG>(query.Time.dwHighDateTime) << 32) | query.Time.dwLowDateTime;
	queryTime += eventNameLeadTime;
	query.Time.dwLowDateTime = static_cast<DWORD>(queryTime);
	query.Time.dwHighDateTime = static_cast<DWORD>(queryTime >> 32);

	TVTest::EpgEventInfo *pEvent = m_pApp->GetEpgEventInfo(&query);
	if (!pEvent) {
		return;
	}
	if (pEvent->pszEventName) {
		pElem->eventName = pEvent->pszEventName;
	}

	FILETIME ftStart = {};
	ULONGLONG updateTick = nowTick + 60000;
	if (pEvent->Duration > 0 && SystemTimeToFileTime(&pEvent->StartTime, &ftStart)) {
		const ULONGLONG jstOffset = 9ULL * 60 * 60 * filetimeSecond;
		ULONGLONG startTime = (static_cast<ULONGLONG>(ftStart.dwHighDateTime) << 32) | ftStart.dwLowDateTime;
		if (startTime >= jstOffset) {
			ULONGLONG endTime = startTime - jstOffset + static_cast<ULONGLONG>(pEvent->Duration) * filetimeSecond;
			if (endTime > queryTime) {
				updateTick = nowTick + (endTime - queryTime) / 10000ULL;
			} else {
				updateTick = nowTick + 10000;
			}
		}
	}
	pElem->eventNameUpdateTick = updateTick;
	m_pApp->FreeEpgEventInfo(pEvent);
#else
	static_cast<void>(pElem);
	static_cast<void>(nowTick);
#endif
}

void CNicoJK::LoadRplListFromIni(LPCTSTR section, std::vector<RPL_ELEM> *pRplList)
{
	std::vector<TCHAR> buf = GetPrivateProfileSectionBuffer(section, iniFileName_.c_str());
	size_t lastSize = pRplList->size();
	for (LPCTSTR p = buf.data(); *p; p += _tcslen(p) + 1) {
		RPL_ELEM e;
		if (!_tcsnicmp(p, TEXT("Pattern"), 7)) {
			LPTSTR endp;
			e.key = _tcstol(&p[7], &endp, 10);
			if (endp != &p[7]) {
				e.section = section;
				TCHAR key[32];
				_stprintf_s(key, TEXT("Comment%d"), e.key);
				e.comment = GetBufferedProfileToString(buf.data(), key, TEXT(""));
				_stprintf_s(key, TEXT("Pattern%d"), e.key);
				tstring val = GetBufferedProfileToString(buf.data(), key, TEXT(""));
				if (!e.SetPattern(val.c_str())) {
					TCHAR text[64];
					_stprintf_s(text, TEXT("%sの正規表現が異常です。"), key);
					m_pApp->AddLog(text, TVTest::LOG_TYPE_ERROR);
				} else {
					pRplList->push_back(e);
				}
			}
		}
	}
	std::sort(pRplList->begin() + lastSize, pRplList->end(), [](const RPL_ELEM &a, const RPL_ELEM &b) { return a.key < b.key; });
}

void CNicoJK::SaveRplListToIni(LPCTSTR section, const std::vector<RPL_ELEM> &rplList, bool bClearSection)
{
	if (bClearSection) {
		WritePrivateProfileString(section, nullptr, nullptr, iniFileName_.c_str());
	}
	for (auto it = rplList.cbegin(); it != rplList.end(); ++it) {
		if (it->section == section) {
			TCHAR key[32];
			_stprintf_s(key, TEXT("Pattern%d"), it->key);
			WritePrivateProfileString(section, key, it->pattern.c_str(), iniFileName_.c_str());
		}
	}
}

HWND CNicoJK::GetFullscreenWindow()
{
	TVTest::HostInfo hostInfo;
	if (m_pApp->GetFullscreen() && m_pApp->GetHostInfo(&hostInfo)) {
		TCHAR className[64];

		if (_sntprintf_s(className, _TRUNCATE, TEXT("%.47s Fullscreen"), hostInfo.pszAppName) < 0) {
			return nullptr;
		}

		HWND hwnd = nullptr;
		while ((hwnd = FindWindowEx(nullptr, hwnd, className, nullptr)) != nullptr) {
			DWORD pid;
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid == GetCurrentProcessId()) {
				return hwnd;
			}
		}
	}
	return nullptr;
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	std::pair<HWND, LPCTSTR> *params = reinterpret_cast<std::pair<HWND, LPCTSTR>*>(lParam);
	TCHAR className[64];
	if (GetClassName(hwnd, className, _countof(className)) && !_tcscmp(className, params->second)) {
		// 見つかった
		params->first = hwnd;
		return FALSE;
	}
	return TRUE;
}

// TVTestのVideo Containerウィンドウを探す
HWND CNicoJK::FindVideoContainer()
{
	std::pair<HWND, LPCTSTR> params(nullptr, nullptr);
	TVTest::HostInfo hostInfo;
	if (m_pApp->GetHostInfo(&hostInfo)) {
		TCHAR searchName[64];
		_tcsncpy_s(searchName, hostInfo.pszAppName, 31);
		_tcscat_s(searchName, TEXT(" Video Container"));

		params.second = searchName;
		HWND hwndFull = GetFullscreenWindow();
		EnumChildWindows(hwndFull ? hwndFull : m_pApp->GetAppWindow(), EnumWindowsProc, reinterpret_cast<LPARAM>(&params));
	}
	return params.first;
}

// 再生中のストリームのネットワーク/サービスIDを取得する
DWORD CNicoJK::GetCurrentNetworkServiceID()
{
	TVTest::ServiceInfo si;
	int index = m_pApp->GetService();
	if (index >= 0 && m_pApp->GetServiceInfo(index, &si)) {
		TVTest::ChannelInfo ci;
		if (m_pApp->GetCurrentChannelInfo(&ci) && ci.NetworkID) {
			if (0x7880 <= ci.NetworkID && ci.NetworkID <= 0x7FEF) {
				// 地上波のサービス種別とサービス番号はマスクする
				return (static_cast<DWORD>(si.ServiceID&~0x0187) << 16) | 0x000F;
			}
			return (static_cast<DWORD>(si.ServiceID) << 16) | ci.NetworkID;
		}
		// チャンネルスキャンしていないとGetCurrentChannelInfo()もネットワークIDの取得に失敗するよう
		if (si.ServiceID >= 0x0400) {
			// 地上波っぽいのでマスクする
			return (static_cast<DWORD>(si.ServiceID&~0x0187) << 16) | 0;
		}
		return (static_cast<DWORD>(si.ServiceID) << 16) | 0;
	}
	return 0;
}

// 指定チャンネルのネットワーク/サービスIDを取得する
bool CNicoJK::GetChannelNetworkServiceID(int tuningSpace, int channelIndex, DWORD *pNtsID)
{
	TVTest::ChannelInfo ci;
	if (m_pApp->GetChannelInfo(tuningSpace, channelIndex, &ci)) {
		if (ci.NetworkID && ci.ServiceID) {
			if (0x7880 <= ci.NetworkID && ci.NetworkID <= 0x7FEF) {
				// 地上波のサービス種別とサービス番号はマスクする
				*pNtsID = (static_cast<DWORD>(ci.ServiceID&~0x0187) << 16) | 0x000F;
				return true;
			}
			*pNtsID = (static_cast<DWORD>(ci.ServiceID) << 16) | ci.NetworkID;
			return true;
		}
		*pNtsID = 0;
		return true;
	}
	return false;
}

// 再生中のストリームのTOT時刻(取得からの経過時間で補正済み)をUTCで取得する
LONGLONG CNicoJK::GetCurrentTot()
{
	lock_recursive_mutex lock(streamLock_);
	DWORD tick = GetTickCount();
	if (llftTot_ < 0) {
		// TOTを取得できていない
		return -1;
	} else if (tick - pcrTick_ >= 2000) {
		// 2秒以上PCRを取得できていない→ポーズ中?
		return llftTot_ - s_.defaultPlaybackDelay * FILETIME_MILLISECOND;
	} else if (llftTotLast_ < 0) {
		// 再生速度は分からない
		return llftTot_ + (static_cast<int>(tick - totTick_) - s_.defaultPlaybackDelay) * FILETIME_MILLISECOND;
	} else {
		DWORD delta = totTick_ - totTickLast_;
		// 再生速度(10%～1000%)
		LONGLONG speed = !delta ? FILETIME_MILLISECOND : (llftTot_ - llftTotLast_) / delta;
		speed = min(max(speed, FILETIME_MILLISECOND / 10), FILETIME_MILLISECOND * 10);
		return llftTot_ + (static_cast<int>(tick - totTick_) - s_.defaultPlaybackDelay) * speed;
	}
}

// 現在のBonDriverが':'区切りのリストに含まれるかどうか調べる
bool CNicoJK::IsMatchDriverName(LPCTSTR drivers)
{
	std::vector<TCHAR> path(m_pApp->GetDriverName(nullptr, 0) + 1);
	m_pApp->GetDriverName(path.data(), static_cast<int>(path.size()));
	LPCTSTR name = path.data() + _tcslen(path.data());
	size_t len = 0;
	for (; name != path.data() && !_tcschr(TEXT("/\\"), name[-1]); --name, ++len);
	if (len > 0) {
		for (LPCTSTR p = drivers; *p; ++p) {
			if ((p == drivers || p[-1] == TEXT(':')) && !_tcsnicmp(p, name, len) && (!p[len] || p[len] == TEXT(':'))) {
				return true;
			}
		}
	}
	return false;
}

// 指定した実況IDのログファイルに書き込む
// jkIDが負値のときはログファイルを閉じる
void CNicoJK::WriteToLogfile(int jkID, const char *text)
{
	if (s_.logfileFolder.empty() || s_.logfileMode == 0 || s_.logfileMode == 1 && !bRecording_) {
		// ログを記録しない
		jkID = -1;
	}
	if (currentLogfileJK_ >= 0 && currentLogfileJK_ != jkID) {
		// 閉じる
		CloseHandle(hLogfile_);
		CloseHandle(hLogfileLock_);
		// ロックファイルを削除
		TCHAR name[64];
		_stprintf_s(name, TEXT("\\jk%d\\lockfile"), currentLogfileJK_);
		DeleteFile((s_.logfileFolder + name).c_str());
		currentLogfileJK_ = -1;
		OutputMessageLog(TEXT("ログファイルの書き込みを終了しました。"));
	}
	if (currentLogfileJK_ < 0 && jkID >= 0) {
		unsigned int tm;
		TCHAR name[64];
		_stprintf_s(name, TEXT("\\jk%d"), jkID);
		tstring path = s_.logfileFolder + name;
		if (CLogReader::GetChatDate(&tm, text) &&
		    (GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES || CreateDirectory(path.c_str(), nullptr))) {
			// ロックファイルを開く
			path += TEXT("\\lockfile");
			hLogfileLock_ = CreateFile(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hLogfileLock_ != INVALID_HANDLE_VALUE) {
				// 開く
				_stprintf_s(name, TEXT("\\jk%d\\%010u.txt"), jkID, tm);
				hLogfile_ = CreateFile((s_.logfileFolder + name).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hLogfile_ != INVALID_HANDLE_VALUE) {
					// ヘッダを書き込む(別に無くてもいい)
					FILETIME ft, ftUtc = LongLongToFileTime(UnixTimeToFileTime(tm));
					FileTimeToLocalFileTime(&ftUtc, &ft);
					SYSTEMTIME st;
					FileTimeToSystemTime(&ft, &st);
					char header[128];
					int len = sprintf_s(header, "<!-- NicoJK logfile from %04d-%02d-%02dT%02d:%02d:%02d -->\r\n",
					                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
					DWORD written;
					WriteFile(hLogfile_, header, len, &written, nullptr);
					currentLogfileJK_ = jkID;
					OutputMessageLog((tstring(TEXT("ログ\"")) + &name[1] + TEXT("\"の書き込みを開始しました。")).c_str());
				} else {
					CloseHandle(hLogfileLock_);
					DeleteFile(path.c_str());
				}
			}
		}
	}
	// 開いてたら書き込む
	if (currentLogfileJK_ >= 0) {
		DWORD written;
		WriteFile(hLogfile_, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
		WriteFile(hLogfile_, "\r\n", 2, &written, nullptr);
	}
}

static inline int CounterDiff(DWORD a, DWORD b)
{
	return (a - b) & 0x80000000 ? -static_cast<int>(b - a - 1) - 1 : static_cast<int>(a - b);
}

bool CNicoJK::ReadFromLogfile(int jkID, const char **text, unsigned int tmToRead)
{
	return logReader_.Read(jkID, [this](LPCTSTR message) {
		static const TCHAR started[] = TEXT("Started reading logfile: ");
		if (!_tcsncmp(message, started, _tcslen(started))) {
			TCHAR log[256];
			_stprintf_s(log, TEXT("ログ\"%.191s\"の読み込みを開始しました。"), message + _tcslen(started));
			OutputMessageLog(log);
		} else if (!_tcscmp(message, TEXT("Closed logfile."))) {
			OutputMessageLog(TEXT("ログファイルの読み込みを終了しました。"));
		} else {
			OutputMessageLog(message);
		}
	}, text, tmToRead);
}

static int GetWindowHeight(HWND hwnd)
{
	RECT rc;
	return hwnd && GetWindowRect(hwnd, &rc) ? rc.bottom - rc.top : 0;
}

// イベントコールバック関数
// 何かイベントが起きると呼ばれる
LRESULT CALLBACK CNicoJK::EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData)
{
	static_cast<void>(lParam2);
	CNicoJK *pThis = static_cast<CNicoJK*>(pClientData);
	switch (Event) {
	case TVTest::EVENT_PLUGINENABLE:
		// プラグインの有効状態が変化した
		{
			pThis->TogglePlugin(lParam1 != 0);
			// パネルの有効状態は(hForce_の有無にかかわらず)常にTVTestのプラグイン状態フラグに合わせる
			TVTest::PanelItemSetInfo psi;
			psi.Size = sizeof(psi);
			psi.Mask = TVTest::PANEL_ITEM_SET_INFO_MASK_STATE;
			psi.ID = 1;
			psi.StateMask = TVTest::PANEL_ITEM_STATE_ENABLED | (pThis->hPanel_ ? TVTest::PANEL_ITEM_STATE_ACTIVE : 0);
			psi.State = lParam1 ? psi.StateMask : 0;
			pThis->m_pApp->SetPanelItem(&psi);
		}
		return TRUE;
	case TVTest::EVENT_PANELITEM_NOTIFY:
		// パネル項目の通知
		{
			TVTest::PanelItemEventInfo *pei = reinterpret_cast<TVTest::PanelItemEventInfo*>(lParam1);
			switch (pei->Event) {
			case TVTest::PANEL_ITEM_EVENT_CREATE:
				{
					TVTest::PanelItemCreateEventInfo *pcei = reinterpret_cast<TVTest::PanelItemCreateEventInfo*>(lParam1);
					pThis->hPanel_ = CreateWindowEx(0, TEXT("ru.jk.panel"), nullptr, WS_CHILD | WS_VISIBLE,
					                                pcei->ItemRect.left, pcei->ItemRect.top,
					                                pcei->ItemRect.right - pcei->ItemRect.left, pcei->ItemRect.bottom - pcei->ItemRect.top,
					                                pcei->hwndParent, nullptr, g_hinstDLL, pThis);
					if (pThis->hPanel_) {
						pcei->hwndItem = pThis->hPanel_;
						// このイベントはTVTest::EVENT_PLUGINENABLEよりも遅れるため
						pThis->TogglePlugin(pThis->m_pApp->IsPluginEnabled());
						return TRUE;
					}
				}
				return FALSE;
			}
		}
		break;
	case TVTest::EVENT_COLORCHANGE:
		// 色の設定が変化した
		if (pThis->hPanel_ && pThis->hForce_) {
			pThis->panelColor_.SetDelaySetColorFlag();
			InvalidateRect(pThis->hForce_, nullptr, TRUE);
		}
		break;
	case TVTest::EVENT_RECORDSTATUSCHANGE:
		// 録画状態が変化した
		pThis->bRecording_ = lParam1 != TVTest::RECORD_STATUS_NOTRECORDING;
		break;
	case TVTest::EVENT_FULLSCREENCHANGE:
		// 全画面表示状態が変化した
		if (pThis->hForce_) {
			// オーナーが変わるのでコメントウィンドウを作りなおす
			pThis->commentWindow_.Destroy();
			if (pThis->commentWindow_.GetOpacity() != 0 && pThis->m_pApp->GetPreview()) {
				HWND hwnd = pThis->FindVideoContainer();
				pThis->commentWindow_.Create(hwnd);
				pThis->bHalfSkip_ = GetWindowHeight(hwnd) >= pThis->s_.halfSkipThreshold;
			}
			// 全画面遷移時は隠れたほうが使い勝手がいいので呼ばない
			if (pThis->hPanelPopup_ || !lParam1) {
				PostMessage(pThis->hForce_, WM_SET_ZORDER, 0, 0);
			}
		}
		break;
	case TVTest::EVENT_PREVIEWCHANGE:
		// プレビュー表示状態が変化した
		if (pThis->hForce_) {
			if (pThis->commentWindow_.GetOpacity() != 0 && lParam1 != 0) {
				HWND hwnd = pThis->FindVideoContainer();
				pThis->commentWindow_.Create(hwnd);
				pThis->bHalfSkip_ = GetWindowHeight(hwnd) >= pThis->s_.halfSkipThreshold;
				pThis->ProcessChatTag("<!--<chat date=\"0\" mail=\"cyan ue\" user_id=\"-\">(NicoJK ON)</chat>-->");
			} else {
				pThis->commentWindow_.Destroy();
			}
		}
		break;
	case TVTest::EVENT_DRIVERCHANGE:
		// ドライバが変更された
		if (pThis->hForce_) {
			pThis->bUsingLogfileDriver_ = pThis->IsMatchDriverName(pThis->s_.logfileDrivers.c_str());
			pThis->ResetForceListEpgInfo();
		}
		// FALL THROUGH!
	case TVTest::EVENT_CHANNELCHANGE:
		// チャンネルが変更された
		if (pThis->hForce_) {
			PostMessage(pThis->hForce_, WM_RESET_STREAM, 0, 0);
		}
		// FALL THROUGH!
	case TVTest::EVENT_SERVICECHANGE:
		// サービスが変更された
		if (pThis->hForce_) {
			// 重複やザッピング対策のためタイマで呼ぶ
			SetTimer(pThis->hForce_, TIMER_SETUP_CURJK, SETUP_CURJK_DELAY, nullptr);
		}
		break;
	case TVTest::EVENT_SERVICEUPDATE:
		// サービスの構成が変化した(再生ファイルを切り替えたときなど)
		if (pThis->hForce_) {
			// ユーザの自発的なチャンネル変更(EVENT_CHANNELCHANGE)を捉えるのが原則だが
			// 非チューナ系のBonDriverだとこれでは不十分なため
			if (pThis->IsMatchDriverName(pThis->s_.nonTunerDrivers.c_str())) {
				SetTimer(pThis->hForce_, TIMER_SETUP_CURJK, SETUP_CURJK_DELAY, nullptr);
			}
		}
		break;
	case TVTest::EVENT_COMMAND:
		// コマンドが選択された
		if (pThis->hForce_) {
			switch (lParam1) {
			case COMMAND_HIDE_FORCE:
				if (IsWindowVisible(pThis->hForce_)) {
					ShowWindow(pThis->hForce_, SW_HIDE);
				} else {
					ShowWindow(pThis->hForce_, SW_SHOWNA);
				}
				SendMessage(pThis->hForce_, WM_UPDATE_LIST, TRUE, 0);
				SendMessage(pThis->hForce_, WM_SET_ZORDER, 0, 0);
				PostMessage(pThis->hForce_, WM_TIMER, TIMER_UPDATE, 0);
				break;
			case COMMAND_HIDE_COMMENT:
				pThis->SetOpacity(pThis->hForce_, -1);
				SendDlgItemMessage(pThis->hForce_, IDC_SLIDER_OPACITY, TBM_SETPOS, TRUE, (pThis->commentWindow_.GetOpacity() * 10 + 254) / 255);
				break;
			default:
				if (COMMAND_FORWARD_A <= lParam1 && lParam1 < COMMAND_FORWARD_A + _countof(pThis->s_.forwardList)) {
					int forward = pThis->s_.forwardList[lParam1 - COMMAND_FORWARD_A];
					if (forward == 0) {
						pThis->forwardOffsetDelta_ = -pThis->forwardOffset_;
					} else {
						pThis->forwardOffsetDelta_ += forward;
					}
				}
				break;
			}
		}
		break;
	case TVTest::EVENT_FILTERGRAPH_INITIALIZED:
		// フィルタグラフの初期化終了
		pThis->commentWindow_.OnFilterGraphInitialized(reinterpret_cast<const TVTest::FilterGraphInfo*>(lParam1)->pGraphBuilder);
		break;
	case TVTest::EVENT_FILTERGRAPH_FINALIZE:
		// フィルタグラフの終了処理開始
		pThis->commentWindow_.OnFilterGraphFinalize(reinterpret_cast<const TVTest::FilterGraphInfo*>(lParam1)->pGraphBuilder);
		break;
	}
	return 0;
}

BOOL CALLBACK CNicoJK::WindowMsgCallback(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pResult, void *pUserData)
{
	static_cast<void>(hwnd);
	static_cast<void>(lParam);
	static_cast<void>(pResult);
	CNicoJK *pThis = static_cast<CNicoJK*>(pUserData);
	switch (uMsg) {
	case WM_ACTIVATE:
		if (LOWORD(wParam) != WA_INACTIVE) {
			SendMessage(pThis->hForce_, WM_SET_ZORDER, 0, 0);
		}
		break;
	case WM_WINDOWPOSCHANGED:
		// WM_ACTIVATEされないZオーダーの変化を捉える。フルスクリーンでもなぜか送られてくるので注意
		SetTimer(pThis->hForce_, TIMER_DONE_POSCHANGE, 1000, nullptr);
		break;
	case WM_MOVE:
		pThis->commentWindow_.OnParentMove();
		// 実際に捉えたいVideo Containerウィンドウの変化はすこし遅れるため
		SetTimer(pThis->hForce_, TIMER_DONE_MOVE, 500, nullptr);
		break;
	case WM_SIZE:
		pThis->commentWindow_.OnParentSize();
		SetTimer(pThis->hForce_, TIMER_DONE_SIZE, 500, nullptr);
		break;
	case WM_DROPFILES:
		if (pThis->s_.dropLogfileMode == 0) {
			break;
		}
		if (pThis->m_pApp->GetFullscreen()) {
			// ファイルダイアログ等でのD&Dを無視するため(確実ではない)
			HWND hwndActive = GetActiveWindow();
			if (hwndActive && (GetWindowLong(GetAncestor(hwndActive, GA_ROOT), GWL_EXSTYLE) & WS_EX_DLGMODALFRAME) != 0) {
				break;
			}
		}
		// 読み込み可能な拡張子をもつ最初にみつかったファイルを開く
		pThis->dropFileTimeout_ = 0;
		for (UINT i = DragQueryFile(reinterpret_cast<HDROP>(wParam), 0xFFFFFFFF, nullptr, 0); i != 0; --i) {
			std::vector<TCHAR> buf(DragQueryFile(reinterpret_cast<HDROP>(wParam), i - 1, nullptr, 0) + 1);
			if (DragQueryFile(reinterpret_cast<HDROP>(wParam), i - 1, buf.data(), (UINT)buf.size())) {
				pThis->dropFileName_ = buf.data();
				if (pThis->dropFileName_.size() >= 5 && !_tcschr(TEXT("/\\"), *(pThis->dropFileName_.end() - 5)) &&
				    (!_tcsicmp(&pThis->dropFileName_.c_str()[pThis->dropFileName_.size() - 4], TEXT(".jkl")) ||
				     !_tcsicmp(&pThis->dropFileName_.c_str()[pThis->dropFileName_.size() - 4], TEXT(".xml")) ||
				     !_tcsicmp(&pThis->dropFileName_.c_str()[pThis->dropFileName_.size() - 4], TEXT(".txt")))) {
					if (pThis->bSpecFile_) {
						pThis->ReadFromLogfile(-1);
						DeleteFile(pThis->tmpSpecFileName_.c_str());
						pThis->bSpecFile_ = false;
					}
					SendDlgItemMessage(pThis->hForce_, IDC_CHECK_SPECFILE, BM_SETCHECK, BST_UNCHECKED, 0);
					if (pThis->s_.dropLogfileMode == 2) {
						// ウィンドウの左右どちらにD&DされたかでRelチェックボックスを変える
						RECT rc;
						HWND hwndFull = pThis->GetFullscreenWindow();
						GetClientRect(hwndFull ? hwndFull : pThis->m_pApp->GetAppWindow(), &rc);
						POINT pt = {};
						DragQueryPoint(reinterpret_cast<HDROP>(wParam), &pt);
						SendDlgItemMessage(pThis->hForce_, IDC_CHECK_RELATIVE, BM_SETCHECK, pt.x > rc.right / 2 ? BST_CHECKED : BST_UNCHECKED, 0);
					}
					bool bRel = SendDlgItemMessage(pThis->hForce_, IDC_CHECK_RELATIVE, BM_GETCHECK, 0, 0) == BST_CHECKED;
					pThis->dropFileTimeout_ = 10;
					SetTimer(pThis->hForce_, TIMER_OPEN_DROPFILE, bRel ? 2000 : 0, nullptr);
					break;
				}
			}
		}
		// DragFinish()せずに本体のデフォルトプロシージャに任せる
		break;
	}
	return FALSE;
}

static int GetBrightness(COLORREF cr)
{
	return 3 * GetRValue(cr) + 6 * GetGValue(cr) + GetBValue(cr);
}

static COLORREF GetForceColor(int force)
{
	if (force <= 0) return RGB(0x80, 0x80, 0x80);// 灰色
	if (force <= 50) return RGB(0x00, 0x80, 0x00);//緑
	if (force <= 100) return RGB(0x00, 0x80, 0xFF);//青
	if (force <= 200) return RGB(0xFF, 0x80, 0x00);//オレンジ
	return RGB(0xFF, 0x00, 0x00);//赤
}

// コメント(chatタグ)1行を解釈してコメントウィンドウに送る
bool CNicoJK::ProcessChatTag(const char *tag, bool bShow, int showDelay, bool *pbRefuge)
{
	static const std::regex reChat("<chat(?= )(.*)>(.*?)</chat>");
	static const std::regex reMail(" mail=\"(.*?)\"");
	static const std::regex reAbone(" abone=\"1\"");
	static const std::regex reYourpost(" yourpost=\"1\"");
	static const std::regex reInsertAt(" insert_at=\"last\"");
	static const std::regex reAlign(" align=\"(left|right)");
	static const std::regex reUserID(" user_id=\"([0-9A-Za-z\\-_:]{0,27})");
	static const std::regex reNo(" no=\"(\\d+)\"");
	static const std::regex reLogcmd(" logcmd=\"(.*?)\"");
	static const std::regex reRefuge(" nx_jikkyo=\"1\"| x_refuge=\"1\"");
	// 置換
	std::string rpl[2];
	if (!rplList_.empty()) {
		rpl[1] = tag;
		int i = 0;
		for (auto it = rplList_.cbegin(); it != rplList_.end(); ++it) {
			if (it->IsEnabled()) {
				try {
					rpl[i % 2] = std::regex_replace(rpl[(i + 1) % 2], it->re, it->fmt);
				} catch (std::regex_error&) {
					// 置換フォーマット異常のため無視する
					continue;
				}
				tag = rpl[i++ % 2].c_str();
			}
		}
	}
	std::cmatch m, mm;
	unsigned int tm;
	if (std::regex_match(tag, m, reChat) && CLogReader::GetChatDate(&tm, tag)) {
		TCHAR text[CHAT_TEXT_MAX];
		int len = MultiByteToWideChar(CP_UTF8, 0, m[2].first, static_cast<int>(m[2].length()), text, _countof(text) - 1);
		text[len] = TEXT('\0');
		DecodeEntityReference(text);
		// mail属性は無いときもある
		char mail[256];
		mail[0] = '\0';
		if (std::regex_search(m[1].first, m[1].second, mm, reMail)) {
			strncpy_s(mail, mm[1].first, min(static_cast<size_t>(mm[1].length()), _countof(mail) - 1));
		}
		// nx_jikkyo|x_refuge属性(有志の避難所等による拡張)
		bool bRefuge = std::regex_search(m[1].first, m[1].second, reRefuge);
		if (pbRefuge) {
			*pbRefuge = bRefuge;
		}
		// abone属性(ローカル拡張)
		bool bAbone = std::regex_search(m[1].first, m[1].second, reAbone);
		if (bShow && !bAbone) {
			bool bYourpost = std::regex_search(m[1].first, m[1].second, reYourpost);
			// insert_at属性(ローカル拡張)
			bool bInsertLast = std::regex_search(m[1].first, m[1].second, reInsertAt);
			// align属性(ローカル拡張)
			CCommentWindow::CHAT_ALIGN align = CCommentWindow::CHAT_ALIGN_CENTER;
			if (std::regex_search(m[1].first, m[1].second, mm, reAlign)) {
				align = mm[1].first[0] == 'l' ? CCommentWindow::CHAT_ALIGN_LEFT : CCommentWindow::CHAT_ALIGN_RIGHT;
			}
			COLORREF cr = GetColor(mail);
			// 暗色の影との輝度の差が小さいときは明色の影を使う
			COLORREF crShadow = bRefuge ? (GetBrightness(cr) - GetBrightness(s_.crRefugeDarkShadow) < 255 ? s_.crRefugeLightShadow : s_.crRefugeDarkShadow) :
			                              (GetBrightness(cr) - GetBrightness(s_.crNicoDarkShadow) < 255 ? s_.crNicoLightShadow : s_.crNicoDarkShadow);
			commentWindow_.AddChat(text, cr, crShadow, HasToken(mail, "shita") ? CCommentWindow::CHAT_POS_SHITA :
			                       HasToken(mail, "ue") ? CCommentWindow::CHAT_POS_UE : CCommentWindow::CHAT_POS_DEFAULT,
			                       HasToken(mail, "small") ? CCommentWindow::CHAT_SIZE_SMALL : CCommentWindow::CHAT_SIZE_DEFAULT,
			                       align, bInsertLast, bYourpost ? 160 : 0, showDelay);
		}

		// リストボックスのログ表示キューに追加
		LOG_ELEM e;
		FILETIME ft, ftUtc = LongLongToFileTime(UnixTimeToFileTime(tm));
		FileTimeToLocalFileTime(&ftUtc, &ft);
		FileTimeToSystemTime(&ft, &e.st);
		e.type = bRefuge ? (bShow ? LOG_ELEM_TYPE_REFUGE : LOG_ELEM_TYPE_REFUGE_HIDE) :
		                   (bShow ? LOG_ELEM_TYPE_DEFAULT : LOG_ELEM_TYPE_HIDE);
		e.no = 0;
		e.cr = RGB(0xFF, 0xFF, 0xFF);
		e.bAbone = bAbone;
		e.marker[0] = TEXT('\0');
		if (!bShow) {
			_tcscpy_s(e.marker, TEXT("."));
		} else if (std::regex_search(m[1].first, m[1].second, mm, reUserID)) {
			len = MultiByteToWideChar(CP_UTF8, 0, mm[1].first, static_cast<int>(mm[1].length()), e.marker, _countof(e.marker) - 1);
			e.marker[len] = TEXT('\0');
			if (std::regex_search(m[1].first, m[1].second, mm, reNo)) {
				e.no = strtol(mm[1].first, nullptr, 10);
			}
			// logcmd属性(ローカル拡張)
			char logcmd[256];
			if (std::regex_search(m[1].first, m[1].second, mm, reLogcmd)) {
				strncpy_s(logcmd, mm[1].first, min(static_cast<size_t>(mm[1].length()), _countof(logcmd) - 1));
				e.cr = GetColor(logcmd);
			}
		}
		e.text = text;
		logList_.push_back(std::move(e));
		return true;
	}
	return false;
}

// ログウィンドウにユーザへのメッセージログを出す
void CNicoJK::OutputMessageLog(LPCTSTR text)
{
	// リストボックスのログ表示キューに追加
	LOG_ELEM e;
	GetLocalTime(&e.st);
	e.type = LOG_ELEM_TYPE_MESSAGE;
	e.no = 0;
	e.cr = RGB(0xFF, 0xFF, 0xFF);
	e.bAbone = false;
	_tcscpy_s(e.marker, TEXT("#"));
	e.text = text;
	logList_.push_back(std::move(e));
	if (hForce_) {
		SendMessage(hForce_, WM_UPDATE_LIST, FALSE, 0);
	}
}


static LPCTSTR GetLocalCommandHelpText()
{
	return
		TEXT("@help\tヘルプを表示")
		TEXT("\r\n@sw\t投稿先を切り替える。")
		TEXT("\r\n@fopa N\t勢い窓の透過レベル1～10(Nを省略すると10)。")
		TEXT("\r\n@mask N\tログの時間(ID)部の省略マスク(Nを省略すると0)。")
		TEXT("\r\n@opa N\tコメントの透過レベル0～10(Nを省略すると10)。")
		TEXT("\r\n@fwd N\tコメントをNミリ秒だけ前進")
		TEXT("\r\n@fwds N\tコメントをN秒だけ前進")
		TEXT("\r\n@jmp yyMMddHHmm\tコメントを指定の年月日時分に移動")
		TEXT("\r\n@size N\tコメントの文字サイズをN%にする(Nを省略すると100%)。")
		TEXT("\r\n@speed N\tコメントの速度をN%にする(Nを省略すると100%)。")
		TEXT("\r\n@rl\t置換リストのすべてのCommentをリストする")
		TEXT("\r\n@rr\t置換リストを設定ファイルから再読み込みする")
		TEXT("\r\n@ra N\tPatternN0～N9を有効にする")
		TEXT("\r\n@rm N\tPatternN0～N9を無効にする")
		TEXT("\r\n@login\tニコニコログイン画面を表示")
		TEXT("\r\n@debug N\tデバッグ0～15");
}

void CNicoJK::ShowLocalCommandHelp()
{
	if (!hHelpWindow_) {
		RECT rc = {};
		if (hForce_) {
			GetWindowRect(hForce_, &rc);
		}
		int x = rc.left ? rc.left + 32 : CW_USEDEFAULT;
		int y = rc.top ? rc.top + 32 : CW_USEDEFAULT;
		int w = 520;
		int h = 320;
		hHelpWindow_ = CreateWindowEx(WS_EX_TOOLWINDOW, TEXT("ru.jk.help"), TEXT("NicoJK - ローカルコマンド"),
		                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
		                              x, y, w, h, hForce_, nullptr, g_hinstDLL, this);
	}
	if (hHelpWindow_) {
		if (hHelpEdit_) {
			SetWindowText(hHelpEdit_, GetLocalCommandHelpText());
			UpdateWindowTheme();
		}
		ShowWindow(hHelpWindow_, SW_SHOWNORMAL);
		SetForegroundWindow(hHelpWindow_);
	}
}

void CNicoJK::ShowNicoLoginWindow()
{
	if (!hLoginWindow_) {
		RECT rc = {};
		if (hForce_) {
			GetWindowRect(hForce_, &rc);
		}
		int x = rc.left ? rc.left + 48 : CW_USEDEFAULT;
		int y = rc.top ? rc.top + 48 : CW_USEDEFAULT;
		int w = 420;
		int h = 220;
		hLoginWindow_ = CreateWindowEx(WS_EX_TOOLWINDOW, TEXT("ru.jk.login"), TEXT("NicoJK - ニコニコログイン"),
		                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
		                               x, y, w, h, hForce_, nullptr, g_hinstDLL, this);
		if (hLoginWindow_) {
			UpdateWindowTheme(nullptr);
		}
	}
	if (hLoginWindow_) {
		UpdateNicoLoginWindowState();
		ShowWindow(hLoginWindow_, SW_SHOWNORMAL);
		SetForegroundWindow(hLoginWindow_);
		RequestJkcnslLoginSettings();
		if (hLoginMailEdit_ && loginState_ == LOGIN_STATE_IDLE) {
			SetFocus(hLoginMailEdit_);
		}
	}
}

void CNicoJK::ShowCommentWindow()
{
	if (pLogWV2_ && logWV2Ready_)
		pLogWV2_->PostWebMessageAsString(L"{\"cmd\":\"focus_input\"}");
}

void CNicoJK::UpdateNicoLoginWindowState(LPCTSTR status)
{
	if (!hLoginWindow_) {
		return;
	}
	if (status && hLoginStatus_) {
		SetWindowText(hLoginStatus_, status);
	} else if (hLoginStatus_) {
		switch (loginState_) {
		case LOGIN_STATE_SET_MAIL:
		case LOGIN_STATE_SET_PASSWORD:
			SetWindowText(hLoginStatus_, TEXT("ログイン情報をjkcnslに送信しています。"));
			break;
		case LOGIN_STATE_LOGIN:
			SetWindowText(hLoginStatus_, TEXT("jkcnslでログインしています。"));
			break;
		case LOGIN_STATE_WAIT_2FA:
			SetWindowText(hLoginStatus_, TEXT("2段階認証コードを入力してください。"));
			break;
		default:
			SetWindowText(hLoginStatus_, TEXT("メールアドレスとパスワードを入力してください。"));
			break;
		}
	}
	bool bWait2FA = loginState_ == LOGIN_STATE_WAIT_2FA;
	bool bBusy = loginState_ != LOGIN_STATE_IDLE;
	if (hLoginMailEdit_) {
		EnableWindow(hLoginMailEdit_, !bBusy);
	}
	if (hLoginPasswordEdit_) {
		EnableWindow(hLoginPasswordEdit_, !bBusy);
	}
	EnableWindow(GetDlgItem(hLoginWindow_, IDC_LOGIN_BUTTON_START), !bBusy);
	EnableWindow(GetDlgItem(hLoginWindow_, IDC_LOGIN_BUTTON_OTP), bWait2FA);
	EnableWindow(GetDlgItem(hLoginWindow_, IDC_LOGIN_BUTTON_CANCEL), bBusy);
	if (hLoginOtpEdit_) {
		EnableWindow(hLoginOtpEdit_, bWait2FA);
		if (bWait2FA) {
			SetFocus(hLoginOtpEdit_);
		}
	}
	InvalidateRect(hLoginWindow_, nullptr, TRUE);
}

void CNicoJK::RequestJkcnslLoginSettings()
{
	if (!hForce_ || !hLoginMailEdit_) {
		return;
	}
	if (bLoginSettingsQuerying_) {
		loginSettingsStream_.Close();
		loginSettingsBuf_.clear();
		bLoginSettingsQuerying_ = false;
	}
	if (hLoginLastLogin_) {
		SetWindowText(hLoginLastLogin_, TEXT("最終ログイン: 取得中..."));
	}
	loginSettingsStream_.Close();
	loginSettingsBuf_.clear();
	if (loginSettingsStream_.Send(hForce_, WMS_LOGIN_SETTINGS, 'S', "")) {
		bLoginSettingsQuerying_ = true;
	}
}

static std::string ToUtf8String(LPCTSTR text)
{
#ifdef UNICODE
	int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0) {
		return std::string();
	}
	std::string ret(len - 1, '\0');
	if (!ret.empty()) {
		WideCharToMultiByte(CP_UTF8, 0, text, -1, &ret[0], len, nullptr, nullptr);
	}
	return ret;
#else
	return text ? text : "";
#endif
}

static bool HasLineBreak(const std::string &text)
{
	return text.find_first_of("\r\n") != std::string::npos;
}

static bool FormatUnixTimeLocal(const char *text, LPTSTR out, size_t outSize)
{
	char *endp = nullptr;
	unsigned long long unixTime = strtoull(text, &endp, 10);
	if (endp == text || unixTime == 0) {
		return false;
	}
	const unsigned long long filetimeEpoch = 11644473600ULL;
	const unsigned long long filetimeSecond = 10000000ULL;
	unsigned long long ftValue = (unixTime + filetimeEpoch) * filetimeSecond;
	FILETIME ftUtc = {};
	ftUtc.dwLowDateTime = static_cast<DWORD>(ftValue);
	ftUtc.dwHighDateTime = static_cast<DWORD>(ftValue >> 32);
	FILETIME ftLocal = {};
	SYSTEMTIME st = {};
	if (!FileTimeToLocalFileTime(&ftUtc, &ftLocal) || !FileTimeToSystemTime(&ftLocal, &st)) {
		return false;
	}
	_stprintf_s(out, outSize, TEXT("最終ログイン: %04u/%02u/%02u %02u:%02u"),
	            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
	return true;
}

bool CNicoJK::StartJkcnslLogin(LPCTSTR mail, LPCTSTR password)
{
	if (!hForce_ || !mail || !password) {
		return false;
	}
	loginMail_ = ToUtf8String(mail);
	loginPassword_ = ToUtf8String(password);
	if (loginMail_.empty() || loginPassword_.empty() || HasLineBreak(loginMail_) || HasLineBreak(loginPassword_)) {
		UpdateNicoLoginWindowState(TEXT("ログイン情報が不正です。"));
		return false;
	}

	loginStream_.Close();
	loginBuf_.clear();
	loginState_ = LOGIN_STATE_SET_MAIL;
	std::string command = "mail " + loginMail_;
	if (!loginStream_.Send(hForce_, WMS_LOGIN, 'S', command.c_str())) {
		loginState_ = LOGIN_STATE_IDLE;
		UpdateNicoLoginWindowState(TEXT("jkcnslへのログイン設定送信に失敗しました。"));
		return false;
	}
	UpdateNicoLoginWindowState();
	return true;
}

bool CNicoJK::SendJkcnslLoginOtp(LPCTSTR otp)
{
	if (loginState_ != LOGIN_STATE_WAIT_2FA || !otp) {
		return false;
	}
	std::string otpUtf8 = ToUtf8String(otp);
	if (otpUtf8.empty() || HasLineBreak(otpUtf8)) {
		return false;
	}
	if (!loginStream_.Send(hForce_, WMS_LOGIN, '+', otpUtf8.c_str())) {
		UpdateNicoLoginWindowState(TEXT("2段階認証コードの送信に失敗しました。"));
		return false;
	}
	UpdateNicoLoginWindowState(TEXT("2段階認証コードを送信しました。"));
	return true;
}

bool CNicoJK::CancelJkcnslLogin()
{
	if (loginState_ == LOGIN_STATE_IDLE) {
		return false;
	}
	if (loginState_ == LOGIN_STATE_WAIT_2FA) {
		loginStream_.Shutdown();
	}
	loginState_ = LOGIN_STATE_IDLE;
	loginMail_.clear();
	loginPassword_.clear();
	loginBuf_.clear();
	UpdateNicoLoginWindowState(TEXT("ニコニコログインを中止しました。"));
	return true;
}

void CNicoJK::ProcessJkcnslLoginRecv()
{
	int ret = loginStream_.ProcessRecv(loginBuf_);
	if (!loginBuf_.empty()) {
		loginBuf_.push_back('\0');
		const char *p = loginBuf_.data();
		while (*p) {
			const char *pEnd = strchr(p, '\n');
			std::string line(p, pEnd ? pEnd : p + strlen(p));
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.find("2FA") != std::string::npos || line.find("one-time password") != std::string::npos) {
				loginState_ = LOGIN_STATE_WAIT_2FA;
				ShowNicoLoginWindow();
				UpdateNicoLoginWindowState(TEXT("2段階認証コードを入力してください。"));
			} else if (!line.empty()) {
				TCHAR text[256];
				int len = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, text, _countof(text) - 1);
				text[max(len, 0)] = TEXT('\0');
				OutputMessageLog(text);
			}
			if (!pEnd) {
				break;
			}
			p = pEnd + 1;
		}
		loginBuf_.clear();
	}
	if (ret >= 0) {
		return;
	}

	if (ret != -2) {
		loginState_ = LOGIN_STATE_IDLE;
		UpdateNicoLoginWindowState(TEXT("jkcnslログイン処理に失敗しました。"));
		return;
	}

	if (loginState_ == LOGIN_STATE_SET_MAIL) {
		loginState_ = LOGIN_STATE_SET_PASSWORD;
		std::string command = "password " + loginPassword_;
		if (!loginStream_.Send(hForce_, WMS_LOGIN, 'S', command.c_str())) {
			loginState_ = LOGIN_STATE_IDLE;
			UpdateNicoLoginWindowState(TEXT("jkcnslへのパスワード送信に失敗しました。"));
		} else {
			UpdateNicoLoginWindowState();
		}
	} else if (loginState_ == LOGIN_STATE_SET_PASSWORD) {
		loginPassword_.clear();
		loginState_ = LOGIN_STATE_LOGIN;
		if (!loginStream_.Send(hForce_, WMS_LOGIN, 'A', "i")) {
			loginState_ = LOGIN_STATE_IDLE;
			UpdateNicoLoginWindowState(TEXT("jkcnslログイン開始に失敗しました。"));
		} else {
			UpdateNicoLoginWindowState();
		}
	} else if (loginState_ == LOGIN_STATE_LOGIN || loginState_ == LOGIN_STATE_WAIT_2FA) {
		loginState_ = LOGIN_STATE_IDLE;
		UpdateNicoLoginWindowState(TEXT("ニコニコログインに成功しました。チャンネル切替または再接続後に反映されます。"));
		RequestJkcnslLoginSettings();
	} else {
		loginState_ = LOGIN_STATE_IDLE;
		UpdateNicoLoginWindowState();
	}
}

void CNicoJK::ProcessJkcnslLoginSettingsRecv()
{
	int ret = loginSettingsStream_.ProcessRecv(loginSettingsBuf_);
	if (!loginSettingsBuf_.empty()) {
		loginSettingsBuf_.push_back('\0');
		const char *p = loginSettingsBuf_.data();
		while (*p) {
			const char *pEnd = strchr(p, '\n');
			std::string line(p, pEnd ? pEnd : p + strlen(p));
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			static const char mailPrefix[] = "mail ";
			if (line.compare(0, sizeof(mailPrefix) - 1, mailPrefix) == 0) {
				const char *mail = line.c_str() + sizeof(mailPrefix) - 1;
				if (hLoginMailEdit_ && GetWindowTextLength(hLoginMailEdit_) == 0) {
#ifdef UNICODE
					TCHAR text[256];
					int len = MultiByteToWideChar(CP_UTF8, 0, mail, -1, text, _countof(text) - 1);
					text[max(len, 0)] = TEXT('\0');
					SetWindowText(hLoginMailEdit_, text);
#else
					SetWindowText(hLoginMailEdit_, mail);
#endif
				}
			}
			static const char lastLoginPrefix[] = "last_login_attempt ";
			if (line.compare(0, sizeof(lastLoginPrefix) - 1, lastLoginPrefix) == 0 && hLoginLastLogin_) {
				TCHAR text[64];
				if (FormatUnixTimeLocal(line.c_str() + sizeof(lastLoginPrefix) - 1, text, _countof(text))) {
					SetWindowText(hLoginLastLogin_, text);
				} else {
					SetWindowText(hLoginLastLogin_, TEXT("最終ログイン: 不明"));
				}
			}
			if (!pEnd) {
				break;
			}
			p = pEnd + 1;
		}
		loginSettingsBuf_.clear();
	}
	if (ret < 0) {
		bLoginSettingsQuerying_ = false;
	}
}

// コメント投稿欄のローカルコマンドを処理する
void CNicoJK::ProcessLocalPost(LPCTSTR comm)
{
	// パラメータ分割
	TCHAR cmd[16] = {};
	size_t cmdLen = _tcscspn(comm, TEXT(" "));
	_tcsncpy_s(cmd, _countof(cmd), comm, min(cmdLen, _countof(cmd) - 1));
	LPCTSTR arg = &comm[cmdLen] + _tcsspn(&comm[cmdLen], TEXT(" "));
	LPTSTR endp;
	LONGLONG llArg = _tcstoi64(arg, &endp, 10);
	if (endp == arg) {
		llArg = LLONG_MAX;
	}
	int nArg = static_cast<int>(min<LONGLONG>(max<LONGLONG>(llArg, INT_MIN), INT_MAX));
	if (!_tcsicmp(cmd, TEXT("help"))) {
		ShowLocalCommandHelp();
	} else if (!_tcsicmp(cmd, TEXT("login"))) {
		ShowNicoLoginWindow();
	} else if (!_tcsicmp(cmd, TEXT("sw"))) {
		if (s_.bRefugeMixing) {
			bPostToRefuge_ = !bPostToRefuge_;
			bPostToRefugeInverted_ = false;
			InvalidateRect(hForce_, nullptr, FALSE);
			ApplyLogWV2Theme();
		}
		TCHAR text[64];
		_stprintf_s(text, TEXT("現在の投稿先は%sです。"),
		            !bPostToRefuge_ ? TEXT("ニコニコ実況") :
		            s_.refugeUri.find("nx-jikkyo") != std::string::npos ? TEXT("NX-Jikkyo") : TEXT("避難所"));
		OutputMessageLog(text);
	} else if (!_tcsicmp(cmd, TEXT("fopa"))) {
		s_.forceOpacity = 0 < nArg && nArg < 10 ? nArg * 255 / 10 : 255;
		if (!hPanel_ || hPanelPopup_) {
			RestorePopupWindowOpacity(hPanelPopup_ ? hPanelPopup_ : hForce_);
		}
	} else if (!_tcsicmp(cmd, TEXT("mask"))) {
		s_.headerMask = 0 < nArg && nArg < INT_MAX ? nArg : 0;
		TCHAR text[64];
		_stprintf_s(text, TEXT("現在の省略マスクは%d(0x%04x)です。"), s_.headerMask, s_.headerMask);
		OutputMessageLog(text);
	} else if (!_tcsicmp(cmd, TEXT("opa"))) {
		int opa = 0 <= nArg && nArg < 10 ? nArg : 10;
		SendDlgItemMessage(hForce_, IDC_SLIDER_OPACITY, TBM_SETPOS, TRUE, opa);
		SendMessage(hForce_, WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, opa), reinterpret_cast<LPARAM>(GetDlgItem(hForce_, IDC_SLIDER_OPACITY)));
		TCHAR text[64];
		_stprintf_s(text, TEXT("現在の透過レベルは%dです。"), opa);
		OutputMessageLog(text);
	} else if ((!_tcsicmp(cmd, TEXT("fwd")) || !_tcsicmp(cmd, TEXT("fwds"))) && nArg != INT_MAX) {
		if (nArg == 0) {
			forwardOffsetDelta_ = -forwardOffset_;
		} else {
			forwardOffsetDelta_ += cmd[3] ? nArg * 1000LL : nArg;
		}
	} else if (!_tcsicmp(cmd, TEXT("jmp")) && llArg == LLONG_MAX) {
		// リセット
		forwardOffsetDelta_ = -forwardOffset_;
	} else if (!_tcsicmp(cmd, TEXT("jmp")) && 0 <= llArg && llArg < 10000000000) {
		LONGLONG llft = GetCurrentTot();
		FILETIME ft = LongLongToFileTime(llft);
		SYSTEMTIME st;
		if (llft >= 0 && FileTimeToSystemTime(&ft, &st)) {
			// 年の上位2桁を適当に補う
			LONGLONG adjustYear = st.wYear / 100 * 100 + llArg / 100000000;
			adjustYear += adjustYear - st.wYear > 50 ? -100 : adjustYear - st.wYear < -50 ? 100 : 0;
			st.wYear = static_cast<WORD>(adjustYear);
			st.wMonth = static_cast<WORD>(llArg / 1000000 % 100);
			st.wDay = static_cast<WORD>(llArg / 10000 % 100);
			st.wHour = static_cast<WORD>(llArg / 100 % 100);
			st.wMinute = static_cast<WORD>(llArg % 100);
			st.wSecond = 0;
			st.wMilliseconds = 0;
			FILETIME ftUtc;
			if (SystemTimeToFileTime(&st, &ft) && LocalFileTimeToFileTime(&ft, &ftUtc)) {
				forwardOffsetDelta_ = FileTimeToLongLong(ftUtc) / FILETIME_MILLISECOND - llft / FILETIME_MILLISECOND - forwardOffset_;
			} else {
				OutputMessageLog(TEXT("Error:引数の日時が不正です。"));
			}
		} else {
			OutputMessageLog(TEXT("Error:ストリームの時刻が不明です。"));
		}
	} else if (!_tcsicmp(cmd, TEXT("size"))) {
		int rate = min(max(nArg == INT_MAX ? 100 : nArg, 10), 1000);
		commentWindow_.SetCommentSize(s_.commentSize * rate / 100, s_.commentSizeMin, s_.commentSizeMax, s_.commentLineMargin);
		TCHAR text[64];
		_stprintf_s(text, TEXT("現在のコメントの文字サイズは%d%%です。"), rate);
		OutputMessageLog(text);
	} else if (!_tcsicmp(cmd, TEXT("speed"))) {
		commentWindow_.SetDisplayDuration(s_.commentDuration * 100 / (nArg <= 0 || nArg == INT_MAX ? 100 : nArg));
		TCHAR text[64];
		_stprintf_s(text, TEXT("現在のコメントの表示期間は%dmsecです。"), commentWindow_.GetDisplayDuration());
		OutputMessageLog(text);
	} else if (!_tcsicmp(cmd, TEXT("rl"))) {
		tstring text;
		for (auto it = rplList_.cbegin(); it != rplList_.end(); ++it) {
			if (!it->comment.empty() && it->section == TEXT("CustomReplace")) {
				TCHAR key[64];
				_stprintf_s(key, TEXT("%sPattern%d="), it->IsEnabled() ? TEXT("") : TEXT("#"), it->key);
				text += key + it->comment + TEXT('\n');
			}
		}
		MessageBox(hForce_, text.c_str(), TEXT("NicoJK - ローカルコマンド"), MB_OK);
	} else if (!_tcsicmp(cmd, TEXT("rr"))) {
		rplList_.clear();
		LoadRplListFromIni(TEXT("AutoReplace"), &rplList_);
		LoadRplListFromIni(TEXT("CustomReplace"), &rplList_);
		OutputMessageLog(TEXT("置換リストを再読み込みしました。"));
	} else if (!_tcsicmp(cmd, TEXT("ra")) || !_tcsicmp(cmd, TEXT("rm"))) {
		bool bFound = false;
		for (auto it = rplList_.begin(); it != rplList_.end(); ++it) {
			if (it->key / 10 == nArg && it->section == TEXT("CustomReplace")) {
				bFound = true;
				it->SetEnabled(cmd[1] == TEXT('a'));
				TCHAR key[64];
				_stprintf_s(key, TEXT("Pattern%d("), it->key);
				OutputMessageLog((key + it->comment + TEXT(")を") + (it->IsEnabled() ? TEXT('有') : TEXT('無')) + TEXT("効にしました。")).c_str());
			}
		}
		if (bFound) {
			SaveRplListToIni(TEXT("CustomReplace"), rplList_, false);
		} else {
			OutputMessageLog(TEXT("Error:パターンが見つかりません。"));
		}
	} else if (!_tcsicmp(cmd, TEXT("debug"))) {
		commentWindow_.SetDebugFlags(nArg);
	} else {
		OutputMessageLog(TEXT("Error:不明なローカルコマンドです。"));
	}
}

bool CNicoJK::BuildUserNGPattern(LPCTSTR marker, RPL_ELEM *pElem, tstring *pOldPattern)
{
	if (!marker || !marker[0]) {
		return false;
	}
	RPL_ELEM e;
	e.section = TEXT("AutoReplace");
	TCHAR pattern[128];
	// 20文字で切っているのは単に表現を短くするため。深い理由はない
	_stprintf_s(pattern, TEXT("s/^<chat(?=[^>]*? user_id=\"%.20s%s)/<chat abone=\"1\"/g"),
	            marker, _tcslen(marker) > 20 ? TEXT("") : TEXT("\""));
	if (!e.SetPattern(pattern)) {
		return false;
	}
	if (pElem) {
		*pElem = e;
	}
	if (pOldPattern) {
		_stprintf_s(pattern, TEXT("s/^<chat(?=.*? user_id=\"%.14s%s.*>.*<)/<chat abone=\"1\"/g"),
		            marker, _tcslen(marker) > 14 ? TEXT("") : TEXT("\""));
		*pOldPattern = pattern;
	}
	return true;
}

int CNicoJK::GetLogListNGState(int index)
{
	if (!bDisplayLogList_ || index < 0 || index >= static_cast<int>(logList_.size())) {
		return -1;
	}
	std::list<LOG_ELEM>::const_iterator it = logList_.begin();
	std::advance(it, index);
	if (it->type != LOG_ELEM_TYPE_DEFAULT && it->type != LOG_ELEM_TYPE_REFUGE) {
		return -1;
	}

	RPL_ELEM e;
	tstring oldPattern;
	if (!BuildUserNGPattern(it->marker, &e, &oldPattern)) {
		return -1;
	}
	return std::find_if(rplList_.cbegin(), rplList_.cend(), [&](const RPL_ELEM &a) {
		return a.section == TEXT("AutoReplace") && (a.pattern == e.pattern || a.pattern == oldPattern);
	}) != rplList_.cend() || it->bAbone ? 1 : 0;
}

void CNicoJK::ToggleLogListNG(int index)
{
	if (!bDisplayLogList_ || index < 0 || index >= static_cast<int>(logList_.size())) {
		return;
	}
	std::list<LOG_ELEM>::const_iterator it = logList_.begin();
	std::advance(it, index);
	if (it->type != LOG_ELEM_TYPE_DEFAULT && it->type != LOG_ELEM_TYPE_REFUGE) {
		return;
	}

	const tstring marker = it->marker;
	const int ngState = GetLogListNGState(index);
	if (ngState < 0) {
		return;
	}
	const bool bEnableNG = ngState == 0;

	RPL_ELEM e;
	tstring oldPattern;
	if (!BuildUserNGPattern(marker.c_str(), &e, &oldPattern)) {
		return;
	}

	// 既存パターンかどうか調べる
	std::vector<RPL_ELEM> autoRplList;
	LoadRplListFromIni(TEXT("AutoReplace"), &autoRplList);
	auto jt = std::find_if(autoRplList.begin(), autoRplList.end(), [&](const RPL_ELEM &a) { return a.pattern == e.pattern || a.pattern == oldPattern; });
	const bool bHasPattern = jt != autoRplList.end();

	if (bEnableNG && !bHasPattern) {
		autoRplList.push_back(e);
		while (static_cast<int>(autoRplList.size()) > max(s_.maxAutoReplace, 0)) {
			autoRplList.erase(autoRplList.begin());
		}
	}
	else if (!bEnableNG && bHasPattern) {
		autoRplList.erase(jt);
	}

	bool bChanged = false;
	for (int i = 0; i < static_cast<int>(autoRplList.size()); autoRplList[i].key = i, ++i);
	if (bEnableNG || bHasPattern) {
		SaveRplListToIni(TEXT("AutoReplace"), autoRplList);
		bChanged = true;
	}

	// 置換リストを更新
	rplList_ = autoRplList;
	LoadRplListFromIni(TEXT("CustomReplace"), &rplList_);

	for (auto kt = logList_.begin(); kt != logList_.end(); ++kt) {
		if (!_tcscmp(kt->marker, marker.c_str()) &&
		    (kt->type == LOG_ELEM_TYPE_DEFAULT || kt->type == LOG_ELEM_TYPE_REFUGE)) {
			if (kt->bAbone != bEnableNG) {
				kt->bAbone = bEnableNG;
				bChanged = true;
			}
		}
	}
	if (bChanged && hForce_) {
		if (logWV2Ready_) {
			SendLogWV2AboneUpdate(marker.c_str(), bEnableNG);
		} else {
			SendMessage(hForce_, WM_UPDATE_LIST, TRUE, 0);
		}
	}
}

static tstring FormatListBoxTextForCopy(LPCTSTR text)
{
	if (!text) {
		return tstring();
	}
	if (text[0] == TEXT('#')) {
		++text;
	}
	if (text[0] == TEXT('[')) {
		LPCTSTR pEnd = _tcschr(text + 1, TEXT(']'));
		if (pEnd) {
			text = pEnd + 1;
		}
	}

	tstring out;
	while (*text) {
		if (*text == TEXT('{')) {
			LPCTSTR pEnd = _tcschr(text + 1, TEXT('}'));
			if (pEnd) {
				size_t fixedLen = pEnd - (text + 1);
				LPCTSTR pDraw = pEnd + 1;
				if (_tcslen(pDraw) >= fixedLen) {
					out.append(pDraw, fixedLen);
					text = pDraw + fixedLen;
					continue;
				}
			}
		}
		out.push_back(*text++);
	}
	return out;
}

static bool CopyTextToClipboard(HWND hwnd, const tstring &text)
{
	if (!OpenClipboard(hwnd)) {
		return false;
	}
	EmptyClipboard();
	const size_t bytes = (text.size() + 1) * sizeof(TCHAR);
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (!hMem) {
		CloseClipboard();
		return false;
	}
	void *p = GlobalLock(hMem);
	if (!p) {
		GlobalFree(hMem);
		CloseClipboard();
		return false;
	}
	memcpy(p, text.c_str(), bytes);
	GlobalUnlock(hMem);
#ifdef UNICODE
	UINT format = CF_UNICODETEXT;
#else
	UINT format = CF_TEXT;
#endif
	if (!SetClipboardData(format, hMem)) {
		GlobalFree(hMem);
		CloseClipboard();
		return false;
	}
	CloseClipboard();
	return true;
}

// WebView2 エリアへのファイルドロップを受け取る IDropTarget 実装
class CNicoJKDropTarget : public IDropTarget {
	HWND hwndForce_;
	LONG refCount_;
	bool hasFiles_;
public:
	explicit CNicoJKDropTarget(HWND hwnd) : hwndForce_(hwnd), refCount_(1), hasFiles_(false) {}
	// IUnknown
	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount_); }
	ULONG STDMETHODCALLTYPE Release() override {
		LONG r = InterlockedDecrement(&refCount_); if (!r) delete this; return r;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
		if (riid == IID_IUnknown || riid == IID_IDropTarget) {
			*ppv = static_cast<IDropTarget*>(this); AddRef(); return S_OK;
		}
		*ppv = nullptr; return E_NOINTERFACE;
	}
	// IDropTarget
	HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDO, DWORD, POINTL, DWORD* pdwEffect) override {
		FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		hasFiles_ = (pDO->QueryGetData(&fmt) == S_OK);
		*pdwEffect = hasFiles_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* pdwEffect) override {
		*pdwEffect = hasFiles_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE DragLeave() override { hasFiles_ = false; return S_OK; }
	HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDO, DWORD, POINTL, DWORD* pdwEffect) override {
		FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		STGMEDIUM stg = {};
		*pdwEffect = DROPEFFECT_NONE;
		if (SUCCEEDED(pDO->GetData(&fmt, &stg))) {
			// HDROP の所有権を WM_DROPFILES に移転。DefWindowProc が DragFinish で解放する。
			SendMessage(hwndForce_, WM_DROPFILES, reinterpret_cast<WPARAM>(stg.hGlobal), 0);
			stg.hGlobal = nullptr; // 二重解放防止
			*pdwEffect = DROPEFFECT_COPY;
		}
		ReleaseStgMedium(&stg);
		hasFiles_ = false;
		return S_OK;
	}
};

// WebView2 内部の Chrome_WidgetWin_* HWND を探す
static BOOL CALLBACK FindWV2HwndEnum(HWND hwnd, LPARAM lParam) {
	TCHAR cls[64];
	if (GetClassName(hwnd, cls, _countof(cls)) && _tcsncmp(cls, TEXT("Chrome_Widget"), 13) == 0) {
		*reinterpret_cast<HWND*>(lParam) = hwnd;
		return FALSE;
	}
	return TRUE;
}


// サブクラス化した投稿欄のプロシージャ

// サブクラス化したボタンのプロシージャ
static LRESULT CALLBACK TVTestPanelButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_PAINT:
		{
			TVTest::CTVTestApp *pApp = reinterpret_cast<TVTest::CTVTestApp*>(GetProp(hwnd, TEXT("App")));
			int state = Button_GetState(hwnd);
			LPCWSTR style = state & BST_HOT ? L"control-panel.item.hot" : state & BST_CHECKED ? L"control-panel.item.checked" : L"control-panel.item";
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			RECT rc;
			GetClientRect(hwnd, &rc);
			pApp->ThemeDrawBackground(style, hdc, rc);
			TCHAR text[256];
			if (GetWindowText(hwnd, text, _countof(text))) {
				HFONT hFont = reinterpret_cast<HFONT>(SendMessage(hwnd, WM_GETFONT, 0, 0));
				HFONT hFontOld = nullptr;
				if (hFont) {
					hFontOld = SelectFont(hdc, hFont);
				}
				int oldBkMode = SetBkMode(hdc, TRANSPARENT);
				pApp->ThemeDrawText(style, hdc, text, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
				SetBkMode(hdc, oldBkMode);
				if (hFont) {
					SelectFont(hdc, hFontOld);
				}
			}
			EndPaint(hwnd, &ps);
		}
		return 0;
	}
	return CallWindowProc(reinterpret_cast<WNDPROC>(GetProp(hwnd, TEXT("DefProc"))), hwnd, uMsg, wParam, lParam);
}

static void SetTVTestPanelItem(HWND hButton, TVTest::CTVTestApp *pApp, LRESULT (CALLBACK *pProc)(HWND, UINT, WPARAM, LPARAM))
{
	SetProp(hButton, TEXT("App"), pApp);
	SetProp(hButton, TEXT("DefProc"), reinterpret_cast<HANDLE>(GetWindowLongPtr(hButton, GWLP_WNDPROC)));
	SetWindowLongPtr(hButton, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(pProc));
}

static void ResetTVTestPanelItem(HWND hButton)
{
	SetWindowLongPtr(hButton, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GetProp(hButton, TEXT("DefProc"))));
	RemoveProp(hButton, TEXT("DefProc"));
	RemoveProp(hButton, TEXT("App"));
}

void CNicoJK::RestorePopupWindowOpacity(HWND hwnd)
{
	LONG style = GetWindowLong(hwnd, GWL_EXSTYLE);
	SetWindowLong(hwnd, GWL_EXSTYLE, s_.forceOpacity == 255 ? style & ~WS_EX_LAYERED : style | WS_EX_LAYERED);
	SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(s_.forceOpacity), LWA_ALPHA);
}

void CNicoJK::RestorePopupWindowState(HWND hwnd)
{
	// 位置を復元
	HMONITOR hMon = MonitorFromRect(&s_.rcForce, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi;
	mi.cbSize = sizeof(MONITORINFO);
	if (s_.rcForce.right <= s_.rcForce.left || !GetMonitorInfo(hMon, &mi) ||
	    s_.rcForce.right < mi.rcMonitor.left + 20 || mi.rcMonitor.right - 20 < s_.rcForce.left ||
	    s_.rcForce.bottom < mi.rcMonitor.top + 20 || mi.rcMonitor.bottom - 20 < s_.rcForce.top) {
		GetWindowRect(hwnd, &s_.rcForce);
	}
	MoveWindow(hwnd, 0, 0, 64, 64, FALSE);
	MoveWindow(hwnd, s_.rcForce.left, s_.rcForce.top, s_.rcForce.right - s_.rcForce.left, s_.rcForce.bottom - s_.rcForce.top, FALSE);
	RestorePopupWindowOpacity(hwnd);
}

LRESULT CALLBACK CNicoJK::PanelWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CREATE) {
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<LPCREATESTRUCT>(lParam)->lpCreateParams));
	}
	CNicoJK *pThis = reinterpret_cast<CNicoJK*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (pThis) {
		switch (uMsg) {
		case WM_CREATE:
			SetProp(hwnd, TEXT("IsHide"), reinterpret_cast<HANDLE>('N'));
			SetTimer(hwnd, 1, 5000, nullptr);
			return 0;
		case WM_DESTROY:
			RemoveProp(hwnd, TEXT("IsHide"));
			pThis->hPanel_ = nullptr;
			break;
		case WM_TIMER:
			if (wParam == 1) {
				// パネルの表示非表示を捕捉する手段が思いつかないので苦肉の策
				bool bHide = pThis->hForce_ && !IsWindowVisible(pThis->hForce_);
				if (!bHide && GetProp(hwnd, TEXT("IsHide")) == reinterpret_cast<HANDLE>('Y')) {
					SetProp(hwnd, TEXT("IsHide"), reinterpret_cast<HANDLE>('N'));
					if (pThis->hForce_) {
						SendMessage(pThis->hForce_, WM_UPDATE_LIST, TRUE, 0);
						PostMessage(pThis->hForce_, WM_TIMER, TIMER_UPDATE, 0);
					}
				} else if (bHide && GetProp(hwnd, TEXT("IsHide")) == reinterpret_cast<HANDLE>('N')) {
					SetProp(hwnd, TEXT("IsHide"), reinterpret_cast<HANDLE>('Y'));
				}
			}
			break;
		case WM_SIZE:
			if (pThis->hForce_ && !pThis->hPanelPopup_) {
				MoveWindow(pThis->hForce_, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
			}
			break;
		default:
			PostMessage(hwnd, WM_TIMER, 1, 0);
			break;
		}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK CNicoJK::PanelPopupWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CREATE) {
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<LPCREATESTRUCT>(lParam)->lpCreateParams));
	}
	CNicoJK *pThis = reinterpret_cast<CNicoJK*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (pThis) {
		switch (uMsg) {
		case WM_CREATE:
			if (pThis->hForce_) {
				// パネルウィンドウから奪う
				if (!SetParent(pThis->hForce_, hwnd)) {
					// 失敗
					return -1;
				}
				PostMessage(pThis->hForce_, WM_SET_ZORDER, 0, 0);
			}
			ShowWindow(pThis->hPanel_, SW_HIDE);
			pThis->RestorePopupWindowState(hwnd);
			ShowWindow(hwnd, SW_SHOWNA);
			{
				if (!pThis->panelColor_.GetPanelBackBrush()) {
					pThis->panelColor_.SetColor(pThis->m_pApp);
				}
				BOOL bDarkBool = pThis->panelColor_.IsDark() ? TRUE : FALSE;
				::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDarkBool, sizeof(bDarkBool));
			}
			return 0;
		case WM_CLOSE:
			if (pThis->hForce_) {
				// パネルウィンドウに戻す
				if (!SetParent(pThis->hForce_, pThis->hPanel_)) {
					// 失敗
					return 0;
				}
				RECT rc;
				GetClientRect(pThis->hPanel_, &rc);
				MoveWindow(pThis->hForce_, 0, 0, rc.right, rc.bottom, TRUE);
			}
			break;
		case WM_DESTROY:
			// 位置を保存
			GetWindowRect(hwnd, &pThis->s_.rcForce);
			ShowWindow(pThis->hPanel_, SW_SHOWNA);
			pThis->hPanelPopup_ = nullptr;
			break;
		case WM_SIZE:
			if (pThis->hForce_) {
				MoveWindow(pThis->hForce_, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
			}
			break;
		}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK CNicoJK::ForceWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CREATE) {
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<LPCREATESTRUCT>(lParam)->lpCreateParams));
	}
	CNicoJK *pThis = reinterpret_cast<CNicoJK*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	return pThis ? pThis->ForceWindowProcMain(hwnd, uMsg, wParam, lParam) : DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK CNicoJK::HelpWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CREATE) {
		CNicoJK *pThis = reinterpret_cast<CNicoJK*>(reinterpret_cast<LPCREATESTRUCT>(lParam)->lpCreateParams);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
		if (pThis) {
			pThis->hHelpWindow_ = hwnd;

			pThis->hHelpEdit_ = CreateWindowEx(0/*WS_EX_CLIENTEDGE*/, TEXT("EDIT"), GetLocalCommandHelpText(),
			                                   WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
			                                   0, 0, 0, 0, hwnd, nullptr, g_hinstDLL, nullptr);


			if (pThis->hHelpEdit_) {
				HFONT hFont = pThis->hForceFont_ ? pThis->hForceFont_ : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
				SendMessage(pThis->hHelpEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
				int tabs = 72;
				SendMessage(pThis->hHelpEdit_, EM_SETTABSTOPS, 1, reinterpret_cast<LPARAM>(&tabs));
			}
		}
		return 0;
	}

	CNicoJK *pThis = reinterpret_cast<CNicoJK*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	switch (uMsg) {
	case WM_SIZE:
		if (pThis && pThis->hHelpEdit_) {
			MoveWindow(pThis->hHelpEdit_, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
		}
		return 0;
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
		if (pThis && reinterpret_cast<HWND>(lParam) == pThis->hHelpEdit_) {
			if (!pThis->panelColor_.GetPanelBackBrush()) {
				pThis->panelColor_.SetColor(pThis->m_pApp);
			}
			HDC hdc = reinterpret_cast<HDC>(wParam);
			SetTextColor(hdc, pThis->panelColor_.GetPanelText());
			SetBkColor(hdc, pThis->panelColor_.GetPanelBack());
			return reinterpret_cast<LRESULT>(pThis->panelColor_.GetPanelBackBrush());
		}
		break;
	case WM_DESTROY:
		if (pThis) {
			pThis->hHelpEdit_ = nullptr;
			pThis->hHelpWindow_ = nullptr;
		}
		break;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK CNicoJK::LoginWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CREATE) {
		CNicoJK *pThis = reinterpret_cast<CNicoJK*>(reinterpret_cast<LPCREATESTRUCT>(lParam)->lpCreateParams);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
		if (pThis) {
			pThis->hLoginWindow_ = hwnd;

			CreateWindowEx(0, TEXT("STATIC"), TEXT("メールアドレス"),
			               WS_CHILD | WS_VISIBLE | SS_LEFT,
			               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_LABEL_MAIL), g_hinstDLL, nullptr);
			pThis->hLoginMailEdit_ = CreateWindowEx(0, TEXT("EDIT"), nullptr,
			                                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
			                                        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_MAIL), g_hinstDLL, nullptr);
			CreateWindowEx(0, TEXT("STATIC"), TEXT("パスワード"),
			               WS_CHILD | WS_VISIBLE | SS_LEFT,
			               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_LABEL_PASSWORD), g_hinstDLL, nullptr);
			pThis->hLoginPasswordEdit_ = CreateWindowEx(0, TEXT("EDIT"), nullptr,
			                                            WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
			                                            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_PASSWORD), g_hinstDLL, nullptr);
			CreateWindowEx(0, TEXT("STATIC"), TEXT("2段階認証コード"),
			               WS_CHILD | WS_VISIBLE | SS_LEFT,
			               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_LABEL_OTP), g_hinstDLL, nullptr);
			pThis->hLoginOtpEdit_ = CreateWindowEx(/*WS_EX_CLIENTEDGE*/0, TEXT("EDIT"), nullptr,
			                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
			                                       0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_OTP), g_hinstDLL, nullptr);
			pThis->hLoginStatus_ = CreateWindowEx(0, TEXT("STATIC"), nullptr,
			                                      WS_CHILD | WS_VISIBLE | SS_LEFT,
			                                      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_STATUS), g_hinstDLL, nullptr);
			pThis->hLoginLastLogin_ = CreateWindowEx(0, TEXT("STATIC"), TEXT("最終ログイン: 未取得"),
			                                         WS_CHILD | WS_VISIBLE | SS_LEFT,
			                                         0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_LAST_LOGIN), g_hinstDLL, nullptr);
			CreateWindowEx(0, TEXT("BUTTON"), TEXT("ログイン"),
			               WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_BUTTON_START), g_hinstDLL, nullptr);
			CreateWindowEx(0, TEXT("BUTTON"), TEXT("認証"),
			               WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_BUTTON_OTP), g_hinstDLL, nullptr);
			CreateWindowEx(0, TEXT("BUTTON"), TEXT("キャンセル"),
			               WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOGIN_BUTTON_CANCEL), g_hinstDLL, nullptr);

			HFONT hFont = pThis->hForceFont_ ? pThis->hForceFont_ : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
			for (int id = IDC_LOGIN_MAIL; id <= IDC_LOGIN_LABEL_OTP; ++id) {
				HWND hItem = GetDlgItem(hwnd, id);
				if (hItem) {
					SendMessage(hItem, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
				}
			}
			int buttonIds[] = {IDC_LOGIN_BUTTON_START, IDC_LOGIN_BUTTON_OTP, IDC_LOGIN_BUTTON_CANCEL};
			for (int id : buttonIds) {
				HWND hButton = GetDlgItem(hwnd, id);
				if (hButton) {
					SetWindowSubclass(hButton, LoginButtonSubclassProc, 1, reinterpret_cast<DWORD_PTR>(pThis));
				}
			}
			pThis->UpdateNicoLoginWindowState();
		}
		return 0;
	}

	CNicoJK *pThis = reinterpret_cast<CNicoJK*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	auto layout = [hwnd, pThis]() {
		RECT rc = {};
		GetClientRect(hwnd, &rc);
		int dpi = pThis && pThis->m_pApp ? pThis->m_pApp->GetDPIFromWindow(hwnd) : 96;
		if (dpi == 0) {
			dpi = 96;
		}
		int margin = 12 * dpi / 96;
		int gap = 8 * dpi / 96;
		int labelW = 104 * dpi / 96;
		int editH = 22 * dpi / 96;
		int buttonW = 84 * dpi / 96;
		int buttonH = 26 * dpi / 96;
		int otpButtonW = 56 * dpi / 96;
		int y = margin;
		int editX = margin + labelW;
		int editW = max<int>(80, static_cast<int>(rc.right) - editX - margin);
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_LABEL_MAIL), margin, y + 3 * dpi / 96, labelW, editH, TRUE);
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_MAIL), editX, y, max(40, editW - buttonW - gap), editH, TRUE);
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_BUTTON_START), rc.right - margin - buttonW, y - 2 * dpi / 96, buttonW, buttonH, TRUE);
		y += editH + gap;
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_LABEL_PASSWORD), margin, y + 3 * dpi / 96, labelW, editH, TRUE);
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_PASSWORD), editX, y, editW, editH, TRUE);
		y += editH + gap;
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_LABEL_OTP), margin, y + 3 * dpi / 96, labelW, editH, TRUE);
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_OTP), editX, y, max(40, editW - otpButtonW * 2 - gap * 2), editH, TRUE);
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_BUTTON_OTP), rc.right - margin - otpButtonW * 2 - gap, y - 2 * dpi / 96, otpButtonW, buttonH, TRUE);
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_BUTTON_CANCEL), rc.right - margin - otpButtonW, y - 2 * dpi / 96, otpButtonW, buttonH, TRUE);
		y += editH + gap;
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_STATUS), margin, y, rc.right - margin * 2, editH, TRUE);
		y += editH + gap + gap;
		MoveWindow(GetDlgItem(hwnd, IDC_LOGIN_LAST_LOGIN), margin, y, rc.right - margin * 2, editH, TRUE);
	};

	switch (uMsg) {
	case WM_ERASEBKGND:
		if (pThis) {
			if (!pThis->panelColor_.GetPanelBackBrush()) {
				pThis->panelColor_.SetColor(pThis->m_pApp);
			}
			RECT rc = {};
			GetClientRect(hwnd, &rc);
			FillRect(reinterpret_cast<HDC>(wParam), &rc, pThis->panelColor_.GetPanelBackBrush());
			return TRUE;
		}
		break;
	case WM_SIZE:
		layout();
		return 0;
	case WM_COMMAND:
		if (!pThis) {
			break;
		}
		switch (LOWORD(wParam)) {
		case IDC_LOGIN_BUTTON_START:
			{
				TCHAR mail[256];
				TCHAR password[256];
				GetWindowText(pThis->hLoginMailEdit_, mail, _countof(mail));
				GetWindowText(pThis->hLoginPasswordEdit_, password, _countof(password));
				if (pThis->StartJkcnslLogin(mail, password)) {
					SetWindowText(pThis->hLoginPasswordEdit_, TEXT(""));
					SetWindowText(pThis->hLoginOtpEdit_, TEXT(""));
				}
			}
			return 0;
		case IDC_LOGIN_BUTTON_OTP:
			{
				TCHAR otp[64];
				GetWindowText(pThis->hLoginOtpEdit_, otp, _countof(otp));
				if (pThis->SendJkcnslLoginOtp(otp)) {
					SetWindowText(pThis->hLoginOtpEdit_, TEXT(""));
				}
			}
			return 0;
		case IDC_LOGIN_BUTTON_CANCEL:
			pThis->CancelJkcnslLogin();
			return 0;
		}
		break;
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
		if (pThis) {
			if (!pThis->panelColor_.GetPanelBackBrush()) {
				pThis->panelColor_.SetColor(pThis->m_pApp);
			}
			HDC hdc = reinterpret_cast<HDC>(wParam);
			SetTextColor(hdc, pThis->panelColor_.GetPanelText());
			SetBkColor(hdc, pThis->panelColor_.GetPanelBack());
			return reinterpret_cast<LRESULT>(pThis->panelColor_.GetPanelBackBrush());
		}
		break;
	case WM_DRAWITEM:
		{
			DRAWITEMSTRUCT *dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
			if (dis->CtlType != ODT_BUTTON || !pThis) {
				break;
			}
			if (!pThis->panelColor_.GetPanelBackBrush()) {
				pThis->panelColor_.SetColor(pThis->m_pApp);
			}
			bool bDisabled = (dis->itemState & ODS_DISABLED) != 0;
			bool bPressed  = (dis->itemState & ODS_SELECTED) != 0;
			bool bHot      = !bDisabled && GetProp(dis->hwndItem, TEXT("Hot")) != nullptr;
			if (pThis->panelColor_.IsDark()) {
				COLORREF crBase = pThis->panelColor_.GetPanelBack();
				auto ch = [](int v) -> BYTE { return static_cast<BYTE>(v < 0 ? 0 : v > 255 ? 255 : v); };
				auto brighter = [&ch](COLORREF cr, int n) {
					return RGB(ch(GetRValue(cr) + n), ch(GetGValue(cr) + n), ch(GetBValue(cr) + n));
				};
				COLORREF crBg     = bDisabled ? crBase : bPressed ? brighter(crBase, 10) : bHot ? brighter(crBase, 30) : brighter(crBase, 18);
				COLORREF crBorder = brighter(crBase, 50);
				COLORREF crText   = bDisabled ? brighter(crBase, 40) : pThis->panelColor_.GetPanelText();
				HBRUSH hBrush = CreateSolidBrush(crBg);
				FillRect(dis->hDC, &dis->rcItem, hBrush);
				DeleteObject(hBrush);
				HPEN hPen = CreatePen(PS_SOLID, 1, crBorder);
				HPEN hOldPen = reinterpret_cast<HPEN>(SelectObject(dis->hDC, hPen));
				HBRUSH hOldBrush = reinterpret_cast<HBRUSH>(SelectObject(dis->hDC, GetStockObject(NULL_BRUSH)));
				Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
				SelectObject(dis->hDC, hOldPen);
				SelectObject(dis->hDC, hOldBrush);
				DeleteObject(hPen);
				TCHAR szText[64];
				GetWindowText(dis->hwndItem, szText, _countof(szText));
				SetTextColor(dis->hDC, crText);
				SetBkMode(dis->hDC, TRANSPARENT);
				HFONT hFont = reinterpret_cast<HFONT>(SendMessage(dis->hwndItem, WM_GETFONT, 0, 0));
				HFONT hOldFont = hFont ? reinterpret_cast<HFONT>(SelectObject(dis->hDC, hFont)) : nullptr;
				DrawText(dis->hDC, szText, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				if (hOldFont) SelectObject(dis->hDC, hOldFont);
				return TRUE;
			}
			HTHEME hTheme = OpenThemeData(dis->hwndItem, L"BUTTON");
			if (hTheme) {
				int iStateId = PBS_NORMAL;
				if (bDisabled)     iStateId = PBS_DISABLED;
				else if (bPressed) iStateId = PBS_PRESSED;
				else if (bHot)     iStateId = PBS_HOT;
				TCHAR szText[64];
				GetWindowText(dis->hwndItem, szText, _countof(szText));
				DrawThemeBackground(hTheme, dis->hDC, BP_PUSHBUTTON, iStateId, &dis->rcItem, nullptr);
				DrawThemeText(hTheme, dis->hDC, BP_PUSHBUTTON, iStateId, szText, -1, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0, &dis->rcItem);
				CloseThemeData(hTheme);
				return TRUE;
			}
			break;
		}
	case WM_DESTROY:
		if (pThis) {
			pThis->hLoginWindow_ = nullptr;
			pThis->hLoginMailEdit_ = nullptr;
			pThis->hLoginPasswordEdit_ = nullptr;
			pThis->hLoginOtpEdit_ = nullptr;
			pThis->hLoginStatus_ = nullptr;
			pThis->hLoginLastLogin_ = nullptr;
		}
		break;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK CNicoJK::LoginButtonSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	CNicoJK *pThis = reinterpret_cast<CNicoJK*>(dwRefData);
	switch (uMsg) {
	case WM_ENABLE:
		// 無効状態への切替時に標準描画が先走るため、TVTest のダークモード対応と同じく再描画へ寄せる
		if (wParam == FALSE && pThis) {
			if (!pThis->panelColor_.GetPanelBackBrush()) {
				pThis->panelColor_.SetColor(pThis->m_pApp);
			}
			if (pThis->panelColor_.IsDark()) {
				SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
				LRESULT result = DefSubclassProc(hwnd, uMsg, wParam, lParam);
				SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
				InvalidateRect(hwnd, nullptr, TRUE);
				return result;
			}
		}
		break;

	case WM_MOUSEMOVE:
		if (!GetProp(hwnd, TEXT("Hot"))) {
			SetProp(hwnd, TEXT("Hot"), reinterpret_cast<HANDLE>(1));
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&tme);
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		break;
	case WM_MOUSELEAVE:
		RemoveProp(hwnd, TEXT("Hot"));
		InvalidateRect(hwnd, nullptr, FALSE);
		break;

	case WM_SIZE:
	case WM_DESTROY:
		BufferedPaintStopAllAnimations(hwnd);
		break;
	case WM_NCDESTROY:
		RemoveProp(hwnd, TEXT("Hot"));
		BufferedPaintStopAllAnimations(hwnd);
		RemoveWindowSubclass(hwnd, LoginButtonSubclassProc, uIdSubclass);
		break;
	}
	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// ---- WebView2 ログ表示ヘルパー ----

static std::wstring LogColorToHex(COLORREF cr) {
	wchar_t buf[8];
	swprintf_s(buf, L"#%02x%02x%02x", GetRValue(cr), GetGValue(cr), GetBValue(cr));
	return buf;
}

static std::wstring LogJsonEsc(const wchar_t* s) {
	std::wstring r;
	for (; *s; ++s) {
		switch (*s) {
		case L'"':  r += L"\\\""; break;
		case L'\\': r += L"\\\\"; break;
		case L'\n': r += L"\\n";  break;
		case L'\r': r += L"\\r";  break;
		default:
			if (*s < 0x20) { wchar_t e[8]; swprintf_s(e, L"\\u%04X", (unsigned)*s); r += e; }
			else r += *s;
		}
	}
	return r;
}

static const wchar_t* kLogHtml = LR"(<!DOCTYPE html><html><head><meta charset="UTF-8"><style>
:root{--bg:#fff;--fg:#000;--sb:rgba(128,128,128,.45)}
*{margin:0;padding:0;box-sizing:border-box}
html,body{width:100%;height:100%;overflow:hidden;background:var(--bg);color:var(--fg);font-size:12pt;display:flex;flex-direction:column}
#la{flex:1;position:relative;overflow:hidden}
#L,#F{width:100%;height:100%;overflow-y:scroll;overflow-x:hidden;position:absolute;top:0;left:0}
#L::-webkit-scrollbar,#F::-webkit-scrollbar{width:8px}
#L::-webkit-scrollbar-track,#F::-webkit-scrollbar-track{background:transparent}
#L::-webkit-scrollbar-thumb,#F::-webkit-scrollbar-thumb{background:var(--sb);border-radius:4px}
#L::-webkit-scrollbar-thumb:hover,#F::-webkit-scrollbar-thumb:hover{background:var(--fg);opacity:.5}
.i{display:flex;align-items:baseline;padding:1px 3px;line-height:1.35;cursor:default;user-select:text;overflow:hidden}
.i:hover{background:rgba(128,128,128,.1)}
.i.s{outline:1px solid rgba(128,128,128,.4)}
.i.ab{opacity:.35}
.tm{flex-shrink:0;opacity:.5;font-size:.82em;margin-right:3px;white-space:nowrap}
.mk{flex-shrink:0;width:3.2em;font-size:.82em;overflow:hidden;white-space:nowrap}
.tx{white-space:nowrap;overflow:hidden;min-width:0;flex:1}
.msg .tx{font-style:italic}
.hide .tx,.refuge-hide .tx{text-decoration:line-through;opacity:.65}
#F{display:none}
.fi{display:flex;align-items:baseline;padding:2px 3px;line-height:1.35;cursor:default;user-select:none;overflow:hidden;white-space:nowrap}
.fi:hover{background:rgba(128,128,128,.15)}
.fi.sel{background:rgba(128,128,128,.22);outline:1px solid rgba(128,128,128,.4)}
.ff{flex-shrink:0;min-width:5.5em;font-variant-numeric:tabular-nums;margin-right:.3em;font-size:.9em}
.fn{flex-shrink:0;margin-right:.3em}
.fe{flex:1;color:#9acd32;overflow:hidden;min-width:0;font-size:.9em}
#pp{position:absolute;bottom:34px;left:0;right:0;height:28px;display:flex;align-items:center;gap:3px;padding:0 5px;background:var(--bg);border-top:1px solid rgba(128,128,128,.3);visibility:hidden;z-index:10}
.cc{width:16px;height:16px;border-radius:50%;cursor:pointer;flex-shrink:0;border:2px solid transparent}
.cc.on{box-shadow:0 0 0 2px var(--fg,#000)}
.c0{background:#fff;border-color:#aaa}
.c1{background:#e00}
.c2{background:#f7a}
.c3{background:#f80}
.c4{background:#fd0}
.c5{background:#0b0}
.c6{background:#0cc}
.c7{background:#00e}
.c8{background:#808}
.c9{background:#222}
.sp{width:1px;height:14px;background:var(--fg,#000);opacity:.25;flex-shrink:0}
.tb{flex-shrink:0;padding:0 4px;height:16px;line-height:16px;border:1px solid var(--fg,#000);border-radius:3px;cursor:pointer;background:transparent;color:var(--fg,#000);font-size:9pt;opacity:.6}
.tb.on{background:var(--fg,#000);color:var(--bg,#fff);opacity:1}
#mn{flex-shrink:0;height:34px;display:flex;align-items:center;padding:3px 5px;gap:4px;border-top:1px solid rgba(128,128,128,.15)}
#cb{flex-shrink:0;width:26px;height:27px;border:1px solid rgba(128,128,128,.6);border-radius:4px;cursor:pointer;background:transparent;color:inherit;font-size:13pt;line-height:1;font-family:inherit;display:flex;align-items:center;justify-content:center}
#cb.open{background:rgba(128,128,128,.15)}
#c{flex:1;height:27px;border:1px solid rgba(128,128,128,.5);border-radius:3px;padding:1px 5px;background:transparent;color:inherit;outline:none;font-family:inherit;font-size:inherit}
</style></head><body>
<div id="la">
<div id="L"></div><div id="F"></div>
</div>
<div id="pp">
<div class="cc c0 on" data-c="" title="白"></div>
<div class="cc c1" data-c="red" title="赤"></div>
<div class="cc c2" data-c="pink" title="ピンク"></div>
<div class="cc c3" data-c="orange" title="橙"></div>
<div class="cc c4" data-c="yellow" title="黄"></div>
<div class="cc c5" data-c="green" title="緑"></div>
<div class="cc c6" data-c="cyan" title="水色"></div>
<div class="cc c7" data-c="blue" title="青"></div>
<div class="cc c8" data-c="purple" title="紫"></div>
<div class="cc c9" data-c="black" title="黒"></div>
<div class="sp"></div>
<button class="tb on" data-p="">流</button>
<button class="tb" data-p="ue">上</button>
<button class="tb" data-p="shita">下</button>
<div class="sp"></div>
<button class="tb" data-s="big">大</button>
<button class="tb on" data-s="">普</button>
<button class="tb" data-s="small">小</button>
</div>
<div id="mn">
<button id="cb" title="コマンド選択">▷</button>
<input id="c" type="text" maxlength="75">
</div>
<script>
const L=document.getElementById('L'),F=document.getElementById('F');
const la=document.getElementById('la'),pp=document.getElementById('pp');
const cb=document.getElementById('cb'),c=document.getElementById('c');
let bot=true,sel=null,fsel=null,sc='',sp='',ss='',po=false;
const TR={'':{'':'▷','big':'▶','small':'▹'},'ue':{'':'△','big':'▲','small':'▵'},'shita':{'':'▽','big':'▼','small':'▿'}};
const CL={'':'','red':'#d00','pink':'#e88','orange':'#e70','yellow':'#b90','green':'#090','cyan':'#088','blue':'#00b','purple':'#707','black':'#555'};
L.addEventListener('scroll',()=>{bot=L.scrollTop+L.clientHeight>=L.scrollHeight-8;});
L.addEventListener('contextmenu',e=>{
  e.preventDefault();const it=e.target.closest('.i');if(!it)return;
  if(sel)sel.classList.remove('s');sel=it;it.classList.add('s');
  window.chrome.webview.postMessage(JSON.stringify({cmd:'ctx',m:it.dataset.m,tm:it.querySelector('.tm').textContent,x:e.screenX,y:e.screenY}));
});
L.addEventListener('dblclick',e=>{
  const it=e.target.closest('.i');if(!it||it.classList.contains('msg'))return;
  window.chrome.webview.postMessage(JSON.stringify({cmd:'dbl',m:it.dataset.m,tm:it.querySelector('.tm').textContent}));
});
function add(d){
  const v=document.createElement('div');
  v.className='i'+(d.tp?' '+d.tp:'')+(d.ab?' ab':'');
  v.dataset.m=d.m||'';v.dataset.t=d.tx||'';v.dataset.mk=d.m?d.m.substring(0,3):'';
  const tm=document.createElement('span');tm.className='tm';tm.textContent=d.tm;
  const mk=document.createElement('span');mk.className='mk';if(d.mc)mk.style.color=d.mc;mk.textContent=d.m?(d.m.substring(0,3)+'  '):'';
  const tx=document.createElement('span');tx.className='tx';if(d.cl)tx.style.color=d.cl;tx.textContent=d.ab?'':d.tx;
  if(d.tp==='msg')v.append(tm,tx);else v.append(tm,mk,tx);
  return v;
}
function fc(v){
  if(v<=0)return'#808080';
  if(v<=50)return'#008000';
  if(v<=100)return'#0080FF';
  if(v<=200)return'#FF8000';
  return'#FF0000';
}
F.addEventListener('click',e=>{
  const fi=e.target.closest('.fi');if(!fi)return;
  if(fsel)fsel.classList.remove('sel');
  fsel=fi;fi.classList.add('sel');
  window.chrome.webview.postMessage(JSON.stringify({cmd:'fsel',id:+fi.dataset.id}));
});
function upd(){
  cb.textContent=TR[sp][ss];
  const col=CL[sc];
  cb.style.color=col||'';
  cb.style.borderColor=col?col+'99':'';
}
function tog(){
  po=!po;
  cb.classList.toggle('open',po);
  pp.style.visibility=po?'visible':'hidden';
}
cb.addEventListener('mousedown',e=>e.preventDefault());
cb.addEventListener('click',tog);
pp.addEventListener('mousedown',e=>{e.preventDefault();e.stopPropagation();});
la.addEventListener('mousedown',()=>{if(po){po=false;cb.classList.remove('open');pp.style.visibility='hidden';}});
la.addEventListener('dragover',e=>{e.preventDefault();la.style.outline='2px dashed var(--fg)';});
la.addEventListener('dragleave',()=>{la.style.outline='';});
la.addEventListener('drop',e=>{e.preventDefault();la.style.outline='';});
const cbs=[...document.querySelectorAll('[data-c]')];
cbs.forEach(b=>b.addEventListener('click',()=>{sc=b.dataset.c;cbs.forEach(x=>x.classList.toggle('on',x.dataset.c===sc));upd();}));
const pbs=[...document.querySelectorAll('[data-p]')];
pbs.forEach(b=>b.addEventListener('click',()=>{sp=b.dataset.p;pbs.forEach(x=>x.classList.toggle('on',x.dataset.p===sp));upd();}));
const sbs=[...document.querySelectorAll('[data-s]')];
sbs.forEach(b=>b.addEventListener('click',()=>{ss=b.dataset.s;sbs.forEach(x=>x.classList.toggle('on',x.dataset.s===ss));upd();}));
c.addEventListener('keydown',e=>{
  if(e.key==='Enter'&&!e.isComposing){
    if(c.value){
      const parts=[sc,sp,ss].filter(Boolean);
      window.chrome.webview.postMessage(JSON.stringify({cmd:'post',mail:parts.join(' '),text:c.value}));
    }
    e.preventDefault();
  }
});)" LR"(
c.addEventListener('focus',()=>{if(po){po=false;cb.classList.remove('open');pp.style.visibility='hidden';}});
window.chrome.webview.addEventListener('message',e=>{
  const msg=JSON.parse(e.data);
  if(msg.cmd==='upd'){
    L.style.display='block';F.style.display='none';
    for(let i=0;i<(msg.tr||0)&&L.firstChild;i++)L.removeChild(L.firstChild);
    (msg.it||[]).forEach(d=>L.appendChild(add(d)));if(bot)L.scrollTop=L.scrollHeight;
  }else if(msg.cmd==='rel'){
    L.style.display='block';F.style.display='none';
    L.innerHTML='';bot=true;sel=null;
    (msg.it||[]).forEach(d=>L.appendChild(add(d)));L.scrollTop=L.scrollHeight;
  }else if(msg.cmd==='clr'){
    L.style.display='block';F.style.display='none';
    L.innerHTML='';bot=true;sel=null;
  }else if(msg.cmd==='ab'){
    document.querySelectorAll('.i[data-m="'+CSS.escape(msg.m)+'"]').forEach(el=>{
      const tx=el.querySelector('.tx');
      if(msg.s){el.classList.add('ab');if(tx)tx.textContent='';}
      else{el.classList.remove('ab');if(tx)tx.textContent=el.dataset.t;}
    });
    if(sel){sel.classList.remove('s');sel=null;}
  }else if(msg.cmd==='frc'){
    L.style.display='none';F.style.display='block';
    const selId=msg.sel,frag=document.createDocumentFragment();
    fsel=null;
    (msg.items||[]).forEach(d=>{
      const el=document.createElement('div');
      el.className='fi'+(d.id===selId?' sel':'');
      if(d.id===selId)fsel=el;
      el.dataset.id=d.id;
      const ff=document.createElement('span'),fn=document.createElement('span'),fe=document.createElement('span');
      ff.className='ff';fn.className='fn';fe.className='fe';
      ff.style.color=fc(d.fo);
      const id3=(d.id+'').padStart(3,'0');
      ff.textContent=d.fo<0?id3+' 勢???':id3+' 勢'+(d.fo+'').padStart(3,'0');
      fn.textContent='('+d.nm+(d.si?'-'+d.si:'')+')';
      fe.textContent=d.ev||'';
      el.append(ff,fn,fe);
      frag.appendChild(el);
    });
    F.innerHTML='';F.appendChild(frag);
  }else if(msg.cmd==='clri'){
    c.value='';
  }else if(msg.cmd==='focus_input'){
    c.focus();
  }else if(msg.cmd==='input_color'){
    c.style.background=msg.bg||'';
    c.style.color=msg.fg||'';
  }else if(msg.cmd==='thm'){
    document.documentElement.style.setProperty('--bg',msg.bg);
    document.documentElement.style.setProperty('--fg',msg.fg);
    if(msg.sb)document.documentElement.style.setProperty('--sb',msg.sb);
    document.body.style.background=msg.bg;
  }else if(msg.cmd==='fnt'){
    document.body.style.fontSize=msg.sz+'pt';
    document.body.style.fontFamily="'Segoe UI Emoji','"+msg.nm+"',sans-serif";
  }
});
upd();
</script></body></html>)";

std::wstring CNicoJK::LogElemToJson(const LOG_ELEM& e) const
{
	const wchar_t* tp = L"nico";
	switch (e.type) {
	case LOG_ELEM_TYPE_HIDE:        tp = L"hide"; break;
	case LOG_ELEM_TYPE_REFUGE:      tp = L"refuge"; break;
	case LOG_ELEM_TYPE_REFUGE_HIDE: tp = L"refuge-hide"; break;
	case LOG_ELEM_TYPE_MESSAGE:     tp = L"msg"; break;
	default: break;
	}
	COLORREF crM = e.type == LOG_ELEM_TYPE_MESSAGE ? RGB(0, 0, 0) :
	    (e.type == LOG_ELEM_TYPE_DEFAULT || e.type == LOG_ELEM_TYPE_HIDE) ? s_.crNicoMarker : s_.crRefugeMarker;
	const wchar_t* mk = e.marker;
	if (!wcsncmp(mk, L"a:", 2)) mk += 2;
	wchar_t tm[12];
	swprintf_s(tm, L"%02d:%02d:%02d", e.st.wHour, e.st.wMinute, e.st.wSecond);
	// WM_DRAWITEM(line 4519): bEmphasis=MSG型のとき赤。それ以外はパネル文字色（CSS var(--fg)）
	std::wstring json = std::wstring(L"{\"tx\":\"") + LogJsonEsc(e.text.c_str());
	if (e.type == LOG_ELEM_TYPE_MESSAGE) {
		json += L"\",\"cl\":\"#ff0000";  // 赤固定
	}
	json += L"\",\"mc\":\"" + LogColorToHex(crM)
	      + L"\",\"tp\":\"" + tp
	      + L"\",\"m\":\""  + LogJsonEsc(mk)
	      + L"\",\"ab\":"   + (e.bAbone ? L"true" : L"false")
	      + L",\"tm\":\""   + tm + L"\"}";
	return json;
}

void CNicoJK::SendLogWV2Update(int trimCount, int newCount)
{
	if (!pLogWV2_ || !logWV2Ready_) return;
	std::wstring json = L"{\"cmd\":\"upd\",\"tr\":" + std::to_wstring(trimCount) + L",\"it\":[";
	auto it = logList_.end();
	for (int i = 0; i < newCount && it != logList_.begin(); ++i) --it;
	bool first = true;
	for (; it != logList_.end(); ++it) {
		if (!first) json += L',';
		json += LogElemToJson(*it);
		first = false;
	}
	json += L"]}";
	pLogWV2_->PostWebMessageAsString(json.c_str());
}

void CNicoJK::SendLogWV2Reload()
{
	if (!pLogWV2_ || !logWV2Ready_) return;
	std::wstring json = L"{\"cmd\":\"rel\",\"it\":[";
	bool first = true;
	for (const auto& e : logList_) {
		if (!first) json += L',';
		json += LogElemToJson(e);
		first = false;
	}
	json += L"]}";
	pLogWV2_->PostWebMessageAsString(json.c_str());
}

void CNicoJK::ApplyLogWV2Theme()
{
	if (!pLogWV2_ || !logWV2Ready_) return;
	if (!panelColor_.GetPanelBackBrush()) return;
	COLORREF bg = panelColor_.GetPanelBack(), fg = panelColor_.GetPanelText();
	// スクロールバー色: bg と fg の中間 (ダーク/ライト両対応)
	COLORREF sb = RGB((GetRValue(bg) + GetRValue(fg)) / 2,
	                  (GetGValue(bg) + GetGValue(fg)) / 2,
	                  (GetBValue(bg) + GetBValue(fg)) / 2);
	wchar_t msg[192];
	swprintf_s(msg, L"{\"cmd\":\"thm\",\"bg\":\"%s\",\"fg\":\"%s\",\"sb\":\"%s\"}",
	    LogColorToHex(bg).c_str(), LogColorToHex(fg).c_str(), LogColorToHex(sb).c_str());
	pLogWV2_->PostWebMessageAsString(msg);
	wchar_t fnt[LF_FACESIZE + 64];
	swprintf_s(fnt, L"{\"cmd\":\"fnt\",\"nm\":\"%s\",\"sz\":%d}",
	    LogJsonEsc(s_.forceFontName).c_str(), s_.forceFontSize);
	pLogWV2_->PostWebMessageAsString(fnt);
	// 投稿先に応じて入力欄の色を設定（bRefugeMixing 時のみ）
	if (s_.bRefugeMixing) {
		COLORREF cr = bPostToRefuge_ ? s_.crRefugeEditBox : s_.crNicoEditBox;
		if (cr != RGB(0xFF, 0xFF, 0xFF)) {
			COLORREF fg2 = GetBrightness(cr) < 255 ? RGB(0xFF, 0xFF, 0xFF) : RGB(0, 0, 0);
			wchar_t ic[64];
			swprintf_s(ic, L"{\"cmd\":\"input_color\",\"bg\":\"%s\",\"fg\":\"%s\"}",
			    LogColorToHex(cr).c_str(), LogColorToHex(fg2).c_str());
			pLogWV2_->PostWebMessageAsString(ic);
			return;
		}
	}
	pLogWV2_->PostWebMessageAsString(L"{\"cmd\":\"input_color\",\"bg\":\"\",\"fg\":\"\"}");
}

void CNicoJK::SendLogWV2AboneUpdate(LPCTSTR marker, bool state)
{
	if (!pLogWV2_ || !logWV2Ready_) return;
	// DOM の data-m は "a:" プレフィックスを除いて格納されているため合わせる
	const wchar_t* m = marker;
	if (m && !wcsncmp(m, L"a:", 2)) m += 2;
	wchar_t msg[300];
	swprintf_s(msg, L"{\"cmd\":\"ab\",\"m\":\"%s\",\"s\":%s}",
	    LogJsonEsc(m).c_str(), state ? L"true" : L"false");
	pLogWV2_->PostWebMessageAsString(msg);
}

void CNicoJK::SendForceListWV2Update()
{
	if (!pLogWV2_ || !logWV2Ready_) return;
	ULONGLONG nowTick = GetTickCount64();
	std::wstring json = L"{\"cmd\":\"frc\",\"sel\":";
	json += std::to_wstring(currentJKToGet_);
	json += L",\"items\":[";
	bool first = true;
	for (auto& it : forceList_) {
		UpdateForceElemEventName(&it, nowTick);
		const tstring* pEventName = &it.eventName;
		tstring wsEventName;
		if (it.eventName.empty() && !programTitleMap_.empty()) {
			auto pit = programTitleMap_.find(it.jkID);
			if (pit != programTitleMap_.end() && !pit->second.empty()) {
				wsEventName = pit->second;
				pEventName = &wsEventName;
			}
		}
		if (!first) json += L",";
		first = false;
		json += L"{\"id\":";
		json += std::to_wstring(it.jkID);
		json += L",\"fo\":";
		json += std::to_wstring(it.force);
		json += L",\"nm\":\"";
		json += LogJsonEsc(it.name.c_str());
		// 接続中ストリームの先頭2文字を si として送信
		const std::string& sid = it.chatStreamID.empty() ? it.refugeChatStreamID : it.chatStreamID;
		json += L"\",\"si\":\"";
		for (int i = 0; i < 2 && i < (int)sid.size(); ++i)
			json += static_cast<wchar_t>(static_cast<unsigned char>(sid[i]));
		json += L"\",\"ev\":\"";
		json += LogJsonEsc(pEventName->c_str());
		json += L"\"}";
	}
	json += L"]}";
	pLogWV2_->PostWebMessageAsString(json.c_str());
}

// ---- channels WebSocket ヘルパー ----

struct ChannelWsEntry {
	int  id          = 0;
	int  force       = -1;
	bool hasForce    = false;
	tstring programTitle;
	bool hasProgramTitle = false; // true = データあり (空文字列 = null)
};
struct ChannelWsMsg {
	int type = 0;  // 1=snapshot  2=stats  3=programs
	std::vector<ChannelWsEntry> channels;
};

static int CwsJInt(const std::string& s, const char* key) {
	std::string nd = std::string("\"") + key + "\":";
	auto p = s.find(nd);
	if (p == std::string::npos) return -1;
	p += nd.size();
	while (p < s.size() && s[p] == ' ') ++p;
	if (p >= s.size() || (s[p] != '-' && (s[p] < '0' || s[p] > '9'))) return -1;
	return std::strtol(s.c_str() + p, nullptr, 10);
}
static std::string CwsJStr(const std::string& s, const char* key) {
	std::string nd = std::string("\"") + key + "\":\"";
	auto p = s.find(nd);
	if (p == std::string::npos) return {};
	p += nd.size();
	std::string r;
	for (; p < s.size() && s[p] != '"'; ++p) {
		if (s[p] != '\\' || p + 1 >= s.size()) { r += s[p]; continue; }
		++p;
		switch (s[p]) {
		case '"': case '\\': case '/': r += s[p]; break;
		case 'n': r += '\n'; break;
		case 'r': r += '\r'; break;
		case 't': r += '\t'; break;
		case 'u':
			if (p + 4 < s.size()) {
				char h[5] = { s[p+1], s[p+2], s[p+3], s[p+4], '\0' };
				wchar_t wc = static_cast<wchar_t>(std::strtoul(h, nullptr, 16));
				p += 4;
				wchar_t wbuf[2] = { wc, L'\0' };
				int wlen = 1;
				// サロゲートペア (U+D800–U+DBFF)
				if (wc >= 0xD800 && wc <= 0xDBFF && p + 6 < s.size() &&
				    s[p+1] == '\\' && s[p+2] == 'u') {
					char h2[5] = { s[p+3], s[p+4], s[p+5], s[p+6], '\0' };
					wchar_t wc2 = static_cast<wchar_t>(std::strtoul(h2, nullptr, 16));
					if (wc2 >= 0xDC00 && wc2 <= 0xDFFF) {
						wbuf[1] = wc2; wlen = 2; p += 6;
					}
				}
				char mb[8] = {};
				int n = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, mb, sizeof(mb), nullptr, nullptr);
				r.append(mb, n);
			}
			break;
		default: r += s[p]; break;
		}
	}
	return r;
}
static void ParseChannelWsJson(const std::string& json, ChannelWsMsg& msg)
{
	std::string t = CwsJStr(json, "type");
	if      (t == "snapshot") msg.type = 1;
	else if (t == "stats")    msg.type = 2;
	else if (t == "programs") msg.type = 3;
	else return;

	auto chArr = json.find("\"channels\":");
	if (chArr == std::string::npos) return;
	auto aStart = json.find('[', chArr);
	if (aStart == std::string::npos) return;

	size_t pos = aStart + 1;
	while (pos < json.size()) {
		auto oS = json.find('{', pos);
		if (oS == std::string::npos) break;
		int depth = 0;
		size_t oE = oS;
		for (; oE < json.size(); ++oE) {
			if      (json[oE] == '{') ++depth;
			else if (json[oE] == '}' && --depth == 0) break;
		}
		if (depth != 0) break;

		std::string obj = json.substr(oS, oE - oS + 1);
		ChannelWsEntry e;
		e.id = CwsJInt(obj, "id");
		if (e.id <= 0) { pos = oE + 1; continue; }

		int force = CwsJInt(obj, "force");
		if (force >= 0) { e.force = force; e.hasForce = true; }

		auto pPos = obj.find("\"program\":");
		if (pPos != std::string::npos) {
			auto vPos = obj.find_first_not_of(" \t\r\n", pPos + 10);
			if (vPos != std::string::npos) {
				if (obj.compare(vPos, 4, "null") == 0) {
					e.hasProgramTitle = true; // null → タイトルなし
				} else if (obj[vPos] == '{') {
					int pd = 0; size_t pE = vPos;
					for (; pE < obj.size(); ++pE) {
						if      (obj[pE] == '{') ++pd;
						else if (obj[pE] == '}' && --pd == 0) break;
					}
					std::string prog = obj.substr(vPos, pE - vPos + 1);
					std::string title = CwsJStr(prog, "title");
					if (!title.empty()) {
						int wl = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
						if (wl > 1) {
							e.programTitle.resize(wl - 1);
							MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, &e.programTitle[0], wl);
						}
					}
					e.hasProgramTitle = true;
				}
			}
		}

		msg.channels.push_back(std::move(e));
		pos = oE + 1;
	}
}

void CNicoJK::ChannelWsThreadFunc(HWND hwndForce, std::string wsUri)
{
	while (WaitForSingleObject(hChannelWsQuit_, 0) != WAIT_OBJECT_0) {
		// ws:// → http:// / wss:// → https:// に変換して WinHttpCrackUrl で解析
		bool isSecure = wsUri.size() >= 6 && wsUri.substr(0, 6) == "wss://";
		std::string httpUrl = (isSecure ? "https://" : "http://") + wsUri.substr(isSecure ? 6 : 5);
		std::wstring wUrl(httpUrl.begin(), httpUrl.end());

		URL_COMPONENTS uc = {};
		uc.dwStructSize = sizeof(uc);
		wchar_t szHost[256] = {}, szPath[1024] = {};
		uc.lpszHostName = szHost; uc.dwHostNameLength = _countof(szHost);
		uc.lpszUrlPath  = szPath; uc.dwUrlPathLength  = _countof(szPath);
		if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &uc)) {
			if (WaitForSingleObject(hChannelWsQuit_, 10000) == WAIT_OBJECT_0) break;
			continue;
		}

		HINTERNET hSession = WinHttpOpen(L"NicoJK",
		    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!hSession) { if (WaitForSingleObject(hChannelWsQuit_, 5000) == WAIT_OBJECT_0) break; continue; }

		HINTERNET hConnect = WinHttpConnect(hSession, szHost, uc.nPort, 0);
		HINTERNET hRequest = hConnect ? WinHttpOpenRequest(hConnect, L"GET", szPath, nullptr,
		    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, isSecure ? WINHTTP_FLAG_SECURE : 0) : nullptr;

		HINTERNET hWs = nullptr;
		if (hRequest) {
			WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
			if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
			    WinHttpReceiveResponse(hRequest, nullptr)) {
				hWs = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
			}
			WinHttpCloseHandle(hRequest);
		}
		if (!hWs) {
			if (hConnect) WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			if (WaitForSingleObject(hChannelWsQuit_, 5000) == WAIT_OBJECT_0) break;
			continue;
		}

		// hWs を共有メンバーに登録: メインスレッドが終了時に WinHttpCloseHandle で受信を即中断できる
		InterlockedExchangePointer(&hChannelWsHandle_, hWs);

		std::string jsonBuf;
		BYTE buf[4096];
		for (;;) {
			if (WaitForSingleObject(hChannelWsQuit_, 0) == WAIT_OBJECT_0) break;
			DWORD dwRead = 0;
			WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
			if (WinHttpWebSocketReceive(hWs, buf, sizeof(buf), &dwRead, &type) != ERROR_SUCCESS) break;
			if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;

			jsonBuf.append(reinterpret_cast<char*>(buf), dwRead);

			if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
				ChannelWsMsg* pMsg = new ChannelWsMsg();
				ParseChannelWsJson(jsonBuf, *pMsg);
				if (pMsg->type > 0) {
					PostMessage(hwndForce, WMS_CHANNEL_WS, pMsg->type, reinterpret_cast<LPARAM>(pMsg));
				} else {
					delete pMsg;
				}
				jsonBuf.clear();
			}
		}

		PostMessage(hwndForce, WMS_CHANNEL_WS, 0, 0); // 切断通知
		// InterlockedExchange でハンドルの所有権を確認: nullptr ならメインスレッドが既に Close 済み
		if (InterlockedExchangePointer(&hChannelWsHandle_, nullptr)) WinHttpCloseHandle(hWs);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);

		if (WaitForSingleObject(hChannelWsQuit_, 5000) == WAIT_OBJECT_0) break;
	}
}

bool CNicoJK::CreateForceWindowItems(HWND hwnd)
{
	int dpi = m_pApp->GetDPIFromWindow(hwnd);
	if (dpi == 0) {
		dpi = 96;
	}
	if (!hForceFont_) {
		// コントロールのフォントを生成
		LOGFONT lf = {};
		lf.lfHeight = -(s_.forceFontSize * dpi / 72);
		lf.lfCharSet = SHIFTJIS_CHARSET;
		_tcscpy_s(lf.lfFaceName, s_.forceFontName);
		hForceFont_ = CreateFontIndirect(&lf);
	}
	int space = 3 * dpi / 96;
	int padding = hPanel_ ? 0 : space;
	int tabWidth = 56 * dpi / 96;
	int checkBoxWidth = 48 * dpi / 96;
	int sliderWidth = 64 * dpi / 96;
	int buttonWidth = 18 * dpi / 96;
	int height = 24 * dpi / 96;
	int left = 0;

	if (CreateWindowEx(0, TEXT("BUTTON"), TEXT("勢い"), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_PUSHLIKE,
	        (left += padding), padding, tabWidth, height, hwnd, reinterpret_cast<HMENU>(IDC_RADIO_FORCE), g_hinstDLL, nullptr) &&
	    CreateWindowEx(0, TEXT("BUTTON"), TEXT("ログ"), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_PUSHLIKE,
	        (left += tabWidth), padding, tabWidth, height, hwnd, reinterpret_cast<HMENU>(IDC_RADIO_LOG), g_hinstDLL, nullptr) &&
	    CreateWindowEx(0, TEXT("BUTTON"), TEXT("File"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
	        (left += tabWidth + space), padding + space, checkBoxWidth, height - space * 2, hwnd, reinterpret_cast<HMENU>(IDC_CHECK_SPECFILE), g_hinstDLL, nullptr) &&
	    CreateWindowEx(0, TEXT("BUTTON"), TEXT("Rel"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
	        (left += checkBoxWidth), padding + space, checkBoxWidth, height - space * 2, hwnd, reinterpret_cast<HMENU>(IDC_CHECK_RELATIVE), g_hinstDLL, nullptr) &&
	    // パネルでは枠外の高さに置くことで実質的に非表示にする
	    CreateWindowEx(0, TRACKBAR_CLASS, TEXT("不透明度"), WS_CHILD | WS_VISIBLE | TBS_BOTH | TBS_NOTICKS | TBS_TOOLTIPS,
	        left + checkBoxWidth, hPanel_ ? -height : padding + space, sliderWidth, height - space, hwnd, reinterpret_cast<HMENU>(IDC_SLIDER_OPACITY), g_hinstDLL, nullptr) &&
	    // パネルでは(描画がとても面倒なので)スライダーをボタン3つで代用
	    CreateWindowEx(0, TEXT("BUTTON"), TEXT(""), WS_CHILD | WS_VISIBLE,
	        (left += checkBoxWidth + space), hPanel_ ? padding + space : -height, buttonWidth, height - space * 2, hwnd, reinterpret_cast<HMENU>(IDC_BUTTON_OPACITY_DOWN), g_hinstDLL, nullptr) &&
	    CreateWindowEx(0, TEXT("BUTTON"), TEXT(""), WS_CHILD | WS_VISIBLE,
	        (left += buttonWidth), hPanel_ ? padding + space : -height, buttonWidth, height - space * 2, hwnd, reinterpret_cast<HMENU>(IDC_BUTTON_OPACITY_TOGGLE), g_hinstDLL, nullptr) &&
	    CreateWindowEx(0, TEXT("BUTTON"), TEXT(""), WS_CHILD | WS_VISIBLE,
	        (left += buttonWidth), hPanel_ ? padding + space : -height, buttonWidth, height - space * 2, hwnd, reinterpret_cast<HMENU>(IDC_BUTTON_OPACITY_UP), g_hinstDLL, nullptr) &&
	    CreateWindowEx(0, TEXT("BUTTON"), TEXT("P"), WS_CHILD | WS_VISIBLE,
	        (left += buttonWidth), hPanel_ ? padding + space : -height, buttonWidth, height - space * 2, hwnd, reinterpret_cast<HMENU>(IDC_BUTTON_POPUP), g_hinstDLL, nullptr) &&
	    CreateWindowEx(0, TEXT("BUTTON"), TEXT("L"), WS_CHILD | WS_VISIBLE,
	        (left += buttonWidth), hPanel_ ? padding + space : -height, buttonWidth, height - space * 2, hwnd, reinterpret_cast<HMENU>(IDC_BUTTON_LOGIN), g_hinstDLL, nullptr) &&
	    CreateWindowEx(0, TEXT("BUTTON"), TEXT("?"), WS_CHILD | WS_VISIBLE,
	        (left += buttonWidth), hPanel_ ? padding + space : -height, buttonWidth, height - space * 2, hwnd, reinterpret_cast<HMENU>(IDC_BUTTON_HELP), g_hinstDLL, nullptr))
	{
		if (hForceFont_) {
			SendDlgItemMessage(hwnd, IDC_RADIO_FORCE, WM_SETFONT, reinterpret_cast<WPARAM>(hForceFont_), 0);
			SendDlgItemMessage(hwnd, IDC_RADIO_LOG, WM_SETFONT, reinterpret_cast<WPARAM>(hForceFont_), 0);
			SendDlgItemMessage(hwnd, IDC_CHECK_SPECFILE, WM_SETFONT, reinterpret_cast<WPARAM>(hForceFont_), 0);
			SendDlgItemMessage(hwnd, IDC_CHECK_RELATIVE, WM_SETFONT, reinterpret_cast<WPARAM>(hForceFont_), 0);
			SendDlgItemMessage(hwnd, IDC_BUTTON_OPACITY_DOWN, WM_SETFONT, reinterpret_cast<WPARAM>(hForceFont_), 0);
			SendDlgItemMessage(hwnd, IDC_BUTTON_OPACITY_UP, WM_SETFONT, reinterpret_cast<WPARAM>(hForceFont_), 0);
			SendDlgItemMessage(hwnd, IDC_BUTTON_OPACITY_TOGGLE, WM_SETFONT, reinterpret_cast<WPARAM>(hForceFont_), 0);
			SendDlgItemMessage(hwnd, IDC_BUTTON_POPUP, WM_SETFONT, reinterpret_cast<WPARAM>(hForceFont_), 0);
			SendDlgItemMessage(hwnd, IDC_BUTTON_LOGIN, WM_SETFONT, reinterpret_cast<WPARAM>(hForceFont_), 0);
			SendDlgItemMessage(hwnd, IDC_BUTTON_HELP, WM_SETFONT, reinterpret_cast<WPARAM>(hForceFont_), 0);
		}
		hForceTooltip_ = CreateWindowEx(0, TOOLTIPS_CLASS, nullptr, WS_POPUP | TTS_ALWAYSTIP,
		                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		                                hwnd, nullptr, g_hinstDLL, nullptr);
		auto addToolTip = [&](int id, LPCTSTR text) {
			HWND hItem = GetDlgItem(hwnd, id);
			if (!hForceTooltip_ || !hItem) {
				return;
			}
			TOOLINFO ti = {};
			ti.cbSize = sizeof(ti);
			ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
			ti.hwnd = hwnd;
			ti.uId = reinterpret_cast<UINT_PTR>(hItem);
			ti.lpszText = const_cast<LPTSTR>(text);
			SendMessage(hForceTooltip_, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&ti));
		};
		addToolTip(IDC_RADIO_FORCE, TEXT("勢いリストを表示"));
		addToolTip(IDC_RADIO_LOG, TEXT("コメントログを表示"));
		addToolTip(IDC_CHECK_SPECFILE, TEXT("実況ログファイルを読み込む"));
		addToolTip(IDC_CHECK_RELATIVE, TEXT("読み込むログを現在の再生位置に合わせる"));
		addToolTip(IDC_BUTTON_OPACITY_TOGGLE, TEXT("透明度を切り替える"));
		addToolTip(IDC_BUTTON_POPUP, TEXT("ポップアップ表示を切り替える"));
		addToolTip(IDC_BUTTON_LOGIN, TEXT("ニコニコログイン"));
		addToolTip(IDC_BUTTON_HELP, TEXT("ローカルコマンドヘルプ"));
		return true;
	}
	return false;
}

void CNicoJK::SetOpacity(HWND hwnd, int opacityOrToggle)
{
	BYTE opacity = commentWindow_.GetOpacity();
	if (opacity == 0) {
		bool bPreview = m_pApp->GetPreview();
		if (opacityOrToggle >= 0) {
			opacity = static_cast<BYTE>(opacityOrToggle);
		} else if (bPreview) {
			// 非表示前の不透明度を復元する
			opacity = static_cast<BYTE>(s_.commentOpacity >> 8);
			opacity = opacity == 0 ? 255 : opacity;
		}
		if (bPreview && opacity != 0) {
			// 非表示->表示
			commentWindow_.ClearChat();
			HWND hwndContainer = FindVideoContainer();
			commentWindow_.Create(hwndContainer);
			bHalfSkip_ = GetWindowHeight(hwndContainer) >= s_.halfSkipThreshold;
			if (opacityOrToggle < 0) {
				commentWindow_.AddChat(TEXT("(Comment ON)"), RGB(0, 0xFF, 0xFF), RGB(0, 0, 0), CCommentWindow::CHAT_POS_UE);
			}
		}
	} else {
		if (opacityOrToggle >= 0) {
			opacity = static_cast<BYTE>(opacityOrToggle);
		} else {
			// 8-15bitに非表示前の不透明度を記憶しておく
			s_.commentOpacity = (s_.commentOpacity & ~0xFF00) | (opacity << 8);
			opacity = 0;
		}
		if (opacity == 0) {
			// 表示->非表示
			commentWindow_.Destroy();
		}
	}
	commentWindow_.SetOpacity(opacity);
	m_pApp->SetPluginCommandState(COMMAND_HIDE_COMMENT, opacity != 0 ? TVTest::COMMAND_ICON_STATE_CHECKED : 0);
	int level = (opacity * 10 + 254) / 255;
	TCHAR text[2] = {};
	text[0] = level < 10 ? static_cast<TCHAR>(TEXT('0') + level) : TEXT('F');
	SetDlgItemText(hwnd, IDC_BUTTON_OPACITY_DOWN, level > 0 ? TEXT("◁") : TEXT(""));
	SetDlgItemText(hwnd, IDC_BUTTON_OPACITY_UP, level < 10 ? TEXT("▷") : TEXT(""));
	SetDlgItemText(hwnd, IDC_BUTTON_OPACITY_TOGGLE, text);
	InvalidateRect(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_DOWN), nullptr, FALSE);
	InvalidateRect(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_UP), nullptr, FALSE);
	InvalidateRect(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_TOGGLE), nullptr, FALSE);
}

void CNicoJK::UpdateWindowTheme(HWND hwnd)
{
	HWND hwndForce = hwnd ? hwnd : hForce_;
	if (!hwndForce) {
		return;
	}
	if (!panelColor_.GetPanelBackBrush()) {
		panelColor_.SetColor(m_pApp);
	}
	bool bDark = panelColor_.IsDark();
	if (hForceTooltip_) {
		SetWindowTheme(hForceTooltip_, bDark ? L"DarkMode_Explorer" : nullptr, nullptr);
	}
	if (hHelpWindow_) {
		SetWindowTheme(hHelpWindow_, bDark ? L"DarkMode_Explorer" : nullptr, nullptr);
	}
	if (hHelpEdit_) {
		SetWindowTheme(hHelpEdit_, bDark ? L"DarkMode_Explorer" : nullptr, nullptr);
		InvalidateRect(hHelpEdit_, nullptr, TRUE);
	}
	if (hLoginWindow_) {
		SetWindowTheme(hLoginWindow_, bDark ? L"DarkMode_Explorer" : nullptr, nullptr);
		for (int id = IDC_LOGIN_MAIL; id <= IDC_LOGIN_LABEL_OTP; ++id) {
			HWND hItem = GetDlgItem(hLoginWindow_, id);
			if (hItem) {
				::SetWindowTheme(hItem, bDark ? L"DarkMode_Explorer" : nullptr, nullptr);
			}
		}
		BOOL bDarkBool = bDark ? TRUE : FALSE;
		::DwmSetWindowAttribute(hLoginWindow_, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDarkBool, sizeof(bDarkBool));
		InvalidateRect(hLoginWindow_, nullptr, TRUE);
		UpdateWindow(hLoginWindow_);
	}
	if (hHelpWindow_) {
		BOOL bDarkBool = bDark ? TRUE : FALSE;
		::DwmSetWindowAttribute(hHelpWindow_, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDarkBool, sizeof(bDarkBool));
	}
	if (hPanelPopup_) {
		BOOL bDarkBool = bDark ? TRUE : FALSE;
		::DwmSetWindowAttribute(hPanelPopup_, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDarkBool, sizeof(bDarkBool));
		SetWindowPos(hPanelPopup_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	}
	ApplyLogWV2Theme();
	DeleteBrush(SetClassLongPtr(hwndForce, GCLP_HBRBACKGROUND,
		reinterpret_cast<LONG_PTR>(CreateSolidBrush(panelColor_.GetPanelBack()))));
	InvalidateRect(hwndForce, nullptr, TRUE);
}

LRESULT CNicoJK::ForceWindowProcMain(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_CREATE:
		if (CreateForceWindowItems(hwnd)) {
			logList_.clear();
			logListDisplayedSize_ = 0;
			bPendingTimerUpdateList_ = false;
			lastCalcLeftText_.clear();
			lastCalcMiddleText_.clear();
			lastCalcLeftTextD2D_.clear();
			lastCalcMiddleTextD2D_.clear();
			commentWindow_.SetStyle(s_.commentFontName, s_.commentFontNameMulti, s_.commentFontNameEmoji, s_.bCommentFontBold, s_.bCommentFontAntiAlias,
			                        s_.commentFontOutline, s_.bUseOsdCompositor, s_.bUseTexture, s_.bUseDrawingThread);
			commentWindow_.SetCommentSize(s_.commentSize, s_.commentSizeMin, s_.commentSizeMax, s_.commentLineMargin);
			commentWindow_.SetDisplayDuration(s_.commentDuration);
			commentWindow_.SetDrawLineCount(s_.commentDrawLineCount);
			commentWindow_.SetOpacity(0);
			SetOpacity(hwnd, static_cast<BYTE>(s_.commentOpacity));
			if (commentWindow_.GetOpacity() != 0 && m_pApp->GetPreview()) {
				ProcessChatTag("<!--<chat date=\"0\" mail=\"cyan ue\" user_id=\"-\">(NicoJK ON)</chat>-->");
			}
			bDisplayLogList_ = (s_.hideForceWindow & 2) != 0;
			forwardTick_ = timeGetTime();
			forwardOffset_ = 0;
			forwardOffsetDelta_ = 0;
			currentJKToGet_ = -1;
			lastPostComm_[0] = TEXT('\0');
			bPostToRefuge_ = s_.bPostToRefuge;
			bPostToRefugeInverted_ = false;
			bUsingLogfileDriver_ = IsMatchDriverName(s_.logfileDrivers.c_str());
			logReader_.ResetCheckInterval();
			bSpecFile_ = false;
			dropFileTimeout_ = 0;
			SendMessage(hwnd, WM_RESET_STREAM, 0, 0);

			SendDlgItemMessage(hwnd, bDisplayLogList_ ? IDC_RADIO_LOG : IDC_RADIO_FORCE, BM_SETCHECK, BST_CHECKED, 0);
			SendDlgItemMessage(hwnd, IDC_CHECK_RELATIVE, BM_SETCHECK, s_.bSetRelative ? BST_CHECKED : BST_UNCHECKED, 0);
			SendDlgItemMessage(hwnd, IDC_SLIDER_OPACITY, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10));
			SendDlgItemMessage(hwnd, IDC_SLIDER_OPACITY, TBM_SETPOS, TRUE, (commentWindow_.GetOpacity() * 10 + 254) / 255);
			SetTimer(hwnd, TIMER_UPDATE, max(UPDATE_FORCE_INTERVAL, 10000), nullptr);
			if (s_.timerInterval >= 0) {
				SetTimer(hwnd, TIMER_FORWARD, s_.timerInterval, nullptr);
			}
			SetTimer(hwnd, TIMER_SETUP_CURJK, SETUP_CURJK_DELAY, nullptr);
			PostMessage(hwnd, WM_TIMER, TIMER_UPDATE, 0);
			PostMessage(hwnd, WM_TIMER, TIMER_JK_WATCHDOG, 0);
			if (hPanel_) {
				// パネルウィンドウに連動
				panelColor_.SetColor(m_pApp);
				UpdateWindowTheme(hwnd);
				RECT rc;
				GetClientRect(hPanel_, &rc);
				MoveWindow(hwnd, 0, 0, rc.right, rc.bottom, TRUE);
			} else {
				RestorePopupWindowState(hwnd);
			}

			m_pApp->SetPluginCommandState(COMMAND_HIDE_FORCE, 0);
			if (hPanel_ || (s_.hideForceWindow & 1) == 0) {
				ShowWindow(hwnd, SW_SHOWNA);
				SendMessage(hwnd, WM_SET_ZORDER, 0, 0);
			}
			// TVTest起動直後はVideo Containerウィンドウの配置が定まっていないようなので再度整える
			SetTimer(hwnd, TIMER_DONE_SIZE, 500, nullptr);

			// パネルアイテムのサブクラス化
			if (hPanel_) {
				SetTVTestPanelItem(GetDlgItem(hwnd, IDC_RADIO_FORCE), m_pApp, TVTestPanelButtonProc);
				SetTVTestPanelItem(GetDlgItem(hwnd, IDC_RADIO_LOG), m_pApp, TVTestPanelButtonProc);
				SetTVTestPanelItem(GetDlgItem(hwnd, IDC_CHECK_SPECFILE), m_pApp, TVTestPanelButtonProc);
				SetTVTestPanelItem(GetDlgItem(hwnd, IDC_CHECK_RELATIVE), m_pApp, TVTestPanelButtonProc);
				SetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_DOWN), m_pApp, TVTestPanelButtonProc);
				SetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_UP), m_pApp, TVTestPanelButtonProc);
				SetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_TOGGLE), m_pApp, TVTestPanelButtonProc);
				SetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_POPUP), m_pApp, TVTestPanelButtonProc);
				SetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_LOGIN), m_pApp, TVTestPanelButtonProc);
				SetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_HELP), m_pApp, TVTestPanelButtonProc);
			}

			if (s_.commentShareMode == 1 || s_.commentShareMode == 2 || s_.bCheckProcessRecording) {
				// コマンドラインで指定されていればコメント共有の検索用プロセスIDを上書きする
				DWORD processID = 0;
				int argc;
				LPTSTR *argv = CommandLineToArgvW(GetCommandLine(), &argc);
				if (argv) {
					for (int i = 1; i + 1 < argc; ++i) {
						if (_tcsicmp(argv[i], TEXT("/jkshpid")) == 0 ||
						    _tcsicmp(argv[i], TEXT("-jkshpid")) == 0) {
							processID = _tcstoul(argv[i + 1], nullptr, 10);
							if (processID != 0 && s_.bCheckProcessRecording) {
								// 録画状態をチェックするスレッドを起動
								hQuitCheckRecordingEvent_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);
								if (hQuitCheckRecordingEvent_) {
									checkRecordingThread_ = std::thread([this, processID]() { CheckRecordingThread(processID); });
								}
							}
							break;
						}
					}
					LocalFree(argv);
				}
				if (processID == 0) {
					processID = GetCurrentProcessId();
				}
				if (s_.commentShareMode == 1 || s_.commentShareMode == 2) {
					// 投稿機能は投稿欄を表示しているときだけ
					if (!jkTransfer_.Open(hwnd, WMS_TRANSFER, s_.commentShareMode == 2 && cookie_[0], processID)) {
						m_pApp->AddLog(L"設定commentShareModeを有効にできませんでした。", TVTest::LOG_TYPE_WARNING);
					}
				}
			}

			// WebView2 ログ表示の非同期作成
			{
				std::wstring udPath = iniFileName_;
				size_t dot = udPath.rfind(L'.');
				if (dot != std::wstring::npos) udPath = udPath.substr(0, dot);
				udPath += L"_log_webview2";
				HWND hwndCap = hwnd;
				CreateCoreWebView2EnvironmentWithOptions(nullptr, udPath.c_str(), nullptr,
				    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
				        [this, hwndCap](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
				            if (FAILED(hr) || !env || !IsWindow(hwndCap)) return S_OK;
				            env->CreateCoreWebView2Controller(hwndCap,
				                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
				                    [this, hwndCap](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
				                        if (FAILED(hr) || !ctrl || !IsWindow(hwndCap)) return S_OK;
				                        pLogWV2Controller_ = ctrl;
				                        ctrl->get_CoreWebView2(&pLogWV2_);
				                        Microsoft::WRL::ComPtr<ICoreWebView2Settings> st;
				                        if (SUCCEEDED(pLogWV2_->get_Settings(&st)) && st) {
				                            st->put_AreDefaultContextMenusEnabled(FALSE);
				                            st->put_IsZoomControlEnabled(FALSE);
				                            st->put_AreDevToolsEnabled(FALSE);
				                        }
				                        // JS→C++ メッセージ受信（右クリックメニュー）
				                        pLogWV2_->add_WebMessageReceived(
				                            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
				                                [this, hwndCap](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
				                                    LPWSTR raw = nullptr;
				                                    args->TryGetWebMessageAsString(&raw);
				                                    if (!raw || !IsWindow(hwndCap)) { CoTaskMemFree(raw); return S_OK; }
				                                    std::wstring s(raw); CoTaskMemFree(raw);
				                                    // シンプルな JSON フィールド抽出
				                                    auto jstr = [&](const wchar_t* key) {
				                                        std::wstring nd = std::wstring(L"\"") + key + L"\":\"";
				                                        size_t p = s.find(nd);
				                                        if (p == std::wstring::npos) return std::wstring();
				                                        p += nd.size(); std::wstring r;
				                                        while (p < s.size() && s[p] != L'"') {
				                                            if (s[p] == L'\\' && p+1 < s.size()) { ++p; r += s[p]; }
				                                            else r += s[p]; ++p;
				                                        }
				                                        return r;
				                                    };
				                                    auto jint = [&](const wchar_t* key) {
				                                        std::wstring nd = std::wstring(L"\"") + key + L"\":";
				                                        size_t p = s.find(nd);
				                                        return p != std::wstring::npos ? _wtoi(s.c_str() + p + nd.size()) : 0;
				                                    };
				                                    std::wstring marker = jstr(L"m");
				                                    std::wstring timeStr = jstr(L"tm");
				                                    int sx = jint(L"x"), sy = jint(L"y");
				                                    // marker と時刻で logList_ を検索
				                                    int logIdx = -1;
				                                    {
				                                        auto it = logList_.begin();
				                                        for (int i = 0; it != logList_.end(); ++it, ++i) {
				                                            const wchar_t* m = it->marker;
				                                            if (!wcsncmp(m, L"a:", 2)) m += 2;
				                                            wchar_t tm[12]; swprintf_s(tm, L"%02d:%02d:%02d", it->st.wHour, it->st.wMinute, it->st.wSecond);
				                                            if (marker == m && timeStr == tm) { logIdx = i; break; }
				                                        }
				                                        if (logIdx < 0) { // 時刻が一致しない場合は marker のみで検索
				                                            it = logList_.begin();
				                                            for (int i = 0; it != logList_.end(); ++it, ++i) {
				                                                const wchar_t* m = it->marker;
				                                                if (!wcsncmp(m, L"a:", 2)) m += 2;
				                                                if (marker == m) { logIdx = i; break; }
				                                            }
				                                        }
				                                    }
				                                    // コメント投稿
				                                    if (s.find(L"\"cmd\":\"post\"") != std::wstring::npos) {
				                                        std::wstring mail = jstr(L"mail");
				                                        std::wstring text = jstr(L"text");
				                                        if (!text.empty()) {
				                                            std::wstring full = mail.empty() ? text : (L"[" + mail + L"]" + text);
				                                            // lParam でテキストを直接渡す (IDC_CB_POST 経由不要)
				                                            SendMessage(hwndCap, WM_POST_COMMENT, 0, reinterpret_cast<LPARAM>(full.c_str()));
				                                            if (pLogWV2_ && logWV2Ready_)
				                                                pLogWV2_->PostWebMessageAsString(L"{\"cmd\":\"clri\"}");
				                                        }
				                                        return S_OK;
				                                    }
				                                    // 勢いリスト チャンネル選択
				                                    if (s.find(L"\"cmd\":\"fsel\"") != std::wstring::npos) {
				                                        PostMessage(hwndCap, WMS_FORCE_LIST_SEL, (WPARAM)jint(L"id"), 0);
				                                        return S_OK;
				                                    }
				                                    // ダブルクリック NG 登録/解除
				                                    if (s.find(L"\"cmd\":\"dbl\"") != std::wstring::npos) {
				                                        if (logIdx >= 0) ToggleLogListNG(logIdx);
				                                        return S_OK;
				                                    }
				                                    HMENU hMenu = CreatePopupMenu();
				                                    if (!hMenu) return S_OK;
				                                    AppendMenu(hMenu, MF_STRING, ID_FORCE_LIST_COPY, TEXT("コピー(&C)"));
				                                    if (logIdx >= 0) {
				                                        int ngState = GetLogListNGState(logIdx);
				                                        if (ngState >= 0) {
				                                            AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
				                                            AppendMenu(hMenu, MF_STRING, ID_FORCE_LIST_TOGGLE_NG,
				                                                ngState ? TEXT("NG解除(&N)") : TEXT("NG登録(&N)"));
				                                        }
				                                    }
				                                    SetForegroundWindow(hwndCap);
				                                    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, sx, sy, 0, hwndCap, nullptr);
				                                    DestroyMenu(hMenu);
				                                    if (cmd == ID_FORCE_LIST_COPY && logIdx >= 0) {
				                                        auto it = logList_.begin(); std::advance(it, logIdx);
				                                        const wchar_t* m = it->marker;
				                                        if (!wcsncmp(m, L"a:", 2)) m += 2;
				                                        TCHAR buf[512];
				                                        _stprintf_s(buf, TEXT("%02d:%02d:%02d (%.3s)%s"),
				                                            it->st.wHour, it->st.wMinute, it->st.wSecond, m,
				                                            it->bAbone ? TEXT("") : it->text.c_str());
				                                        CopyTextToClipboard(hwndCap, buf);
				                                    } else if (cmd == ID_FORCE_LIST_TOGGLE_NG && logIdx >= 0) {
				                                        ToggleLogListNG(logIdx);
				                                    }
				                                    return S_OK;
				                                }).Get(), &logWV2MsgToken_);
				                        // ページ読み込み完了後に初期化
				                        pLogWV2_->add_NavigationCompleted(
				                            Callback<ICoreWebView2NavigationCompletedEventHandler>(
				                                [this, hwndCap](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
				                                    if (logWV2Ready_) return S_OK;
				                                    logWV2Ready_ = true;
				                                    ApplyLogWV2Theme();
				                                    if (bDisplayLogList_) {
				                                        SendLogWV2Reload();
				                                        logListDisplayedSize_ = (int)logList_.size();
				                                    } else {
				                                        SendForceListWV2Update();
				                                    }
				                                    pLogWV2Controller_->put_IsVisible(TRUE);
				                                    // WebView2 内部 HWND に IDropTarget を登録してファイルドロップを受け取る
				                                    {
				                                        HWND wv2Hwnd = nullptr;
				                                        EnumChildWindows(hwndCap, FindWV2HwndEnum, reinterpret_cast<LPARAM>(&wv2Hwnd));
				                                        if (wv2Hwnd) {
				                                            RevokeDragDrop(wv2Hwnd); // WebView2 の既定ドロップを無効化
				                                            auto* pDT = new CNicoJKDropTarget(hwndCap);
				                                            if (SUCCEEDED(RegisterDragDrop(wv2Hwnd, pDT))) {
				                                                hLogWV2ContentHwnd_ = wv2Hwnd;
				                                            }
				                                            pDT->Release(); // RegisterDragDrop が AddRef 済み
				                                        }
				                                    }
				                                    RECT rc; GetClientRect(hwndCap, &rc);
				                                    PostMessage(hwndCap, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
				                                    return S_OK;
				                                }).Get(), nullptr);
				                        ctrl->put_IsVisible(FALSE);
				                        pLogWV2_->NavigateToString(kLogHtml);
				                        return S_OK;
				                    }).Get());
				            return S_OK;
				        }).Get());
			}

			// channels WebSocket スレッド起動
			if (!s_.channelsWsUri.empty()) {
				hChannelWsQuit_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);
				if (hChannelWsQuit_) {
					channelWsThread_ = std::thread([this, hwnd]() {
						ChannelWsThreadFunc(hwnd, s_.channelsWsUri);
					});
				}
			}
			return 0;
		}
		return -1;
	case WM_DESTROY:
		{
			// WebView2 D&D 解除
			if (hLogWV2ContentHwnd_) {
				RevokeDragDrop(hLogWV2ContentHwnd_);
				hLogWV2ContentHwnd_ = nullptr;
			}
			// channels WebSocket スレッドを停止
			if (channelWsThread_.joinable()) {
				SetEvent(hChannelWsQuit_);
				// 受信中なら WinHttpCloseHandle で WinHttpWebSocketReceive を即中断
				PVOID h = InterlockedExchangePointer(&hChannelWsHandle_, nullptr);
				if (h) WinHttpCloseHandle(static_cast<HINTERNET>(h));
				channelWsThread_.join();
				CloseHandle(hChannelWsQuit_);
				hChannelWsQuit_ = nullptr;
			}
			channelWsConnected_ = false;
			// 未処理の WMS_CHANNEL_WS ポインタを解放
			{
				MSG msg;
				while (PeekMessage(&msg, hwnd, WMS_CHANNEL_WS, WMS_CHANNEL_WS, PM_REMOVE))
					if (msg.lParam) delete reinterpret_cast<ChannelWsMsg*>(msg.lParam);
			}
			programTitleMap_.clear();
			// 録画状態をチェックするスレッドを終了
			if (hQuitCheckRecordingEvent_) {
				SetEvent(hQuitCheckRecordingEvent_);
				checkRecordingThread_.join();
				CloseHandle(hQuitCheckRecordingEvent_);
				hQuitCheckRecordingEvent_ = nullptr;
			}
			// パネルアイテムのサブクラス化を解除
			if (hPanel_) {
				ResetTVTestPanelItem(GetDlgItem(hwnd, IDC_RADIO_FORCE));
				ResetTVTestPanelItem(GetDlgItem(hwnd, IDC_RADIO_LOG));
				ResetTVTestPanelItem(GetDlgItem(hwnd, IDC_CHECK_SPECFILE));
				ResetTVTestPanelItem(GetDlgItem(hwnd, IDC_CHECK_RELATIVE));
				ResetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_DOWN));
				ResetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_UP));
				ResetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_TOGGLE));
				ResetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_POPUP));
				ResetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_LOGIN));
				ResetTVTestPanelItem(GetDlgItem(hwnd, IDC_BUTTON_HELP));
			}
			if (hForceTooltip_) {
				DestroyWindow(hForceTooltip_);
				hForceTooltip_ = nullptr;
			}
			if (hHelpWindow_) {
				DestroyWindow(hHelpWindow_);
				hHelpWindow_ = nullptr;
				hHelpEdit_ = nullptr;
			}
			if (hLoginWindow_) {
				DestroyWindow(hLoginWindow_);
				hLoginWindow_ = nullptr;
				hLoginMailEdit_ = nullptr;
				hLoginPasswordEdit_ = nullptr;
				hLoginOtpEdit_ = nullptr;
				hLoginStatus_ = nullptr;
				hLoginLastLogin_ = nullptr;
			}
			// 位置を保存
			if (!hPanel_ || hPanelPopup_) {
				GetWindowRect(hPanelPopup_ ? hPanelPopup_ : hwnd, &s_.rcForce);
			}
			s_.commentOpacity = (s_.commentOpacity&~0xFF) | commentWindow_.GetOpacity();
			s_.bSetRelative = SendDlgItemMessage(hwnd, IDC_CHECK_RELATIVE, BM_GETCHECK, 0, 0) == BST_CHECKED;
			// ログファイルを閉じる
			WriteToLogfile(-1);
			ReadFromLogfile(-1);
			if (bSpecFile_) {
				DeleteFile(tmpSpecFileName_.c_str());
			}
			commentWindow_.Destroy();

			channelStream_.BeginClose();
			jkStream_.BeginClose();
			loginStream_.BeginClose();
			loginSettingsStream_.BeginClose();
			jkTransfer_.BeginClose();
			channelStream_.Close();
			jkStream_.Close();
			loginStream_.Close();
			loginSettingsStream_.Close();
			jkTransfer_.Close();
			currentJK_ = -1;

			if (syncThread_.joinable()) {
				bQuitSyncThread_ = true;
				syncThread_.join();
			}
			ToggleStreamCallback(false);
			m_pApp->SetWindowMessageCallback(nullptr);
			SaveToIni();
			m_pApp->SetPluginCommandState(COMMAND_HIDE_FORCE, TVTest::PLUGIN_COMMAND_STATE_DISABLED);
			m_pApp->SetPluginCommandState(COMMAND_HIDE_COMMENT, TVTest::PLUGIN_COMMAND_STATE_DISABLED);
			// WebView2 ログ表示を解放
			if (pLogWV2_) { pLogWV2_->remove_WebMessageReceived(logWV2MsgToken_); }
			if (pLogWV2Controller_) { pLogWV2Controller_->Close(); }
			pLogWV2_.Reset(); pLogWV2Controller_.Reset(); logWV2Ready_ = false;
			hForce_ = nullptr;
		}
		break;
	case WM_ERASEBKGND:
		if (hPanel_ && panelColor_.DelaySetColor(m_pApp)) {
			UpdateWindowTheme();
		}
		break;
	case WM_MEASUREITEM:
		break;
	case WM_CLOSE:
		// 隠すだけ
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	case WM_DROPFILES:
		{
			dropFileTimeout_ = 0;
			std::vector<TCHAR> buf(DragQueryFile(reinterpret_cast<HDROP>(wParam), 0, nullptr, 0) + 1);
			if (DragQueryFile(reinterpret_cast<HDROP>(wParam), 0, buf.data(), (UINT)buf.size())) {
				dropFileName_ = buf.data();
				if (bSpecFile_) {
					ReadFromLogfile(-1);
					DeleteFile(tmpSpecFileName_.c_str());
					bSpecFile_ = false;
				}
				SendDlgItemMessage(hwnd, IDC_CHECK_SPECFILE, BM_SETCHECK, BST_UNCHECKED, 0);
				dropFileTimeout_ = 1;
				SetTimer(hwnd, TIMER_OPEN_DROPFILE, 0, nullptr);
			}
		}
		break;
	case WM_HSCROLL:
		if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_SLIDER_OPACITY) && LOWORD(wParam) == SB_THUMBTRACK) {
			SetOpacity(hwnd, HIWORD(wParam) * 255 / 10);
		}
		break;
	case WM_DRAWITEM:
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_RADIO_FORCE:
		case IDC_RADIO_LOG:
			bDisplayLogList_ = SendDlgItemMessage(hwnd, IDC_RADIO_LOG, BM_GETCHECK, 0, 0 ) == BST_CHECKED;
			if (pLogWV2Controller_) pLogWV2Controller_->put_IsVisible(logWV2Ready_ ? TRUE : FALSE);
			SendMessage(hwnd, WM_UPDATE_LIST, TRUE, 0);
			PostMessage(hwnd, WM_TIMER, TIMER_UPDATE, 0);
			break;
		case IDC_CHECK_SPECFILE:
			if (bSpecFile_ != (SendDlgItemMessage(hwnd, IDC_CHECK_SPECFILE, BM_GETCHECK, 0, 0) == BST_CHECKED)) {
				if (bSpecFile_) {
					ReadFromLogfile(-1);
					DeleteFile(tmpSpecFileName_.c_str());
					bSpecFile_ = false;
				} else {
					LONGLONG llft = 0;
					TCHAR path[MAX_PATH];
					bool bRel = SendDlgItemMessage(hwnd, IDC_CHECK_RELATIVE, BM_GETCHECK, 0, 0) == BST_CHECKED;
					// ダイアログを開いている間にD&Dされるかもしれない
					if ((!bRel || (llft = GetCurrentTot()) >= 0) &&
					    FileOpenDialog(hwnd, TEXT("実況ログ(*.jkl;*.xml)\0*.jkl;*.xml\0すべてのファイル\0*.*\0"), path, _countof(path)) &&
					    !bSpecFile_ && ImportLogfile(path, tmpSpecFileName_.c_str(), bRel ? FileTimeToUnixTime(llft) + 2 : 0))
					{
						logReader_.ResetCheckInterval();
						bSpecFile_ = true;
					}
				}
				SendDlgItemMessage(hwnd, IDC_CHECK_SPECFILE, BM_SETCHECK, bSpecFile_ ? BST_CHECKED : BST_UNCHECKED, 0);
			}
			break;
		case IDC_BUTTON_OPACITY_DOWN:
			SetOpacity(hwnd, max((commentWindow_.GetOpacity() * 10 + 254) / 255 - 1, 0) * 255 / 10);
			break;
		case IDC_BUTTON_OPACITY_UP:
			SetOpacity(hwnd, min((commentWindow_.GetOpacity() * 10 + 254) / 255 + 1, 10) * 255 / 10);
			break;
		case IDC_BUTTON_OPACITY_TOGGLE:
			SetOpacity(hwnd, -1);
			break;
		case IDC_BUTTON_POPUP:
			TogglePanelPopup();
			break;
		case IDC_BUTTON_LOGIN:
			ShowNicoLoginWindow();
			break;
		case IDC_BUTTON_HELP:
			ShowLocalCommandHelp();
			break;
		}
		break;
	case WM_TIMER:
		switch (wParam) {
		case TIMER_UPDATE:
			if (currentJK_ >= 0) {
				// 自力計算による勢い情報を更新する
				DWORD tick = GetTickCount();
				if (tick - currentJKForceByChatCountTick_ >= 30000) {
					// 初回は10コメ引く
					int count = max(currentJKChatCount_ - (currentJKForceByChatCount_ < 0 ? 10 : 0), 0);
					currentJKForceByChatCount_ = count * 60 / ((tick - currentJKForceByChatCountTick_) / 1000);
					currentJKChatCount_ = 0;
					currentJKForceByChatCountTick_ = tick;
				}
			}
			if (!bDisplayLogList_ && IsWindowVisible(hwnd)) {
				// 勢いを更新する (channelsWsUri 接続中は WMS_CHANNEL_WS が担うためポーリング不要)
				if (!channelWsConnected_ && !s_.channelsUri.empty() &&
				    channelStream_.Send(hwnd, WMS_FORCE, 'G', s_.channelsUri.c_str())) {
					channelBuf_.clear();
				} else if (!channelWsConnected_) {
					for (auto it = forceList_.begin(); it != forceList_.end(); ++it) {
						// 自力計算による勢い情報を使う
						it->force = it->jkID == currentJK_ ? currentJKForceByChatCount_ : -1;
					}
					SendMessage(hwnd, WM_UPDATE_LIST, 2, 0);
				}
			}
			break;
		case TIMER_JK_WATCHDOG:
			SetTimer(hwnd, TIMER_JK_WATCHDOG, max(JK_WATCHDOG_INTERVAL, 10000), nullptr);
			if (currentJKToGet_ >= 0 && !bUsingLogfileDriver_) {
				// chatStreamIDに変換
				std::vector<FORCE_ELEM>::const_iterator it = LowerBoundJKID(forceList_.begin(), forceList_.end(), currentJKToGet_);
				if (it != forceList_.end() && it->jkID == currentJKToGet_) {
					if (!it->refugeChatStreamID.empty() && !s_.refugeUri.empty()) {
						// 避難所に接続
						std::string uri = s_.refugeUri;
						for (size_t i; (i = uri.find("{jkID}")) != std::string::npos;) {
							char text[16];
							sprintf_s(text, "jk%d", it->jkID);
							uri.replace(i, sizeof("{jkID}") - 1, text);
						}
						for (size_t i; (i = uri.find("{chatStreamID}")) != std::string::npos;) {
							uri.replace(i, sizeof("{chatStreamID}") - 1, it->refugeChatStreamID);
						}
						bool bMix = s_.bRefugeMixing && !it->chatStreamID.empty();

						if (jkStream_.Send(hwnd, WMS_JK, 'R',
						                   ((s_.bDropForwardedComment || bMix ? "2 " : "1 ") + uri +
						                    (bMix ? " " + it->chatStreamID + " " + cookie_ : "")).c_str())) {
							currentJK_ = currentJKToGet_;
							currentJKChatCount_ = 0;
							currentJKForceByChatCount_ = -1;
							currentJKForceByChatCountTick_ = GetTickCount();
							TCHAR text[64];
							_stprintf_s(text, TEXT("%s%sに接続開始しました。"), bMix ? TEXT("ニコニコ実況と") : TEXT(""),
							            s_.refugeUri.find("nx-jikkyo") != std::string::npos ? TEXT("NX-Jikkyo") : TEXT("避難所"));
							OutputMessageLog(text);

							if (bPostToRefugeInverted_) {
								// 一時的な投稿先を戻す
								bPostToRefuge_ = !bPostToRefuge_;
								bPostToRefugeInverted_ = false;
								InvalidateRect(hwnd, nullptr, FALSE);
								ApplyLogWV2Theme();
							}
							if (!bMix && !bPostToRefuge_) {
								// 一時的に投稿先を変える
								bPostToRefuge_ = true;
								bPostToRefugeInverted_ = true;
								InvalidateRect(hwnd, nullptr, FALSE);
								ApplyLogWV2Theme();
							}
							// 過去のコメントの出力状態をリセット
							bNicoReceivingPastChat_ = false;
							bRefugeReceivingPastChat_ = false;
						}
					} else if (!it->chatStreamID.empty() && (s_.refugeUri.empty() || s_.bRefugeMixing)) {
						// ニコニコ実況に接続
						if (jkStream_.Send(hwnd, WMS_JK, 'L', (it->chatStreamID + " " + cookie_).c_str())) {
							currentJK_ = currentJKToGet_;
							currentJKChatCount_ = 0;
							currentJKForceByChatCount_ = -1;
							currentJKForceByChatCountTick_ = GetTickCount();
							OutputMessageLog(TEXT("ニコニコ実況に接続開始しました。"));

							if (bPostToRefugeInverted_) {
								// 一時的な投稿先を戻す
								bPostToRefuge_ = !bPostToRefuge_;
								bPostToRefugeInverted_ = false;
								InvalidateRect(hwnd, nullptr, FALSE);
								ApplyLogWV2Theme();
							}
							if (bPostToRefuge_) {
								// 一時的に投稿先を変える
								bPostToRefuge_ = false;
								bPostToRefugeInverted_ = true;
								InvalidateRect(hwnd, nullptr, FALSE);
								ApplyLogWV2Theme();
							}
							// 過去のコメントの出力状態をリセット
							bNicoReceivingPastChat_ = false;
							bRefugeReceivingPastChat_ = false;
						}
					}
				}
			}
			break;
		case TIMER_FORWARD:
			bFlipFlop_ = !bFlipFlop_;
			if (syncThread_.joinable() || !bHalfSkip_ || bFlipFlop_) {
				bool resyncComment = false;
				{
					lock_recursive_mutex lock(streamLock_);
					if (bResyncComment_) {
						resyncComment = true;
						bResyncComment_ = false;
					}
				}
				// オフセットを調整する
				bool bNotify = false;
				if (0 < forwardOffsetDelta_ && forwardOffsetDelta_ <= 30000) {
					// 前進させて調整
					int delta = min(static_cast<int>(forwardOffsetDelta_), forwardOffsetDelta_ < 10000 ? 500 : 2000);
					forwardOffset_ += delta;
					forwardOffsetDelta_ -= delta;
					bNotify = forwardOffsetDelta_ == 0;
					commentWindow_.Forward(delta);
				} else if (forwardOffsetDelta_ != 0) {
					// ログファイルを閉じて一気に調整
					forwardOffset_ += forwardOffsetDelta_;
					forwardOffsetDelta_ = 0;
					bNotify = true;
					ReadFromLogfile(-1);
					commentWindow_.ClearChat();
				} else if (resyncComment) {
					// シーク時のコメント再生位置の再調整
					ReadFromLogfile(-1);
					commentWindow_.ClearChat();
				}
				if (bNotify) {
					TCHAR text[64];
					int sign = forwardOffset_ < 0 ? -1 : 1;
					LONGLONG absSec = sign * forwardOffset_ / 1000;
					LONGLONG absMin = absSec / 60;
					LONGLONG absHour = absMin / 60;
					if (absSec < 60) {
						_stprintf_s(text, TEXT("(Offset %lld)"), sign * absSec);
					} else if (absMin < 60) {
						_stprintf_s(text, TEXT("(Offset %lld:%02lld)"), sign * absMin, absSec % 60);
					} else if (absHour < 24) {
						_stprintf_s(text, TEXT("(Offset %lld:%02lld:%02lld)"), sign * absHour, absMin % 60, absSec % 60);
					} else {
						_stprintf_s(text, TEXT("(Offset %lld'%02lld:%02lld:%02lld)"), sign * (absHour / 24), absHour % 24, absMin % 60, absSec % 60);
					}
					commentWindow_.AddChat(text, RGB(0, 0xFF, 0xFF), RGB(0, 0, 0), CCommentWindow::CHAT_POS_UE);
				}
				// コメントの表示を進める
				DWORD tick = timeGetTime();
				commentWindow_.Forward(min(static_cast<int>(tick - forwardTick_), 5000));
				forwardTick_ = tick;
				// 過去ログがあれば処理する
				LONGLONG llft = GetCurrentTot();
				if (llft >= 0) {
					bool bRead = false;
					const char *text;
					LONGLONG tm = FileTimeToUnixTime(llft);
					tm = min(max(forwardOffset_ < 0 ? tm - (-forwardOffset_ / 1000) : tm + forwardOffset_ / 1000, 0LL), UINT_MAX - 3600LL);
					while (ReadFromLogfile(bSpecFile_ ? 0 : bUsingLogfileDriver_ ? currentJKToGet_ : -1, &text, static_cast<unsigned int>(tm))) {
						ProcessChatTag(text);
						bRead = true;
						if (!logReader_.IsOpen()) {
							// 次の読み込みは確実に失敗するので省略
							break;
						}
					}
					if (bRead) {
						// date属性値は秒精度しかないのでコメント表示が団子にならないよう適当にごまかす
						commentWindow_.ScatterLatestChats(1000);
						PostMessage(hwnd, WM_UPDATE_LIST, FALSE, 0);
					}
				}
				commentWindow_.Update();
				bPendingTimerForward_ = false;
			}
			break;
		case TIMER_SETUP_CURJK:
			{
				// 視聴状態が変化したので視聴中のサービスに対応する実況IDを調べて変更する
				KillTimer(hwnd, TIMER_SETUP_CURJK);
				DWORD ntsID = GetCurrentNetworkServiceID();
				std::vector<NETWORK_SERVICE_ID_ELEM>::const_iterator it = LowerBoundNetworkServiceID(ntsIDList_.begin(), ntsIDList_.end(), ntsID);
				int jkID = it != ntsIDList_.end() && (it->ntsID == ntsID || (!(ntsID & 0xFFFF) && ntsID == (it->ntsID & 0xFFFF0000))) && it->jkID > 0 ?
					(it->jkID & ~NETWORK_SERVICE_ID_ELEM::JKID_PRIOR) : -1;
				if (currentJKToGet_ != jkID) {
					currentJKToGet_ = jkID;
					jkStream_.Shutdown();
					commentWindow_.ClearChat();
					SetTimer(hwnd, TIMER_JK_WATCHDOG, JK_WATCHDOG_RECONNEC_DELAY, nullptr);
					// 選択項目を更新するため
					SendMessage(hwnd, WM_UPDATE_LIST, TRUE, 0);
				}
			}
			break;
		case TIMER_OPEN_DROPFILE:
			// D&Dされた実況ログファイルを開く
			// TSファイルとの同時D&Dを考慮してRelチェック時は基準とするTOTの取得タイミングを遅らせる
			if (--dropFileTimeout_ < 0 || bSpecFile_) {
				KillTimer(hwnd, TIMER_OPEN_DROPFILE);
			} else {
				LONGLONG llft = 0;
				bool bRel = SendDlgItemMessage(hwnd, IDC_CHECK_RELATIVE, BM_GETCHECK, 0, 0) == BST_CHECKED;
				if (!bRel || (llft = GetCurrentTot()) >= 0) {
					KillTimer(hwnd, TIMER_OPEN_DROPFILE);
					if (ImportLogfile(dropFileName_.c_str(), tmpSpecFileName_.c_str(), bRel ? FileTimeToUnixTime(llft) + 2 : 0)) {
						logReader_.ResetCheckInterval();
						bSpecFile_ = true;
						SendDlgItemMessage(hwnd, IDC_CHECK_SPECFILE, BM_SETCHECK, BST_CHECKED, 0);
					}
				}
			}
			break;
		case TIMER_DONE_MOVE:
			KillTimer(hwnd, TIMER_DONE_MOVE);
			commentWindow_.OnParentMove();
			break;
		case TIMER_DONE_SIZE:
			KillTimer(hwnd, TIMER_DONE_SIZE);
			commentWindow_.OnParentSize();
			bHalfSkip_ = GetWindowHeight(FindVideoContainer()) >= s_.halfSkipThreshold;
			break;
		case TIMER_DONE_POSCHANGE:
			KillTimer(hwnd, TIMER_DONE_POSCHANGE);
			if (!m_pApp->GetFullscreen() && (hPanelPopup_ || (s_.hideForceWindow & 4) || (GetWindowLong(m_pApp->GetAppWindow(), GWL_STYLE) & WS_MAXIMIZE))) {
				SendMessage(hwnd, WM_SET_ZORDER, 0, 0);
			}
			break;
		case TIMER_UPDATE_LIST:
			KillTimer(hwnd, TIMER_UPDATE_LIST);
			lastUpdateListTick_ = 0;
			bPendingTimerUpdateList_ = false;
			SendMessage(hwnd, WM_UPDATE_LIST, FALSE, 0);
			break;
		}
		break;
	case WM_NICOJK_GET_MSGVER:
		return NICOJK_CURRENT_MSGVER;
	case WM_NICOJK_GET_JKID:
		return currentJK_;
	case WM_NICOJK_GET_JKID_TO_GET:
		return currentJKToGet_;
	case WM_NICOJK_OPEN_LOGFILE:
		if ((lParam & NICOJK_OPEN_FLAG_TXT) || (lParam & NICOJK_OPEN_FLAG_JKL) || (lParam & NICOJK_OPEN_FLAG_XML)) {
			// プラグインフォルダからの相対パスを {lParamから3文字}_{wParam数値}.{拡張子} の形式で構築する
			// (ポインタを使うとプロセス外から扱えないのであえてこのようにしている)
			TCHAR name[32] = {};
			name[0] = static_cast<char>(lParam);
			name[1] = static_cast<char>(lParam >> 8);
			name[2] = static_cast<char>(lParam >> 16);
			size_t len = _tcslen(name);
			_stprintf_s(name + len, _countof(name) - len, TEXT("_%u%s"), static_cast<UINT>(wParam),
			            lParam & NICOJK_OPEN_FLAG_JKL ? TEXT(".jkl") :
			            lParam & NICOJK_OPEN_FLAG_XML ? TEXT(".xml") : TEXT(".txt"));
			size_t lastSep = iniFileName_.find_last_of(TEXT("/\\"));
			if (lastSep != tstring::npos) {
				tstring path = iniFileName_.substr(0, lastSep + 1) + name;
				if (bSpecFile_) {
					ReadFromLogfile(-1);
					DeleteFile(tmpSpecFileName_.c_str());
					bSpecFile_ = false;
				}
				SendDlgItemMessage(hwnd, IDC_CHECK_SPECFILE, BM_SETCHECK, BST_UNCHECKED, 0);
				if ((lParam & NICOJK_OPEN_FLAG_RELATIVE) || (lParam & NICOJK_OPEN_FLAG_ABSOLUTE)) {
					SendDlgItemMessage(hwnd, IDC_CHECK_RELATIVE, BM_SETCHECK, lParam & NICOJK_OPEN_FLAG_RELATIVE ? BST_CHECKED : BST_UNCHECKED, 0);
				}
				LONGLONG llft = 0;
				bool bRel = SendDlgItemMessage(hwnd, IDC_CHECK_RELATIVE, BM_GETCHECK, 0, 0) == BST_CHECKED;
				if ((!bRel || (llft = GetCurrentTot()) >= 0) &&
				    ImportLogfile(path.c_str(), tmpSpecFileName_.c_str(), bRel ? FileTimeToUnixTime(llft) + 2 : 0)) {
					logReader_.ResetCheckInterval();
					bSpecFile_ = true;
					SendDlgItemMessage(hwnd, IDC_CHECK_SPECFILE, BM_SETCHECK, BST_CHECKED, 0);
					return TRUE;
				}
			}
		}
		return FALSE;
	case WM_RESET_STREAM:
#ifdef _DEBUG
		OutputDebugString(TEXT("CNicoJK::ForceWindowProcMain() WM_RESET_STREAM\n"));
#endif
		{
			lock_recursive_mutex lock(streamLock_);
			llftTot_ = -1;
		}
		ReadFromLogfile(-1);
		return TRUE;
	case WM_UPDATE_LIST:
		{
			if (!wParam) {
				// 再描画の頻度を抑える
				DWORD tick = lastUpdateListTick_;
				lastUpdateListTick_ = GetTickCount();
				if (bPendingTimerUpdateList_) return TRUE;
				if (lastUpdateListTick_ - tick < COMMENT_REDRAW_INTERVAL) {
					bPendingTimerUpdateList_ = true;
					SetTimer(hwnd, TIMER_UPDATE_LIST, COMMENT_REDRAW_INTERVAL - (lastUpdateListTick_ - tick), nullptr);
					return TRUE;
				}
			}
			if (!bDisplayLogList_ || !IsWindowVisible(hwnd)) {
				for (; logList_.size() > COMMENT_TRIMEND; logList_.pop_front());
				logListDisplayedSize_ = 0;
			}
			if (!IsWindowVisible(hwnd)) return TRUE;
			else if (!bDisplayLogList_ && !wParam) return TRUE;
			if (bDisplayLogList_) {
				if (logWV2Ready_) {
					if (wParam) {
						for (; logList_.size() > COMMENT_TRIMEND; logList_.pop_front());
						logListDisplayedSize_ = (int)logList_.size();
						SendLogWV2Reload();
					} else {
						int trimCount = 0;
						while ((int)logList_.size() > COMMENT_TRIMEND) {
							logList_.pop_front(); ++trimCount;
							if (logListDisplayedSize_ > 0) --logListDisplayedSize_;
						}
						int newCount = (int)logList_.size() - (int)logListDisplayedSize_;
						if (newCount < 0) {
							logListDisplayedSize_ = 0;
							SendLogWV2Reload();
							logListDisplayedSize_ = (int)logList_.size();
						} else if (trimCount > 0 || newCount > 0) {
							SendLogWV2Update(trimCount, newCount);
							logListDisplayedSize_ = (int)logList_.size();
						}
					}
				}
			} else {
				if (logWV2Ready_) {
					SendForceListWV2Update();
				}
			}
		}
		return TRUE;
	case WMS_FORCE:
		{
			static const std::regex reChannel("<((?:bs_|radio_)?channel)>([^]*?)</\\1>");
			static const std::regex reVideo("<video>jk(\\d+)</video>");
			static const std::regex reForce("<force>(\\d+)</force>");
			static const std::regex reName("<name>([^<]*)</name>");

			int ret = channelStream_.ProcessRecv(channelBuf_);
			if (ret < 0) {
				// 切断
				if (ret == -2) {
					channelBuf_.push_back('\0');
					std::cmatch m;
					const char *p = channelBuf_.data();
					const char *pLast = &p[strlen(p)];
					for (; std::regex_search(p, pLast, m, reChannel); p = m[0].second) {
						std::cmatch mVideo;
						if (std::regex_search(m[2].first, m[2].second, mVideo, reVideo)) {
							int jkID = strtol(mVideo[1].first, nullptr, 10);
							std::vector<FORCE_ELEM>::iterator it = LowerBoundJKID(forceList_.begin(), forceList_.end(), jkID);
							if (it != forceList_.end() && it->jkID == jkID) {
								// 勢いと(もしあれば)名前を上書き
								std::cmatch mForce, mName;
								it->force = std::regex_search(m[2].first, m[2].second, mForce, reForce) ? strtol(mForce[1].first, nullptr, 10) : -1;
								if (!it->bFixedName && std::regex_search(m[2].first, m[2].second, mName, reName)) {
									TCHAR szName[64];
									int len = MultiByteToWideChar(CP_UTF8, 0, mName[1].first, static_cast<int>(mName[1].length()), szName, _countof(szName) - 1);
									szName[len] = TEXT('\0');
									DecodeEntityReference(szName);
									it->name = szName;
								}
							}
						}
					}
				}
				SendMessage(hwnd, WM_UPDATE_LIST, 2, 0);
			}
		}
		return TRUE;
	case WMS_FORCE_LIST_SEL:
		// WebView2 勢いリストのチャンネル選択（LBN_SELCHANGE と同じ処理）
		if (!bDisplayLogList_) {
			int jkID = static_cast<int>(wParam);
			if (currentJKToGet_ != jkID) {
				currentJKToGet_ = jkID;
				jkStream_.Shutdown();
				commentWindow_.ClearChat();
				SetTimer(hwnd, TIMER_JK_WATCHDOG, JK_WATCHDOG_RECONNEC_DELAY, nullptr);
			}
			if (s_.bSetChannel && !bUsingLogfileDriver_ && !bRecording_ && jkID > 0) {
				int spaceNum = 0;
				m_pApp->GetTuningSpace(&spaceNum);
				const DWORD currentNtsID = GetCurrentNetworkServiceID();
				bool bSelected = false;
				for (int currentTuning = 0; currentTuning < spaceNum && !bSelected; ++currentTuning) {
					for (int stage = 0; stage < 2 && !bSelected; ++stage) {
						DWORD ntsID;
						for (int i = 0; GetChannelNetworkServiceID(currentTuning, i, &ntsID); ++i) {
							auto it = LowerBoundNetworkServiceID(ntsIDList_.begin(), ntsIDList_.end(), ntsID);
							int chJK = it != ntsIDList_.end() && it->ntsID == ntsID ? it->jkID : -1;
							if ((stage > 0 || (chJK & NETWORK_SERVICE_ID_ELEM::JKID_PRIOR)) && jkID == (chJK & ~NETWORK_SERVICE_ID_ELEM::JKID_PRIOR)) {
								if (ntsID != currentNtsID) {
									TVTest::ChannelSelectInfo cinfo = {};
									cinfo.Size = sizeof(cinfo);
									cinfo.Flags = TVTest::CHANNEL_SELECT_FLAG_STRICTSERVICE;
									cinfo.Space = -1;
									cinfo.Channel = -1;
									if ((ntsID & 0xFFFF) == 0x000F) {
										cinfo.Space = currentTuning;
									} else {
										cinfo.NetworkID = static_cast<WORD>(ntsID & 0xFFFF);
									}
									cinfo.ServiceID = static_cast<WORD>(ntsID >> 16);
									m_pApp->SelectChannel(&cinfo);
								}
								bSelected = true;
								break;
							}
						}
					}
				}
			}
		}
		return 0;
	case WMS_CHANNEL_WS:
		{
			if (!wParam) {
				// 切断通知
				channelWsConnected_ = false;
				break;
			}
			ChannelWsMsg* pMsg = reinterpret_cast<ChannelWsMsg*>(lParam);
			if (!pMsg) break;
			for (const auto& ch : pMsg->channels) {
				// 勢い値を更新
				if (ch.hasForce) {
					auto it = LowerBoundJKID(forceList_.begin(), forceList_.end(), ch.id);
					if (it != forceList_.end() && it->jkID == ch.id)
						it->force = ch.force;
				}
				// 番組タイトルを更新
				if (ch.hasProgramTitle) {
					if (!ch.programTitle.empty())
						programTitleMap_[ch.id] = ch.programTitle;
					else
						programTitleMap_.erase(ch.id); // null
				}
			}
			if (pMsg->type == 1) // snapshot 受信で接続確立とみなす
				channelWsConnected_ = true;
			delete pMsg;
			if (!bDisplayLogList_ && IsWindowVisible(hwnd))
				SendMessage(hwnd, WM_UPDATE_LIST, 2, 0);
		}
		return TRUE;
	case WMS_JK:
		{
			static const std::regex reChatResult("^<chat_result(?= )[^>]*? status=\"(\\d+)\"");
			static const std::regex reXRoom("^<x_room ");
			static const std::regex reNickname("^<x_room(?= )[^>]*? nickname=\"(.*?)\"");
			static const std::regex reIsLoggedIn("^<x_room(?= )[^>]*? is_logged_in=\"1\"");
			static const std::regex reIsRefuge("^<[^>]*? refuge=\"1\"");
			static const std::regex reXDisconnect("^<x_disconnect(?= )[^>]*? status=\"(\\d+)\"");
			static const std::regex reXPastChatBegin("^<x_past_chat_begin ");
			static const std::regex reXPastChatEnd("^<x_past_chat_end ");

			jkBuf_.clear();
			int ret = jkStream_.ProcessRecv(jkBuf_);
			if (ret < 0) {
				// 切断
				OutputMessageLog(TEXT("コメントサーバとの接続を終了しました。"));
				WriteToLogfile(-1);
				currentJK_ = -1;
				if (bPostToRefugeInverted_) {
					// 一時的な投稿先を戻す
					bPostToRefuge_ = !bPostToRefuge_;
					bPostToRefugeInverted_ = false;
					InvalidateRect(hwnd, nullptr, FALSE);
					ApplyLogWV2Theme();
				}
			} else {
				// 受信中
				bool bRead = false;
				for (std::vector<char>::iterator it = jkBuf_.begin(); ; ) {
					std::vector<char>::iterator itEnd = std::find(it, jkBuf_.end(), '\n');
					if (itEnd == jkBuf_.end()) {
						break;
					}
					*itEnd = '\0';
					if (itEnd - it >= CLogReader::CHAT_TAG_MAX) {
						*(it + CLogReader::CHAT_TAG_MAX - 1) = '\0';
					}
					const char *rpl = &*it;
					if (!strncmp(rpl, "<chat ", 6)) {
						// 指定ファイル再生中は混じると鬱陶しいので表示しない。後退指定はある程度反映
						bool bRefuge = false;
						if (ProcessChatTag(rpl, !bSpecFile_, static_cast<int>(min(max(-forwardOffset_, 0LL), 30000LL)), &bRefuge)) {
							bool bReceivingPastChat = bRefuge ? bRefugeReceivingPastChat_ : bNicoReceivingPastChat_;
#ifdef _DEBUG
							OutputDebugString(bReceivingPastChat ? TEXT("#P#") : TEXT("#L#"));
#endif
							// ログの不整合を避けるため過去のコメントは保存しない
							if (!bReceivingPastChat) {
								WriteToLogfile(currentJK_, rpl);
							}
							jkTransfer_.SendChat(currentJK_, rpl);
							++currentJKChatCount_;
							bRead = true;
						}
					} else {
						std::cmatch m;
						if (std::regex_search(rpl, m, reChatResult)) {
							// コメント投稿の応答を取得した
							int status = strtol(m[1].first, nullptr, 10);
							if (status != 0) {
								TCHAR text[64];
								_stprintf_s(text, TEXT("Error:コメント投稿に失敗しました(status=%d)。"), status);
								OutputMessageLog(text);
							}
							jkTransfer_.SendChat(currentJK_, rpl);
						} else if (std::regex_search(rpl, reXRoom)) {
							// 接続情報を取得した
							TCHAR nickname[64];
							nickname[0] = TEXT('\0');
							if (std::regex_search(rpl, m, reNickname)) {
								int len = MultiByteToWideChar(CP_UTF8, 0, m[1].first, static_cast<int>(m[1].length()), nickname, _countof(nickname) - 1);
								nickname[len] = TEXT('\0');
								DecodeEntityReference(nickname);
							}
							bool isLoggedIn = std::regex_search(rpl, reIsLoggedIn);
							bool isRefuge = std::regex_search(rpl, reIsRefuge);
							TCHAR text[128];
							_stprintf_s(text, TEXT("%sに接続しました(%s%s)。"),
							            !isRefuge ? TEXT("ニコニコ実況") : s_.refugeUri.find("nx-jikkyo") != std::string::npos ? TEXT("NX-Jikkyo") : TEXT("避難所"),
							            isLoggedIn ? TEXT("login=") : TEXT(""), nickname);
							OutputMessageLog(text);
						} else if (std::regex_search(rpl, m, reXDisconnect)) {
							// 混合接続時に個々切断した
							int status = strtol(m[1].first, nullptr, 10);
							bool isRefuge = std::regex_search(rpl, reIsRefuge);
							TCHAR text[64];
							_stprintf_s(text, TEXT("%sから切断または接続に失敗しました(status=%d)。"),
							            !isRefuge ? TEXT("ニコニコ実況") : s_.refugeUri.find("nx-jikkyo") != std::string::npos ? TEXT("NX-Jikkyo") : TEXT("避難所"),
							            status);
							// 過去のコメントの出力状態をリセット
							(isRefuge ? bRefugeReceivingPastChat_ : bNicoReceivingPastChat_) = false;
							OutputMessageLog(text);
						} else if (std::regex_search(rpl, m, reXPastChatBegin)) {
							// 過去のコメントの出力開始
							bool isRefuge = std::regex_search(rpl, reIsRefuge);
							(isRefuge ? bRefugeReceivingPastChat_ : bNicoReceivingPastChat_) = true;
						} else if (std::regex_search(rpl, m, reXPastChatEnd)) {
							// 過去のコメントの出力終了
							bool isRefuge = std::regex_search(rpl, reIsRefuge);
							(isRefuge ? bRefugeReceivingPastChat_ : bNicoReceivingPastChat_) = false;
						}
					}
#ifdef _DEBUG
					TCHAR debug[512];
					debug[MultiByteToWideChar(CP_UTF8, 0, rpl, -1, debug, _countof(debug) - 2)] = TEXT('\0');
					_tcscat_s(debug, TEXT("\n"));
					OutputDebugString(debug);
#endif
					it = itEnd + 1;
				}
				if (bRead && bDisplayLogList_) {
					SendMessage(hwnd, WM_UPDATE_LIST, FALSE, 0);
				}
			}
		}
		return TRUE;
	case WMS_LOGIN:
		ProcessJkcnslLoginRecv();
		return TRUE;
	case WMS_LOGIN_SETTINGS:
		ProcessJkcnslLoginSettingsRecv();
		return TRUE;
	case WMS_TRANSFER:
		{
			std::string u8post = jkTransfer_.ProcessRecvPost();
			size_t mailEndPos = u8post.find(']');
			if (mailEndPos != std::string::npos && u8post[0] == '[') {
				if (GetTickCount() - lastPostTick_ < POST_COMMENT_INTERVAL) {
					OutputMessageLog(TEXT("Error:投稿間隔が短すぎます。"));
					jkTransfer_.SendChat(currentJK_, "<!-- M=Post error! Short interval. -->");
				} else if (u8post.size() - mailEndPos - 1 > 0) {
					TCHAR comm[POST_COMMENT_MAX + 1];
					int len = MultiByteToWideChar(CP_UTF8, 0, u8post.c_str() + mailEndPos + 1, -1, comm, _countof(comm) - 1);
					comm[len] = TEXT('\0');
					if (!comm[0]) {
						OutputMessageLog(TEXT("Error:投稿コメントが長すぎます。"));
						jkTransfer_.SendChat(currentJK_, "<!-- M=Post error! Too long. -->");
					} else if (!_tcscmp(comm, lastPostComm_)) {
						OutputMessageLog(TEXT("Error:投稿コメントが前回と同じです。"));
						jkTransfer_.SendChat(currentJK_, "<!-- M=Post error! Same as previous. -->");
					} else {
						if (s_.bAnonymity) {
							u8post.insert(mailEndPos, " 184");
						}
						// コメント投稿
						if (jkStream_.Send(hwnd, WMS_JK, '+', u8post.c_str())) {
							lastPostTick_ = GetTickCount();
							_tcscpy_s(lastPostComm_, comm);
						} else {
							OutputMessageLog(TEXT("Error:コメントサーバに接続していません。"));
							jkTransfer_.SendChat(currentJK_, "<!-- M=Post error! Not connected. -->");
						}
					}
				}
			}
		}
		return TRUE;
	case WM_SET_ZORDER:
		if (!hPanel_ || hPanelPopup_) {
			HWND hPopup = hPanelPopup_ ? hPanelPopup_ : hwnd;
			// 全画面や最大化時は前面のほうが都合がよいはず
			if (hPanelPopup_ || (s_.hideForceWindow & 4) || m_pApp->GetFullscreen() || (GetWindowLong(m_pApp->GetAppWindow(), GWL_STYLE) & WS_MAXIMIZE)) {
				// TVTestウィンドウの前面にもってくる
				SetWindowPos(hPopup, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
				SetWindowPos(hPopup, m_pApp->GetFullscreen() || m_pApp->GetAlwaysOnTop() ? HWND_TOPMOST : HWND_TOP,
				             0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
			} else {
				// TVTestウィンドウの背面にもってくる
				SetWindowPos(hPopup, m_pApp->GetAppWindow(), 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
			}
		}
		return TRUE;
	case WM_POST_COMMENT:
		// lParam: 投稿テキスト (LPCTSTR)。"[mail]comm" または "@localcmd" 形式
		{
			if (!lParam) return TRUE;
			const TCHAR* pFull = reinterpret_cast<const TCHAR*>(lParam);
			// ローカルコマンド (@...) の処理
			if (pFull[0] == TEXT('@')) {
				ProcessLocalPost(&pFull[1]);
				if (pLogWV2_ && logWV2Ready_)
					pLogWV2_->PostWebMessageAsString(L"{\"cmd\":\"clri\"}");
				return TRUE;
			}
			// [mail]comm を解析
			TCHAR comm[POST_COMMENT_MAX + 1] = {};
			TCHAR mail[64] = {};
			size_t i = 0;
			if (pFull[0] == TEXT('[')) {
				i = _tcscspn(pFull, TEXT("]"));
				if (pFull[i] == TEXT(']')) {
					_tcsncpy_s(mail, &pFull[1], min(i - 1, static_cast<size_t>(63)));
					++i;
				}
			}
			_tcsncpy_s(comm, _countof(comm), &pFull[i], _TRUNCATE);
			if (GetTickCount() - lastPostTick_ < POST_COMMENT_INTERVAL) {
				OutputMessageLog(TEXT("Error:投稿間隔が短すぎます。"));
			} else if (_tcslen(comm) >= POST_COMMENT_MAX) {
				OutputMessageLog(TEXT("Error:投稿コメントが長すぎます。"));
			} else if (comm[0] && !_tcscmp(comm, lastPostComm_)) {
				OutputMessageLog(TEXT("Error:投稿コメントが前回と同じです。"));
			} else if (comm[0]) {
				TCHAR post[POST_COMMENT_MAX + 128];
				_stprintf_s(post, TEXT("[%s%s%s]%s"), mail, s_.bAnonymity ? TEXT(" 184") : TEXT(""),
				            bPostToRefuge_ ? TEXT(" refuge") : TEXT(" nico"), comm);
				size_t j = 0;
				for (size_t k = 0; post[k]; ++k) {
					// Tab文字or改行->レコードセパレータ
					post[j] = post[k] == TEXT('\t') || post[k] == TEXT('\n') ? TEXT('\x1e') : post[k];
					if (post[j] != TEXT('\r')) ++j;
				}
				post[j] = TEXT('\0');
				// 文字コード変換
				char u8post[_countof(post) * 3];
				int len = WideCharToMultiByte(CP_UTF8, 0, post, -1, u8post, _countof(u8post) - 1, nullptr, nullptr);
				u8post[len] = '\0';
				// コメント投稿
				if (jkStream_.Send(hwnd, WMS_JK, '+', u8post)) {
					lastPostTick_ = GetTickCount();
					_tcscpy_s(lastPostComm_, comm);
#ifdef _DEBUG
					OutputDebugString(TEXT("##POST##"));
					OutputDebugString(post);
					OutputDebugString(TEXT("\n"));
#endif
				} else {
					OutputMessageLog(TEXT("Error:コメントサーバに接続していません。"));
				}
			}
		}
		return TRUE;
	case WM_TOGGLE_LOG_LIST_NG:
		ToggleLogListNG(static_cast<int>(wParam));
		return TRUE;
	case WM_GET_LOG_LIST_NG_STATE:
		return GetLogListNGState(static_cast<int>(wParam));
	case WM_SHOWWINDOW:
		m_pApp->SetPluginCommandState(COMMAND_HIDE_FORCE, wParam != 0 ? TVTest::COMMAND_ICON_STATE_CHECKED : 0);
		// FALL THROUGH!
	case WM_SIZE:
		{
			RECT rcParent;
			GetClientRect(hwnd, &rcParent);
			{
				int dpi = m_pApp ? m_pApp->GetDPIFromWindow(hwnd) : 96;
				if (dpi == 0) dpi = 96;
				int space = 3 * dpi / 96;
				int listPadding = hPanel_ ? 0 : space;
				int buttonH = 24 * dpi / 96;
				int listLeft = listPadding;
				int listTop = listPadding + buttonH;
				int listWidth = max(0, (int)(rcParent.right) - listLeft * 2);
				int listHeight = max(0, (int)(rcParent.bottom) - listTop);
				int swShow = listHeight < 10 ? SW_HIDE : SW_SHOW;
				if (uMsg == WM_SHOWWINDOW || (GetWindowLong(GetDlgItem(hwnd, IDC_RADIO_FORCE), GWL_STYLE) & WS_VISIBLE ? true : false) != (swShow != SW_HIDE)) {
					ShowWindow(GetDlgItem(hwnd, IDC_RADIO_FORCE), swShow);
					ShowWindow(GetDlgItem(hwnd, IDC_RADIO_LOG), swShow);
					ShowWindow(GetDlgItem(hwnd, IDC_CHECK_SPECFILE), swShow);
					ShowWindow(GetDlgItem(hwnd, IDC_CHECK_RELATIVE), swShow);
					ShowWindow(GetDlgItem(hwnd, IDC_SLIDER_OPACITY), swShow);
					ShowWindow(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_DOWN), swShow);
					ShowWindow(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_UP), swShow);
					ShowWindow(GetDlgItem(hwnd, IDC_BUTTON_OPACITY_TOGGLE), swShow);
					ShowWindow(GetDlgItem(hwnd, IDC_BUTTON_POPUP), swShow);
					ShowWindow(GetDlgItem(hwnd, IDC_BUTTON_LOGIN), swShow);
					ShowWindow(GetDlgItem(hwnd, IDC_BUTTON_HELP), swShow);
				}
				if (pLogWV2Controller_) {
					RECT wv2Bounds = { listLeft, listTop, listLeft + listWidth, listTop + listHeight };
					if (wv2Bounds.right < wv2Bounds.left) wv2Bounds.right = wv2Bounds.left;
					if (wv2Bounds.bottom < wv2Bounds.top) wv2Bounds.bottom = wv2Bounds.top;
					pLogWV2Controller_->put_Bounds(wv2Bounds);
					pLogWV2Controller_->put_IsVisible(logWV2Ready_ ? TRUE : FALSE);
				}
			}
		}
		break;
	case WM_CTLCOLOREDIT:
		break;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ストリームコールバック(別スレッド)
BOOL CALLBACK CNicoJK::StreamCallback(BYTE *pData, void *pClientData)
{
	CNicoJK *pThis = static_cast<CNicoJK*>(pClientData);
	int pid = ((pData[1]&0x1F)<<8) | pData[2];
	BYTE bTransportError = pData[1]&0x80;
	BYTE bPayloadUnitStart = pData[1]&0x40;
	BYTE bHasAdaptation = pData[3]&0x20;
	BYTE bHasPayload = pData[3]&0x10;
	BYTE bAdaptationLength = pData[4];
	BYTE bPcrFlag = pData[5]&0x10;

	// シークやポーズを検出するためにPCRを調べる
	if (bHasAdaptation && bAdaptationLength >= 5 && bPcrFlag && !bTransportError) {
		DWORD pcr = (static_cast<DWORD>(pData[5+1])<<24) | (pData[5+2]<<16) | (pData[5+3]<<8) | pData[5+4];
		// 参照PIDのPCRが現れることなく5回別のPCRが出現すれば、参照PIDを変更する
		if (pid != pThis->pcrPid_) {
			int i = 0;
			for (; pThis->pcrPids_[i] >= 0; ++i) {
				if (pThis->pcrPids_[i] == pid) {
					if (++pThis->pcrPidCounts_[i] >= 5) {
						pThis->pcrPid_ = pid;
					}
					break;
				}
			}
			if (pThis->pcrPids_[i] < 0 && i + 1 < _countof(pThis->pcrPids_)) {
				pThis->pcrPids_[i] = pid;
				pThis->pcrPidCounts_[i] = 1;
				pThis->pcrPids_[++i] = -1;
			}
		}
		if (pid == pThis->pcrPid_) {
			pThis->pcrPids_[0] = -1;
		}
		//OutputDebugString(TEXT("CNicoJK::StreamCallback() PCR\n"));
		lock_recursive_mutex lock(pThis->streamLock_);
		DWORD tick = GetTickCount();
		// 2秒以上PCRを取得できていない→ポーズから回復?
		bool bReset = tick - pThis->pcrTick_ >= 2000;
		pThis->pcrTick_ = tick;
		if (pid == pThis->pcrPid_) {
			long long pcrDiff = CounterDiff(pcr, pThis->pcr_);
			// ラップアラウンド近傍を特別扱いする必要はない(またいでシークする場合だってある)

			if (bReset || 0 <= pcrDiff && pcrDiff < 45000) {
				// 1秒以内は通常の再生と見なす
			} else if (abs(pcrDiff) < 15 * 60 * 45000) {
				// -15～0分、+1秒～15分PCRが飛んでいる場合、シークとみなし、
				// シークした分だけTOTをずらして読み込み直す
				if (pThis->llftTot_ >= 0 && pThis->llftTotPending_ != -1) {
					long long totDiff = pcrDiff * FILETIME_MILLISECOND / 45;
					pThis->llftTot_ += totDiff;
					pThis->totPcr_ += pcr - pThis->pcr_;
					if (pThis->llftTotLast_ >= 0) {
						pThis->llftTotLast_ += totDiff;
					}
					// 保留中のTOTはシーク後に取得した可能性があるので捨てる(再生速度の推定が狂ってコメントが大量に流れたりするのを防ぐため)
					pThis->llftTotPending_ = -2;
					pThis->bResyncComment_ = true;
				} else {
					bReset = true;
				}
			} else {
				// それ以上飛んでたら別ストリームと見なしてリセット
				bReset = true;
			}
			// 保留中のTOTはPCRの取得後に利用可能(llftTot_にシフト)にする
			if (pThis->llftTot_ >= 0) {
				if (pThis->llftTotPending_ >= 0) {
					pThis->llftTotLast_ = pThis->llftTot_;
					pThis->llftTot_ = pThis->llftTotPending_;
					pThis->totTickLast_ = pThis->totTick_;
					pThis->totTick_ = pThis->totTickPending_;
					// TOTの変化と対応するPCRの変化が5秒以上食い違っている場合、TOTがジャンプしたとみなして前回TOTを捨てる
					if (abs((pThis->llftTot_ - pThis->llftTotLast_) / FILETIME_MILLISECOND - CounterDiff(pcr, pThis->totPcr_) / 45) >= 5000) {
						pThis->llftTotLast_ = -1;
						// 状況はシークとそう違わないので読み直しも必要
						// 食い違い地点をまたいでシークすると二度読み直しが発生するが、解決は簡単ではなさそうなので保留
						pThis->bResyncComment_ = true;
					}
				}
				if (pThis->llftTotPending_ != -2) {
					// llftTot_に対応するPCRを取得済みであることを示す
					pThis->llftTotPending_ = -2;
					pThis->totPcr_ = pcr;
				}
			}
			pThis->pcr_ = pcr;
		}
		if (bReset) {
			// TOTを取得できていないことを表す
			pThis->llftTot_ = -1;
			PostMessage(pThis->hForce_, WM_RESET_STREAM, 0, 0);
		}
	}

	// TOTパケットは地上波の実測で6秒に1個程度
	// ARIB規格では最低30秒に1個
	if (pid == 0x14 && bPayloadUnitStart && bHasPayload && !bTransportError) {
		BYTE *pPayload = pData + 4;
		if (bHasAdaptation) {
			// アダプテーションフィールドをスキップする
			if (bAdaptationLength > 182) {
				pPayload = nullptr;
			} else {
				pPayload += 1 + bAdaptationLength;
			}
		}
		if (pPayload) {
			BYTE *pTable = pPayload + 1 + pPayload[0];
			// TOT or TDT (ARIB STD-B10)
			if (pTable + 7 < pData + 188 && (pTable[0] == 0x73 || pTable[0] == 0x70)) {
				// TOT時刻とTickカウントを記録する
				LONGLONG llft = AribToFileTime(&pTable[3]);
				if (llft >= 0) {
					// UTCに変換
					llft += -32400000LL * FILETIME_MILLISECOND;
#ifdef _DEBUG
					OutputDebugString(TEXT("CNicoJK::StreamCallback() TOT\n"));
#endif
					lock_recursive_mutex lock(pThis->streamLock_);
					// 時刻が変化したときだけ
					if (llft != pThis->llftTot_) {
						pThis->llftTotPending_ = llft;
						pThis->totTickPending_ = GetTickCount();
						if (pThis->llftTot_ < 0) {
							// 初回だけ速やかに取得
							pThis->llftTot_ = pThis->llftTotPending_;
							pThis->totTick_ = pThis->totTickPending_;
							pThis->llftTotLast_ = -1;
							pThis->llftTotPending_ = -1;
							pThis->totPcr_ = 0;
						}
					}
				}
			}
		}
	}
	return TRUE;
}

TVTest::CTVTestPlugin *CreatePluginClass()
{
	return new CNicoJK();
}
