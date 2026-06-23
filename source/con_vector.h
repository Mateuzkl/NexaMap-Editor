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

#ifndef RME_CON_VECTOR_H_
#define RME_CON_VECTOR_H_

#include <vector>

template <class T> // This only really works with pointers.. hrhr "T" might be abit misleading.. :o
class contigous_vector {
public:
	contigous_vector(size_t start_size = 7) {
		v.resize(start_size, nullptr);
	}
	~contigous_vector() = default;

	contigous_vector(const contigous_vector&) = delete;
	contigous_vector& operator=(const contigous_vector&) = delete;

	size_t size() const {
		return v.size();
	}

	void set(size_t index, T value) {
		if (index >= v.size()) {
			v.resize(index + 1, nullptr);
		}
		v[index] = value;
	}

	T operator[](size_t index) const {
		return index < v.size() ? v[index] : nullptr;
	}

private:
	std::vector<T> v;
};

#endif
