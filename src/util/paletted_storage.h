#pragma once

#include "irrTypes.h"

#include <memory>
#include <vector>

template<typename T, u32 stored_length>
class PalettedStorage;

static u16* paletted_storage_allocate_(u32 length) {
	u16* buf = new u16[length];
	std::memset(buf, 0, sizeof(u16) * length);
	return buf;
}

template<typename T, u32 stored_length>
class PalettedStorageMutRef {
	PalettedStorage<T, stored_length>& storage;
	u32 idx;

	PalettedStorageMutRef(PalettedStorage<T, stored_length>& storage, u32 idx):
		storage(storage), idx(idx) {}

	void operator =(T t) {
		storage.write_index(idx, t);
	}
	T operator *() const {
		return storage.read_index(idx);
	}
	operator T() const {
		return storage.read_index(idx);
	}
	// haha...
	PalettedStorageMutRef operator->() {
		return *this;
	}
};

// "Using the system allocator here, especially on Windows, will cost way too much time/latency."
// - from https://voxel.wiki/wiki/palettes/
//
// I looked at std::pmr::unsynchronized_pool_resource, but std::unique_ptr did not take such an option.

template<typename T, u32 stored_length = (1 << 12)>
class PalettedStorage {
	std::vector<T> palette {};
	std::unique_ptr<u16[]> storage {
		paletted_storage_allocate_(stored_length >> 4)
	};
	u8 palette_shift = 4;

	u32 u16_mask() const {
		// goal: {0x1, 0x3, 0xf, 0xff, 0xffff}
		// 4 - palette_shift: {4, 3, 2, 1, 0}
		// 1 << (4-shift)   : {16, 8, 4, 2, 1}
		// 1 << 1<<(4-shift): {0x10000, 0x100, 0x10, 0x4, 0x2}
		// (1<<1<<(4-shift))-1: {0xffff, 0xff, 0xf, 0x3, 0x1}
		u32 u16_mask = (u32(1) << (u32(1) << (4 - palette_shift))) - 1;
		return u16_mask;
	}
	u32 u16_bit_mask() const {
		// goal: {0x0, 0x1, 0x3, 0x7, 0xf}[shift]
		// 1 << shift: {1, 2, 4, 8, 16}
		// (1<<shift)-1: {0, 0x1, 0x3, 0x7, 0xf}
		u32 u16_bit_mask = (u32(1) << palette_shift) - 1;
		return u16_bit_mask;
	}

	s32 map_t(T t) const {
		for (s32 i = 0; i < palette.size(); ++i) {
			if (palette[i] == t) {
				return i;
			}
		}
		return -1;
	}

	u32 mapped_t(T t);

	template<typename Fun>
	void change_storage_size(u8 new_shift, const Fun& mapper);

public:
	T read_index(u32 idx) const;
	void write_index(u32 idx, T t);

	T operator [](u32 idx) const {
		return read_index(idx);
	}
	auto operator [](u32 idx) {
		return PalettedStorageMutRef<T, stored_length>(*this, idx);
	}

	void try_shrink();

public:
	template<typename U = T, typename Accessor>
	void fromArray(const U us[stored_length], const Accessor& accessor);

	void fromArray(const T ts[stored_length]);

	template<typename U, typename Setter>
	void toArray(std::array<U, stored_length>& us, const Setter& setter) const;

	void toArray(std::array<T, stored_length>& ts) const {
		toArray<T>(ts, [](T& dest, T src) { dest = src; });
	}
};
