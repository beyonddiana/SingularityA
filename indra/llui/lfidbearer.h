/* Copyright (C) 2019 Liru Færs
 *
 * LFIDBearer is a class that holds an ID or IDs that menus can use
 * This class also bears the type of ID/IDs that it is holding
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA */

#pragma once

#include "lluuid.h"

class LLMenuGL;
class LLView;

struct LFIDBearer
{
	enum Type : S8
	{
		MULTIPLE = -2,
		NONE = -1,
		AVATAR = 0,
		GROUP,
		OBJECT,
		COUNT
	};

	virtual ~LFIDBearer() { if (sActive == this) sActive = nullptr; }
	virtual LLUUID getStringUUIDSelectedItem() const = 0;
	virtual uuid_vec_t getSelectedIDs() const { return { getStringUUIDSelectedItem() }; }
	virtual S32 getNumSelected() const { return getStringUUIDSelectedItem().notNull(); }
	virtual Type getSelectedType() const { return AVATAR; }

	template<typename T> static T* getActive() { return static_cast<T*>(sActive); }
	static LLUUID getActiveSelectedID() { return sActive->getStringUUIDSelectedItem(); }
	static uuid_vec_t getActiveSelectedIDs() { return sActive->getSelectedIDs(); }
	static S32 getActiveNumSelected() { return sActive->getNumSelected(); }
	static Type getActiveType() { return sActive->getSelectedType(); }

	static void buildMenus();
	LLMenuGL* showMenu(LLView* self, const std::string& menu_name, S32 x, S32 y, std::function<void(LLMenuGL*)> on_menu_built = nullptr);
	void showMenu(LLView* self, LLMenuGL* menu, S32 x, S32 y);

protected:
	// Menus that recur, such as general avatars or groups menus
	static const std::array<const std::string, COUNT> sMenuStrings;
	static std::array<LLMenuGL*, COUNT> sMenus;
	static LFIDBearer* sActive;
};
