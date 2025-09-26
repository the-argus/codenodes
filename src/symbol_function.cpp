#include "clang_to_graphml_impl.h"
#include <algorithm>
#include <ranges>

namespace cn {
size_t FunctionSymbol::get_num_symbols_this_references() const
{
    return std::ranges::fold_left(
               this->parameter_types, 0UL,
               [](size_t left, const TypeIdentifier& right) -> size_t {
                   return left + right.get_num_symbols();
               }) +
           this->return_type.get_num_symbols();
}

Symbol* FunctionSymbol::get_symbol_this_references(size_t index) const
{
    size_t iter = 0;

    for (const TypeIdentifier& iden : this->parameter_types) {
        const size_t num_symbols = iden.get_num_symbols();
        if (iter + num_symbols <= index) {
            iter += num_symbols;
            continue;
        }
        const size_t sub_index = index - iter;
        assert(sub_index < num_symbols);
        auto* out = iden.try_get_symbol(sub_index);
        if (out == nullptr) {
            auto* out2 = iden.try_get_symbol(sub_index);
        }
        assert(out);
        return out;
    }
    const size_t num_symbols = this->return_type.get_num_symbols();
    const size_t sub_index = index - iter;
    assert(sub_index < num_symbols);
    auto* out = this->return_type.try_get_symbol(sub_index);
    assert(out);
    return out;
}

bool FunctionSymbol::visit_children_impl(ClangToGraphMLBuilder::Job& job,
                                         const CXCursor& cursor)
{
    CXType type = get_cannonical_type(cursor);
    const int num_args = clang_getNumArgTypes(type);

    if (num_args < 0) {
        std::ignore = fprintf(
            stderr,
            "Non-function type { kind: %d } of cursor %s passed to "
            "FunctionSymbol::create_and_visit_children\n",
            type.kind, OwningCXString::clang_getCursorSpelling(cursor).c_str());
        return false;
    }

    CXType return_type = get_cannonical_type(clang_getResultType(type));

    this->return_type = clang_type_to_type_identifier(job, return_type);

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
        CXType arg_type = get_cannonical_type(clang_getArgType(type, i));
        this->parameter_types.push_back(
            clang_type_to_type_identifier(job, arg_type));
    }

    return true;
}

} // namespace cn
