#include "clang_to_graphml_impl.h"

namespace cn {
struct Args
{
    ClangToGraphMLBuilder::Job& job;
    const CXCursor& cursor;
    Symbol* semantic_parent;
    Vector<Symbol*>& symbols; // output of visitor
};

namespace {
enum CXChildVisitResult visitor(CXCursor input_cursor, CXCursor /* parent */,
                                void* userdata)
{
    Args* args = reinterpret_cast<Args*>(userdata);
    CXCursor cursor = clang_getCanonicalCursor(input_cursor);
    const enum CXCursorKind kind = clang_getCursorKind(cursor);

    switch (kind) {
    case CXCursorKind::CXCursor_FunctionDecl: {
        auto& function =
            args->job.create_or_find_symbol_with_cursor<FunctionSymbol>(
                args->semantic_parent, cursor);
        args->symbols.push_back(&function);
        break;
    }
    case CXCursor_UnionDecl:
    case CXCursor_ClassDecl:
    case CXCursor_StructDecl: {
        auto& class_symbol =
            args->job.create_or_find_symbol_with_cursor<ClassSymbol>(
                args->semantic_parent, cursor);
        args->symbols.push_back(&class_symbol);
        break;
    }
    case CXCursor_EnumDecl: {
        auto& enum_symbol =
            args->job.create_or_find_symbol_with_cursor<EnumTypeSymbol>(
                args->semantic_parent, cursor);
        args->symbols.push_back(&enum_symbol);
        break;
    }
    case CXCursor_Namespace: {
        auto& namespace_symbol =
            args->job.create_or_find_symbol_with_cursor<NamespaceSymbol>(
                args->semantic_parent, cursor);
        args->symbols.push_back(&namespace_symbol);
        break;
    }
    default:
        std::ignore = fprintf(
            stderr, "unexpected cursor kind %d in namespace\n", int(kind));
        break;
    }

    return CXChildVisit_Continue;
}
} // namespace

NamespaceSymbol*
NamespaceSymbol::create_and_visit_children(Symbol* semantic_parent,
                                           ClangToGraphMLBuilder::Job& job,
                                           const CXCursor& input_cursor)
{
    auto* out = job.allocator.new_object<NamespaceSymbol>(
        semantic_parent, OwningCXString::clang_getCursorSpelling(input_cursor)
                             .copy_to_string(job.allocator));

    Args args{
        .job = job,
        .cursor = input_cursor,
        .semantic_parent = out,
        .symbols = out->symbols,
    };

    clang_visitChildren(input_cursor, visitor, &args);

    return out;
}
} // namespace cn
