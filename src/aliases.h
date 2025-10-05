#ifndef __ALIASES_H__
#define __ALIASES_H__

#include <cassert>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <memory_resource>
#include <string>
#include <vector>

// NOTE: this type does not guarantee that its elements can have stable pointers
// in between calls to emplace_back. this program doesn't really use that
// feature though as there's already indirection for polymorphism with the
// Symbol class, and anything thats by-value is just copied rather than having
// its address taken
template <typename T> class OrderedCollection
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
    OrderedCollection() = delete;

    OrderedCollection(const OrderedCollection& other) = delete;
    OrderedCollection& operator=(const OrderedCollection& other) = delete;

    OrderedCollection(OrderedCollection&& other) = default;
    OrderedCollection& operator=(OrderedCollection&& other) = default;

    ~OrderedCollection() = default;

    constexpr explicit OrderedCollection(
        std::pmr::polymorphic_allocator<> allocator)
        : elements(allocator)
    {
    }

    // using iterator = decltype(elements)::iterator;
    using const_iterator = decltype(elements)::const_iterator;

    const_iterator cbegin() const { return elements.cbegin(); }
    const_iterator cend() const { return elements.cend(); }
    // iterator begin() const { return elements.begin(); }
    // iterator end() const { return elements.end(); }

    // [[nodiscard]] constexpr T& at(size_t index) &
    // {
    //     return elements.at(index);
    // }

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
    // [[nodiscard]] constexpr T&& at(size_t index) &&
    // {
    //     return elements.at(index);
    // }

    [[nodiscard]] constexpr T& back() & { return elements.back(); }
    [[nodiscard]] constexpr const T& back() const { return elements.back(); }
    [[nodiscard]] constexpr T&& back() && { return elements.back(); }

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

using String = std::pmr::string;

template <typename K, typename V> using Map = std::pmr::map<K, V>;

#endif
