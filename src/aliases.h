#ifndef __ALIASES_H__
#define __ALIASES_H__

#include <cassert>
#include <cmath>
#include <cstring>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <memory_resource>
#include <span>
#include <string>
#include <utility>
#include <vector>

// NOTE: this type does not guarantee that its elements can have stable pointers
// in between calls to emplace_back. this program doesn't really use that
// feature though as there's already indirection for polymorphism with the
// Symbol class, and anything thats by-value is just copied rather than having
// its address taken
template <typename T> class OrderedCollectionImpl
{
  private:
    std::pmr::deque<T> elements;
    mutable decltype(elements)::const_iterator last_visited_const;
    mutable size_t last_visited_const_index = 0;
    mutable bool is_cache_valid = false;
    bool is_last_emplaced = false;
    size_t m_size = 0;
    decltype(elements)::const_iterator last_emplaced;

  public:
    OrderedCollectionImpl() = delete;

    OrderedCollectionImpl(const OrderedCollectionImpl& other) = delete;
    OrderedCollectionImpl&
    operator=(const OrderedCollectionImpl& other) = delete;

    OrderedCollectionImpl(OrderedCollectionImpl&& other) = default;
    OrderedCollectionImpl& operator=(OrderedCollectionImpl&& other) = delete;

    ~OrderedCollectionImpl() = default;

    constexpr explicit OrderedCollectionImpl(
        std::pmr::polymorphic_allocator<> allocator)
        : elements(allocator)
    {
    }

    [[nodiscard]] constexpr const T& at(size_t index) const
    {
        if constexpr (std::is_same_v<decltype(elements), std::pmr::list<T>> ||
                      std::is_same_v<decltype(elements),
                                     std::pmr::forward_list<T>>) {
            // list is unordered, but we only really ever access it in linear
            // order
            if (index >= this->size()) {
                std::abort();
            } else if (is_cache_valid && index == 0) {
                last_visited_const = elements.cbegin();
                last_visited_const_index = index;
            } else if (is_cache_valid &&
                       index == last_visited_const_index + 1) {
                ++last_visited_const;
                last_visited_const_index = index;
            } else if (is_cache_valid && index == last_visited_const_index) {
                // our cache is already good, do nothing
            } else {
                // some kind of random access or cache not initialized, we have
                // to linear search
                size_t search = 0;
                for (auto iter = elements.cbegin(); iter != elements.cend();
                     ++iter) {
                    if (search == index) {
                        last_visited_const_index = 0;
                        last_visited_const = iter;
                        is_cache_valid = true;
                        return *last_visited_const;
                    }
                    search += 1;
                }

                // if we get here, we did linear seach but it was out of bounds?
                // unreachable
                std::abort();
            }
            return *last_visited_const;

        } else {
            // vector and deq are ordered
            return elements.at(index);
        }
    }

    [[nodiscard]] constexpr size_t size() const { return m_size; }

    constexpr void reserve(size_t num_elements)
    {
        if constexpr (std::is_same_v<decltype(elements), std::pmr::vector<T>>) {
            elements.reserve(num_elements);
        } else {
            // none of these have reserve() operations so its okay if we skip it
            // for them
            static_assert(
                std::is_same_v<decltype(elements), std::pmr::deque<T>> ||
                std::is_same_v<decltype(elements), std::pmr::list<T>> ||
                std::is_same_v<decltype(elements), std::pmr::forward_list<T>>);
        }
    }

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr void emplace_back(Args&&... args)
    {
        if constexpr (std::is_same_v<decltype(elements),
                                     std::pmr::forward_list<T>>) {
            if (is_last_emplaced) {
                elements.emplace_after(last_emplaced,
                                       std::forward<Args>(args)...);
                ++last_emplaced;
            } else {
                elements.emplace_front(std::forward<Args>(args)...);
                assert(m_size == 0);
                last_emplaced = elements.cbegin();
                is_last_emplaced = true;
            }
        } else {
            elements.emplace_back(std::forward<Args>(args)...);
        }
        ++m_size;
    }
};

template <typename T> class OrderedCollectionCustom
{
  private:
    static constexpr size_t block_size = 10;
    static constexpr size_t num_initial_blocks = 8;

    struct M
    {
        std::pmr::polymorphic_allocator<> allocator;
        std::span<T*> block_dynamic_array;
        size_t block_dynamic_array_num_occupied = 0;
        size_t num_occupied = 0;
    } m;

    [[nodiscard]] constexpr size_t block_dynamic_array_size()
    {
        return m.block_dynamic_array_num_occupied;
    }

    [[nodiscard]] constexpr size_t block_dynamic_array_capacity()
    {
        return m.block_dynamic_array.size();
    }

    [[nodiscard]] constexpr std::span<T> block_dynamic_array_get(size_t index)
    {
        assert(index >= block_dynamic_array_size());
        return {m.block_dynamic_array[index], block_size};
    }

    [[nodiscard]] constexpr size_t capacity()
    {
        return block_dynamic_array_size() * block_size;
    }

  public:
    OrderedCollectionCustom() = delete;
    constexpr explicit OrderedCollectionCustom(
        std::pmr::polymorphic_allocator<> allocator)
        : m{allocator}
    {
    }

    OrderedCollectionCustom(const OrderedCollectionCustom& other) = delete;
    OrderedCollectionCustom&
    operator=(const OrderedCollectionCustom& other) = delete;

    constexpr OrderedCollectionCustom(OrderedCollectionCustom&& other) noexcept
        : m{std::move(other.m.allocator)}
    {
        m.num_occupied = std::exchange(other.m.num_occupied, 0);
        m.block_dynamic_array = std::exchange(other.m.block_dynamic_array, {});
        m.block_dynamic_array_num_occupied =
            std::exchange(other.m.block_dynamic_array_num_occupied, 0);
    }

    constexpr OrderedCollectionCustom&
    operator=(OrderedCollectionCustom&& other) noexcept = delete;

    constexpr ~OrderedCollectionCustom()
    {
        if (m.block_dynamic_array_num_occupied != 0) {
            for (size_t block_index = 0;
                 block_index < block_dynamic_array_size(); ++block_index) {
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    for (size_t sub_index = 0; sub_index < block_size;
                         ++sub_index) {
                        m.block_dynamic_array[block_index][sub_index].~T();
                    }
                }
                m.allocator.deallocate_bytes(m.block_dynamic_array[block_index],
                                             block_size);
            }
            m.allocator.deallocate_bytes(m.block_dynamic_array.data(),
                                         m.block_dynamic_array.size_bytes());
        }
    }

    // TODO: implement these
    template <typename... Args>
        requires std::is_nothrow_constructible_v<T, Args...>
    constexpr void emplace_back(Args&&... args)
    {
        T* target_slot = nullptr;
        assert(capacity() >= size());
        if (capacity() == size()) {
            if (block_dynamic_array_capacity() == block_dynamic_array_size()) {
                reallocate_blocks_leave_uninit();
            }
            assert(block_dynamic_array_capacity() > block_dynamic_array_size());

            T*& next = m.block_dynamic_array[block_dynamic_array_size()];

            next = static_cast<T*>(
                m.allocator.allocate_bytes(sizeof(T) * block_size, alignof(T)));
            ++m.block_dynamic_array_num_occupied;

            if (next == nullptr) {
                std::abort();
            }

            target_slot = next;
        } else {
            // in this case, we have space already
            std::span last =
                block_dynamic_array_get(block_dynamic_array_size() - 1);

            target_slot = last.data() + (size() % block_size);
        }

        assert(capacity() > size());
        std::construct_at(target_slot, std::forward<Args>(args)...);
        ++m.num_occupied;
    }

    constexpr void reserve(size_t /**/) {}

    [[nodiscard]] constexpr const T& at(size_t index) const
    {
        if (index >= size()) {
            std::abort();
        }
        assert(index / block_size < block_dynamic_array_size());
        return m.block_dynamic_array[index / block_size][index % block_size];
    }

    [[nodiscard]] constexpr size_t size() const { return m.num_occupied; }

  private:
    // reallocates a buffer and leaves new items uninitialized
    void reallocate_blocks_leave_uninit()
    {
        const size_t new_buffer_size_items_multiplier =
            std::ceilf(float(m.block_dynamic_array.size()) * 1.5F);
        const size_t new_buffer_size_items =
            new_buffer_size_items_multiplier == 0
                ? num_initial_blocks
                : new_buffer_size_items_multiplier;
        const size_t new_buffer_size_bytes = new_buffer_size_items * sizeof(T*);

        void* new_buffer_mem =
            m.allocator.allocate_bytes(new_buffer_size_bytes, alignof(T*));

        if (new_buffer_mem == nullptr) {
            std::abort();
        }

        std::memcpy(new_buffer_mem, m.block_dynamic_array.data(),
                    m.block_dynamic_array.size_bytes());

        m.allocator.deallocate_bytes(m.block_dynamic_array.data(),
                                     m.block_dynamic_array.size_bytes());

        const size_t old_size_items = m.block_dynamic_array.size();
        m.block_dynamic_array = std::span<T*>{
            static_cast<T**>(new_buffer_mem),
            new_buffer_size_items,
        };
    }
};

// template <typename T> using OrderedCollection = OrderedCollectionImpl<T>;
template <typename T> using OrderedCollection = OrderedCollectionCustom<T>;

using String = std::pmr::string;

template <typename K, typename V> using Map = std::pmr::map<K, V>;

#endif
