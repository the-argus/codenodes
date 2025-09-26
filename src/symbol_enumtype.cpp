#include "clang_to_graphml_impl.h"

namespace cn {
bool EnumTypeSymbol::visit_children_impl(ClangToGraphMLBuilder::Job& /*job*/,
                                         const CXCursor& /*cursor*/)
{
    return true; // enums never reference other stuff
}
} // namespace cn
