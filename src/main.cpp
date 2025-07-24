#include <clang-c/Index.h>
#include <iostream>

int main()
{
    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit = clang_parseTranslationUnit(index, "file.cpp", nullptr, 0, nullptr, 0, CXTranslationUnit_None);

    if (unit == nullptr) {
        std::cerr << "Unable to parse translation unit. Quitting.\n";
        return 0;
    }

    CXCursor cursor = clang_getTranslationUnitCursor(unit);
}
