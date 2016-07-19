#pragma once

#include <stdint.h>
#include <stdio.h>

//macros for controlling inlining behavior (if wanted)
#if defined(_MSC_VER) && defined(AWH_CONTROL_INLINING)
	#define AWH_INLINE __forceinline
	#define AWH_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) && defined(AWH_CONTROL_INLINING)
	#define AWH_INLINE __attribute__((always_inline)) inline
	#define AWH_NOINLINE __attribute__((noinline))
#else
	#define AWH_INLINE inline
	#define AWH_NOINLINE 
#endif

#ifdef AWH_TESTING	//only for testing purposes
	//assert that is never thrown away
	#define AWH_ASSERT_ALWAYS(expr) { \
		if (!(expr)) { \
			fprintf(stderr, "Assertion failed: %s in %s:%d\n", #expr, __FILE__, __LINE__); \
			assert(#expr != 0); \
		} \
	}
	//boolean implication operator
	template<class typeA, class typeB> bool follows(typeA a, typeB b) {
		return !a || b;
	}
#endif


//support the cases when C++11 is not available
#ifndef AWH_NO_CPP11
	#define AWH_MOVE(x) std::move(x)
#else
	#define AWH_MOVE(x) x
#endif


//namespace for ArrayWithHash 
namespace AWH_NAMESPACE {

//================================================================
//integer logarithm routines are defined below

//returns minimal K such that X fits into array of 2^K size
//used extensively for hash elements during relocations
template<class Size> Size log2size(Size x) {
	Size k = 0;
	while (k < 8 * sizeof(Size) && (Size(1) << k) <= x)
		k++;
	return k;
}
//returns binary logarithm of X rounded up
//used rarely (does not affect performance)
template<class Size> static AWH_INLINE Size log2up(Size x) {
	return x == 0 ? 0 : log2size(x - 1);
}

//compiler-specific fast implementations relying in BSR x86 instruction
#if _MSC_VER >= 1600
	static AWH_INLINE uint32_t log2size(uint32_t sz) {
		unsigned long pos;
		if (!_BitScanReverse(&pos, (unsigned long)sz))		//bsr
			pos = -1;		//branchless
		return uint32_t((long)pos) + 1;
	}
	#if defined(_M_X64)
	static AWH_INLINE uint64_t log2size(uint64_t sz) {
		unsigned long pos;
		if (!_BitScanReverse64(&pos, (unsigned __int64)sz))
			pos = -1;
		return uint64_t((long)pos) + 1;
	}
	#endif
#elif __GNUC__
	static AWH_INLINE uint32_t log2size(uint32_t sz) {
		int pos = 31 ^ __builtin_clz((unsigned int)sz);		//bsr
		pos++;
		pos = (sz == 0 ? 0 : pos);	//branch on mingw?...
		return uint32_t(pos);
	}
	#if defined(__amd64__)
	static AWH_INLINE uint64_t log2size(uint64_t sz) {
		int pos = 63 ^ __builtin_clzll((unsigned long long)sz);
		pos++;
		pos = (sz == 0 ? 0 : pos);
		return uint64_t(pos);
	}
	#endif
#endif
//fast version for x64 integers on x32 platform
#if (_MSC_VER >= 1600 || __GNUC__) && !(defined(_M_X64) || defined(__amd64__))
	static AWH_INLINE uint64_t log2size(uint64_t sz) {
		union {
			struct { uint32_t low, high; };
			uint64_t both;
		};
		both = sz;
		uint32_t highRes = log2size(high);
		uint32_t lowRes = log2size(low);
		return highRes == 0 ? lowRes : highRes + 32;
	}
#endif

//call 32-bit version for integers of smaller size
static AWH_INLINE uint16_t log2size(uint16_t sz) { return log2size(uint32_t(sz)); }
static AWH_INLINE uint8_t log2size(uint8_t sz) { return log2size(uint32_t(sz)); }

//================================================================

//end namespace
}
