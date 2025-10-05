#ifndef __CLANG_WRAPPER_H__
#define __CLANG_WRAPPER_H__

#include <clang-c/Index.h>
#include <cstring>
#include <string_view>
#include <utility>

#include "aliases.h"

/// Get the type of a type, resolving any namespaces, usings, and typedefs
constexpr CXType get_cannonical_type(CXType type)
{
    while (type.kind == CXType_Elaborated || type.kind == CXType_Typedef) {
        if (type.kind == CXType_Elaborated) {
            type = clang_Type_getNamedType(type);
        } else if (type.kind == CXType_Typedef) {
            type = clang_getCanonicalType(type);
        }
    }
    return type;
}

/// Get the type of a cursor, resolving any namespaces, usings, and typedefs
constexpr CXType get_cannonical_type(CXCursor cursor)
{
    return get_cannonical_type(clang_getCursorType(cursor));
}

struct OwningCXString : private CXString
{
    constexpr static OwningCXString clang_getCursorUSR(const CXCursor& cursor)
    {
        return OwningCXString(::clang_getCursorUSR(cursor));
    }

    constexpr static OwningCXString
    clang_getCursorSpelling(const CXCursor& cursor)
    {
        return OwningCXString(::clang_getCursorSpelling(cursor));
    }

    constexpr static OwningCXString
    clang_getCursorKindSpelling(const CXCursorKind& kind)
    {
        return OwningCXString(::clang_getCursorKindSpelling(kind));
    }

    constexpr static OwningCXString clang_getTypeSpelling(const CXType& type)
    {
        return OwningCXString(::clang_getTypeSpelling(type));
    }

    constexpr static OwningCXString
    clang_getTypeKindSpelling(const CXTypeKind& kind)
    {
        return OwningCXString(::clang_getTypeKindSpelling(kind));
    }

    constexpr static OwningCXString clang_getFileName(const CXFile& cursor)
    {
        return OwningCXString(::clang_getFileName(cursor));
    }

    constexpr static OwningCXString
    clang_getCursorDisplayName(const CXCursor& cursor)
    {
        return OwningCXString(::clang_getCursorDisplayName(cursor));
    }

    constexpr static OwningCXString
    clang_formatDiagnostic(CXDiagnostic diagnostic,
                           CXDiagnosticDisplayOptions display_options)
    {
        return OwningCXString(
            ::clang_formatDiagnostic(diagnostic, display_options));
    }

    constexpr String
    copy_to_string(std::pmr::polymorphic_allocator<>& allocator)
    {
        return {c_str(), allocator};
    }

    OwningCXString() = delete;
    constexpr explicit OwningCXString(CXString nonowning) : CXString(nonowning)
    {
    }

    constexpr OwningCXString(OwningCXString&& other) noexcept : CXString(other)
    {
        other.data = nullptr;
        other.private_flags = 0;
    }

    constexpr OwningCXString& operator=(OwningCXString&& other) noexcept
    {
        data = std::exchange(other.data, nullptr);
        private_flags = std::exchange(other.private_flags, 0);
        return *this;
    }

    OwningCXString(const OwningCXString& other) = delete;
    OwningCXString& operator=(const OwningCXString& other) = delete;

    const char* c_str() noexcept { return clang_getCString(*this); }
    constexpr std::string_view view() noexcept
    {
        return {clang_getCString(*this)};
    }

    constexpr ~OwningCXString() { clang_disposeString(*this); }
};

#endif
