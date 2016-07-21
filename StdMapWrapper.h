//          Copyright Stepan Gatilov 2016.
// Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <algorithm>

#ifndef AWH_NO_CPP11
#include <unordered_map>
#else
#include <map>
#endif

//Note: placed into same namespace as the main container
namespace Awh {

//Wrapper around std::unordered_map (or std::map) with exactly the same interface as of ArrayWithHash.
//Used for comparison between ArrayWithHash and STL container (results and performance).
//Can be used to easily disable/remove ArrayWithHash from project.
template<
	class TKey, class TValue,
#ifndef AWH_NO_CPP11
	class TKeyTraits = DefaultKeyTraits<TKey>, class TValueTraits = DefaultValueTraits<TValue>
#else
	class TKeyTraits, class TValueTraits
#endif
>
class StdMapWrapper {
public:
	typedef TKey Key;
	typedef TValue Value;
	typedef TKeyTraits KeyTraits;
	typedef TValueTraits ValueTraits;

private:
	//use unordered map if available, tree-based map otherwise
#ifndef AWH_NO_CPP11
	typedef std::unordered_map<Key, Value> Map;
#else
	typedef std::map<Key, Value> Map;
#endif
	typedef typename Map::iterator Iter;
	typedef typename KeyTraits::Size Size;

	//internal implementation of container
	Map dict;

public:
	//small wrapper to be used instead of "Value*"
	struct Ptr {
		//iterator of underlying map
		Iter it;
		//null flag: Ptr is nullable
		bool null;

		inline Ptr() : null(true) {}
		inline Ptr(Iter it) : it(it), null(false) {}
		inline operator bool() const { return !null; }
		inline bool operator!() const { return null; }
		inline Value &operator* () const { return it->second; }
		inline Value &operator-> () const { return it->second; }
	};

	inline void Swap(StdMapWrapper &other) {
		std::swap(dict, other.dict);
	}
	inline void Clear() {
		dict.clear();
	}
	inline Size GetSize() const {
		return (Size)dict.size();
	}
	inline Value Get(Key key) const {
		Iter it = const_cast<Map&>(dict).find(key);
		return it == dict.end() ? ValueTraits::GetEmpty() : it->second;
	}
	inline Ptr GetPtr(Key key) const {
		Iter it = const_cast<Map&>(dict).find(key);
		return it == dict.end() ? Ptr() : Ptr(it);
	}
	inline Ptr Set(Key key, Value value) {
		dict[key] = AWH_MOVE(value);
		return Ptr(dict.find(key));
	}
	inline Ptr SetIfNew(Key key, Value value) {
		std::pair<Iter, bool> pib = dict.insert(std::make_pair(key, AWH_MOVE(value)));
		return pib.second ? Ptr() : Ptr(pib.first);
	}
	inline void Remove(Key key) {
		dict.erase(key);
	}
	inline void RemovePtr(Ptr ptr) {
		dict.erase(ptr.it);
	}
	inline Key KeyOf(Ptr ptr) const {
		return ptr.it->first;
	}
	void Reserve(Size arraySizeLB, Size hashSizeLB, bool alwaysCleanHash = false) {
#ifndef AWH_NO_CPP11
		dict.rehash(size_t(arraySizeLB + hashSizeLB));
#endif
	}


	template<class Action> void ForEach(Action &action) const {
		for (Iter it = const_cast<Map&>(dict).begin(); it != const_cast<Map&>(dict).end(); it++)
			if (action(Key(it->first), it->second))
				return;
	}

#if defined(AWH_TESTING) && !defined(AWH_NO_CPP11)
	//note: used only for testing purposes
	template<class Rnd> Key SomeKey(Rnd &rnd) const {
		size_t idx = std::uniform_int_distribution<size_t>(0, dict.size() - 1)(rnd);
		typename Map::const_iterator iter = dict.begin();
		std::advance(iter, idx);
		return iter->first;
	}
#endif
};

//end namespace
}
