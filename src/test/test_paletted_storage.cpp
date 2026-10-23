#include "catch.h"
#include "irrTypes.h"

#include "util/paletted_storage.h"

template<typename T, u32 stored_length>
struct PalettedStorageTestAccessors {
	using PStore = PalettedStorage<T, stored_length>;

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

TEST_CASE("branchless-read-equivalence") {
	SECTION("storage index") {
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
};
