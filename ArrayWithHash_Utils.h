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

//for testing purposes and for method AssertCorrectness
#define AWH_ASSERT_ALWAYS(expr) { \
	if (!(expr)) { \
		fprintf(stderr, "Assertion failed: %s in %s:%s\n", #expr, __FILE__, __LINE__); \
		assert(#expr != 0); \
	} \
}

template<class typeA, class typeB> bool follows(typeA a, typeB b) {
	return !a || b;
}

//to support the case when C++11 is not available
#ifndef AWH_NO_CPP11
	#define AWH_MOVE(x) std::move(x)
#else
	#define AWH_MOVE(x) (x)
#endif


//default slow implementations
template<class Size> Size log2size(Size sz) {
	Size res = 0;
	while (res < 8 * sizeof(Size) && (Size(1) << res) <= sz)
		res++;
	return res;
}
template<class Size> Size log2up(Size sz) {
	return sz == 0 ? 0 : log2size(sz - 1);
}
//fast implementations
#if _MSC_VER >= 1600
	inline uint32_t log2size(uint32_t sz) {
		unsigned long pos;
		if (!_BitScanReverse(&pos, (unsigned long)sz))		//bsr
			pos = -1;		//branchless
		return uint32_t((long)pos) + 1;
	}
	#if defined(_M_X64)
	inline uint64_t log2size(uint64_t sz) {
		unsigned long pos;
		if (!_BitScanReverse64(&pos, (unsigned __int64)sz))
			pos = -1;
		return uint64_t((long)pos) + 1;
	}
	#endif
#elif __GNUC__
	inline uint32_t log2size(uint32_t sz) {
		int pos = 31 ^ __builtin_clz((unsigned int)sz);		//bsr
		pos++;
		pos = (sz == 0 ? 0 : pos);	//branch on mingw?...
		return uint32_t(pos);
	}
	#if defined(__amd64__)
	inline uint64_t log2size(uint64_t sz) {
		int pos = 63 ^ __builtin_clzll((unsigned long long)sz);
		pos++;
		pos = (sz == 0 ? 0 : pos);
		return uint64_t(pos);
	}
	#endif
#endif
#if (_MSC_VER >= 1600 || __GNUC__) && !(defined(_M_X64) || defined(__amd64__))
	inline uint64_t log2size(uint64_t sz) {
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

inline uint16_t log2size(uint16_t sz) { return log2size(uint32_t(sz)); }
inline uint8_t log2size(uint8_t sz) { return log2size(uint32_t(sz)); }
