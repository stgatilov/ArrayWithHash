#pragma once

#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <algorithm>
#include <memory>
#ifdef AWH_TESTING
#include <set>   //used only in AssertCorrectness
#endif

#include "ArrayWithHash_Utils.h"
#ifndef AWH_NO_CPP11
#include "ArrayWithHash_Traits.h"
#endif


//minimal allowed fill ratio of array part on automatic reallocation
static const double ARRAY_MIN_FILL = 0.45;
//minimal allowed fill ratiu of hash table part on automatic reallocation
static const double HASH_MIN_FILL = 0.30;
//maximal allowed fill ratio of hash table part ever (next insert -> reallocation)
static const double HASH_MAX_FILL = 0.75;
//minimal size of non-empty array part
static const size_t ARRAY_MIN_SIZE = 8;
//minimal size of non-empty hash part
static const size_t HASH_MIN_SIZE = 8;
//fast check for reaching HASH_MAX_FILL ratio (without float arithmetics)
template<class Size> static AWH_INLINE bool IsHashFull(Size cfill, Size sz) {
	return cfill >= ((sz >> 2) * 3);
}

//array with hash table backup = hash table with array optimization
//Key type must be an integer (32-bit or 64-bit are advised).
//Value type can be anything: integer, real, pointer, smart pointer, string, ...
template<
	class TKey, class TValue,
#ifndef AWH_NO_CPP11
	class TKeyTraits = DefaultKeyTraits<TKey>, class TValueTraits = DefaultValueTraits<TValue>
#else
	class TKeyTraits, class TValueTraits	//note: without C++11, user has to always specify traits
#endif
>
class ArrayWithHash {
	//accessing template arguments from outside
	typedef TKey Key;
	typedef TValue Value;
	typedef TKeyTraits KeyTraits;
	typedef TValueTraits ValueTraits;
	//default unsigned integer type
	typedef typename KeyTraits::Size Size;

private:
	//pseudonyms for making code more readable
	static const Key EMPTY_KEY = KeyTraits::EMPTY_KEY;
	static const Key REMOVED_KEY = KeyTraits::REMOVED_KEY;

	//array part: total size = maximal number of elements (power of two or zero)
	Size arraySize;
	//array part: number of valid elements
	Size arrayCount;
	//hash part: total number of cells (power of two or zero)
	Size hashSize;
	//hash part: number of valid elements
	Size hashCount;
	//hash part: number of cells used (including those tagged as REMOVED)
	Size hashFill;
	//array part: pointer to buffer
	Value *arrayValues;
	//hash part: pointer to buffer with values only
	Value *hashValues;
	//hash part: pointer to buffer with keys only
	Key *hashKeys;
	//Note: i-th cell of hash table is (hashKeys[i], hashValues[i])


	//routines used for memory allocation/deallocation
	//Note: malloc/free are used in order to take benefits of realloc
	template<class Elem> static Elem* AllocateBuffer(Size elemCount) {
		if (elemCount == 0)
			return NULL;
		return (Elem*) malloc (size_t(elemCount) * sizeof(Elem));
	}
	template<class Elem> static void DeallocateBuffer(Elem *buffer) {
		free(buffer);
	}

	//relocate single value from alive src to dead dst
	//after relocation: src is dead, dst is alive
	static AWH_INLINE void RelocateOne(Value &dst, Value &src) {
		if (ValueTraits::RELOCATE_WITH_MEMCPY)
			memcpy(&dst, &src, sizeof(Value));
		else {
			new (&dst) Value(AWH_MOVE(src));
			src.~Value();
		}
	}
	//relocate array of values
	static void RelocateMany(Value *dst, Value *src, Size cnt) {
		if (ValueTraits::RELOCATE_WITH_MEMCPY)
			memcpy(dst, src, size_t(cnt) * sizeof(Value));
		else {
			for (Size i = 0; i < cnt; i++) {
				new (&dst[i]) Value(AWH_MOVE(src[i]));
				src[i].~Value();
			}
		}
	}

	//checks whether given key belongs to the array part
	AWH_INLINE bool InArray(Key key) const {
		//one comparison is used for signed numbers too:
		//  http://stackoverflow.com/a/17095534/556899
		return Size(key) < arraySize;
	}
	//checks whether given value belongs to the array part
	AWH_INLINE bool InArray(Value *ptr) const {
		size_t offset = (char*)ptr - (char*)arrayValues;
		return offset < size_t(arraySize) * sizeof(Value);
	}

	//calculate hash function for given key and resolve collision by linear probing
	//returns the first EMPTY cell found (caller must ensure that key is not yet present)
	//used only in internal relocation methods
	AWH_INLINE Size FindCellEmpty(Key key) const {
		assert(hashSize);
		Size cell = KeyTraits::HashFunction(key) & (hashSize - 1);
		while (hashKeys[cell] != EMPTY_KEY)
			cell = (cell + 1) & (hashSize - 1);
		return cell;
	}
	//returns the first cell which is EMPTY of contains specified key
	//used in all user-called methods
	AWH_INLINE Size FindCellKeyOrEmpty(Key key) const {
		assert(hashSize);
		Size cell = KeyTraits::HashFunction(key) & (hashSize - 1);
		while (hashKeys[cell] != EMPTY_KEY && hashKeys[cell] != key)
			cell = (cell + 1) & (hashSize - 1);
		return cell;
	}

	//resize array and hash parts due to hash table fill ratio maximized
	//newKey parameter is the new key to be inserted right after resizing
	//the new sizes are chosen so that both the old keys and the new one fit
	AWH_NOINLINE void AdaptSizes(Key newKey) {
		static const int BITS = sizeof(Size) * 8;
		//logHisto[t] = number of keys in range [2^(t-1); 2^t - 1]
		Size logHisto[BITS + 1] = {0};
		Size logArraySize = log2up(arraySize);

		//=== populate logHisto histogram with all the valid elements ===
		logHisto[logArraySize] = arrayCount;	//elements in array part
		logHisto[log2size(newKey)]++;			//to-be-inserted element
		for (Size i = 0; i < hashSize; i++) {
			//Note: only elements in hash table part are processed
			Key key = hashKeys[i];
			if (key == EMPTY_KEY || key == REMOVED_KEY)
				continue;
			//valid element: increment histogram count
			Size keyBits = log2size((Size)key);
			assert(keyBits >= logArraySize);
			logHisto[keyBits]++;
		}

		//=== choose appropriate size for the array part ===
		Size newArraySize = 0, newArrayCount = 0;
		//note: array cannot be shrink, and it cannot be too small
		Size lowerBound = std::max(arraySize, (Size)ARRAY_MIN_SIZE);
		Size prefSum = 0;
		for (Size i = logArraySize; i < BITS; i++) {
			//prefSum is number of elements less than 2^i
			prefSum += logHisto[i];
			Size aSize = 1 << i;
			//array must have enough fill ratio for any viable size
			Size required = Size(ARRAY_MIN_FILL * aSize);
			if (aSize <= lowerBound || prefSum >= required) {
				//maximal array size is chosen among viable options
				newArraySize = aSize;
				newArrayCount = prefSum;
			}
			else if (arrayCount + hashCount + 1 < required)
				break;	//this size and greater are surely not viable
		}
		//if still no element is in the array part, then do not create it
		if (arraySize == 0 && newArrayCount == 0)
			newArraySize = 0;

		//=== choose appropriate size for the hash table part ===
		Size newHashCount = arrayCount + hashCount - newArrayCount + 1;
		//hash table part cannot shrink, and it cannot be too small
		Size newHashSize = std::max(hashSize, (Size)HASH_MIN_SIZE);
		//increase hash size as long as hash fill ratio does not drop too small
		while (newHashCount >= HASH_MIN_FILL * newHashSize * 2)
			newHashSize *= 2;
		//if still no element is in the hash table part, then do not create it
		if (hashSize == 0 && newHashCount == 0)
			newHashSize = 0;

		//physically relocate all the data
		Reallocate(newArraySize, newHashSize);
	}

	//reallocate the array part of the data structure
	//newArraySize is the desired new size of the array
	AWH_NOINLINE void RelocateArrayPart(Size newArraySize) {
		Value *newArrayValues;
		if (ValueTraits::RELOCATE_WITH_MEMCPY)
			//values are marked as trivially relocatable: realloc can be used
			newArrayValues = (Value*) realloc(arrayValues, size_t(newArraySize) * sizeof(Value));
		else {
			//allocate new buffer
			newArrayValues = AllocateBuffer<Value>(newArraySize);
			//relocate all elements to it
			RelocateMany(newArrayValues, arrayValues, arraySize);
			//free old buffer
			DeallocateBuffer<Value>(arrayValues);
		}
		//upper part of the array is still dead (i.e. raw, not constructed)
		//we construct all these elements with EMPTY value
		for (Value *ptr = newArrayValues + arraySize; ptr < newArrayValues + newArraySize; ptr++)
			new (ptr) Value(ValueTraits::GetEmpty());

		//save the new array
		arrayValues = newArrayValues;
		arraySize = newArraySize;
	}

	//perform a single pass over hash table in order to:
	// 1. clean, i.e. eliminate all REMOVED entries
	// 2. move some elements into array part (if RELOC_ARRAY is true)
	template<bool RELOC_ARRAY> AWH_NOINLINE void RelocateHashInPlace(Size newArraySize) {
		//reallocate the array part (if required)
		if (RELOC_ARRAY)
			RelocateArrayPart(newArraySize);

		//hash is empty, no action required
		if (hashSize == 0)
			return;
		Size totalCount = arrayCount + hashCount;
		
		//find first empty cell
		//it must be present because hash fill ratio is bounded
		Size firstEmpty = 0;
		while (hashKeys[firstEmpty] != EMPTY_KEY)
			firstEmpty++;

		//do a full round over the hash table
		//note: iteration is started from the first empty cell
		//as a result, each group of non-empty elements is enumerated in order
		Size pos = firstEmpty;
		do {
			Key key = hashKeys[pos];
			//mark key as empty, even if it is valid,
			//allowing it to be found by FindCellEmpty later
			hashKeys[pos] = EMPTY_KEY;

			if (key != EMPTY_KEY && key != REMOVED_KEY) {
				//valid element: must be relocated
				Value &value = hashValues[pos];
				if (RELOC_ARRAY && InArray(key)) {
					//fits into expanded array part
					//note: destination must be killed before relocation
					arrayValues[key].~Value();
					RelocateOne(arrayValues[key], value);
					arrayCount++;
				}
				else {
					//must be retained in the hash table part
					//in order to find its new place, insert it as usual
					Size cell = FindCellEmpty(key);
					hashKeys[cell] = key;
					//relocate element's value (only if its cell has changed)
					if (cell != pos)
						RelocateOne(hashValues[cell], value);
				}
			}

			//go to the next cell (cyclically)
			pos = (pos + 1) & (hashSize - 1);
		} while (pos != firstEmpty);

		//if necessary, update hash table count
		if (RELOC_ARRAY)
			hashCount = totalCount - arrayCount;
		//all the REMOVED entries were dropped, forget them
		hashFill = hashCount;
	}

	//reallocate the hash table part, doing the following in process:
	// 1. clean, i.e. eliminate all REMOVED entries
	// 2. move some elements into array part (if RELOC_ARRAY is true)
	template<bool RELOC_ARRAY> AWH_NOINLINE void RelocateHashToNew(Size newHashSize, Size newArraySize) {
		//reallocate the array part (if required)
		if (RELOC_ARRAY)
			RelocateArrayPart(newArraySize);

		//create new buffers for the hash table
		Key *newHashKeys = AllocateBuffer<Key>(newHashSize);
		//note: fill keys buffer with EMPTY key
		std::uninitialized_fill_n(newHashKeys, newHashSize, EMPTY_KEY);
		Value *newHashValues = AllocateBuffer<Value>(newHashSize);
		//note: leave values buffer raw (i.e. no elements constructed)

		//install the new buffers into "this" object
		std::swap(hashKeys, newHashKeys);
		std::swap(hashValues, newHashValues);
		std::swap(hashSize, newHashSize);
		//Note: newXXX are now actually old values

		Size totalCount = arrayCount + hashCount;
		//iterate over all elements in the old hash table (and relocate them)
		for (Size i = 0; i < newHashSize; i++) {
			Key key = newHashKeys[i];
			if (key == EMPTY_KEY || key == REMOVED_KEY)
				continue;
			//valid key found, must be inserted into the new hash table
			Value &value = newHashValues[i];
			if (RELOC_ARRAY && InArray(key)) {
				//fits into expanded array part
				//note: destination must be killed before relocation
				arrayValues[key].~Value();
				RelocateOne(arrayValues[key], value);
				arrayCount++;
			}
			else {
				//must be inserted into the new hash table
				Size cell = FindCellEmpty(key);
				hashKeys[cell] = key;
				RelocateOne(hashValues[cell], value);
			}
		}

		//free the old hash table buffers
		//note: the keys are integers, so no destruction is necessary for them
		DeallocateBuffer<Key>(newHashKeys);
		//note: values buffer is not raw, since all the valid values have been relocated
		DeallocateBuffer<Value>(newHashValues);

		//if necessary, update hash table count
		if (RELOC_ARRAY)
			hashCount = totalCount - arrayCount;
		//all the REMOVED entries were dropped, forget them
		hashFill = hashCount;
	}

	AWH_NOINLINE void Reallocate(Size newArraySize, Size newHashSize) {
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


	AWH_NOINLINE Value HashGet(Key key) const {
		if (hashSize == 0)
			return ValueTraits::GetEmpty();
		Size cell = FindCellKeyOrEmpty(key);
		return hashKeys[cell] == EMPTY_KEY ? ValueTraits::GetEmpty() : hashValues[cell];
	}

	AWH_NOINLINE Value *HashGetPtr(Key key) const {
		if (hashSize == 0)
			return NULL;
		Size cell = FindCellKeyOrEmpty(key);
		return hashKeys[cell] == EMPTY_KEY ? NULL : &hashValues[cell];
	}

	AWH_NOINLINE Value *HashSet(Key key, Value value) {
		if (IsHashFull(hashFill, hashSize)) {
			AdaptSizes(key);
			return Set(key, AWH_MOVE(value));
		}
		Size cell = FindCellKeyOrEmpty(key);
		bool newElement = (hashKeys[cell] == EMPTY_KEY);
		hashFill += newElement;
		hashCount += newElement;
		hashKeys[cell] = key;
		if (!newElement)
			hashValues[cell].~Value();
		new (&hashValues[cell]) Value(AWH_MOVE(value));
		return &hashValues[cell];
	}

	AWH_NOINLINE Value *HashSetIfNew(Key key, Value value) {
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

	AWH_NOINLINE void HashRemove(Key key) {
		if (hashSize == 0)
			return;
		Size cell = FindCellKeyOrEmpty(key);
		if (hashKeys[cell] == EMPTY_KEY)
			return;
		hashKeys[cell] = REMOVED_KEY;
		hashCount--;
		hashValues[cell].~Value();
	}

	AWH_NOINLINE void HashRemovePtr(Value *ptr) {
		size_t cell = ptr - &hashValues[0];
		assert(hashKeys[cell] != EMPTY_KEY && hashKeys[cell] != REMOVED_KEY);
		hashKeys[cell] = REMOVED_KEY;
		hashCount--;
		hashValues[cell].~Value();
	}

	AWH_INLINE void Flush() {
		arraySize = 0;
		hashSize = 0;
		arrayCount = 0;
		hashCount = 0;
		hashFill = 0;
		arrayValues = NULL;
		hashValues = NULL;
		hashKeys = NULL;
	}
	AWH_INLINE void RelocateFrom(const ArrayWithHash &iSource) {
		arraySize = iSource.arraySize;
		hashSize = iSource.hashSize;
		arrayCount = iSource.arrayCount;
		hashCount = iSource.hashCount;
		hashFill = iSource.hashFill;
		arrayValues = iSource.arrayValues;
		hashValues = iSource.hashValues;
		hashKeys = iSource.hashKeys;
	}
	AWH_INLINE void DestroyAllHashValues() {
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
	AWH_NOINLINE ~ArrayWithHash() {
		for (Size i = 0; i < arraySize; i++)
			arrayValues[i].~Value();
		DestroyAllHashValues();
		DeallocateBuffer<Value>(arrayValues);
		DeallocateBuffer<Value>(hashValues);
		DeallocateBuffer<Key>(hashKeys);
	}

#ifndef AWH_NO_CPP11
	//container is easily movable in C++11
	ArrayWithHash(ArrayWithHash &&iSource) {
		RelocateFrom(iSource);
		iSource.Flush();
	}
	void operator= (ArrayWithHash &&iSource) {
		RelocateFrom(iSource);
		iSource.Flush();
	}
#endif

	//fast O(1) swap between containers
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
	AWH_NOINLINE void Clear() {
		if (arraySize && arrayCount) {
			for (Size i = 0; i < arraySize; i++)
				arrayValues[i] = AWH_MOVE(ValueTraits::GetEmpty());
		}
		if (hashSize && hashFill) {
			DestroyAllHashValues();
			std::fill_n(hashKeys, hashSize, EMPTY_KEY);
		}
		arrayCount = hashCount = hashFill = 0;
	}

	AWH_INLINE Size GetSize() const {
		return arrayCount + hashCount;
	}

	//return value for given key
	//or "empty" value if not present
	//Note: requires Value type to be copyable
	AWH_INLINE Value Get(Key key) const {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key))
			return arrayValues[key];
		else
			return HashGet(key);
	}

	//returns pointer to the value for given key
	//or NULL if not present
	AWH_INLINE Value *GetPtr(Key key) const {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key)) {
			Value &val = arrayValues[key];
			return ValueTraits::IsEmpty(val) ? NULL : &val;	//branchless
		}
		else
			return HashGetPtr(key);
	}

	//sets value associated with given key
	//key is inserted if not present before
	//returns pointer to the updated/inserted value
	AWH_INLINE Value *Set(Key key, Value value) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		assert(!ValueTraits::IsEmpty(value));
		if (InArray(key)) {
			Value &oldVal = arrayValues[key];
			arrayCount += ValueTraits::IsEmpty(oldVal);	//branchless
			oldVal = AWH_MOVE(value);
			return &oldVal;
		}
		else
			return HashSet(key, AWH_MOVE(value));
	}

	//if key is present, then returns pointer to it
	//otherwise adds a new key with associated value, and returns NULL
	AWH_INLINE Value *SetIfNew(Key key, Value value) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		assert(!ValueTraits::IsEmpty(value));
		if (InArray(key)) {
			Value &oldVal = arrayValues[key];
			if (ValueTraits::IsEmpty(oldVal)) {					//real branch
				oldVal = AWH_MOVE(value);
				arrayCount++;
				return NULL;
			}
			else
				return &oldVal;
/*			Value *pOldVal = &arrayValues[key];		//branchless version worth it?
			Value stored = *pOldVal;
			bool empty = ValueTraits::IsEmpty(stored);
			stored = (empty ? value : stored);
			arrayCount += empty;
			*pOldVal = stored;
			return empty ? NULL : pOldVal;*/
		}
		else
			return HashSetIfNew(key, AWH_MOVE(value));
	}

	//removes given key (if present)
	AWH_INLINE void Remove(Key key) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key)) {
			Value &val = arrayValues[key];
			arrayCount -= !ValueTraits::IsEmpty(val);	//branchless
			val = ValueTraits::GetEmpty();
		}
		else
			HashRemove(key);
	}

	//removes key specified by pointer to its value
	AWH_INLINE void RemovePtr(Value *ptr) {
		assert(ptr);
		assert(!ValueTraits::IsEmpty(*ptr));
		if (InArray(ptr)) {
			arrayCount--;
			*ptr = ValueTraits::GetEmpty();
		}
		else
			HashRemovePtr(ptr);
	}

	//returns key for the given value
	AWH_INLINE Key KeyOf(Value *ptr) const {
		assert(ptr);
		if (InArray(ptr))
			return Key(ptr - arrayValues);
		else {
			size_t cell = ptr - hashValues;
			return hashKeys[cell];
		}
	}

	//force to allocate memory for at least given amount of elements (separately for array and hash parts)
	AWH_NOINLINE void Reserve(Size arraySizeLB, Size hashSizeLB, bool alwaysCleanHash = false) {
		if (arraySizeLB || arraySize)
			arraySizeLB = std::max(Size(Size(1) << log2up(arraySizeLB)), std::max(arraySize, (Size)ARRAY_MIN_SIZE));
		if (hashSizeLB  ||  hashSize)
			hashSizeLB  = std::max(Size(Size(1) << log2up( hashSizeLB)), std::max( hashSize, (Size) HASH_MIN_SIZE));
		if (arraySizeLB == arraySize && hashSizeLB == hashSize && !alwaysCleanHash)
			return;
		Reallocate(arraySizeLB, hashSizeLB);
	}

	//perform given action for all the elements in container
	//callback is specified in format:
	//  bool action(Key key, Value &value);
	//it must return: false to continue iteration, true to stop
	template<class Action> void ForEach(Action &action) const {
		for (Size i = 0; i < arraySize; i++)
			if (!ValueTraits::IsEmpty(arrayValues[i]))
				if (action(Key(i), arrayValues[i]))
					return;
		for (Size i = 0; i < hashSize; i++)
			if (hashKeys[i] != EMPTY_KEY && hashKeys[i] != REMOVED_KEY)
				if (action(Key(hashKeys[i]), hashValues[i]))
					return;
	}

#ifdef AWH_TESTING
	//internal method: checks all the invariants of the container
	//it is not called from anywhere (except tests), and you should not call it too
	AWH_NOINLINE bool AssertCorrectness(int verbosity = 2) const {
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
				if (ValueTraits::IsEmpty(arrayValues[i]))
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
					AWH_ASSERT_ALWAYS(!ValueTraits::IsEmpty(value));	//must be alive
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
#endif
};

//make sure std::swap works via Swap method
namespace std {
	template<class Key, class Value, class KeyTraits, class ValueTraits>
	AWH_INLINE void swap(
		ArrayWithHash<Key, Value, KeyTraits, ValueTraits> &a,
		ArrayWithHash<Key, Value, KeyTraits, ValueTraits> &b
	) {
		a.Swap(b);
	}
};
