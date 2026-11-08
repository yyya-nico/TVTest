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
#include "NotificationBar.h"
#include "DrawUtil.h"
#include "Common/DebugDef.h"


namespace TVTest
{

namespace
{

const LPCTSTR NOTIFICATION_BAR_WINDOW_CLASS = APP_NAME TEXT(" Notification Bar");

}




HINSTANCE CNotificationBar::m_hinst = nullptr;


bool CNotificationBar::Initialize(HINSTANCE hinst)
{
	if (m_hinst == nullptr) {
		WNDCLASS wc;

		wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
		wc.lpfnWndProc = WndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = hinst;
		wc.hIcon = nullptr;
		wc.hCursor = nullptr;
		wc.hbrBackground = nullptr;
		wc.lpszMenuName = nullptr;
		wc.lpszClassName = NOTIFICATION_BAR_WINDOW_CLASS;
		if (RegisterClass(&wc) == 0)
			return false;
		m_hinst = hinst;
	}
	return true;
}


CNotificationBar::CNotificationBar(bool fCentered)
	: m_fCentered(fCentered)
{
	GetSystemFont(DrawUtil::FontType::Message, &m_StyleFont);
	m_StyleFont.LogFont.lfHeight = ::MulDiv(m_StyleFont.LogFont.lfHeight, 12, 10);
	m_StyleFont.Size.Value = ::MulDiv(m_StyleFont.Size.Value, 12, 10);
}


bool CNotificationBar::Create(HWND hwndParent, DWORD Style, DWORD ExStyle, int ID)
{
	return CreateBasicWindow(
		hwndParent, Style, ExStyle, ID,
		NOTIFICATION_BAR_WINDOW_CLASS, nullptr, m_hinst);
}


void CNotificationBar::SetStyle(const Style::CStyleManager *pStyleManager)
{
	m_Style.SetStyle(pStyleManager);
}


void CNotificationBar::NormalizeStyle(
	const Style::CStyleManager *pStyleManager,
	const Style::CStyleScaling *pStyleScaling)
{
	m_Style.NormalizeStyle(pStyleManager, pStyleScaling);
}


void CNotificationBar::SetTheme(const Theme::CThemeManager *pThemeManager)
{
	pThemeManager->GetBackgroundStyle(Theme::CThemeManager::STYLE_NOTIFICATIONBAR, &m_BackStyle);

	m_TextColor[static_cast<int>(MessageType::Info)] =
		pThemeManager->GetColor(CColorScheme::COLOR_NOTIFICATIONBARTEXT);
	m_TextColor[static_cast<int>(MessageType::Warning)] =
		pThemeManager->GetColor(CColorScheme::COLOR_NOTIFICATIONBARWARNINGTEXT);
	m_TextColor[static_cast<int>(MessageType::Error)] =
		pThemeManager->GetColor(CColorScheme::COLOR_NOTIFICATIONBARERRORTEXT);

	if (m_hwnd != nullptr)
		Invalidate();
}


bool CNotificationBar::Show(
	LPCTSTR pszText, MessageType Type, DWORD Timeout, bool fSkippable, bool fChannel)
{
	if (m_hwnd == nullptr || IsStringEmpty(pszText))
		return false;

	const bool fReplaceLast = !m_MessageQueue.empty()
		&& m_MessageQueue.back().fSkippable && !m_MessageQueue.back().fChannel;
	bool fChanged = m_MessageQueue.empty();

	if (fChannel) {
		if (!m_MessageQueue.empty() && m_MessageQueue.front().fChannel)
			m_MessageQueue.pop_front();
		m_MessageQueue.push_front({pszText, Type, Timeout, fSkippable, true});
		fChanged = true;
	} else {
		if (fReplaceLast) {
			m_MessageQueue.pop_back();
			fChanged = m_MessageQueue.empty();
		}
		m_MessageQueue.push_back({pszText, Type, Timeout, fSkippable, false});
	}

	if (fChanged)
		UpdateBar();
	return true;
}


void CNotificationBar::ClearChannel()
{
	if (!m_MessageQueue.empty() && m_MessageQueue.front().fChannel) {
		m_MessageQueue.pop_front();
		UpdateBar();
	}
}


void CNotificationBar::SetHost(HWND hwndParent)
{
	if (m_hwnd != nullptr && GetParent() != hwndParent) {
		SetParent(hwndParent);
		AdjustPosition();
	}
}


void CNotificationBar::AdjustPosition()
{
	if (GetVisible()) {
		RECT rc;
		GetBarPosition(&rc);
		SetPosition(&rc);
		::BringWindowToTop(m_hwnd);
	}
}


void CNotificationBar::UpdateBar()
{
	if (m_MessageQueue.empty()) {
		Hide();
		return;
	}
	RECT rc;
	GetBarPosition(&rc);
	SetPosition(&rc);
	::BringWindowToTop(m_hwnd);
	SetVisible(true);
	Invalidate();
	Update();
	SetHideTimer();
}


bool CNotificationBar::Hide()
{
	if (m_hwnd == nullptr)
		return false;

	EndAllTimers();
	SetVisible(false);
	m_MessageQueue.clear();
	return true;
}


bool CNotificationBar::SetFont(const Style::Font &Font)
{
	m_StyleFont = Font;
	if (m_hwnd != nullptr)
		ApplyStyle();
	return true;
}


void CNotificationBar::CalcBarHeight()
{
	const int FontHeight = Style::GetFontHeight(
		m_hwnd, m_Font.GetHandle(), m_Style.TextExtraHeight);
	const int IconHeight = m_Style.IconSize.Height + m_Style.IconMargin.Vert();
	const int TextHeight = FontHeight + m_Style.TextMargin.Vert();

	m_BarHeight = std::max(IconHeight, TextHeight) + m_Style.Padding.Vert();
	m_ChannelNameHeight = Style::GetFontHeight(
		m_hwnd, m_ChannelNameFont.GetHandle(), m_Style.TextExtraHeight);
	m_ProgramTitleHeight = Style::GetFontHeight(
		m_hwnd, m_ProgramTitleFont.GetHandle(), m_Style.TextExtraHeight);
}


int CNotificationBar::GetBarHeight() const
{
	if (!m_MessageQueue.empty() && m_MessageQueue.front().fChannel) {
		return m_Style.ChannelPadding.Vert() + m_ChannelNameHeight
			+ (m_MessageQueue.front().Text.find(TEXT('\n')) != String::npos ? m_ProgramTitleHeight : 0);
	}
	return m_BarHeight;
}


void CNotificationBar::GetBarPosition(RECT *pRect) const
{
	::GetClientRect(::GetParent(m_hwnd), pRect);
	if (!m_fCentered) {
		pRect->bottom = GetBarHeight();
		return;
	}

	const int HostWidth = pRect->right - pRect->left;
	const int HostHeight = pRect->bottom - pRect->top;
	const int Margin = m_pStyleScaling->LogicalPixelsToPhysicalPixels(16);
	const int MaxWidth = m_pStyleScaling->LogicalPixelsToPhysicalPixels(640);
	const int Width = std::min(std::max(0, HostWidth - Margin * 2), MaxWidth);
	int Height = GetBarHeight();

	if (!m_MessageQueue.empty()) {
		const HDC hdc = ::GetDC(m_hwnd);
		if (hdc != nullptr) {
			const HFONT hOldFont = SelectFont(hdc, m_Font.GetHandle());
			const int TextWidth = Width - m_Style.Padding.Horz() - m_Style.TextMargin.Horz()
				- m_Style.IconSize.Width - m_Style.IconMargin.Horz();
			RECT rcText = {0, 0, std::max(1, TextWidth), 0};
			::DrawText(
				hdc, m_MessageQueue.front().Text.c_str(), -1, &rcText,
				DT_LEFT | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
			::SelectObject(hdc, hOldFont);
			::ReleaseDC(m_hwnd, hdc);
			Height = std::max(Height, static_cast<int>(rcText.bottom) + m_Style.Padding.Vert()
				+ m_Style.TextMargin.Vert() + m_Style.TextExtraHeight);
		}
	}
	Height = std::min(Height, std::max(0, HostHeight - Margin * 2));

	pRect->left = (HostWidth - Width) / 2;
	pRect->top = (HostHeight - Height) / 2;
	pRect->right = pRect->left + Width;
	pRect->bottom = pRect->top + Height;
}


void CNotificationBar::SetHideTimer()
{
	if (!m_MessageQueue.empty()) {
		const DWORD Timeout = m_MessageQueue.front().Timeout;
		if (Timeout != 0)
			BeginTimer(TIMER_ID_HIDE, Timeout);
		else
			EndTimer(TIMER_ID_HIDE);
	}
}


LRESULT CNotificationBar::OnMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_CREATE:
		{
			InitializeUI();

			InitializeTimer(hwnd);
		}
		return 0;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			RECT rc;

			::BeginPaint(hwnd, &ps);

			::GetClientRect(hwnd, &rc);

			{
				Theme::CThemeDraw ThemeDraw(BeginThemeDraw(ps.hdc));
				ThemeDraw.Draw(m_BackStyle, rc);
			}

			if (!m_MessageQueue.empty()) {
				const MessageInfo &Info = m_MessageQueue.front();

				if (Info.fChannel) {
					Style::Subtract(&rc, m_Style.ChannelPadding);
					const auto Break = Info.Text.find(TEXT('\n'));
					constexpr UINT Format = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
					RECT rcName = rc;
					rcName.bottom = rcName.top + m_ChannelNameHeight;
					const String Name = Info.Text.substr(0, Break);
					const COLORREF TextColor = m_TextColor[static_cast<int>(Info.Type)];
					DrawUtil::DrawText(ps.hdc, Name.c_str(), rcName, Format, &m_ChannelNameFont, TextColor);
					if (Break != String::npos) {
						rc.top = rcName.bottom;
						DrawUtil::DrawText(
							ps.hdc, Info.Text.c_str() + Break + 1, rc, Format, &m_ProgramTitleFont, TextColor);
					}
				} else {
					Style::Subtract(&rc, m_Style.Padding);
					if (rc.left < rc.right) {
						const int TypeIndex = static_cast<int>(Info.Type);

						if (TypeIndex >= 0 && TypeIndex < lengthof(m_Icons) && m_Icons[TypeIndex]) {
							rc.left += m_Style.IconMargin.Left;
							::DrawIconEx(
								ps.hdc,
								rc.left,
								rc.top + m_Style.IconMargin.Top +
								((rc.bottom - rc.top) - m_Style.IconMargin.Vert() - m_Style.IconSize.Height) / 2,
								m_Icons[TypeIndex],
								m_Style.IconSize.Width, m_Style.IconSize.Height,
								0, nullptr, DI_NORMAL);
							rc.left += m_Style.IconSize.Width + m_Style.IconMargin.Right;
						}
						Style::Subtract(&rc, m_Style.TextMargin);
						DrawUtil::DrawText(
							ps.hdc, Info.Text.c_str(), rc,
							DT_NOPREFIX | DT_END_ELLIPSIS
								| (m_fCentered ? DT_CENTER | DT_WORDBREAK : DT_LEFT | DT_SINGLELINE | DT_VCENTER),
							&m_Font, m_TextColor[TypeIndex]);
					}
				}
			}

			::EndPaint(hwnd, &ps);
		}
		return 0;

	case WM_TIMER:
		if (wParam == TIMER_ID_HIDE && !m_MessageQueue.empty()) {
			m_MessageQueue.pop_front();
			UpdateBar();
		}
		return 0;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
	case WM_MOUSEMOVE:
		{
			POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			const HWND hwndParent = ::GetParent(hwnd);

			::MapWindowPoints(hwnd, hwndParent, &pt, 1);
			return ::SendMessage(hwndParent, uMsg, wParam, MAKELPARAM(pt.x, pt.y));
		}

	case WM_NCHITTEST:
		return HTTRANSPARENT;
	}

	return CCustomWindow::OnMessage(hwnd, uMsg, wParam, lParam);
}


void CNotificationBar::ApplyStyle()
{
	if (m_hwnd == nullptr)
		return;

	CreateDrawFont(m_StyleFont, &m_Font);
	Style::Font Font = m_StyleFont;
	Font.Size.Value = ::MulDiv(m_StyleFont.Size.Value, 110, 100);
	CreateDrawFont(Font, &m_ChannelNameFont);
	Font.Size.Value = ::MulDiv(m_StyleFont.Size.Value, 155, 100);
	Font.LogFont.lfWeight = std::max<LONG>(Font.LogFont.lfWeight, FW_BOLD);
	CreateDrawFont(Font, &m_ProgramTitleFont);

	/*
	m_Icons[static_cast<int>(MessageType::Info)].Attach(
		LoadSystemIcon(IDI_INFORMATION, m_Style.IconSize.Width, m_Style.IconSize.Height));
	*/
	m_Icons[static_cast<int>(MessageType::Warning)].Attach(
		LoadSystemIcon(IDI_WARNING, m_Style.IconSize.Width, m_Style.IconSize.Height));
	m_Icons[static_cast<int>(MessageType::Error)].Attach(
		LoadSystemIcon(IDI_ERROR, m_Style.IconSize.Width, m_Style.IconSize.Height));

	CalcBarHeight();
}




void CNotificationBar::NotificationBarStyle::SetStyle(const Style::CStyleManager *pStyleManager)
{
	*this = NotificationBarStyle();
	pStyleManager->Get(TEXT("notification-bar.padding"), &Padding);
	pStyleManager->Get(TEXT("notification-bar.icon"), &IconSize);
	pStyleManager->Get(TEXT("notification-bar.icon.margin"), &IconMargin);
	pStyleManager->Get(TEXT("notification-bar.text.margin"), &TextMargin);
	pStyleManager->Get(TEXT("notification-bar.text.extra-height"), &TextExtraHeight);
}


void CNotificationBar::NotificationBarStyle::NormalizeStyle(
	const Style::CStyleManager *pStyleManager,
	const Style::CStyleScaling *pStyleScaling)
{
	pStyleScaling->ToPixels(&Padding);
	pStyleScaling->ToPixels(&ChannelPadding);
	pStyleScaling->ToPixels(&IconSize);
	pStyleScaling->ToPixels(&IconMargin);
	pStyleScaling->ToPixels(&TextMargin);
	pStyleScaling->ToPixels(&TextExtraHeight);
}


} // namespace TVTest
