The ArrayWithHash container is an integer-keyed hash table with array optimization for small positive keys.
Equivalently, it is an array with hash table backup for large and negative indices.
It consists of two automatically managed parts: array part and hash table part.

This container is very performance-centric by design, especially for the operations performed within the array part.
The algorithm is heavily inspired by implementation of tables in LUA interpreter (see [ltable.c](http://www.lua.org/source/5.1/ltable.c.html)).


### Intended usage case? ###

ArrayWithHash is particularly useful when you need to maintain a mapping from some IDs into objects,
and you are not sure that IDs are always consecutive, although they usually are.
If performance is important, ArrayWithHash would be order of magnitude faster than std::unordered_map in such case.

### How to use it in one's project? ###

Simply copy the following three headers into your source code repo:
```
	ArrayWithHash.h
	ArrayWithHash_Traits.h
	ArrayWithHash_Utils.h
```
Make sure that all these headers are in the include path of the compiler.
Include directly only the *ArrayWithHash.h* file.

ArrayWithHash library is licensed under the [Boost Software License 1.0](http://www.boost.org/LICENSE_1_0.txt).

### Any code samples? ###

**1a)** Reading list of objects and registering them by ID:
```
#!c++
ArrayWithHash<int, shared_ptr<BaseObject>> idToObj;
...
while (!feof(input_file)) {
	shared_ptr<BaseObject> obj;
	...	//read object into obj
	assert(idToObj.GetPtr(obj->GetId()) == NULL);
	idToObj.Set(obj->GetId(), std::move(obj));
}
```


**1b)** Printing out the list of all objects registered:
```
#!c++
idToObj.ForEach([](int id, const shared_ptr<BaseObject> &obj) -> bool {
	printf("ID = %3d: %s\n", id, obj->GetName());
	return false;	//continue iteration
});
```

**1c)** Deleting an object:
```
#!c++
idToObj.Remove(obj->GetId());	//note: works even if obj is already removed
...
```

**2)** Checking array of integers for duplicates:
```
#!c++
ArrayWithHash<int, bool> present;
for (int key : input)
	if (present.SetIfNew(key, false))	//note: true is reserved as "empty"
		return true;
return false;
```

### Why isn't interface STL-like? ###

The container has its own interface with custom methods.
Interface of std::unordered_map is rather heavy and relies on things like iterators.
Since ArrayWithHash is mostly targeted at blazingly fast operations in the array part,
it usually uses lightweight methods and parameters, often with passing by value.
Pointer-to-value is used instead of iterator, and ForEach is used for iteration instead of external loop with iterator.
Look at the comments near public methods for brief documentation of the interface.

### What are special values? What are they used for? ###

Since hash table part is stored in a flat array and array part may contain missing elements,
it is necessary to choose several special values.
Note that it is forbidden to use these values in valid elements.
This is very similar to the requirements of the [Google Hash Table](https://github.com/sparsehash/sparsehash) library (see API section).

Here is the full list of special values:

1. *Empty* key: used to mark empty cells (buckets) in the hash table.

2. *Removed* key: used to mark cells (buckets) with removed elements in the hash table.

3. *Empty* value: used to mark nonexistent elements in the array part.

By default, two maximal representable integers are used as *empty* and *removed* keys.
The *empty* value is chosen differently for different types:

* integer: maximal representable integer

* real: quiet NaN with all bits set

* raw pointer: maximal representable pointer (almost)

* object types (e.g. smart pointers, STL strings): default-constructed value

### How to change special values? ###

Special values are defined in KeyTraits and ValueTraits types, which are template arguments of ArrayWithHash.
Constants EMPTY_KEY and REMOVED_KEY in KeyTraits define special values for key type.
In ValueTraits, two methods IsEmpty and GetEmpty together define *empty* value for value type.
Look DefaultKeyTraits and DefaultValueTraits structs in *ArrayWithHash_Traits.h* for details.

In order to change special values, you have to specify other types as template arguments.
Perhaps the easiest approach is to create types inherited from the default traits types:

	struct MyKeyTraits : DefaultKeyTraits<int> {
		static const int EMPTY_KEY = (int)0xCCCCCCCC;
		static const int REMOVED_KEY = (int)0xDEADBEEF;
	};
	struct MyValueTraits : DefaultValueTraits<double> {
		static double GetEmpty() { return 1e+100; }
		static bool IsEmpty(double value) { return value == GetEmpty(); }
	};
	typedef ArrayWithHash<int, double, MyKeyTraits, MyValueTraits> IntToRealMap;

### What are compiler requirements? ###

Any compiler with C++11 (or C++0x) support should be sufficient for proper usage of ArrayWithHash.
C++11 compiler is recommended for using ArrayWithHash.

Compatibility with compilers based on C++03 is also maintained.
You have to define AWH_NO_CPP11 macro in order to remove all C++11 features from the library.
Note that in such case you have to define your own traits types, since there are no default ones.
See Cpp03Test.cpp for a sample.
Also, it is not recommended to use large value types (e.g. std::string, std::vector) in C++03 mode,
because without move semantics temporary copies of value objects would be created.
You can use std::shared_ptr-s or raw pointers instead.

Currently the library is being tested on MSVC and MinGW GCC compilers.
Visual C++ 2013 is being used to run tests, but version 2010 is also enough to use library in C++11 mode.
If you have issues with using it on any compiler, please contact the author.

### Are there any requirements on the key type? ###

Of course, key must be a builtin integer, since arrays have integer indices.
Both signed and unsigned integers can be used.
Note that in case of signed keys, array optimization does not apply to negative indices.
Using small integers (i.e. 16 or 8 bit) is heavily discouraged: only part of key space can be used in such case.

### Are there any requirements on the value type? ###

The main goal of ArrayWithHash design was making sure that it works as fast as possible in case if value type is primitive.
However, most of object types can also be easily used with it.
For example, the following types are OK to used as values: std::unique_ptr, std::shared_ptr, std::string.

As a general requirement, any value type must be movable (or copyable in C++03 mode).
The method "Get" is a bit special: it requires values to be copyable even in C++11 mode.
All the other methods avoid making copies, they move values instead (in C++11 mode).

Also keep in mind that it must be possible to define an "empty" value.
For performance reasons, this empty value should have no dynamic memory associated with it.
Usually default-constructed value (e.g. empty std::string) is a good choice for "empty" value, and it is used by default.

Keep in mind that by default values are relocated with memcpy.
If your value type is not trivially relocatable, you can disable memcpy optimization.
More details are provided in a separate answer.

### How does it work? Give an algorithmic overview. ###

The container keeps its elements in two parts: array part and hash table part.
Hash table is stored in a linear array (in fact, two separate arrays for keys and values).
Both array and hash table always have power-of-two sizes, or can be missing.
Note that both parts never shrink: their sizes can only increase over time.

Hash table is based on the most simple and compact algorithm.
By default, very simple hash function is used: Knuth's multiplicative hash (you can change it).
Linear probing is used to resolve collisions.
When an element is removed from the hash table, its cell is marked as "removed":
it cannot be used for any future operations until a reallocation happens.
During any reallocation, all the "removed" entries in the hash table are dropped.

Any operation which fits into the current array part is performed immediately in a very fast way.
If operation uses a key outside of the array part, then hash table operation is done instead.
If an element may be added during operation, then a check for maximum fill ratio is performed before doing it.
If at this moment fill ratio of hash table achieves maximal allowed value (75%), then a reallocation is performed.

Before reallocation it is necessary to determine new sizes of array and hash table parts.
Any power-of-two integer is considered a viable choice for array size,
if the new array would have at least 40% of elements present in it.
Among all viable choices the maximum one is chosen.
After that maximum power-of-two size of hash table is chosen so that its fill ratio would be at least 30%.
Note that only the elements in the hash table part are analyzed in order to determine the new sizes.
Sizes can never decrease as a result of reallocation.

### How to iterate over elements of container? What is equivalent of STL's iterator here? ###

In order to iterate over all the elements in the container, use ForEach method.
It accepts a callback (usually a lambda) which would be called once for each element.
You can also terminate iteration prematurely if you want.
Look comments near this method for details.

Pointers to values are used instead of iterators in ArrayWithHash.
They are returned from methods like GetPtr and SetIfNew.
They can be used to remove elements or change their values.
Quite obviously, any pointer-to-value may be invalidated on any reallocation.
Reallocation can happen only inside methods: Set, SetIfNew, Reserve.
Read explanation of the algorithm for more detailed information.

### What can be said about library performance? ###

Performance was the main consideration when this library was implemented.
ArrayWithHash is very well-optimized for operations within the array part, especially for primitive value types.
For best results, ensure that NDEBUG macro is defined: library uses asserts from C library.
You can also define macro AWH_CONTROL_INLINING to make sure that inlining behavior is as intended.

The following can be said regarding branches in the code.
When working entirely within array part, there are no unpredictable branches (except for SetIfNew method).
There is a single branch to check if the operation falls into the array part in each method.
It becomes unpredictable when you mix operations across two parts of the container.

Several benchmarks were run to compare performance of ArrayWithHash to performance of std::unordered_map.
They are simple and very artificial in nature, testing only int32+int32 key-value pairs.
Although it is hard to claim any precise numbers, it is reasonable to expect:
 20-50 times speedup when working solely in the array part
 4-6 times speedup when working solely in the hash table part
You can run these tests on your machine by running "TestsMain.exe -sc" after you build testing application.

### What is "relocate with memcpy", "trivially relocatable"? ###

This is a popular optimization of relocation in C++ which is not yet supported by the language standard.
Hopefully, it would be included in future, as proposed by this draft:
	http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0023r0.pdf

Relocation of an object is an operation equivalent to calling move constructor and then calling destructor on source:
	void relocate(Type &destination, Type &source) {
		new(&destination) Type (std::move(source));
		source.~Type();
	}
If memcpy-ing an object is a correct way to relocate it, then the object is called trivially relocatable.
It turns out that almost all movable objects are trivially relocatable (e.g. std::vector, std::shared_ptr).
Notable exceptions include objects which contain pointers into themselves, e.g. strings with short string optimization.

In a dynamic array of trivially relocatable objects, it is also correct to call realloc to increase array size.
This is a great optimization for std::vector-like containers, and it is enabled by default in ArrayWithHash.
Note that this may result in crashes if you use ArrayWithHash for a value type which is not trivially relocatable.

### I have weird crashes with ArrayWithHash in my case.
I suspect "relocate with memcpy" is a problem. Can I disable it? ###

If your class is not trivially relocatable, then it is absolutely necessary to disable this optimization for it.
In particular, the optimization must be disabled for std::string value type, because its implementation in libstd uses SSO.

There are three ways to control "relocate with memcpy" behavior:
you can disable it globally, you can disable it for specified types, and you can disable it in chosen containers.
You can find more information about all these ways in ArrayWithHash_Traits.h.

In order to disable optimization for a single type, simply write in global namespace (before any usages):
	AWH_SET_RELOCATE_WITH_MEMCPY(std::string, false)	//disable optimization for std::string

### How can I change hash function used in the hash table part? ###

You can define your own hash function in the KeyTraits type.
The easiest way to achieve it is to create new traits type derived from default:

	struct GoodHashTraits : DefaultKeyTraits<int> {
		static unsigned int HashFunction(int key) {
			return unsigned(key) * 13;	//write whatever you like here
		}
	};
	typedef ArrayWithHash<int, int, GoodHashTraits> GoodIntHashMap;

Note that return value of your hash function is taken modulo hash table size to find main cell (bucket) for an element.
Hash table size if always some power of two.

### I have used ArrayWithHash in my project and now I want to remove it completely. ###

Look at the StdMapWrapper class then.
Its external interface is almost equivalent to that of ArrayWithHash, but it is based on std::map or std::unordered_map internally.
It can help during the switch time.

### Why is the container growing so fast/slow? ###

ArrayWithHash should grow very fast when adding elements sequentally from zero, just like std::vector.
Unlike most std::vector implementations, ArrayWithHash uses x2 growth factor instead of x1.5
As a bonus, ArrayWithHash uses realloc to increase array part size in most cases (unless "relocate with memcpy" is disabled).
When you have large hash table part, reallocations stop being so fast,
because the hash table is fully scanned during each reallocation.

### I have some problems with log2size function in ArrayWithHash_Utils.h
What the hell is there? What is CLZ and BSR? ###

log2size function finds minimal integer, such that its power-of-two exceeds given input value.
This function is called for every element in the hash table part during each reallocation.
Unless you are sure that hash table part has negligible size in you case, it is important for log2size to work fast.
This is best achieved by using proper instructions supported by hardware.
Bit Scan Reverse (BSR) is such instruction on x86 architecture.
GCC has no direct intrinsic for this instruction, but __builtin_clz provides very similar functionality.

Note that there is also default slow implementation of log2size.
If you have compilation problems, you can perhaps stick to this slow version.
If you have performance problems (inside AdaptSizes private method), please contact author of the library.

### What are AWH_INLINE and AWH_NOINLINE for? ###

Even modern smart compilers are not always perfect at decisions regarding inlining functions.
Luckily, there are ways to force or forbid inlining of a function in most compilers.
Each function within the library is marked with either AWH_INLINE or AWH_NOINLINE, showing whether it is intended to be inlined.
If you define AWH_CONTROL_INLINING macro, then these intentions are forced onto compiler (if supported).
Otherwise inlining behavior is completely up to your compiler.
Although AWH_CONTROL_INLINING does not necessarily result in faster execution,
it should protect you from code bloat due to excessive inlining.

### How to run tests of your library? ###

Building test console application should be simple: just compile all the .cpp and .c files and link them together.
On windows you can use batch scripts to do it (make sure your compiler is in PATH).
Then run the resulting executable TestsMain with appropriate parameters.

Here is the list of possible modes:
 TestsMain -t           //run random tests infinitely to check correctness (stops on error)
 TestsMain -s			//run performance measurements of ArrayWithHash
 TestsMain -sc			//run performance measurements and compare to STL equivalent