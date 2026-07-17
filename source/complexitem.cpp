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

#include "main.h"

#include "complexitem.h"

// Container
Container::Container(const uint16_t type) :
	Item(type, 0) {
	////
}

Container::~Container() {
	for (Item const* item : contents) {
		delete item;
	}
}

Item* Container::deepCopy() const {
	// Preserve nested items even if the converted ID is not classified as a
	// container by the currently loaded item database.
	auto* copy = newd Container(id);
	copyBaseStateTo(*copy);
	for (Item const* item : contents) {
		copy->contents.push_back(item->deepCopy());
	}
	return copy;
}

Item* Container::getItem(size_t index) const {
	if (index < contents.size()) {
		return contents[index];
	}
	return nullptr;
}

double Container::getWeight() {
	return g_items[id].weight;
}

// Teleport
Teleport::Teleport(const uint16_t type) :
	Item(type, 0),
	destination(0, 0, 0) {
	////
}

Item* Teleport::deepCopy() const {
	// A converted ID may no longer be classified as a teleport by the active
	// items.otb. Preserve the dynamic type and its OTBM-only data regardless.
	auto* copy = newd Teleport(id);
	copyBaseStateTo(*copy);
	copy->destination = destination;
	return copy;
}

// Door
Door::Door(const uint16_t type) :
	Item(type, 0),
	doorId(0) {
	////
}

Item* Door::deepCopy() const {
	// Do not let an ID/type-table change slice a house door into a plain Item.
	auto* copy = newd Door(id);
	copyBaseStateTo(*copy);
	copy->doorId = doorId;
	return copy;
}

// Depot
Depot::Depot(const uint16_t type) :
	Item(type, 0),
	depotId(0) {
	////
}

Item* Depot::deepCopy() const {
	// A converted ID may not be classified as a depot by the active item
	// metadata. Preserve the dynamic type and Town association explicitly.
	auto* copy = newd Depot(id);
	copyBaseStateTo(*copy);
	copy->depotId = depotId;
	return copy;
}

// Podium
Podium::Podium(const uint16_t type) :
	Item(type, 0),
	outfit(Outfit()), showOutfit(true), showMount(true), showPlatform(true), direction(0) {
	////
}

Item* Podium::deepCopy() const {
	// Keep podium-only state even if the converted ID is classified differently.
	auto* copy = newd Podium(id);
	copyBaseStateTo(*copy);
	copy->outfit = outfit;
	copy->showOutfit = showOutfit;
	copy->showMount = showMount;
	copy->showPlatform = showPlatform;
	copy->direction = direction;
	return copy;
}
