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

namespace TVTest
{


namespace
{

constexpr int OpenFolderButton = 100;
constexpr int SkipButton = 101;

bool PrepareFolder(LPCTSTR pszName, LPTSTR pszPath)
{
	if (GetAppClass().GetAppDirectory(pszPath) && ::PathAppend(pszPath, pszName)) {
		if (::CreateDirectory(pszPath, nullptr) || ::PathIsDirectory(pszPath))
			return true;
	}

	::MessageBox(
		nullptr, TEXT("TVTest.exe と同じ場所にフォルダーを作成できませんでした。"),
		APP_NAME TEXT(" セットアップ"), MB_OK | MB_ICONERROR);
	return false;
}

void OpenFolder(HWND hwndOwner, LPCTSTR pszPath)
{
	if (reinterpret_cast<INT_PTR>(
			::ShellExecute(hwndOwner, TEXT("open"), pszPath, nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
		::MessageBox(hwndOwner, pszPath, TEXT("フォルダーを開けませんでした"), MB_OK | MB_ICONERROR);
	}
}

bool ShowFolderStep(
	HWND hwndOwner, LPCTSTR pszPath, LPCTSTR pszTitle, LPCTSTR pszInstructions,
	CDriverManager *pDriverManager)
{
	const TASKDIALOG_BUTTON Buttons[] = {
		{IDOK, pDriverManager != nullptr ? TEXT("次へ") : TEXT("完了")},
		{OpenFolderButton, TEXT("フォルダーを開く")},
		{SkipButton, TEXT("スキップ")},
	};
	TASKDIALOGCONFIG Dialog = {};
	Dialog.cbSize = sizeof(Dialog);
	Dialog.hwndParent = hwndOwner;
	Dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
	Dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON;
	Dialog.pszWindowTitle = APP_NAME TEXT(" セットアップ");
	Dialog.pszMainInstruction = pszTitle;
	const String Content = String(pszInstructions) + TEXT("\n\n") + pszPath;
	Dialog.pszContent = Content.c_str();
	Dialog.cButtons = pDriverManager != nullptr ? 2 : 3;
	Dialog.pButtons = Buttons;
	Dialog.nDefaultButton = IDOK;

	OpenFolder(hwndOwner, pszPath);
	for (;;) {
		int Button;
		if (FAILED(::TaskDialogIndirect(&Dialog, &Button, nullptr, nullptr)))
			return false;

		switch (Button) {
		case OpenFolderButton:
			OpenFolder(hwndOwner, pszPath);
			break;

		case IDOK:
			if (pDriverManager == nullptr)
				return true;
			pDriverManager->Find(pszPath);
			if (pDriverManager->GetDriverInfo(0) != nullptr)
				return true;
			::MessageBox(
				hwndOwner, TEXT("BonDriver フォルダーに BonDriver*.dll を入れてください。"),
				APP_NAME TEXT(" セットアップ"), MB_OK | MB_ICONINFORMATION);
			break;

		case SkipButton:
			return true;

		default:
			return false;
		}
	}
}

} // namespace


bool CInitialSettings::Show(HWND hwndOwner, CDriverManager &DriverManager)
{
	TCHAR szPath[MAX_PATH];

	if (!PrepareFolder(TEXT("BonDriver"), szPath)
			|| !ShowFolderStep(
				hwndOwner, szPath, TEXT("1 / 2  BonDriver"),
				TEXT("開いたフォルダーに、TVTest と同じビット数の BonDriver DLL を入れてから「次へ」を押してください。"),
				&DriverManager))
		return false;

	return PrepareFolder(TEXT("WinSCard"), szPath)
		&& ShowFolderStep(
			hwndOwner, szPath, TEXT("2 / 2  WinSCard"),
			TEXT("WinSCard 互換 DLL を使う場合は、開いたフォルダーに TVTest と同じビット数の DLL を入れてください。")
			TEXT("\n使わない場合は「スキップ」を押してください。"),
			nullptr);
}

} // namespace TVTest
