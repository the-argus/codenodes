#ifndef __CLANG_WRAPPER_H__
#define __CLANG_WRAPPER_H__

#include <clang-c/Index.h>
#include <string_view>
#include <utility>

#include "aliases.h"

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

    constexpr String copy_to_string(Allocator& allocator)
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
