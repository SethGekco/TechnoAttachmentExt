#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// A set of pointers for a hot "is it in there?" on a set that stays small:
// open addressing, linear probing, at most half full, backward-shift erase (no
// tombstones, so a lookup never walks further than the run its key hashes
// into). An empty set answers from its count alone, without touching memory.
// Not thread safe.
class PointerSet
{
public:
	bool Contains(void const* ptr) const
	{
		if (!this->Count)
			return false;

		auto const key = reinterpret_cast<uintptr_t>(ptr);
		for (size_t i = this->Home(key); ; i = (i + 1) & this->Mask)
		{
			auto const slot = this->Slots[i];
			if (!slot)
				return false;
			if (slot == key)
				return true;
		}
	}

	void Insert(void const* ptr)
	{
		auto const key = reinterpret_cast<uintptr_t>(ptr);
		if (!key)
			return;

		if ((this->Count + 1) * 2 > this->Slots.size())
			this->Grow();

		size_t i = this->Home(key);
		for (; this->Slots[i]; i = (i + 1) & this->Mask)
		{
			if (this->Slots[i] == key)
				return;
		}

		this->Slots[i] = key;
		++this->Count;
	}

	void Erase(void const* ptr)
	{
		auto const key = reinterpret_cast<uintptr_t>(ptr);
		if (!this->Count || !key)
			return;

		size_t hole = this->Home(key);
		for (; this->Slots[hole] != key; hole = (hole + 1) & this->Mask)
		{
			if (!this->Slots[hole])
				return;
		}

		// Close the hole: each later entry of the run whose home is not in
		// (hole, its slot] moves back into it, so every entry stays reachable
		// from its home without crossing an empty slot.
		for (size_t j = (hole + 1) & this->Mask; this->Slots[j]; j = (j + 1) & this->Mask)
		{
			size_t const home = this->Home(this->Slots[j]);
			if (((j - home) & this->Mask) >= ((j - hole) & this->Mask))
			{
				this->Slots[hole] = this->Slots[j];
				hole = j;
			}
		}

		this->Slots[hole] = 0;
		--this->Count;
	}

	size_t Size() const
	{
		return this->Count;
	}

private:
	// Fibonacci hashing of the address without its alignment bits: the top
	// Bits bits of a 32-bit product.
	size_t Home(uintptr_t key) const
	{
		return static_cast<size_t>(static_cast<uint32_t>(static_cast<uint32_t>(key >> 2) * 2654435769u) >> this->Shift);
	}

	void Grow()
	{
		std::vector<uintptr_t> old;
		old.swap(this->Slots);

		unsigned const bits = old.empty() ? 4 : this->Bits + 1;
		this->Bits = bits;
		this->Shift = 32 - bits;
		this->Mask = (size_t(1) << bits) - 1;
		this->Slots.assign(size_t(1) << bits, 0);
		this->Count = 0;

		for (auto const key : old)
		{
			if (!key)
				continue;

			size_t i = this->Home(key);
			while (this->Slots[i])
				i = (i + 1) & this->Mask;

			this->Slots[i] = key;
			++this->Count;
		}
	}

	std::vector<uintptr_t> Slots;
	size_t Count = 0;
	size_t Mask = 0;
	unsigned Bits = 0;
	unsigned Shift = 32;
};
