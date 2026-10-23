#include "catch.h"
#include "irrTypes.h"

#include "util/paletted_storage.h"

#include <iostream>
#include <vector>
#include <memory>
#include <cstring>

template<typename T, u32 stored_length>
struct PalettedStorageTestAccessors {
	using PStore = PalettedStorage<T, stored_length>;

	std::vector<T>& getPaletteMut(PStore& s) const {
		return s.palette;
	}
	const std::vector<T>& getPalette(const PStore& s) const {
		return s.palette;
	}

	const u16* getStorage(const PStore& s) const {
		return s.storage.get();
	}
	std::unique_ptr<u16[]>& getStorageMut(PStore& s) const {
		return s.storage;
	}

	void setPaletteShift(PStore& s, u8 shift) const {
		s.palette_shift = shift;
	}
	u8 getPaletteShift(const PStore& s) const {
		return s.palette_shift;
	}
	u32 u16Mask(const PStore& s) const {
		return s.u16_mask();
	}
	u32 u16BitMask(const PStore& s) const {
		return s.u16_bit_mask();
	}
	s32 mapT(const PStore& s, T t) const {
		return s.mapT(t);
	}
	u32 mappedT(PStore& s, T t) const {
		return s.mappedT(t);
	}
};

//introduce a compile-time error to see if this is compiling

TEST_CASE("branchless-read-equivalence", "[paletted_storage]") {
	CAPTURE("testing paletted_storage bitops");
	std::cerr << "testing paletted storage";

	SECTION("storage-index") {
		// true by definition:
		//		idx >> palette_shift ==
		//		[=](){ switch(palette_shift) {
		//			case 4: return idx >> 4;
		//			case 3: return idx >> 3;
		//			case 2: return idx >> 2;
		//			case 1: return idx >> 1;
		//			case 0: return idx; // >> 0;
		//		} }
		//
		// Test it anyway
		for (u32 idx = 0; idx < 4096; ++idx) {
			for (u8 palette_shift = 0; palette_shift <= 4; ++palette_shift) {
				u32 shifted = idx >> palette_shift;
				u32 switched = [=](){
					switch(palette_shift) {
						case 4: return idx >> 4;
						case 3: return idx >> 3;
						case 2: return idx >> 2;
						case 1: return idx >> 1;
						case 0: return idx; // >> 0;
					};
					return u32(~u32());
				}();
				REQUIRE( shifted == switched );
			}
		}
	}

	PalettedStorage<s8, 4096> storage {};
	PalettedStorageTestAccessors<s8, 4096> accessor {};

	SECTION("u16_bit (bit index of requested item within the stored u16)") {
		for (u8 shift = 0; shift <= 4; ++shift) {
			accessor.setPaletteShift(storage, shift);
			u32 u16_bit_mask = accessor.u16BitMask(storage);

			for (u32 idx = 0; idx < 4096; ++idx) {
				u32 anded = idx & u16_bit_mask;
				u32 switched = [=]() -> u32 {
					switch(shift) {
						case 4: return idx & 0xf;
						case 3: return (idx & 0x7) << 1;
						case 2: return (idx & 0x3) << 2;
						case 1: return (idx & 0x1) << 3;
						case 0: return u32(0);
					};
					return u32(~u32());
				}();
				REQUIRE( anded == switched );
			}
		}
	}

	SECTION("u16_mask (a bit mask the size of the stored element)") {
		for (u8 shift = 0; shift <= 4; ++shift) {
			accessor.setPaletteShift(storage, shift);

			u32 u16_mask = accessor.u16Mask(storage);
			u32 switched = [=]() -> u32 {
				switch(shift){
					case 4: return 0x1; // 1 bit
					case 3: return 0x3; // 2 bits
					case 2: return 0xf; // 4 bits
					case 1: return 0xff; // 8 bits
					case 0: return 0xffff; // 16 bits
				};
				return 0;
			}();

			REQUIRE( u16_mask == switched );
		}
	}
}

TEST_CASE("array <-> palette tests") {
	SECTION("fixed fromArray tests") {
		PalettedStorage<int, 8> storage {};
		PalettedStorageTestAccessors<int, 8> accessor {};
		// 0110100110010110... (thue-morse sequence)
		storage.fromArray(std::array<int, 8>({2, 4, 4, 2, 4, 2, 2, 4}).data());
		std::vector<int> palette {2, 4};
		REQUIRE( accessor.getPalette(storage) == palette );
		REQUIRE( accessor.getPaletteShift(storage) == 4 );

		// 1 bit eacah, and in opposite order, so...
		// 0b1001_0110 -> 0x96
		u16 packed[] = {0x96};
		REQUIRE( std::memcmp( accessor.getStorage(storage), packed, sizeof(u16) * 1 ) == 0 );
	}
	SECTION("fixed toArray tests") {
		PalettedStorageTestAccessors<int, 8> accessor {};
		// same example as above, but in reverse
		PalettedStorage<int, 8> storage {};
		accessor.setPaletteShift(storage, 4);
		accessor.getPaletteMut(storage).assign({2, 4});
		accessor.getStorageMut(storage).reset(new u16[1]);
		accessor.getStorageMut(storage)[0] = 0x96;

		std::array<int, 8> unpacked {};
		storage.toArray(unpacked);
		std::array<int, 8> expected {2, 4, 4, 2, 4, 2, 2, 4};
		REQUIRE( unpacked == expected );
	}

	REQUIRE( false );
}
