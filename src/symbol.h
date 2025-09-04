#ifndef __SYMBOL_H__
#define __SYMBOL_H__

#include <clang-c/Index.h>

#include "aliases.h"
#include "clang_to_graphml.h"
#include "type_identifier.h"

namespace cn {

enum class SymbolKind : uint8_t
{
    Namespace,
    Function,
    Enum,
    Aggregate, // union, class, struct
};

struct Symbol
{
    Symbol() = delete;
    constexpr Symbol(Symbol* _semantic_parent, SymbolKind _kind, String&& _name)
        : semantic_parent(_semantic_parent), symbol_kind(_kind),
          name(std::move(_name))
    {
    }

    Symbol(const Symbol&) = delete;
    Symbol& operator=(const Symbol&) = delete;
    Symbol(Symbol&&) = default;
    Symbol& operator=(Symbol&&) = default;
    virtual ~Symbol() = default;

    [[nodiscard]] virtual size_t get_num_symbols_this_references() const = 0;
    [[nodiscard]] virtual Symbol*
    get_symbol_this_references(size_t index) const = 0;

    SymbolKind symbol_kind;
    String name;
    Symbol* semantic_parent;
    Vector<Symbol*> symbols_that_reference_this;
};

struct FunctionSymbol;
struct EnumTypeSymbol;
struct ClassSymbol;

struct NamespaceSymbol : public Symbol
{
    constexpr static auto kind = SymbolKind::Namespace;

    constexpr NamespaceSymbol(Symbol* _semantic_parent, String&& _name)
        : Symbol(_semantic_parent, SymbolKind::Namespace, std::move(_name))
    {
    }

    [[nodiscard]] size_t get_num_symbols_this_references() const final
    {
        return symbols.size();
    }

    [[nodiscard]] Symbol* get_symbol_this_references(size_t index) const final
    {
        return symbols.at(index);
    }

    // cursor must be of type CXCursor_Namespace
    static ClassSymbol
    create_and_visit_children(ClangToGraphMLBuilder::Job& job,
                              CXCursor& cursor);

    Vector<Symbol*> symbols;
};

struct ClassSymbol : public Symbol
{
    constexpr static auto kind = SymbolKind::Aggregate;

    enum class AggregateKind : uint8_t
    {
        Class,
        Struct,
        Union,
    };

    struct FunctionReference
    {
        FunctionSymbol* symbol;
        bool is_static;
    };

    struct ClassReference
    {
        ClassSymbol* symbol;
    };

    struct EnumReference
    {
        EnumTypeSymbol* symbol;
    };

    // cursor must be of type CXCursor_ClassDecl or CXCursor_UnionDecl or
    // CXCursor_StructDecl
    static ClassSymbol
    create_and_visit_children(ClangToGraphMLBuilder::Job& job,
                              CXCursor& cursor);

    AggregateKind aggregate_kind;
    Vector<ClassSymbol> parent_classes;
    Vector<ClassSymbol> inner_classes;
    Vector<FunctionReference> member_functions;
    Vector<EnumReference> inner_enums;
};

struct EnumTypeSymbol : public Symbol
{
    constexpr static auto kind = SymbolKind::Enum;

    // cursor must be of type CXCursor_EnumDecl
    static EnumTypeSymbol
    create_and_visit_children(ClangToGraphMLBuilder::Job& job,
                              CXCursor& cursor);

    [[nodiscard]] size_t get_num_symbols_this_references() const final
    {
        return 0;
    }

    [[nodiscard]] Symbol*
    get_symbol_this_references(size_t /*index*/) const final
    {
        return nullptr;
    }
};

struct FunctionSymbol : public Symbol
{
    constexpr static auto kind = SymbolKind::Function;

    // cursor must be of type CXCursor_FunctionDecl
    static FunctionSymbol
    create_and_visit_children(ClangToGraphMLBuilder::Job& job,
                              CXCursor& cursor);

    TypeIdentifier return_type;
    // if true, then parameter_types will not include the type of `this`, you
    // get that from .semantic_parent
    bool is_method = false;
    Vector<TypeIdentifier> parameter_types;
};

} // namespace cn

#endif
