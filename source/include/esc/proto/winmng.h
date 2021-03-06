/**
 * $Id$
 * Copyright (C) 2008 - 2014 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <esc/ipc/ipcstream.h>
#include <esc/proto/richui.h>
#include <esc/stream/ostream.h>
#include <esc/vthrow.h>
#include <sys/common.h>
#include <sys/messages.h>
#include <string>
#include <vector>

namespace esc {

/**
 * The IPC-interface for the window-manager-device. You can also use (most) of the UI/Screen
 * interface (the others are hidden).
 */
class WinMng : public RichUI {
public:
	/**
	 * Opens the given device
	 *
	 * @param path the path to the device
	 * @throws if the open failed
	 */
	explicit WinMng(const char *path) : RichUI(path) {
	}

	/**
	 * Creates a new window.
	 *
	 * @param x the x coordinate
	 * @param y the y coordinate
	 * @param w the width
	 * @param h the height
	 * @param style flags for the window
	 * @param titleHeight the height of the titlebar
	 * @param title the title
	 * @return the window-id
	 * @throws if the operation failed
	 */
	gwinid_t create(gpos_t x,gpos_t y,gsize_t w,gsize_t h,uint style,gsize_t titleHeight,const char *title) {
		ValueResponse<gwinid_t> r;
		_is << x << y << w << h << style << titleHeight << CString(title);
		_is << esc::SendReceive(MSG_WIN_CREATE) >> r;
		if(r.err < 0)
			VTHROWE("create(" << x << "," << y << "," << w << "," << h << ")",r.err);
		return r.res;
	}

	/**
	 * Shares the window buffer with the window manager
	 *
	 * @param wid the window id
	 * @param fd the file descriptor
	 * @throws if the delegate failed
	 */
	void shareWinBuf(gwinid_t wid,int fd) {
		if(delegate(_is.fd(),fd,O_RDWR,wid) < 0)
			VTHROW("shareWinBuf(" << wid << "," << fd << ")");
	}

	/**
	 * Activates the window with given id, i.e. pulls it to the front.
	 *
	 * @param wid the window-id
	 * @throws if the send failed
	 */
	void setActive(gwinid_t wid) {
		_is << wid << esc::Send(MSG_WIN_SET_ACTIVE);
	}

	/**
	 * Destroys the window with given id.
	 *
	 * @param wid the window-id
	 * @throws if the send failed
	 */
	void destroy(gwinid_t wid) {
		_is << wid << esc::Send(MSG_WIN_DESTROY);
	}

	/**
	 * Moves the window with given id to <x>,<y>.
	 *
	 * @param wid the window-id
	 * @param x the new x coordinate
	 * @param y the new y coordinate
	 * @param finished whether to actually move the window (instead of showing the preview)
	 * @throws if the send failed
	 */
	void move(gwinid_t wid,gpos_t x,gpos_t y,bool finished) {
		_is << wid << x << y << finished << esc::Send(MSG_WIN_MOVE);
	}

	/**
	 * Resizes the window with given id.
	 *
	 * @param wid the window-id
	 * @param x the new x coordinate
	 * @param y the new y coordinate
	 * @param width the width
	 * @param height the height
	 * @param finished whether to actually resize the window (instead of showing the preview)
	 * @throws if the send failed
	 */
	void resize(gwinid_t wid,gpos_t x,gpos_t y,gsize_t width,gsize_t height,bool finished) {
		_is << wid << x << y << width << height << finished << esc::Send(MSG_WIN_RESIZE);
	}

	/**
	 * Updates the given rectangle of the window with given id.
	 *
	 * @param wid the window-id
	 * @param x the x coordinate
	 * @param y the y coordinate
	 * @param width the width
	 * @param height the height
	 * @throws if the operation failed
	 */
	void update(gwinid_t wid,gpos_t x,gpos_t y,gsize_t width,gsize_t height) {
		errcode_t res;
		_is << wid << x << y << width << height << esc::SendReceive(MSG_WIN_UPDATE) >> res;
		if(res < 0)
			VTHROWE("update(" << x << "," << y << "," << width << "," << height << ")",res);
	}

	/**
	 * @return the name of the current theme
	 */
	std::string getTheme() {
		CStringBuf<64> name;
		_is << esc::SendReceive(MSG_WIN_GETTHEME) >> name;
		return name.str();
	}

	/**
	 * Sets the theme to the given one.
	 *
	 * @param name the name of the theme
	 * @throws if the operation failed
	 */
	void setTheme(const std::string &name) {
		errcode_t res;
		_is << CString(name.c_str(),name.length()) << esc::SendReceive(MSG_WIN_SETTHEME) >> res;
		if(res < 0)
			VTHROWE("setTheme(" << name << ")",res);
	}

private:
	/* hide these members since they are not supported */
	using UI::setCursor;
};

/**
 * The IPC-interface for the window-manager-event-device. It is supposed to be used in conjunction
 * with WinMng.
 */
class WinMngEvents {
public:
	static const size_t MAX_WINTITLE_LEN	= 64;
	struct Event {
		enum Type {
			TYPE_KEYBOARD,
			TYPE_MOUSE,
			TYPE_RESIZE,
			TYPE_SET_ACTIVE,
			TYPE_RESET,
			/* subscribable events */
			TYPE_CREATED,
			TYPE_ACTIVE,
			TYPE_DESTROYED,
		} type;
		union {
			struct {
				/* the keycode (see keycodes.h) */
				uchar keycode;
				/* modifiers (STATE_CTRL, STATE_SHIFT, STATE_ALT, STATE_BREAK) */
				uchar modifier;
				/* the character, translated by the current keymap */
				char character;
			} keyb;
			struct {
				/* current position */
				gpos_t x;
				gpos_t y;
				/* movement */
				short movedX;
				short movedY;
				short movedZ;
				/* pressed buttons */
				uchar buttons;
			} mouse;
			struct {
				bool active;
				gpos_t mouseX;
				gpos_t mouseY;
			} setactive;
			struct {
				char title[MAX_WINTITLE_LEN];
			} created;
		} d;
		/* window id */
		gwinid_t wid;
	};

	/**
	 * Opens the given device
	 *
	 * @param path the path to the device
	 * @throws if the open failed
	 */
	explicit WinMngEvents(const char *path) : _is(path) {
	}

	/**
	 * @return the file-descriptor
	 */
	int fd() const {
		return _is.fd();
	}

	/**
	 * Attaches this event-channel to receive events for the given window.
	 *
	 * @param wid the window-id (default: all)
	 * @throws if the operation failed
	 */
	void attach(gwinid_t wid = -1) {
		errcode_t res;
		_is << wid << SendReceive(MSG_WIN_ATTACH) >> res;
		if(res < 0)
			VTHROWE("attach(" << wid << ")",res);
	}

	/**
	 * Subscribes the given event-type (only possible for TYPE_{CREATED,ACTIVE,DESTROYED}).
	 *
	 * @param type the event-type
	 * @throws if the send failed
	 */
	void subscribe(Event::Type type) {
		_is << type << esc::Send(MSG_WIN_ADDLISTENER);
	}

	/**
	 * Unsubscribes the given event-type (only possible for TYPE_{CREATED,ACTIVE,DESTROYED}).
	 *
	 * @param type the event-type
	 * @throws if the send failed
	 */
	void unsubscribe(Event::Type type) {
		_is << type << esc::Send(MSG_WIN_REMLISTENER);
	}

	/**
	 * Reads the next event from this channel.
	 *
	 * @throws if the receive failed
	 */
	WinMngEvents &operator>>(Event &ev) {
		_is >> esc::ReceiveData(&ev,sizeof(ev));
		return *this;
	}

private:
	IPCStream _is;
};

OStream &operator<<(OStream &os,const WinMngEvents::Event &ev);

}
