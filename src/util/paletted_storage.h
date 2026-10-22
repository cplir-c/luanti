#pragma once

#include "irrTypes.h"

#include <memory>
#include <vector>
#include <cstring>

template<typename T, u32 stored_length>
class PalettedStorage;

static inline u16* paletted_storage_allocate_(u32 length) {
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
		storage.writeIndex(idx, t);
	}
	T operator *() const {
		return storage.readIndex(idx);
	}
	operator T() const {
		return storage.readIndex(idx);
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

	u32 u16_mask() const;
	u32 u16_bit_mask() const;

	s32 mapT(T t) const;
	u32 mappedT(T t);

	template<typename Fun>
	inline void changeStorageSize(u8 new_shift, const Fun& mapper);

public:
	T readIndex(u32 idx) const;
	void writeIndex(u32 idx, T t);

	T operator [](u32 idx) const {
		return readIndex(idx);
	}
	auto operator [](u32 idx) {
		return PalettedStorageMutRef<T, stored_length>(*this, idx);
	}

	void tryShrink();

public:
	template<typename U = T, typename Accessor>
	inline void fromArray(const U us[stored_length], const Accessor& accessor);

	void fromArray(const T ts[stored_length]);

	template<typename U, typename Setter>
	inline void toArray(std::array<U, stored_length>& us, const Setter& setter) const;

	void toArray(std::array<T, stored_length>& ts) const {
		toArray<T>(ts, [](T& dest, T src) { dest = src; });
	}
};

// get around the definitions-must-be-visible-from-invocations thing
#define PALETTED_STORAGE_HEADER__ 1
#include "util/paletted_storage.cpp"
