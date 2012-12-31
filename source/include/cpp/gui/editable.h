/**
 * $Id$
 * Copyright (C) 2008 - 2011 Nils Asmussen
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

#include <esc/common.h>
#include <gui/control.h>
#include <string>
#include <assert.h>

namespace gui {
	class Editable : public Control {
	public:
		static const uchar DIR_NONE;
		static const uchar DIR_LEFT;
		static const uchar DIR_RIGHT;

		static const gsize_t CURSOR_WIDTH;
		static const gsize_t CURSOR_OVERLAP;
		static const gsize_t DEF_WIDTH;

	public:
		Editable()
			: Control(), _cursor(0), _begin(0), _focused(false), _selecting(false),
			  _startSel(false), _selDir(DIR_NONE), _selStart(-1), _selEnd(-1), _str() {
		};
		Editable(gpos_t x,gpos_t y,gsize_t width,gsize_t height)
			: Control(x,y,width,height), _cursor(0), _begin(0), _focused(false), _selecting(false),
			  _startSel(false), _selDir(DIR_NONE), _selStart(-1), _selEnd(-1), _str() {
		};

		inline size_t getCursorPos() const {
			return _cursor;
		};
		inline void setCursorPos(size_t pos) {
			size_t max = getMaxCharNum(*getGraphics());
			assert(pos <= _str.length());
			_cursor = pos;
			if(_cursor > max)
				_begin = _cursor - max;
			else
				_begin = 0;
		};

		inline const std::string &getText() const {
			return _str;
		};
		inline void setText(const std::string &text) {
			_str = text;
			setCursorPos(text.length());
			clearSelection();
			repaint();
		};

		void insertAtCursor(char c);
		void insertAtCursor(const std::string &str);
		void removeLast();
		void removeNext();

		virtual gsize_t getMinWidth() const;
		virtual gsize_t getMinHeight() const;
		virtual gsize_t getPreferredWidth() const;
		virtual void onFocusGained();
		virtual void onFocusLost();
		virtual void onMouseMoved(const MouseEvent &e);
		virtual void onMouseReleased(const MouseEvent &e);
		virtual void onMousePressed(const MouseEvent &e);
		virtual void onKeyPressed(const KeyEvent &e);

	protected:
		virtual void paint(Graphics &g);

	private:
		int getPosAt(gpos_t x);
		void moveCursor(int amount);
		bool moveCursorTo(size_t pos);
		void clearSelection();
		bool changeSelection(int pos,int oldPos,uchar dir);
		void deleteSelection();
		inline size_t getMaxCharNum(Graphics &g) {
			if(getWidth() < getTheme().getTextPadding() * 2)
				return 0;
			return (getWidth() - getTheme().getTextPadding() * 2) / g.getFont().getWidth();
		};

	private:
		size_t _cursor;
		size_t _begin;
		bool _focused;
		bool _selecting;
		bool _startSel;
		uchar _selDir;
		int _selStart;
		int _selEnd;
		std::string _str;
	};
}
