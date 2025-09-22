#include "clang_to_graphml_impl.h"

namespace cn {
EnumTypeSymbol*
EnumTypeSymbol::create_and_visit_children(Symbol* semantic_parent,
                                          ClangToGraphMLBuilder::Job& job,
                                          const CXCursor& cursor)
{
    return &job.create_or_find_symbol_with_cursor<EnumTypeSymbol>(
        semantic_parent, cursor);
}
} // namespace cn
