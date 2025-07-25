#include <clang-c/Index.h>
#include <cstdio>

#include "translation_unit.h"

namespace cn {
std::optional<TranslationUnit>
TranslationUnit::parse(const char* filename,
                       std::span<const std::string_view> command_args) noexcept
{
    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit = clang_parseTranslationUnit(
        index, filename, nullptr, 0, nullptr, 0, CXTranslationUnit_None);

    if (unit == nullptr) {
        auto&& _unused =
            fprintf(stderr, "Unable to parse translation unit. Quitting.\n");
        return {};
    }

    CXCursor cursor = clang_getTranslationUnitCursor(unit);

    clang_visitChildren(
        cursor, // Root cursor
        [](CXCursor current_cursor, CXCursor parent, CXClientData client_data) {
            CXString current_display_name =
                clang_getCursorDisplayName(current_cursor);
            // Allocate a CXString representing the name of the current
            // cursor
            printf("Visiting element %s\n",
                   clang_getCString(current_display_name));
            // Print the char* value of current_display_name
            clang_disposeString(current_display_name);
            // Since clang_getCursorDisplayName allocates a new CXString, it
            // must be freed. This applies to all functions returning a CXString
            return CXChildVisit_Recurse;
        },      // CXCursorVisitor: a function pointer
        nullptr // client_data
    );

    return {};
}
} // namespace cn
