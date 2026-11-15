#include "DxUserInput.h"

#include "ActiveDocView.h"
#include "MainFrm.h"
#include "UserInputUtil.h"
#include "rbxgraphics/TextureProxyBase.h"
#include "util/Rect.h"
#include "util/StandardOut.h"
#include "v8datamodel/DataModel.h"

#include <windowsx.h>

// FUNCTION: ROBLOX 0x0046b7f0
bool DxUserInput::FUN_0046b7f0()
{
	G3D::Vector2 extent = m_unk0x58.center();
	if (m_unk0x70.x < -extent.x) {
		return false;
	}

	if (m_unk0x70.x > extent.x) {
		return false;
	}

	if (m_unk0x70.y < -extent.y) {
		return false;
	}

	if (m_unk0x70.y > extent.y) {
		return false;
	}

	return true;
}

// FUNCTION: ROBLOX 0x0046b880
bool DxUserInput::FUN_0046b880()
{
	CRect rect;
	m_unk0x180->GetClientRect(&rect);

	CPoint clientOrigin = rect.TopLeft();
	CPoint screenOrigin = clientOrigin;
	m_unk0x180->ClientToScreen(&screenOrigin);

	if (screenOrigin == clientOrigin) {
		return true;
	}
	return false;
}

// FUNCTION: ROBLOX 0x0046b8f0
G3D::Vector2int16 DxUserInput::FUN_0046b8f0()
{
	return G3D::Vector2int16((G3D::int16) m_unk0x58.width(), (G3D::int16) m_unk0x58.height());
}

// FUNCTION: ROBLOX 0x0046b920
bool DxUserInput::FUN_0046b920()
{
	return keyDown(SDLK_w) || keyDown(SDLK_s) || keyDown(SDLK_a) || keyDown(SDLK_d) || keyDown(SDLK_UP) ||
		   keyDown(SDLK_DOWN) || keyDown(SDLK_LEFT) || keyDown(SDLK_RIGHT);
}

// FUNCTION: ROBLOX 0x0046b9b0
void DxUserInput::setKeyState(SDLKey key, bool down)
{
	m_unk0x17c = down ? key : SDLK_UNKNOWN;
}

// STUB: ROBLOX 0x0046b9d0
void DxUserInput::FUN_0046b9d0()
{
	HRESULT hr = m_unk0x188->CreateDevice(GUID_SysMouse, &m_unk0x18c, NULL);
	if (FAILED(hr)) {
		AtlThrow(hr);
	}

	hr = m_unk0x18c->SetDataFormat(&c_dfDIMouse2);
	if (FAILED(hr)) {
		AtlThrow(hr);
	}

	hr = m_unk0x18c->SetCooperativeLevel(m_unk0x184, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);
	if (FAILED(hr)) {
		AtlThrow(hr);
	}

	DIPROPDWORD dipdw;
	dipdw.diph.dwSize = sizeof(DIPROPDWORD);
	dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	dipdw.diph.dwObj = 0;
	dipdw.diph.dwHow = DIPH_DEVICE;
	dipdw.dwData = 0x800;

	hr = m_unk0x18c->SetProperty(DIPROP_BUFFERSIZE, &dipdw.diph);
	if (FAILED(hr)) {
		AtlThrow(hr);
	}
}

// FUNCTION: ROBLOX 0x0046ba80
void DxUserInput::FUN_0046ba80()
{
	HRESULT hr = m_unk0x188->CreateDevice(GUID_SysKeyboard, &m_unk0x190, NULL);
	if (FAILED(hr)) {
		AtlThrow(hr);
	}

	hr = m_unk0x190->SetDataFormat(&c_dfDIKeyboard);
	if (FAILED(hr)) {
		AtlThrow(hr);
	}

	DIPROPDWORD dipdw;
	dipdw.diph.dwSize = sizeof(DIPROPDWORD);
	dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	dipdw.diph.dwObj = 0;
	dipdw.diph.dwHow = DIPH_DEVICE;
	dipdw.dwData = 0x800;

	hr = m_unk0x190->SetProperty(DIPROP_BUFFERSIZE, &dipdw.diph);
	if (FAILED(hr)) {
		AtlThrow(hr);
	}
}

// FUNCTION: ROBLOX 0x0046bb10
void DxUserInput::FUN_0046bb10()
{
	CRect rect;
	m_unk0x180->GetClientRect(&rect);
	m_unk0x58 = G3D::Rect2D(G3D::Vector2(rect.Width(), rect.Height()));
}

// STUB: ROBLOX 0x0046bc20
void DxUserInput::FUN_0046bc20(const G3D::Vector2& position)
{
	ATL::CCritSecLock lock(m_unk0x34);
	if (mouseReleasedUntilClick || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) || GetForegroundWindow() != m_unk0x184) {
		return;
	}

	if (m_unk0x68) {
		if (FAILED(m_unk0x18c->Acquire())) {
			m_unk0x68 = false;
		}

		if (m_unk0x68) {
			return;
		}
	}

	if (m_unk0x180->IsTopParentActive()) {
		m_unk0x18c->Unacquire();
		m_unk0x18c->SetCooperativeLevel(m_unk0x184, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
		m_unk0x68 = SUCCEEDED(m_unk0x18c->Acquire());

		if (m_unk0x68) {
			G3D::Rect2D rect = m_unk0x58;
			m_unk0x70 = position - rect.center();
			updateMouseConfinement();
			::PostMessage(m_unk0x180->m_hWnd, WM_UPDATE_CURSOR, 0, 0);
		}
	}
}

// STUB: ROBLOX 0x0046bd60
void DxUserInput::FUN_0046bd60()
{
	ATL::CCritSecLock lock(m_unk0x34);
	bool wanted = m_unk0x78 && !keyboardReleasedUntilClick && viewportHasKeyboardFocus() &&
		GetForegroundWindow() == m_unk0x184;

	if (m_unk0x190) {
		if (m_unk0x79 && FAILED(m_unk0x190->Acquire())) {
			m_unk0x79 = false;
		}

		if (wanted != m_unk0x79 || (m_unk0x79 && m_unk0x7a != true)) {
			m_unk0x190->Unacquire();

			if (wanted) {
				m_unk0x7a = true;

				if (m_unk0x190->SetCooperativeLevel(m_unk0x184, DISCL_EXCLUSIVE | DISCL_FOREGROUND) == DI_OK) {
					m_unk0x79 = SUCCEEDED(m_unk0x190->Acquire());
				}
			}
			else {
				m_unk0x79 = false;
				memset(m_unk0x7b, 0, sizeof(m_unk0x7b));
				m_unk0x17c = SDLK_UNKNOWN;
			}
		}
	}
}

bool DxUserInput::viewportHasKeyboardFocus() const
{
	GUITHREADINFO info = {sizeof(info)};
	DWORD thread = GetWindowThreadProcessId(m_unk0x184, NULL);
	return GetGUIThreadInfo(thread, &info) && info.hwndFocus == m_unk0x180->GetSafeHwnd();
}

// FUNCTION: ROBLOX 0x0046be50
void DxUserInput::FUN_0046be50(bool p_unk0x78)
{
	ATL::CCritSecLock lock(m_unk0x34);
	m_unk0x78 = p_unk0x78;
	FUN_0046bd60();
}

// FUNCTION: ROBLOX 0x0046bed0
bool DxUserInput::FUN_0046bed0(DIDEVICEOBJECTDATA* data, DWORD* count, IDirectInputDevice8* device)
{
	ATL::CCritSecLock lock(m_unk0x34);

	switch (device->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), data, count, 0)) {
	case DI_OK:
		return true;
	case DI_BUFFEROVERFLOW: {
		std::string name = device == m_unk0x190 ? "Keyboard " : "Mouse ";
		std::string message = name + "DI_BUFFEROVERFLOW!\n";
		RBX::StandardOut::singleton()->print(RBX::MESSAGE_ERROR, message.c_str());
		return true;
	}
	default: {
		std::string name = device == m_unk0x190 ? "Keyboard " : "Mouse ";
		return false;
	}
	}
}

// STUB: ROBLOX 0x0046c090
void DxUserInput::FUN_0046c090()
{
	DIDEVICEOBJECTDATA data[0x800];
	DWORD count = 0x800;
	{
		ATL::CCritSecLock lock(m_unk0x34);
		if (!FUN_0046bed0(data, &count, m_unk0x190)) {
			m_unk0x79 = false;
			return;
		}
	}

	RBX::DataModel::Lock dataModelLock(m_unk0x194);
	for (DWORD i = 0; i < count; i++) {
		bool down = (data[i].dwData & 0x80) != 0;
		DWORD scanCode = data[i].dwOfs;
		SDLMod mod = FUN_0046e2e0(m_unk0x7b);
		SDLKey key = FUN_0046d6f0(scanCode);
		if (down && key == SDLK_ESCAPE) {
			releaseInput(true, true);
			return;
		}
		bool handled = false;
		if (m_unk0x7a && down && m_unk0x4c) {
			static HKL layout = GetKeyboardLayout(0);
			UINT virtualKey = MapVirtualKeyEx(scanCode, 1, layout);
			if (virtualKey == 0) {
				virtualKey = FUN_0046dff0(key);
			}

			if (virtualKey == VK_SNAPSHOT) {
				::PostMessage(m_unk0x4c->m_hWnd, WM_COMMAND, MAKEWPARAM(0x80fc, 1), 0);
			}
			else if (
				virtualKey == VK_F1 || virtualKey == VK_F4 ||
				!static_cast<CMainFrame*>(AfxGetApp()->m_pMainWnd)->m_unk0xec
			) {
				UINT character = MapVirtualKeyEx(virtualKey, 2, layout);
				BYTE flags =
					(mod & KMOD_ALT ? FALT : 0) | (mod & KMOD_CTRL ? FCONTROL : 0) | (mod & KMOD_SHIFT ? FSHIFT : 0);
				for (int j = 0; j < m_unk0x54; j++) {
					const ACCEL& accel = m_unk0x50[j];
					bool matches =
						accel.fVirt & FVIRTKEY ? accel.key == virtualKey : accel.key == static_cast<char>(character);
					if (matches && (accel.fVirt & (FSHIFT | FCONTROL | FALT)) == flags) {
						// The input worker owns the DataModel lock here. The UI
						// command handler needs it too: SendMessage deadlocks both
						// threads (including Copy/Paste/Delete accelerators).
						::PostMessage(m_unk0x4c->m_hWnd, WM_COMMAND, MAKEWPARAM(m_unk0x50[j].cmd, 1), 0);
						handled = true;
						break;
					}
				}
			}
		}

		if (!handled) {
			m_unk0x194->processGui(RBX::UIEvent(this, down ? RBX::UIEvent::KEY_DOWN : RBX::UIEvent::KEY_UP, key, mod));
		}
	}
}

// FUNCTION: ROBLOX 0x0046c3c0
void DxUserInput::FUN_0046c3c0(RBX::UIEvent::EventType eventType, const G3D::Vector2& position)
{
	G3D::Vector2int16 windowSize(m_unk0x58.width(), m_unk0x58.height());
	G3D::Vector2int16 mousePosition(position);
	RBX::UIEvent event(this, eventType, mousePosition, windowSize);

	RBX::DataModel::Lock lock(m_unk0x194);
	m_unk0x194->processGui(event);
}

// FUNCTION: ROBLOX 0x0046c4b0
G3D::ReferenceCountedPointer<RBX::TextureProxyBase> DxUserInput::getGameCursor(RBX::Adorn* adorn)
{
	if (m_unk0x68) {
		return UserInputBase::getGameCursor(adorn);
	}

	return NULL;
}

// FUNCTION: ROBLOX 0x0046c4f0
G3D::Vector2 DxUserInput::FUN_0046c4f0()
{
	ATL::CCritSecLock lock(m_unk0x34);
	G3D::Rect2D rect = m_unk0x58;
	return rect.center() + m_unk0x70;
}

// FUNCTION: ROBLOX 0x0046c590
G3D::Vector2 DxUserInput::FUN_0046c590()
{
	ATL::CCritSecLock lock(m_unk0x34);

	CPoint point;
	GetCursorPos(&point);
	m_unk0x180->ScreenToClient(&point);

	return G3D::Vector2(point.x, point.y);
}

// STUB: ROBLOX 0x0046c600
G3D::Vector2 DxUserInput::getCursorPosition()
{
	ATL::CCritSecLock lock(m_unk0x34);
	return m_unk0x68 && m_unk0x6c == WRAP_CENTER ? FUN_0046c4f0() : FUN_0046c590();
}

// FUNCTION: ROBLOX 0x0046c6a0
bool DxUserInput::keyDown(SDLKey key) const
{
	ATL::CCritSecLock lock(m_unk0x34);

	if (m_unk0x17c != SDLK_UNKNOWN && key == m_unk0x17c) {
		return true;
	}

	if (m_unk0x7b[FUN_0046db70(key)] & 0x80) {
		return true;
	}

	return false;
}

// STUB: ROBLOX 0x0046c780
bool DxUserInput::FUN_0046c780()
{
	if (static_cast<CMainFrame*>(AfxGetApp()->m_pMainWnd)->m_unk0xec) {
		if (ActiveDocView::FUN_00434840()) {
			RBX::DataModel::Lock lock(ActiveDocView::FUN_00434840());
			RBX::Verb* verb = ActiveDocView::FUN_00434840()->getVerb("ToggleToolbox");
			if (verb) {
				return !verb->isChecked();
			}
		}
	}

	return false;
}

// FUNCTION: ROBLOX 0x0046c950
void DxUserInput::FUN_0046c950(const G3D::Vector2& mouseDelta, G3D::Vector2& wrapMouseDelta)
{
	switch (m_unk0x6c) {
	case WRAP_AUTO:
		// Normal pointing must be able to leave every viewport edge, even
		// while movement keys are held. Camera-look uses WRAP_CENTER instead.
		m_unk0x70 = FUN_0046c590() - m_unk0x58.center();
		break;
	case WRAP_CENTER:
		FUN_0046d6a0(mouseDelta, wrapMouseDelta, m_unk0x70);
		break;
	}
}

// STUB: ROBLOX 0x0046ca90
void DxUserInput::FUN_0046ca90()
{
	releaseMouse(true);
}

void DxUserInput::releaseMouse(bool restoreCursor)
{
	ATL::CCritSecLock lock(m_unk0x34);
	if (mouseConfined) {
		::ClipCursor(NULL);
		mouseConfined = false;
	}
	bool captured = m_unk0x68;
	m_unk0x68 = false;
	// Always unacquire: device loss can leave the cached flag out of sync.
	if (m_unk0x18c) {
		m_unk0x18c->Unacquire();
	}
	if (captured) {
		G3D::Vector2 position = FUN_0046c4f0();
		CPoint point(position.x, position.y);
		m_unk0x180->ClientToScreen(&point);
		if (restoreCursor && m_unk0x6c == WRAP_CENTER && GetForegroundWindow() == m_unk0x184) {
			SetCursorPos(point.x, point.y);
		}

		m_unk0x180->InvalidateRect(NULL);
	}
	// Heartbeat runs off the window thread. SetCursor there does not restore
	// the window thread's cursor, so marshal the update back to the owner.
	::PostMessage(m_unk0x180->m_hWnd, WM_UPDATE_CURSOR, 0, 0);
}

void DxUserInput::releaseInput(bool restoreCursor, bool untilClick, bool pointerUntilClick)
{
	ATL::CCritSecLock lock(m_unk0x34);
	keyboardReleasedUntilClick = untilClick;
	mouseReleasedUntilClick = untilClick && pointerUntilClick;
	releaseMouse(restoreCursor);
	if (m_unk0x190) {
		m_unk0x190->Unacquire();
	}
	m_unk0x78 = false;
	m_unk0x79 = false;
	m_unk0x7a = false;
	m_unk0x6c = WRAP_AUTO;
	memset(m_unk0x7b, 0, sizeof(m_unk0x7b));
	m_unk0x17c = SDLK_UNKNOWN;
}

void DxUserInput::releasePointer(bool restoreCursor)
{
	ATL::CCritSecLock lock(m_unk0x34);
	releaseMouse(restoreCursor);
	m_unk0x6c = WRAP_AUTO;
}

// STUB: ROBLOX 0x0046cbc0
void DxUserInput::FUN_0046cbc0(UINT message, int wParam, LPARAM lParam)
{
	switch (message) {
	case WM_KEYDOWN:
		FUN_0046bd60();
		break;
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP: {
		G3D::Vector2 position(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN) {
			mouseReleasedUntilClick = false;
			keyboardReleasedUntilClick = false;
			FUN_0046be50(true);
		}
		// Buffered DirectInput supplies events while acquired. Do not dispatch
		// the duplicate Windows mouse messages in non-exclusive mode.
		if (m_unk0x68) {
			break;
		}
		FUN_0046c3c0(FUN_0046e290(message), position);

		CPoint point(position.x, position.y);
		CClientDC dc(m_unk0x180);

		if (dc.PtVisible(point)) {
			FUN_0046bc20(position);
		}
		break;
	}
	case WM_MOUSEWHEEL: {
		G3D::Vector2 position(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		FUN_0046c3c0(wParam > 0.0 ? RBX::UIEvent::MOUSE_WHEEL_FORWARD : RBX::UIEvent::MOUSE_WHEEL_BACKWARD, position);
		break;
	}
	case WM_ENTERMENULOOP:
	case WM_ENTERSIZEMOVE:
		FUN_0046ca90();
		break;
	}
}

// FUNCTION: ROBLOX 0x0046cd90
void DxUserInput::FUN_0046cd90()
{
	DIDEVICEOBJECTDATA data[0x800];
	DWORD count = 0x800;
	{
		ATL::CCritSecLock lock(m_unk0x34);
		if (!FUN_0046bed0(data, &count, m_unk0x18c)) {
			releasePointer();
			return;
		}
	}

	G3D::Vector2 wrapMouseDelta(0.0f, 0.0f);
	bool moved = false;
	for (DWORD i = 0; i < count; i++) {
		switch (data[i].dwOfs) {
		case DIMOFS_BUTTON0:
			if (data[i].dwData == 0x80) {
				keyboardReleasedUntilClick = false;
				m_unk0x180->PostMessage(WM_USER + 0xbb);
			}
			FUN_0046c3c0(
				data[i].dwData == 0x80 ? RBX::UIEvent::MOUSE_LEFT_BUTTON_DOWN : RBX::UIEvent::MOUSE_LEFT_BUTTON_UP,
				getCursorPosition()
			);
			break;
		case DIMOFS_BUTTON1:
			if (data[i].dwData == 0x80) {
				keyboardReleasedUntilClick = false;
				m_unk0x180->PostMessage(WM_USER + 0xbb);
			}
			FUN_0046c3c0(
				data[i].dwData == 0x80 ? RBX::UIEvent::MOUSE_RIGHT_BUTTON_DOWN : RBX::UIEvent::MOUSE_RIGHT_BUTTON_UP,
				getCursorPosition()
			);
			break;
		case DIMOFS_X:
		case DIMOFS_Y:
			moved = true;
			FUN_0046c950(FUN_0046d6c0(data[i]), wrapMouseDelta);
			break;
		case DIMOFS_Z: {
			float wheel = (int) data[i].dwData / 120.0;
			FUN_0046c3c0(
				wheel > 0.0f ? RBX::UIEvent::MOUSE_WHEEL_FORWARD : RBX::UIEvent::MOUSE_WHEEL_BACKWARD,
				getCursorPosition()
			);
			break;
		}
		}

		if (!m_unk0x68) {
			break;
		}
		if (m_unk0x6c == WRAP_AUTO && !FUN_0046b7f0()) {
			releasePointer(true);
			return;
		}
	}

	if (wrapMouseDelta != G3D::Vector2::zero()) {
		RBX::DataModel::Lock lock(m_unk0x194);
		m_unk0x194->getWorkspace()->onWrapMouse(wrapMouseDelta);
		FUN_0046c3c0(RBX::UIEvent::MOUSE_DELTA, wrapMouseDelta);
	}

	if (moved) {
		FUN_0046c3c0(RBX::UIEvent::MOUSE_MOVE, getCursorPosition());
	}
}

// STUB: ROBLOX 0x0046d0a0
void DxUserInput::FUN_0046d0a0()
{
	FUN_0046bb10();

	ATL::CCritSecLock lock(m_unk0x34);

	if (m_unk0x68) {
		G3D::Vector2 position = getCursorPosition();
		CPoint point(position.x, position.y);
		CClientDC dc(m_unk0x180);

		if (!dc.PtVisible(point)) {
			releasePointer(true);
		}
	}
}

// STUB: ROBLOX 0x0046d180
void DxUserInput::onEvent(const RBX::RunService* source, RBX::Heartbeat event)
{
	ATL::CCritSecLock lock(m_unk0x34);
	if ((m_unk0x68 || m_unk0x79) && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
		releaseInput(true, true);
		return;
	}
	if (GetForegroundWindow() != m_unk0x184 || !m_unk0x180->IsWindowVisible()) {
		if (m_unk0x68 || m_unk0x79 || m_unk0x78) {
			releaseInput();
		}
		return;
	}

	// Keyboard ownership follows the clicked/focused control, not the pointer.
	FUN_0046be50(!keyboardReleasedUntilClick && viewportHasKeyboardFocus());

	if (m_unk0x68) {
		G3D::Vector2 physicalPosition = FUN_0046c590();
		if (!m_unk0x58.contains(physicalPosition)) {
			releasePointer();
		}
		if (m_unk0x6c == WRAP_AUTO) {
			m_unk0x70 = FUN_0046c590() - m_unk0x58.center();
			if (!FUN_0046b7f0()) {
				releasePointer();
			}
		}
		if (m_unk0x68) FUN_0046cd90();
	}

	if (m_unk0x190) {
		if (FAILED(m_unk0x190->GetDeviceState(sizeof(m_unk0x7b), m_unk0x7b))) {
			memset(m_unk0x7b, 0, sizeof(m_unk0x7b));
		}
	}

	if (m_unk0x79) {
		FUN_0046c090();
	}
}

// FUNCTION: ROBLOX 0x0046d220
void DxUserInput::onEvent(const RBX::RunService* source, RBX::RunTransition event)
{
	FUN_0046bd60();
}

// FUNCTION: ROBLOX 0x0046d230
DxUserInput::~DxUserInput()
{
	if (mouseConfined) {
		::ClipCursor(NULL);
		mouseConfined = false;
	}
	if (m_unk0x194) {
		RBX::stopListening(
			m_unk0x194->find<RBX::RunService>(), static_cast<RBX::Listener<RBX::RunService, RBX::Heartbeat>*>(this)
		);
	}

	if (m_unk0x194) {
		RBX::stopListening(
			m_unk0x194->find<RBX::RunService>(), static_cast<RBX::Listener<RBX::RunService, RBX::RunTransition>*>(this)
		);
	}

	if (m_unk0x18c) {
		m_unk0x18c->Unacquire();
		m_unk0x18c = NULL;
	}

	if (m_unk0x190) {
		m_unk0x190->Unacquire();
		m_unk0x190 = NULL;
	}

	if (m_unk0x188) {
		m_unk0x188 = NULL;
	}

	delete[] m_unk0x50;
}

// STUB: ROBLOX 0x0046d3f0
void DxUserInput::setWrapMode(WrapMode wrapMode)
{
	ATL::CCritSecLock lock(m_unk0x34);
	if (wrapMode == m_unk0x6c) {
		return;
	}
	// Preserve the current cursor anchor for RMB camera-look. Explicit
	// first-person centering still goes through centerCursor().
	m_unk0x6c = wrapMode;
	updateMouseConfinement();
}

void DxUserInput::updateMouseConfinement()
{
	if (m_unk0x68 && m_unk0x6c == WRAP_CENTER && GetForegroundWindow() == m_unk0x184) {
		G3D::Vector2 anchor = FUN_0046c4f0();
		CPoint point(anchor.x, anchor.y);
		m_unk0x180->ClientToScreen(&point);
		// DirectInput still supplies relative movement while the native pointer
		// stays at the visible cursor anchor. No hidden drift or release warp.
		RECT clip = {point.x, point.y, point.x + 1, point.y + 1};
		mouseConfined = ::ClipCursor(&clip) != FALSE;
	}
	else if (mouseConfined) {
		::ClipCursor(NULL);
		mouseConfined = false;
	}
}

// FUNCTION: ROBLOX 0x0046d400
void DxUserInput::centerCursor()
{
	ATL::CCritSecLock lock(m_unk0x34);
	m_unk0x70 = G3D::Vector2::zero();
	updateMouseConfinement();
}

// STUB: ROBLOX 0x0046d440
DxUserInput::DxUserInput(CWnd* wnd, RBX::DataModel* dataModel)
	: m_unk0x50(NULL), m_unk0x54(0), m_unk0x68(false), mouseConfined(false), keyboardReleasedUntilClick(false),
	  m_unk0x6c(WRAP_AUTO), m_unk0x78(false),
	  m_unk0x79(false), m_unk0x7a(false), mouseReleasedUntilClick(false), m_unk0x17c(SDLK_UNKNOWN), m_unk0x180(wnd),
	  m_unk0x184(wnd->GetTopLevelFrame()->GetSafeHwnd()), m_unk0x194(RBX::shared_from(dataModel))
{
	FUN_0046bb10();

	for (m_unk0x4c = m_unk0x180->GetParentFrame(); m_unk0x4c != NULL; m_unk0x4c = m_unk0x4c->GetParentFrame()) {
		m_unk0x54 = CopyAcceleratorTable(m_unk0x4c->GetDefaultAccelerator(), NULL, 0);

		if (m_unk0x54) {
			m_unk0x50 = new ACCEL[m_unk0x54];
			CopyAcceleratorTable(m_unk0x4c->GetDefaultAccelerator(), m_unk0x50, m_unk0x54);
			break;
		}
	}

	memset(m_unk0x7b, 0, sizeof(m_unk0x7b));

	HRESULT hr =
		DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**) &m_unk0x188, NULL);
	if (FAILED(hr)) {
		AtlThrow(hr);
	}

	FUN_0046b9d0();
	FUN_0046ba80();

	if (dataModel) {
		RBX::startListening(
			dataModel->create<RBX::RunService>(), static_cast<RBX::Listener<RBX::RunService, RBX::Heartbeat>*>(this)
		);
	}

	if (dataModel) {
		RBX::startListening(
			dataModel->create<RBX::RunService>(), static_cast<RBX::Listener<RBX::RunService, RBX::RunTransition>*>(this)
		);
		dataModel->create<RBX::RunService>();
	}

	FUN_0046bd60();
}
