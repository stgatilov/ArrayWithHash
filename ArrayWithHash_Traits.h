#pragma once

#include <type_traits>
#include <cstdint>
#include <limits>

//helper for hacking with floats representation
template<class type> struct EquallySizedInteger {};
template<> struct EquallySizedInteger<float> { typedef int32_t sint; };
template<> struct EquallySizedInteger<double> { typedef int64_t sint; };
//workaround for compile-time constant max values of integers
template<class type> struct IntegerMaxValue {
	static const int bits = 8 * sizeof(type) - (1 + std::is_signed<type>::value);
	static const type max = (type(1) << bits) - 1 + (type(1) << bits);
};

//default hash function by Knuth: http://stackoverflow.com/a/665545/556899
inline uint32_t DefaultHashFunction(uint32_t key) {
	return 2654435761U * key;
}
inline uint64_t DefaultHashFunction(uint64_t key) {
	return 11400714819323198485ULL * key;
}
inline uint16_t DefaultHashFunction(uint16_t key) { return DefaultHashFunction(uint32_t(key)); }
inline uint8_t  DefaultHashFunction(uint8_t  key) { return DefaultHashFunction(uint32_t(key)); }
inline uint64_t DefaultHashFunction( int64_t key) { return DefaultHashFunction(uint64_t(key)); }
inline uint32_t DefaultHashFunction( int32_t key) { return DefaultHashFunction(uint32_t(key)); }
inline uint16_t DefaultHashFunction( int16_t key) { return DefaultHashFunction(uint16_t(key)); }
inline uint8_t  DefaultHashFunction( int8_t  key) { return DefaultHashFunction(uint8_t (key)); }

//default empty value for custom types: default constructed instance
template<class Value> inline bool DefaultIsEmpty(const Value &value, void*) {
	return value == Value();
}
template<class Value> inline Value DefaultGetEmpty(void*) {
	return Value();
}
//default empty value for integers: maximal representable
template<class Value> inline typename std::enable_if<std::is_integral<Value>::value, bool>::type DefaultIsEmpty(const Value &value, int) {
	return value == std::numeric_limits<Value>::max();
}
template<class Value> inline typename std::enable_if<std::is_integral<Value>::value, Value>::type DefaultGetEmpty(int) {
	return std::numeric_limits<Value>::max();
}
//default empty value for floats: NaN with all bits set
template<class Value> inline typename std::enable_if<std::is_floating_point<Value>::value, bool>::type DefaultIsEmpty(const Value &value, int) {
	typedef typename EquallySizedInteger<Value>::sint Int;
	return *(Int*)&value == (Int)-1;
}
template<class Value> inline typename std::enable_if<std::is_floating_point<Value>::value, Value>::type DefaultGetEmpty(int) {
	typedef typename EquallySizedInteger<Value>::sint Int;
	union  {
		Value value;
		Int integer;
	};
	integer = Int(-1);
	return value;
}
//default empty value for raw pointers: maximal well-aligned pointer
template<class Value> inline typename std::enable_if<std::is_pointer<Value>::value, bool>::type DefaultIsEmpty(const Value &value, int) {
	return size_t(value) == (size_t(0) - sizeof(Value));
}
template<class Value> inline typename std::enable_if<std::is_pointer<Value>::value, Value>::type DefaultGetEmpty(int) {
	return Value(size_t(0) - sizeof(Value));
}

//Default traits of key type.
//Note: you can subclass it and change EMPTY_KEY, REMOVED_KEY, hash function.
template<class Key> struct DefaultKeyTraits {
	//unsigned version of key, used for counts and sizes (do not change)
	typedef typename std::make_unsigned<Key>::type Size;

	//special value of Key: denotes empty cell in hash table
	//static const Key EMPTY_KEY = std::numeric_limits<Key>::max();
	static const Key EMPTY_KEY = IntegerMaxValue<Key>::max;
	//special value of Key: denotes removed cell in hash table
	static const Key REMOVED_KEY = EMPTY_KEY - 1;

	//hash function used for hash table
	static inline Size HashFunction(Key key) {
		return DefaultHashFunction(key);
	}
};

//Default traits of value type.
//Note: you can subclass it and change its empty value.
//Empty value is used for denoting empty elements in array.
template<class Value> struct DefaultValueTraits {
	//determines whether a given value is empty
	static inline bool IsEmpty(const Value &value) {
		return DefaultIsEmpty(value, 0);
	}
	//returns a temporary empty value
	static inline Value GetEmpty() {
		return DefaultGetEmpty<Value>(0);
	}
	//is it ok to do memcpy for relocation?
	//override to false in order to use std::move + destructor instead
	static const bool RELOCATE_WITH_MEMCPY = true;
};
