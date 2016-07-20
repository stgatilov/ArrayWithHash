#pragma once

#include <stdint.h>
#include <type_traits>

//namespace for ArrayWithHash 
namespace AWH_NAMESPACE {

//getting integer type by its size in bytes
//used for hacking with floats representation
template<int bytes> struct IntegerBySize {};
template<> struct IntegerBySize<4> { typedef int32_t sint; };
template<> struct IntegerBySize<8> { typedef int64_t sint; };

//getting maximal representable value for given integer type
//generally, it is equivalent to std::numeric_limits<type>::max()
//but it gives a compile-time constant
template<class type> struct IntegerMaxValue {
	static const int bits = 8 * sizeof(type) - (1 + std::is_signed<type>::value);
	static const type max = (type(1) << bits) - 1 + (type(1) << bits);
};

//=======================================================================
//Default hash function is defined here.
//Trivial implementation is chosen to improve code size and performance.

//for 32-bit integers, introduced by Knuth:
// http://stackoverflow.com/a/665545/556899
static AWH_INLINE uint32_t DefaultHashFunction(uint32_t key) {
	return 2654435761U * key;
}
//analogous hash function for 64-bit integers
static AWH_INLINE uint64_t DefaultHashFunction(uint64_t key) {
	return 11400714819323198485ULL * key;
}
//32-bit version is used for integers of smaller size
static AWH_INLINE uint16_t DefaultHashFunction(uint16_t key) { return DefaultHashFunction(uint32_t(key)); }
static AWH_INLINE uint8_t  DefaultHashFunction(uint8_t  key) { return DefaultHashFunction(uint32_t(key)); }
//signed integers are treated as unsigned ones
static AWH_INLINE uint64_t DefaultHashFunction( int64_t key) { return DefaultHashFunction(uint64_t(key)); }
static AWH_INLINE uint32_t DefaultHashFunction( int32_t key) { return DefaultHashFunction(uint32_t(key)); }
static AWH_INLINE uint16_t DefaultHashFunction( int16_t key) { return DefaultHashFunction(uint16_t(key)); }
static AWH_INLINE uint8_t  DefaultHashFunction( int8_t  key) { return DefaultHashFunction(uint8_t (key)); }

//=======================================================================
//Here the value treated as the EMPTY one (by default) is defined.
//These functions are called from DefaultValueTraits (see below).

//for custom types, EMPTY = default constructed instance
//examples include: std::vector, std::unique_ptr, std::shared_ptr, std::string
template<class Value> inline
bool DefaultIsEmpty(const Value &value, void*) {
	return value == Value();
}
template<class Value> inline
Value DefaultGetEmpty(void*) {
	return Value();
}

//all the other versions are specialized using SFINAE
//they have higher priority in overload resolution thanks to helper argument

//for integers: EMPTY = maximal representable value
template<class Value> static AWH_INLINE
typename std::enable_if<std::is_integral<Value>::value, bool>::type DefaultIsEmpty(const Value &value, int) {
	return value == IntegerMaxValue<Value>::max;
}
template<class Value> static AWH_INLINE
typename std::enable_if<std::is_integral<Value>::value, Value>::type DefaultGetEmpty(int) {
	return IntegerMaxValue<Value>::max;
}

//for floats: EMPTY = NaN with all set bits (e.g. 0xFFFFFFFF for float)
//this is the most fitting choice, but it has several issues:
// 1. separate IsEmpty function is required, since NaNs are not equal to each other
// 2. it may be unsafe: implementation is allowed to change NaN's representation even on copy
template<class Value> static AWH_INLINE
typename std::enable_if<std::is_floating_point<Value>::value, bool>::type DefaultIsEmpty(const Value &value, int) {
	typedef typename IntegerBySize<sizeof(Value)>::sint Int;
	return *(Int*)&value == (Int)-1;
}
template<class Value> static AWH_INLINE
typename std::enable_if<std::is_floating_point<Value>::value, Value>::type DefaultGetEmpty(int) {
	typedef typename IntegerBySize<sizeof(Value)>::sint Int;
	union  {
		Value value;
		Int integer;
	};
	integer = Int(-1);
	return value;
}

//for raw pointers: maximal properly-aligned pointer 
//e.g. 0xFFFFFFFF for char*, 0xFFFFFFF8 for double*
template<class Value> static AWH_INLINE
typename std::enable_if<std::is_pointer<Value>::value, bool>::type DefaultIsEmpty(const Value &value, int) {
	//Note: sizeof(void) is undefined, but usually equals 1 =)
	return size_t(value) == (size_t(0) - sizeof(Value));
}
template<class Value> static AWH_INLINE
typename std::enable_if<std::is_pointer<Value>::value, Value>::type DefaultGetEmpty(int) {
	return Value(size_t(0) - sizeof(Value));
}

//=======================================================================
//Flag "relocate with memcpy" is controlled here on global level.
//
//It should be defined only for trivially relocatable types:
//  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0023r0.pdf
//When enabled, it makes array growth much faster: realloc is used.
//
//It is enabled for all value types by default.
//There are three ways to disable it:
//  For all types: define macro AWH_RELOCATE_DEFAULT to false
//  For single type: write AWH_SET_RELOCATE_WITH_MEMCPY(ValueType, false)
//  For single object: use your own ValueTraits with RELOCATE_WITH_MEMCPY = false

//global value of the flag
template<class Value> struct DefaultRelocationPolicy {
#ifndef AWH_RELOCATE_DEFAULT
	static const bool RELOCATE_WITH_MEMCPY = true;
#else
	static const bool RELOCATE_WITH_MEMCPY = AWH_RELOCATE_DEFAULT;
#endif
};
//use this macro to set the per-type value of the flag
#define AWH_SET_RELOCATE_WITH_MEMCPY(Value, policy) \
namespace AWH_NAMESPACE { \
	template<> struct DefaultRelocationPolicy<Value> { \
		static const bool RELOCATE_WITH_MEMCPY = policy; \
	}; \
}

//=======================================================================

//Default traits of key type.
//Note: you can subclass it and change EMPTY_KEY, REMOVED_KEY, hash function.
template<class Key> struct DefaultKeyTraits {
	//unsigned version of key, used for counts and sizes (do not change)
	typedef typename std::make_unsigned<Key>::type Size;

	//special value of Key: denotes empty cell in hash table
	static const Key EMPTY_KEY = IntegerMaxValue<Key>::max;
	//special value of Key: denotes removed cell in hash table
	static const Key REMOVED_KEY = EMPTY_KEY - 1;

	//hash function used for hash table
	static AWH_INLINE Size HashFunction(Key key) {
		return DefaultHashFunction(key);
	}
};

//Default traits of value type.
//Note: you can subclass it and change its empty value.
//Empty value is used for denoting empty elements in array.
template<class Value> struct DefaultValueTraits : public DefaultRelocationPolicy<Value> {
	//determines whether a given value is empty
	static AWH_INLINE bool IsEmpty(const Value &value) {
		return DefaultIsEmpty(value, 0);
	}
	//returns a temporary empty value
	static AWH_INLINE Value GetEmpty() {
		return DefaultGetEmpty<Value>(0);
	}
	//is it ok to do memcpy for relocation?
	//override to false in order to use std::move + destructor instead
	//static const bool RELOCATE_WITH_MEMCPY = true;	//inherited
};

//end namespace
}
