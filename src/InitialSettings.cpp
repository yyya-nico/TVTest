/*
  TVTest
  Copyright(c) 2008-2020 DBCTRADO

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include "stdafx.h"
#include "TVTest.h"
#include "AppMain.h"
#include "InitialSettings.h"
#include "Dialog.h"
#include "DarkMode.h"
#include "LibISDB/LibISDB/Windows/Utilities/COMUtilities.hpp"
#include "resource.h"
#include <shobjidl.h>

namespace TVTest
{

namespace
{

constexpr LPCTSTR SetupCaption = APP_NAME TEXT(" セットアップ");

class CSetupWizard final
	: public CResizableDialog
{
public:
	explicit CSetupWizard(CDriverManager &DriverManager)
		: m_DriverManager(DriverManager)
	{
	}

	bool Show(HWND hwndOwner) override;

private:
	enum class Page {
		Welcome,
		BonDriver,
		WinSCard,
	};

	static constexpr UINT_PTR TIMER_ID_WINSCARD_FILES = 1;
	static inline thread_local CSetupWizard *m_pActiveWizard = nullptr;
	static LRESULT CALLBACK MessageFilter(int Code, WPARAM wParam, LPARAM lParam);

	CDriverManager &m_DriverManager;
	Page m_Page = Page::Welcome;
	TCHAR m_szDestination[MAX_PATH] = {};
	LibISDB::COMPointer<IExplorerBrowser> m_Browser;
	DrawUtil::CFont m_TitleFont;

	INT_PTR DlgProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam) override;
	void RealizeStyle() override;
	void ShowPage(Page NewPage);
	void UpdateWinSCardButton();
	void Next();
	void Finish();
	bool FindBonDrivers();
	bool OpenBrowser(HWND hwndHost);
	void CloseBrowser();
	void ResizeBrowser();
	void ImportFiles();
};


bool CSetupWizard::Show(HWND hwndOwner)
{
	// OLE is required by the Shell view's native drag-and-drop and clipboard support.
	const HRESULT OleResult = ::OleInitialize(nullptr);
	CSetupWizard *pPreviousWizard = m_pActiveWizard;
	m_pActiveWizard = this;

	const HHOOK hHook = ::SetWindowsHookEx(WH_MSGFILTER, MessageFilter, nullptr, ::GetCurrentThreadId());
	const int Result = ShowDialog(
		hwndOwner, GetAppClass().GetResourceInstance(), MAKEINTRESOURCE(IDD_INITIALSETTINGS));
	if (hHook != nullptr)
		::UnhookWindowsHookEx(hHook);
	m_pActiveWizard = pPreviousWizard;

	if (SUCCEEDED(OleResult))
		::OleUninitialize();
	return Result == IDOK;
}


LRESULT CALLBACK CSetupWizard::MessageFilter(int Code, WPARAM wParam, LPARAM lParam)
{
	if (Code == MSGF_DIALOGBOX && m_pActiveWizard != nullptr) {
		MSG *pMsg = reinterpret_cast<MSG *>(lParam);
		const CSetupWizard &Wizard = *m_pActiveWizard;
		const HWND hwndHost = ::GetDlgItem(Wizard.m_hDlg, IDC_SETUP_BROWSER);
		if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST
				&& hwndHost != nullptr && ::IsChild(hwndHost, pMsg->hwnd) && Wizard.m_Browser) {
			// Give the focused Shell view first refusal before the modal dialog handles the key.
			LibISDB::COMPointer<IInputObject> Input;
			if (SUCCEEDED(Wizard.m_Browser.QueryInterface(&Input))) {
				if (Input->TranslateAcceleratorIO(pMsg) == S_OK)
					return TRUE;
			} else {
				LibISDB::COMPointer<IShellView> View;
				if (SUCCEEDED(Wizard.m_Browser->GetCurrentView(IID_PPV_ARGS(View.GetPP())))
						&& View->TranslateAccelerator(pMsg) == S_OK)
					return TRUE;
			}
		}
	}

	return ::CallNextHookEx(nullptr, Code, wParam, lParam);
}


INT_PTR CSetupWizard::DlgProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg) {
	case WM_INITDIALOG:
		AddControls({
			{IDC_SETUP_BROWSER, AlignFlag::All},
			{IDC_SETUP_DESTINATION, AlignFlag::Horz},
			{IDC_SETUP_IMPORT, AlignFlag::Bottom},
			{IDC_SETUP_STATUS, AlignFlag::HorzBottom},
			{IDCANCEL, AlignFlag::BottomRight},
		});
		RealizeStyle();
		ShowPage(Page::Welcome);
		return FALSE;

	case WM_SIZE:
		ResizeBrowser();
		break;

	case WM_TIMER:
		if (wParam == TIMER_ID_WINSCARD_FILES) {
			UpdateWinSCardButton();
			return TRUE;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			Next();
			return TRUE;

		case IDC_SETUP_BACK:
			ShowPage(m_Page == Page::WinSCard ? Page::BonDriver : Page::Welcome);
			return TRUE;

		case IDC_SETUP_IMPORT:
			ImportFiles();
			return TRUE;

		case IDCANCEL:
			::EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}
		break;

	case WM_DESTROY:
		::KillTimer(hDlg, TIMER_ID_WINSCARD_FILES);
		CloseBrowser();
		break;
	}

	return FALSE;
}


void CSetupWizard::RealizeStyle()
{
	CResizableDialog::RealizeStyle();

	if (m_hDlg != nullptr) {
		LOGFONT lf;
		if (m_Font.GetLogFont(&lf)) {
			lf.lfHeight = ::MulDiv(lf.lfHeight, 22, 10);
			lf.lfWeight = FW_BOLD;
			m_TitleFont.Create(&lf);
			::SendDlgItemMessage(
				m_hDlg, IDC_SETUP_TITLE, WM_SETFONT,
				reinterpret_cast<WPARAM>(m_TitleFont.GetHandle()), TRUE);
		}
		ResizeBrowser();
	}
}


void CSetupWizard::ShowPage(Page NewPage)
{
	const bool fImport = NewPage != Page::Welcome;
	const bool fOptional = NewPage == Page::WinSCard;
	const LPCTSTR pszFolder = fOptional ? TEXT("WinSCard") : TEXT("BonDriver");

	TCHAR szPath[MAX_PATH] = {};
	const bool fHaveFolder = fImport && GetAppClass().GetAppDirectory(szPath)
		&& ::PathAppend(szPath, pszFolder)
		&& (::CreateDirectory(szPath, nullptr) || ::PathIsDirectory(szPath));
	if (NewPage == Page::BonDriver && !fHaveFolder) {
		::MessageBox(m_hDlg, TEXT("BonDriver フォルダーを作成できませんでした。"), SetupCaption, MB_OK | MB_ICONERROR);
		return;
	}

	::KillTimer(m_hDlg, TIMER_ID_WINSCARD_FILES);
	CloseBrowser();
	m_Page = NewPage;
	StringCopy(m_szDestination, fHaveFolder ? szPath : TEXT(""));
	const HWND hwndHost = ::GetDlgItem(m_hDlg, IDC_SETUP_BROWSER);
	::ShowWindow(hwndHost, fHaveFolder ? SW_SHOW : SW_HIDE);
	::ShowWindow(::GetDlgItem(m_hDlg, IDC_SETUP_BACK), fImport ? SW_SHOW : SW_HIDE);
	::ShowWindow(::GetDlgItem(m_hDlg, IDC_SETUP_IMPORT), fHaveFolder ? SW_SHOW : SW_HIDE);

	::SetDlgItemText(m_hDlg, IDC_SETUP_BACK,
		fOptional ? TEXT("← BonDriver に戻る") : TEXT("← はじめに戻る"));
	::SetDlgItemText(m_hDlg, IDC_SETUP_TITLE,
		!fImport ? TEXT("視聴をはじめる") :
		fOptional ? TEXT("WinSCard をインポート") : TEXT("BonDriver をインポート"));
	::SetDlgItemText(m_hDlg, IDC_SETUP_DESCRIPTION,
		!fImport ? TEXT("視聴に使う BonDriver をセットアップします。必要に応じて WinSCard 互換 DLL も追加できます。") :
		fOptional ? TEXT("WinSCard 互換 DLL と関連ファイルを選択してください。使わない場合はスキップできます。") :
		TEXT("お使いになる BonDriver DLL と関連ファイルを選択してください。選んだファイルは右のフォルダーにコピーされます。"));
	if (fOptional) {
		UpdateWinSCardButton();
		if (fHaveFolder)
			::SetTimer(m_hDlg, TIMER_ID_WINSCARD_FILES, 500, nullptr);
	} else {
		::SetDlgItemText(m_hDlg, IDOK, fImport ? TEXT("次へ  →") : TEXT("セットアップを始める  →"));
	}
	::SetDlgItemText(m_hDlg, IDC_SETUP_DESTINATION, m_szDestination);
	::SetDlgItemText(m_hDlg, IDC_SETUP_STATUS, TEXT(""));

	if (fHaveFolder && !OpenBrowser(hwndHost)) {
		::SetDlgItemText(m_hDlg, IDC_SETUP_STATUS, TEXT("一覧を開けませんでした。ファイルのインポートはできます。"));
	} else if (fOptional && !fHaveFolder) {
		::SetDlgItemText(m_hDlg, IDC_SETUP_STATUS, TEXT("WinSCard フォルダーを作成できませんでした。"));
	}

	::SetFocus(::GetDlgItem(m_hDlg, fHaveFolder ? IDC_SETUP_IMPORT : IDOK));
}


void CSetupWizard::UpdateWinSCardButton()
{
	if (m_Page != Page::WinSCard)
		return;

	bool fHasFiles = false;
	TCHAR szPattern[MAX_PATH];
	if (m_szDestination[0] != TEXT('\0') && ::PathCombine(szPattern, m_szDestination, TEXT("*"))) {
		WIN32_FIND_DATA Data;
		const HANDLE hFind = ::FindFirstFileEx(
			szPattern, FindExInfoBasic, &Data, FindExSearchNameMatch, nullptr, 0);
		if (hFind != INVALID_HANDLE_VALUE) {
			do {
				if ((Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
					fHasFiles = true;
					break;
				}
			} while (::FindNextFile(hFind, &Data));
			::FindClose(hFind);
		}
	}

	const LPCTSTR pszLabel = fHasFiles ? TEXT("完了") : TEXT("スキップして完了");
	TCHAR szLabel[32];
	::GetDlgItemText(m_hDlg, IDOK, szLabel, lengthof(szLabel));
	if (::lstrcmp(szLabel, pszLabel) != 0)
		::SetDlgItemText(m_hDlg, IDOK, pszLabel);
}


bool CSetupWizard::FindBonDrivers()
{
	TCHAR szPath[MAX_PATH];
	if (GetAppClass().GetAppDirectory(szPath) && ::PathAppend(szPath, TEXT("BonDriver"))) {
		m_DriverManager.Find(szPath);
		if (m_DriverManager.GetDriverInfo(0) != nullptr)
			return true;
	}

	::MessageBox(
		m_hDlg, TEXT("BonDriver フォルダーに BonDriver*.dll を追加してください。"),
		SetupCaption, MB_OK | MB_ICONINFORMATION);
	return false;
}


void CSetupWizard::Next()
{
	switch (m_Page) {
	case Page::Welcome:
		ShowPage(Page::BonDriver);
		break;

	case Page::BonDriver:
		if (FindBonDrivers())
			ShowPage(Page::WinSCard);
		break;

	case Page::WinSCard:
		Finish();
		break;
	}
}


void CSetupWizard::Finish()
{
	if (FindBonDrivers())
		::EndDialog(m_hDlg, IDOK);
	else
		ShowPage(Page::BonDriver);
}


bool CSetupWizard::OpenBrowser(HWND hwndHost)
{
	LibISDB::COMPointer<IShellItem> Folder;
	if (FAILED(::SHCreateItemFromParsingName(m_szDestination, nullptr, IID_PPV_ARGS(Folder.GetPP())))
			|| FAILED(::CoCreateInstance(
				__uuidof(ExplorerBrowser), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(m_Browser.GetPP()))))
		return false;

	if (IsDarkMode())
		SetWindowAllowDarkMode(hwndHost, true);

	RECT rc;
	::GetClientRect(hwndHost, &rc);
	FOLDERSETTINGS Settings = {FVM_DETAILS, FWF_NOCLIENTEDGE | FWF_NOGROUPING};
	if (FAILED(m_Browser->Initialize(hwndHost, &rc, &Settings))) {
		m_Browser.Release();
		return false;
	}

	m_Browser->SetOptions(EBO_NAVIGATEONCE | EBO_NOTRAVELLOG | EBO_NOPERSISTVIEWSTATE);
	m_Browser->SetEmptyText(TEXT("「ファイルを選択」からファイルを追加できます。"));
	if (FAILED(m_Browser->BrowseToObject(Folder.Get(), SBSP_ABSOLUTE))) {
		CloseBrowser();
		return false;
	}

	return true;
}


void CSetupWizard::CloseBrowser()
{
	if (m_Browser) {
		m_Browser->Destroy();
		m_Browser.Release();
	}
}


void CSetupWizard::ResizeBrowser()
{
	if (m_Browser) {
		RECT rc;
		::GetClientRect(::GetDlgItem(m_hDlg, IDC_SETUP_BROWSER), &rc);
		m_Browser->SetRect(nullptr, rc);
	}
}


void CSetupWizard::ImportFiles()
{
	LibISDB::COMPointer<IFileOpenDialog> Picker;
	FILEOPENDIALOGOPTIONS Options;
	HRESULT Result = ::CoCreateInstance(
		__uuidof(::FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(Picker.GetPP()));
	if (SUCCEEDED(Result))
		Result = Picker->GetOptions(&Options);
	if (SUCCEEDED(Result))
		Result = Picker->SetOptions(Options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT);
	if (SUCCEEDED(Result)) {
		const COMDLG_FILTERSPEC Filter = {TEXT("すべてのファイル"), TEXT("*.*")};
		Picker->SetFileTypes(1, &Filter);
		Picker->SetTitle(TEXT("インポートするファイルを選択"));
		Picker->SetOkButtonLabel(TEXT("コピー"));
		Result = Picker->Show(m_hDlg);
	}
	if (Result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
		return;

	LibISDB::COMPointer<IShellItemArray> Files;
	LibISDB::COMPointer<IShellItem> Folder;
	LibISDB::COMPointer<IFileOperation> Copy;
	if (SUCCEEDED(Result))
		Result = Picker->GetResults(Files.GetPP());
	if (SUCCEEDED(Result))
		Result = ::SHCreateItemFromParsingName(m_szDestination, nullptr, IID_PPV_ARGS(Folder.GetPP()));
	if (SUCCEEDED(Result)) {
		Result = ::CoCreateInstance(
			__uuidof(FileOperation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(Copy.GetPP()));
	}
	if (SUCCEEDED(Result))
		Result = Copy->SetOwnerWindow(m_hDlg);
	if (SUCCEEDED(Result))
		Result = Copy->CopyItems(Files.Get(), Folder.Get());

	BOOL fAborted = FALSE;
	if (SUCCEEDED(Result)) {
		Result = Copy->PerformOperations();
		const HRESULT AbortResult = Copy->GetAnyOperationsAborted(&fAborted);
		if (SUCCEEDED(Result))
			Result = AbortResult;

		LibISDB::COMPointer<IShellView> View;
		if (m_Browser && SUCCEEDED(m_Browser->GetCurrentView(IID_PPV_ARGS(View.GetPP()))))
			View->Refresh();
	}

	UpdateWinSCardButton();
	::SetDlgItemText(m_hDlg, IDC_SETUP_STATUS,
		fAborted ? TEXT("コピーが中断されました。一覧でコピー済みのファイルを確認してください。") :
		FAILED(Result) ? TEXT("インポートを完了できませんでした。コピー先とファイルを確認してください。") :
		TEXT("ファイルをコピーしました。"));
}

} // namespace


bool CInitialSettings::Show(HWND hwndOwner, CDriverManager &DriverManager)
{
	CSetupWizard Wizard(DriverManager);
	return Wizard.Show(hwndOwner);
}

} // namespace TVTest
