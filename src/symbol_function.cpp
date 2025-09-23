#include "clang_to_graphml_impl.h"
#include <algorithm>
#include <ranges>

namespace cn {
size_t FunctionSymbol::get_num_symbols_this_references() const
{
    return std::ranges::fold_left(
               this->parameter_types, 0UL,
               [](size_t left, const TypeIdentifier& right) -> size_t {
                   return left + size_t(right.try_get_symbol() != nullptr);
               }) +
           size_t(this->return_type.try_get_symbol() != nullptr);
}

Symbol* FunctionSymbol::get_symbol_this_references(size_t index) const
{
    constexpr auto has_symbol = [](const TypeIdentifier& iden) -> bool {
        return iden.try_get_symbol();
    };

    if (index == 0 && this->return_type.try_get_symbol() != nullptr) {
        return this->return_type.try_get_symbol();
    }

    auto view = this->parameter_types | std::ranges::views::filter(has_symbol) |
                std::ranges::views::drop(index);
    assert(view.begin() != view.end());
    return view.begin()->try_get_symbol(); // nonnull yay
}

bool FunctionSymbol::visit_children_impl(ClangToGraphMLBuilder::Job& job,
                                         const CXCursor& cursor)
{
    CXType type = clang_getCursorType(cursor);
    const int num_args = clang_getNumArgTypes(type);

    if (num_args < 0) {
        std::ignore = fprintf(
            stderr,
            "Non-function type { kind: %d } of cursor %s passed to "
            "FunctionSymbol::create_and_visit_children\n",
            type.kind, OwningCXString::clang_getCursorSpelling(cursor).c_str());
        return false;
    }

    CXType return_type = clang_getResultType(type);

    this->return_type =
        clang_type_to_type_identifier(*job.shared_data, return_type);

    if (clang_isFunctionTypeVariadic(type) == 1) {
        std::ignore =
            fprintf(stderr,
                    "WARNING: Encountered variadic function %s, pretending it "
                    "has no arguments\n",
                    this->usr.c_str());
        return true;
    }

    this->parameter_types.reserve(num_args);

    for (unsigned i = 0; i < num_args; ++i) {
        CXType arg_type = clang_getArgType(type, i);
        this->parameter_types.push_back(
            clang_type_to_type_identifier(*job.shared_data, arg_type));
    }

    return true;
}

} // namespace cn
