/* Copyright (c) 2026 iceman50. Distributed under GPL-3.0-or-later. */
#ifndef DWT_CLIENT_FRAME_H
#define DWT_CLIENT_FRAME_H

#include <dwt/widgets/Composite.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/Menu.h>

namespace dwt {

class Frame;

/** Client-area caption and menu bar. The parent retains its HMENU for command
 * routing, while this widget owns all visible frame controls. */
class ClientFrame : public Composite {
public:
	explicit ClientFrame(Frame* owner);
	~ClientFrame() override;
	void refresh();
	void releaseFrameInput();
	void layout() override;
	long height() const { return frameHeight; }
	bool isActive() const { return active; }
	bool menuFocused() const;
	bool handleFrameMessage(const MSG& msg, LRESULT& result);
	LRESULT hitTest(LPARAM position) const;
	void themeSystemMenu(HMENU menu);
	void restoreSystemMenu();

private:
	struct MenuItem {
		Button* button;
		UINT state;
	};
	struct SystemMenuItem {
		MENUITEMINFO original;
		ULONG_PTR data;
	};
	MenuPtr systemMenu;
	HMENU nativeSystemMenu = nullptr;
	HBRUSH systemMenuBackground = nullptr;
	BrushPtr systemMenuBrush;
	std::vector<SystemMenuItem> systemItems;
	Frame* owner;
	std::vector<std::pair<Button*, UINT>> buttons;
	std::vector<MenuItem> items;
	FontPtr captionFont;
	FontPtr menuFont;
	long captionHeight;
	long frameHeight;
	bool active;
	bool layingOut;
	int tracking;
	int nextMenu;
	HMENU selectedMenu;
	UINT selectedFlags;
	HWND previousFocus;
	HWND captionFocus;
	HHOOK menuHook;
	static thread_local ClientFrame* trackingFrame;

	void paint(Canvas& canvas);
	LRESULT drawButton(Button* button, UINT command, NMCUSTOMDRAW& data);
	LRESULT drawMenuButton(size_t index, NMCUSTOMDRAW& data);
	bool drawSystemMenuItem(DRAWITEMSTRUCT& data);
	Button* addCaptionButton(const tstring& label, UINT command);
	void openMenu(size_t index, bool keyboard);
	void focusMenu(int index);
	int adjacentMenu(int index, int direction) const;
	void leaveMenu();
	bool menuKey(size_t index, const MSG& msg, LRESULT& result);
	int mnemonic(TCHAR character) const;
	LRESULT resizeHitTest(LPARAM position) const;
	static LRESULT CALLBACK filterMenu(int code, WPARAM wParam, LPARAM lParam);
};

}
#endif
