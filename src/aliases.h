#ifndef __ALIASES_H__
#define __ALIASES_H__

#include <memory>
#include <memory_resource>
#include <string>
#include <unordered_map>

template <typename T> using Vector = std::pmr::vector<T>;
using String = std::pmr::string;
template <typename K, typename V> using Map = std::pmr::unordered_map<K, V>;
using Allocator = std::pmr::polymorphic_allocator<>;
using MemoryResource = std::pmr::memory_resource;
// resource user per parse job
using JobResource = std::pmr::unsynchronized_pool_resource;

template <typename T> struct PMRDeleter
{
    Allocator* allocator;
    constexpr void operator()(T* object) const
    {
        allocator->delete_object(object);
    }
};

template <typename T> using OwningPointer = std::unique_ptr<T, PMRDeleter<T>>;

template <typename T, typename... ConstructorArgs>
    requires std::is_constructible_v<T, ConstructorArgs...>
OwningPointer<T> make_owning(Allocator& allocator, ConstructorArgs&&... args)
{
    return OwningPointer<T>(
        allocator.new_object<T>(std::forward<ConstructorArgs>(args)...),
        PMRDeleter<T>{std::addressof(allocator)});
}

#endif
