#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include <set>
#include <limits>

// Key - some integer
//   special values: EMPTY, REMOVED
// Value - POD (bool, int, double, pointer, POD struct, etc)
//   special value: EMPTY
//   zero-byte empty? NO
// shrinking? NO

#define AWH_ASSERT_ALWAYS(expr) { \
	if (!(expr)) { \
		fprintf(stderr, "Assertion failed: %s in %s:%s\n", #expr, __FILE__, __LINE__); \
		assert(#expr != 0); \
	} \
}

typedef int32_t Key;
typedef int32_t Value;
typedef std::make_unsigned<Key>::type Size;

static const Key EMPTY_KEY = std::numeric_limits<Key>::max();
static const Key REMOVED_KEY = std::numeric_limits<Key>::max() - 1;

static const Value EMPTY_VALUE = std::numeric_limits<Value>::max();
inline bool IsEmpty(const Value &value) { return value == EMPTY_VALUE; }
//inline void ConstructEmpty(Value *value) { value = EMPTY_VALUE; }
inline Value GetEmpty() { return EMPTY_VALUE; }

static const bool RELOCATE_WITH_MEMCPY = true;
#ifndef AWH_NO_CPP11
	#define AWH_MOVE(x) std::move(x)
#else
	#define AWH_MOVE(x) (x)
#endif


inline Size HashFunction(Key key) {
	//Knuth's function: http://stackoverflow.com/a/665545/556899
	return Size(2654435761) * Size(key);
}

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
		return uint32_t(pos) + 1;
	}
	#if defined(_M_X64)
	inline uint64_t log2size(uint64_t sz) {
		unsigned long pos;
		if (!_BitScanReverse64(&pos, (unsigned __int64)sz))
			pos = -1;
		return uint64_t(pos) + 1;
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
inline uint16_t log2size(uint16_t sz) { return log2size(uint32_t(sz)); }
inline uint8_t log2size(uint8_t sz) { return log2size(uint32_t(sz)); }


static const double ARRAY_MIN_FILL = 0.45;
static const double HASH_MIN_FILL = 0.30;
static const double HASH_MAX_FILL = 0.75;
static const size_t ARRAY_MIN_SIZE = 8;
static const size_t HASH_MIN_SIZE = 8;

template<class typeA, class typeB> bool follows(typeA a, typeB b) {
	return !a || b;
}
inline Size IsHashFull(Size cfill, Size sz) {
	return cfill >= ((sz >> 2) * 3);
};


class ArrayWithHash {
	Size arrayCount, arraySize;
	Size hashSize, hashCount, hashFill;
	Value *arrayValues, *hashValues;
	Key *hashKeys;


	template<class Elem> static Elem* AllocateBuffer(Size elemCount) {
		if (elemCount == 0)
			return NULL;
		return (Elem*) operator new (elemCount * sizeof(Elem));
	}
	template<class Elem> static void DeallocateBuffer(Elem *buffer) {
		operator delete (buffer);
	}

	static inline void RelocateOne(Value &dst, const Value &src) {
		if (RELOCATE_WITH_MEMCPY)
			memcpy(&dst, &src, sizeof(Value));
		else {
			dst = AWH_MOVE(src);
			src.~Value();
		}
	}
	static inline void RelocateMany(Value *dst, const Value *src, Size cnt) {
		if (RELOCATE_WITH_MEMCPY)
			memcpy(dst, src, cnt * sizeof(Value));
		else {
			for (Size i = 0; i < cnt; i++) {
				dst[i] = AWH_MOVE(src[i]);
				src[i].~Value();
			}
		}
	}

	inline bool InArray(Key key) const {
		return Size(key) < arraySize;
	}
	inline bool InArray(Value *ptr) const {
		size_t offset = (char*)ptr - (char*)arrayValues;
		return offset < arraySize * sizeof(Value);
	}

	Size FindCellEmpty(Key key) const {
		assert(hashSize);
		Size cell = HashFunction(key) & (hashSize - 1);
		while (hashKeys[cell] != EMPTY_KEY)
			cell = (cell + 1) & (hashSize - 1);
		return cell;
	}
	Size FindCellKeyOrEmpty(Key key) const {
		assert(hashSize);
		Size cell = HashFunction(key) & (hashSize - 1);
		while (hashKeys[cell] != EMPTY_KEY && hashKeys[cell] != key)
			cell = (cell + 1) & (hashSize - 1);
		return cell;
	}

	void AdaptSizes(Key newKey) {
		static const int BITS = sizeof(Size) * 8;
		Size logHisto[BITS + 1] = {0};
		Size logArraySize = log2up(arraySize);

		//count number of keys of size in [2^(t-1); 2^t - 1]
		logHisto[logArraySize] = arrayCount;
		logHisto[log2size(newKey)]++;
		for (Size i = 0; i < hashSize; i++) {
			Key key = hashKeys[i];
			if (key == EMPTY_KEY || key == REMOVED_KEY)
				continue;
			Size keyBits = log2size((Size)key);
			assert(keyBits >= logArraySize);
			logHisto[keyBits]++;
		}

		//choose appropriate array size
		Size newArraySize = 0, newArrayCount = 0;
		Size lowerBound = std::max(arraySize, (Size)ARRAY_MIN_SIZE);
		Size prefSum = 0;
		for (Size i = logArraySize; i < BITS; i++) {
			prefSum += logHisto[i];
			Size aSize = 1 << i;
			Size required = Size(ARRAY_MIN_FILL * aSize);
			if (aSize <= lowerBound || prefSum >= required) {
				newArraySize = aSize;
				newArrayCount = prefSum;
			}
			else if (arrayCount + hashCount < required)
				break;
		}
		if (arraySize == 0 && newArrayCount == 0)
			newArraySize = 0;

		//choose hash table size
		Size newHashCount = arrayCount + hashCount - newArrayCount + 1;
		Size newHashSize = std::max(hashSize, (Size)HASH_MIN_SIZE);
		while (newHashCount >= HASH_MIN_FILL * newHashSize * 2)
			newHashSize *= 2;
		if (hashSize == 0 && newHashCount == 0)
			newHashSize = 0;

		//change sizes and relocate data
		Reallocate(newArraySize, newHashSize);
	}

	void RelocateArrayPart(Size &newArraySize) {
		Value *newArrayValues = AllocateBuffer<Value>(newArraySize);
		//Note: trivial relocation
		RelocateMany(newArrayValues, arrayValues, arraySize);
		DeallocateBuffer<Value>(arrayValues);
		for (Value *ptr = newArrayValues + arraySize; ptr < newArrayValues + newArraySize; ptr++)
			new (ptr) Value(GetEmpty());

		std::swap(arrayValues, newArrayValues);
		std::swap(arraySize, newArraySize);
	}

	template<bool RELOC_ARRAY> void RelocateHashInPlace(Size newArraySize) {
		//relocate array if required
		if (RELOC_ARRAY)
			RelocateArrayPart(newArraySize);

		if (hashSize == 0)
			return;
		Size totalCount = arrayCount + hashCount;
		
		//find first empty cell
		Size firstEmpty = 0;
		while (hashKeys[firstEmpty] != EMPTY_KEY)
			firstEmpty++;

		//do a full round from it
		Size pos = firstEmpty;
		do {
			Key key = hashKeys[pos];
			hashKeys[pos] = EMPTY_KEY;

			if (key != EMPTY_KEY && key != REMOVED_KEY) {
				const Value &value = hashValues[pos];
				if (RELOC_ARRAY && InArray(key)) {
					arrayValues[key].~Value();
					RelocateOne(arrayValues[key], value);
					arrayCount++;
				}
				else {
					//insert key as usual
					Size cell = FindCellEmpty(key);
					hashKeys[cell] = key;
					//move value if necessary
					if (cell != pos)
						RelocateOne(hashValues[cell], value);
				}
			}

			pos = (pos + 1) & (hashSize - 1);
		} while (pos != firstEmpty);

		//update hash count
		if (RELOC_ARRAY)
			hashCount = totalCount - arrayCount;
		//forget about removed entries
		hashFill = hashCount;
	}

	template<bool RELOC_ARRAY> void RelocateHashToNew(Size newHashSize, Size newArraySize) {
		//relocate array if required
		if (RELOC_ARRAY)
			RelocateArrayPart(newArraySize);

		//create new hash table and swap with it
		Key *newHashKeys = AllocateBuffer<Key>(newHashSize);
		std::uninitialized_fill_n(newHashKeys, newHashSize, EMPTY_KEY);
		Value *newHashValues = AllocateBuffer<Value>(newHashSize);

		std::swap(hashKeys, newHashKeys);
		std::swap(hashValues, newHashValues);
		std::swap(hashSize, newHashSize);
		//Note: new* are now actually old values

		Size totalCount = arrayCount + hashCount;

		for (Size i = 0; i < newHashSize; i++) {
			Key key = newHashKeys[i];
			if (key == EMPTY_KEY || key == REMOVED_KEY)
				continue;
			const Value &value = newHashValues[i];
			if (RELOC_ARRAY && InArray(key)) {
				arrayValues[key].~Value();
				RelocateOne(arrayValues[key], value);
				arrayCount++;
			}
			else {
				Size cell = FindCellEmpty(key);
				hashKeys[cell] = key;
				RelocateOne(hashValues[cell], value);
			}
		}

		DeallocateBuffer<Key>(newHashKeys);
		DeallocateBuffer<Value>(newHashValues);

		//update hash count
		if (RELOC_ARRAY)
			hashCount = totalCount - arrayCount;
		//forget about removed entries
		hashFill = hashCount;
	}

	void Reallocate(Size newArraySize, Size newHashSize) {
		assert(newArraySize >= arraySize && newHashSize >= hashSize);

		if (newHashSize == hashSize) {
			if (newArraySize == arraySize)
				RelocateHashInPlace<false>(newArraySize);
			else
				RelocateHashInPlace<true>(newArraySize);
		}
		else {
			if (newArraySize == arraySize)
				RelocateHashToNew<false>(newHashSize, newArraySize);
			else
				RelocateHashToNew<true>(newHashSize, newArraySize);
		}
	}


	Value HashGet(Key key) const {
		if (hashSize == 0)
			return GetEmpty();
		Size cell = FindCellKeyOrEmpty(key);
		return hashKeys[cell] == EMPTY_KEY ? GetEmpty() : hashValues[cell];
	}

	Value *HashGetPtr(Key key) const {
		if (hashSize == 0)
			return NULL;
		Size cell = FindCellKeyOrEmpty(key);
		return hashKeys[cell] == EMPTY_KEY ? NULL : &hashValues[cell];
	}

	Value *HashSet(Key key, Value value) {
		if (IsHashFull(hashFill, hashSize)) {
			AdaptSizes(key);
			return Set(key, AWH_MOVE(value));
		}
		Size cell = FindCellKeyOrEmpty(key);
		hashFill += (hashKeys[cell] == EMPTY_KEY);
		hashCount += (hashKeys[cell] == EMPTY_KEY);
		hashKeys[cell] = key;
		new (&hashValues[cell]) Value(AWH_MOVE(value));
		return &hashValues[cell];
	}

	Value *HashSetIfNew(Key key, Value value) {
		if (IsHashFull(hashFill, hashSize)) {
			AdaptSizes(key);
			return SetIfNew(key, AWH_MOVE(value));
		}
		Size cell = FindCellKeyOrEmpty(key);
		if (hashKeys[cell] != EMPTY_KEY)
			return &hashValues[cell];
		hashFill++;
		hashCount++;
		hashKeys[cell] = key;
		new (&hashValues[cell]) Value(AWH_MOVE(value));
		return NULL;
	}

	void HashRemove(Key key) {
		if (hashSize == 0)
			return;
		Size cell = FindCellKeyOrEmpty(key);
		if (hashKeys[cell] == EMPTY_KEY)
			return;
		hashKeys[cell] = REMOVED_KEY;
		hashCount--;
		hashValues[cell].~Value();
	}

	void HashRemovePtr(Value *ptr) {
		Size cell = ptr - &hashValues[0];
		assert(hashKeys[cell] != EMPTY_KEY && hashKeys[cell] != REMOVED_KEY);
		hashKeys[cell] = REMOVED_KEY;
		hashCount--;
		hashValues[cell].~Value();
	}

	inline void Flush() {
		arraySize = 0;
		hashSize = 0;
		arrayCount = 0;
		hashCount = 0;
		hashFill = 0;
		arrayValues = NULL;
		hashValues = NULL;
		hashKeys = NULL;
	}
	inline void RelocateFrom(const ArrayWithHash &iSource) {
		arraySize = iSource.arraySize;
		hashSize = iSource.hashSize;
		arrayCount = iSource.arrayCount;
		hashCount = iSource.hashCount;
		hashFill = iSource.hashFill;
		arrayValues = iSource.arrayValues;
		hashValues = iSource.hashValues;
		hashKeys = iSource.hashKeys;
	}
	void DestroyAllHashValues() {
		for (Size i = 0; i < hashSize; i++)
			if (hashKeys[i] != EMPTY_KEY && hashKeys[i] != REMOVED_KEY)
				hashValues[i].~Value();
	}

	//non-copyable
	ArrayWithHash (const ArrayWithHash &iSource);
	void operator= (const ArrayWithHash &iSource);

public:
	ArrayWithHash() {
		Flush();
	}
	~ArrayWithHash() {
		for (Size i = 0; i < arraySize; i++)
			arrayValues[i].~Value();
		DestroyAllHashValues();
		DeallocateBuffer<Value>(arrayValues);
		DeallocateBuffer<Value>(hashValues);
		DeallocateBuffer<Key>(hashKeys);
	}

	ArrayWithHash(ArrayWithHash &&iSource) {
		RelocateFrom(iSource);
		iSource.Flush();
	}
	void operator= (ArrayWithHash &&iSource) {
		RelocateFrom(iSource);
		iSource.Flush();
	}

	void Swap(ArrayWithHash &other) {
		std::swap(arraySize, other.arraySize);
		std::swap(arrayCount, other.arrayCount);
		std::swap(hashSize, other.hashSize);
		std::swap(hashCount, other.hashCount);
		std::swap(hashFill, other.hashFill);
		std::swap(arrayValues, other.arrayValues);
		std::swap(hashValues, other.hashValues);
		std::swap(hashKeys, other.hashKeys);
	}

	//remove all elements without shrinking
	void Clear() {
		if (arraySize && arrayCount) {
			for (Size i = 0; i < arraySize; i++)
				new (&arrayValues[i]) Value(GetEmpty());
		}
		if (hashSize && hashFill) {
			DestroyAllHashValues();
			std::fill_n(hashKeys, hashSize, EMPTY_KEY);
		}
		arrayCount = hashCount = hashFill = 0;
	}

	inline Size GetSize() const {
		return arrayCount + hashCount;
	}

	//returns value for given key
	//or EMPTY_VALUE if not present
	inline Value Get(Key key) const {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key))
			return arrayValues[key];
		else
			return HashGet(key);
	}

	//returns pointer to the value for given key
	//or NULL if not present
	inline Value *GetPtr(Key key) const {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key)) {
			Value &val = arrayValues[key];
			return IsEmpty(val) ? NULL : &val;	//branchless
		}
		else
			return HashGetPtr(key);
	}

	//sets value associated with given key
	//key is inserted if not present before
	//returns pointer to the updated value
	inline Value *Set(Key key, Value value) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		assert(!IsEmpty(value));
		if (InArray(key)) {
			Value &oldVal = arrayValues[key];
			arrayCount += IsEmpty(oldVal);	//branchless
			oldVal = AWH_MOVE(value);
			return &oldVal;
		}
		else
			return HashSet(key, AWH_MOVE(value));
	}

	//if key is present, then returns pointer to it
	//otherwise adds a new key with associated value, returns NULL
	inline Value *SetIfNew(Key key, Value value) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		assert(!IsEmpty(value));
		if (InArray(key)) {
			Value &oldVal = arrayValues[key];
			if (IsEmpty(oldVal)) {					//real branch
				oldVal = AWH_MOVE(value);
				arrayCount++;
				return NULL;
			}
			else
				return &oldVal;
/*			Value *pOldVal = &arrayValues[key];		//branchless version worth it?
			Value stored = *pOldVal;
			bool empty = IsEmpty(stored);
			stored = (empty ? value : stored);
			arrayCount += empty;
			*pOldVal = stored;
			return empty ? NULL : pOldVal;*/
		}
		else
			return HashSetIfNew(key, AWH_MOVE(value));
	}

	//removes given key (if present)
	inline void Remove(Key key) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key)) {
			Value &val = arrayValues[key];
			arrayCount -= !IsEmpty(val);	//branchless
			val = GetEmpty();
		}
		else
			HashRemove(key);
	}

	//removes key by pointer to its value
	inline void RemovePtr(Value *ptr) {
		assert(ptr);
		assert(!IsEmpty(*ptr));
		if (InArray(ptr)) {
			arrayCount--;
			*ptr = GetEmpty();
		}
		else
			HashRemovePtr(ptr);
	}

	//returns key for the given value
	inline Key KeyOf(Value *ptr) const {
		assert(ptr);
		if (InArray(ptr))
			return Key(ptr - arrayValues);
		else {
			Size cell = ptr - hashValues;
			return hashKeys[cell];
		}
	}

	//force to allocate memory for at least given amount of elements (separately for array and hash parts)
	void Reserve(Size arraySizeLB, Size hashSizeLB, bool alwaysCleanHash = false) {
		if (arraySizeLB || arraySize)
			arraySizeLB = std::max(Size(1) << log2up(arraySizeLB), std::max(arraySize, (Size)ARRAY_MIN_SIZE));
		if (hashSizeLB  ||  hashSize)
			hashSizeLB  = std::max(Size(1) << log2up( hashSizeLB), std::max( hashSize, (Size) HASH_MIN_SIZE));
		if (arraySizeLB == arraySize && hashSizeLB == hashSize && !alwaysCleanHash)
			return;
		Reallocate(arraySizeLB, hashSizeLB);
	}

	//perform given action for all the elements
	//callback is specified in format:
	//  bool action(Key key, Value &value);
	//it must return false to continue iteration, true to stop
	template<class Action> void ForEach(Action action) const {
		for (Size i = 0; i < arraySize; i++)
			if (!IsEmpty(arrayValues[i]))
				if (action(Key(i), arrayValues[i]))
					return;
		for (Size i = 0; i < hashSize; i++)
			if (hashKeys[i] != EMPTY_KEY && hashKeys[i] != REMOVED_KEY)
				if (action(Key(hashKeys[i]), hashValues[i]))
					return;
	}

	bool AssertCorrectness(int verbosity = 2) const {
		if (verbosity >= 0) {
			//check array/hash sizes
			AWH_ASSERT_ALWAYS(arraySize == 0 || arraySize >= ARRAY_MIN_SIZE);
			AWH_ASSERT_ALWAYS( hashSize == 0 ||  hashSize >=  HASH_MIN_SIZE);
			AWH_ASSERT_ALWAYS((arraySize & (arraySize - 1)) == 0);
			AWH_ASSERT_ALWAYS(( hashSize & ( hashSize - 1)) == 0);
			//check buffers
			AWH_ASSERT_ALWAYS(follows(arraySize == 0, !arrayValues));
			AWH_ASSERT_ALWAYS(follows(arraySize != 0,  arrayValues));
			AWH_ASSERT_ALWAYS(follows( hashSize == 0,  !hashValues && !hashKeys));
			AWH_ASSERT_ALWAYS(follows( hashSize != 0,   hashValues &&  hashKeys));
			//check hash fill ratio
			AWH_ASSERT_ALWAYS(hashFill <= HASH_MAX_FILL * hashSize);
		}
		if (verbosity >= 1) {
			//iterate over array
			Size trueArrayCount = 0;
			for (Size i = 0; i < arraySize; i++) {
				if (IsEmpty(arrayValues[i]))
					continue;
				trueArrayCount++;
			}
			AWH_ASSERT_ALWAYS(arrayCount == trueArrayCount);
			//iterate over hash table
			Size trueHashCount = 0, trueHashFill = 0;
			for (Size i = 0; i < hashSize; i++) {
				Key key = hashKeys[i];
				AWH_ASSERT_ALWAYS(Size(key) >= arraySize);
				if (key != EMPTY_KEY)
					trueHashFill++;
				if (key != EMPTY_KEY && key != REMOVED_KEY) {
					trueHashCount++;
					const Value &value = hashValues[i];
					AWH_ASSERT_ALWAYS(!IsEmpty(value));	//must be alive
				} //hashValues[i] must be dead otherwise
			}
			AWH_ASSERT_ALWAYS(hashCount == trueHashCount && hashFill == trueHashFill);
		}
		if (verbosity >= 2) {
			//hash verbose check: keys are unique
			std::set<Key> keys;
			for (Size i = 0; i < hashSize; i++) {
				Key key = hashKeys[i];
				if (key == EMPTY_KEY || key == REMOVED_KEY)
					continue;
				AWH_ASSERT_ALWAYS(keys.count(key) == 0);
				keys.insert(key);
			}
			//hash verbose check: key placed near hash function value
			for (Size i = 0; i < hashSize; i++) {
				Key key = hashKeys[i];
				if (key == EMPTY_KEY || key == REMOVED_KEY)
					continue;
				Size cell = FindCellKeyOrEmpty(key);
				AWH_ASSERT_ALWAYS(hashKeys[cell] == key);
			}
		}
		return true;
	}

};

namespace std {
	inline void swap(ArrayWithHash &a, ArrayWithHash &b) {
		a.Swap(b);
	}
};
