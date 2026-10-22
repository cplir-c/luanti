#include "util/paletted_storage.h"

#include "irrTypes.h"

#include <memory>
#include <vector>
#include <bitset>
#include <memory_resource>
#include <algorithm>
#include <iterator>
// for content_t
#include "mapnode.h"

template<typename T, u32 stored_length>
u32 PalettedStorage<T, stored_length>::u16_mask() const {
	// goal: {0x1, 0x3, 0xf, 0xff, 0xffff}
	// 4 - palette_shift: {4, 3, 2, 1, 0}
	// 1 << (4-shift)   : {16, 8, 4, 2, 1}
	// 1 << 1<<(4-shift): {0x10000, 0x100, 0x10, 0x4, 0x2}
	// (1<<1<<(4-shift))-1: {0xffff, 0xff, 0xf, 0x3, 0x1}
	u32 u16_mask = (u32(1) << (u32(1) << (4 - palette_shift))) - 1;
	return u16_mask;
}
template<typename T, u32 stored_length>
u32 PalettedStorage<T, stored_length>::u16_bit_mask() const {
	// goal: {0x0, 0x1, 0x3, 0x7, 0xf}[shift]
	// 1 << shift: {1, 2, 4, 8, 16}
	// (1<<shift)-1: {0, 0x1, 0x3, 0x7, 0xf}
	u32 u16_bit_mask = (u32(1) << palette_shift) - 1;
	return u16_bit_mask;
}

template<typename T, u32 stored_length>
s32 PalettedStorage<T, stored_length>::mapT(T t) const {
	for (s32 i = 0; i < palette.size(); ++i) {
		if (palette[i] == t) {
			return i;
		}
	}
	return -1;
}

template<typename T, u32 stored_length>
u32 PalettedStorage<T, stored_length>::mappedT(T t) {
	for (u32 i = 0; i < palette.size(); ++i) {
		if (palette[i] == t) {
			return i;
		}
	}
	if (palette.size() + 1 > this->u16_mask()) {
		// it doesn't fit the current index size;
		//		we need to expand all our indices
		this->changeStorageSize(this->palette_shift - 1, [](u32 i){ return i; });
	}
	u32 idx = u32(this->palette.size());
	this->palette.emplace_back(t);
	return idx;
}
template<typename T, u32 stored_length>
template<typename Fun>
void PalettedStorage<T, stored_length>::changeStorageSize<Fun>(
			u8 new_shift, const Fun& mapper
		) {
	// unpack our indices,
	// increase our palette shift,
	// then repack our indices
	u32 indices[stored_length] = {};

	// unpack indices
	{
		u32 u16_bit_mask = this->u16_bit_mask();
		u32 u16_mask = this->u16_mask();
		for (u32 i = 0; i < stored_length; ++i) {
			u32 storage_index = i >> palette_shift;
			u32 u16_bit = i & u16_bit_mask;
			// apply the mapper function to the index as it's unpacked
			indices[i] = mapper(u32((storage[storage_index] >> u16_bit) & u16_mask));
		}
	}

	this->palette_shift = new_shift;
	this->storage.reset(paletted_storage_allocate_(stored_length >> palette_shift));

	// repack indices
	{
		u32 u16_bit_mask = this->u16_bit_mask();
		for (u32 i = 0; i < stored_length; ++i) {
			u32 storage_index = i >> palette_shift;
			u32 u16_bit = i & u16_bit_mask;
			// no overlaps, so we can just or it in
			storage[storage_index] |= (indices[i] << u16_bit);
		}
	}
}

template<typename T, u32 stored_length>
T PalettedStorage<T, stored_length>::readIndex(u32 idx) const {
	u32 storage_index = idx >> palette_shift;
	u32 u16_bit = idx & this->u16_bit_mask();
	u32 u16_mask = this->u16_mask();
	return palette[(storage[storage_index] >> u16_bit) & u16_mask];

	/*
	// The above is a branchess version of this:

	switch(palette_shift) {
		case 4:  // 16 indices per u16 (2**4)
			indexed = (storage[idx >> 4] >> (idx & 0xf)) & 0x1;
			break;
		case 3:  // eight indices per u16 (2**3), each 2 bits
			indexed = (storage[idx >> 3] >> ((idx & 0x7) << 1)) & 0x3;
			break;
		case 2:  // four indices per u16 (2**2), each 4 bits
			indexed = (storage[idx >> 2] >> ((idx & 0x3) << 2)) & 0xf;
			break;
		case 1:  // two indices per u16 (2**1), each 8 bits
			indexed = (storage[idx >> 1] >> ((idx & 1) << 3)) & 0xff;
			break;
		case 0: // one index per u16 (2**0), each 16 (actually 12) bits
			indexed = storage[idx];
			break;
	}
	return palette[indexed];
	*/
}
template<typename T, u32 stored_length>
void PalettedStorage<T, stored_length>::writeIndex(u32 idx, T t) {
	u32 palette_idx = this->mappedT(t);

	u32 storage_index = idx >> palette_shift;
	u32 u16_bit = idx & this->u16_bit_mask();
	u32 u16_mask = this->u16_mask();

	// remove the old index
	storage[storage_index] &= ~(u16_mask << u16_bit);
	// add in the new one
	storage[storage_index] |= (palette_idx & u16_mask) << u16_bit;
}

template<typename T, u32 stored_length>
void PalettedStorage<T, stored_length>::tryShrink() {
	std::bitset<stored_length> used_palette_slots = {};
	{ // read all the indices, and set the corresponding palette bit
		u32 u16_bit_mask = this->u16_bit_mask();
		u32 u16_mask = this->u16_mask();
		for (u32 i = 0; i < stored_length; ++i) {
			u32 storage_index = i >> palette_shift;
			u32 u16_bit = i & u16_bit_mask;
			used_palette_slots.set((storage[storage_index] >> u16_bit) & u16_mask);
		}
	}
	size_t used_slots = used_palette_slots.count();
	// if less than half the palette is used, remove those and remap
	if (used_slots < ((this->u16_mask() + 1) >> 1)) {
		// generate remapping-map
		std::vector<u32> map;
		map.resize(this->u16_mask() + 1);
		{
			u32 old_i = 0;
			u32 new_i = 0;
			for (; old_i < palette.size(); ++old_i) {
				if (used_palette_slots.test(old_i)) {
					// it's used!
					map[old_i] = new_i;
					++new_i;
				}
			}
		}

		// find the smallest palette shift that will accomodate our entries
		u8 old_palette_shift = this->palette_shift;
		this->palette_shift = 4;
		while (this->u16_mask() + 1 < used_slots) {
			--this->palette_shift;
		}
		// save the new-found shift, and restore the old one
		u8 new_palette_shift = this->palette_shift;
		this->palette_shift = old_palette_shift;

		// remap the storage for the new palette shift,
		//		using our index map
		this->changeStorageSize(
				new_palette_shift,
				[&map](u32 idx) { return map[idx]; }
			);
	} // if used_slots < (current_slots >> 1)
} // void tryShrink()

template<typename T, u32 stored_length>
template<typename U = T, typename Accessor>
void PalettedStorage<T, stored_length>::fromArray<U, Accessor>(
			const U us[stored_length], const Accessor& accessor
		) {
	// unpack the Us
	T ts[stored_length];
	for (u32 i = 0; i < stored_length; ++i) {
		// becuase ts is uninitialized,
		//		we have to use placement-new here
		new (ts + i) T(accessor(us[i]));
	}

	this->fromArray(ts);
}

template<typename T, u32 stored_length>
void PalettedStorage<T, stored_length>::fromArray(
			const T ts[stored_length]
		) {
	// create our palette
	{
		std::array<T, stored_length> arr {};
		for (u32 i = 0; i < stored_length; ++i) {
			arr[i] = ts[i];
		}
		std::sort(arr.begin(), arr.end());
		this->palette.assign(arr.begin(), arr.end());
	}

	// find the required shift
	this->palette_shift = 4;
	while (this->palette.size() > this->u16_mask() + 1) {
		--this->palette_shift;
	}

	// create the storage
	this->storage.reset(paletted_storage_allocate_(stored_length >> palette_shift));

	{ // put our Ts in
		u32 u16_bit_mask = this->u16_bit_mask();
		u32 u16_mask = this->u16_mask();
		for (u32 i = 0; i < stored_length; ++i) {
			// we happen to know it's sorted, so binary search for the index
			auto palette_it = std::lower_bound(palette.begin(), palette.end(), ts[i]);
			u32 palette_idx = std::distance(palette.begin(), palette_it);

			u32 storage_index = i >> palette_shift;
			u32 u16_bit = i & u16_bit_mask;
			// no overlaps, so we can just or it in
			storage[storage_index] |= (palette_idx << u16_bit) & u16_mask;
		}
	}
}

template<typename T, u32 stored_length>
template<typename U, typename Setter>
void PalettedStorage<T, stored_length>::toArray(std::array<U, stored_length>& us, const Setter& setter) const {
	u32 u16_bit_mask = this->u16_bit_mask();
	u32 u16_mask = this->u16_mask();
	for (u32 i = 0; i < stored_length; ++i) {
		u32 storage_index = i >> palette_shift;
		u32 u16_bit = i & u16_bit_mask;
		setter(us[i], this->storage[(storage[storage_index] >> u16_bit) & u16_mask]);
	}
}
