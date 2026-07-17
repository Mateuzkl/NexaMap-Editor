//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_MAP_IO_H_
#define RME_MAP_IO_H_

#include "client_version.h"

#include <cstdint>

enum ImportType {
	IMPORT_DONT,
	IMPORT_MERGE,
	IMPORT_SMART_MERGE,
	IMPORT_INSERT,
};

class Map;

class ItemIdCodec {
public:
	virtual ~ItemIdCodec() = default;

	virtual bool Decode(uint16_t storedId, uint16_t& serverId) const = 0;
	virtual bool Encode(uint16_t serverId, uint16_t& storedId) const = 0;
};

class IOMap {
protected:
	wxArrayString warnings;
	wxString errorstr;

	void warning(const wxString format, ...);
	void error(const wxString format, ...);

public:
	IOMap() {
		version.otbm = MAP_OTBM_1;
		version.client = CLIENT_VERSION_NONE;
	}
	virtual ~IOMap() { }

	MapVersion version;

	wxArrayString& getWarnings() {
		return warnings;
	}
	wxString& getError() {
		return errorstr;
	}

	void setItemIdCodec(const ItemIdCodec* codec) {
		itemIdCodec = codec;
	}
	bool decodeStoredItemId(uint16_t storedId, uint16_t& serverId) const {
		if (!itemIdCodec) {
			serverId = storedId;
			return true;
		}
		return itemIdCodec->Decode(storedId, serverId);
	}
	bool encodeStoredItemId(uint16_t serverId, uint16_t& storedId) const {
		if (!itemIdCodec) {
			storedId = serverId;
			return true;
		}
		return itemIdCodec->Encode(serverId, storedId);
	}

	virtual bool loadMap(Map& map, const FileName& identifier) = 0;
	virtual bool saveMap(Map& map, const FileName& identifier) = 0;

private:
	const ItemIdCodec* itemIdCodec = nullptr;
};

#endif
