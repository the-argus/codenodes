#ifndef __SYMBOL_H__
#define __SYMBOL_H__

#include <clang-c/Index.h>

#include "aliases.h"
#include "clang_to_graphml.h"
#include "type_identifier.h"

namespace cn {

struct FunctionSymbol;
struct EnumTypeSymbol;
struct ClassSymbol;
struct NamespaceSymbol;

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
    constexpr Symbol(std::pmr::polymorphic_allocator<> allocator,
                     Symbol* _semantic_parent, SymbolKind _kind, String&& _usr,
                     std::optional<CXCursor> /* cursor */,
                     String&& _display_name)
        : semantic_parent(_semantic_parent), symbol_kind(_kind),
          usr(std::move(_usr)), display_name(std::move(_display_name)),
          symbols_that_reference_this(allocator)
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

    constexpr void try_visit_children(ClangToGraphMLBuilder::Job& job,
                                      const CXCursor& cursor)
    {
        if (!this->visited) {
            // prevent recursive visiting, technically we have not been visited
            // yet but should be fine, usually if recursion was going to happen
            // it's because we weren't a forward declaration anyways
            this->visited = true;
            bool actually_visited = this->visit_children_impl(job, cursor);
            this->visited = actually_visited;
        }
    }

    template <typename T> T* upcast() &
    {
        if constexpr (std::same_as<T, FunctionSymbol>) {
            return this->symbol_kind == SymbolKind::Function
                       ? reinterpret_cast<FunctionSymbol*>(this)
                       : nullptr;
        } else if constexpr (std::same_as<T, EnumTypeSymbol>) {
            return this->symbol_kind == SymbolKind::Enum
                       ? reinterpret_cast<EnumTypeSymbol*>(this)
                       : nullptr;
        } else if constexpr (std::same_as<T, NamespaceSymbol>) {
            return this->symbol_kind == SymbolKind::Namespace
                       ? reinterpret_cast<NamespaceSymbol*>(this)
                       : nullptr;
        } else if constexpr (std::same_as<T, ClassSymbol>) {
            return this->symbol_kind == SymbolKind::Aggregate
                       ? reinterpret_cast<ClassSymbol*>(this)
                       : nullptr;
        } else {
            static_assert(false, "unknown symbol type to upcast to");
        }
    }

  protected:
    /// Return true if succeeded, ie. this isn't a forward decl
    [[nodiscard]] virtual bool
    visit_children_impl(ClangToGraphMLBuilder::Job& job,
                        const CXCursor& cursor) = 0;

  public:
    SymbolKind symbol_kind;
    String usr;
    String display_name;
    Symbol* semantic_parent;
    bool visited = false;    // if this is a forward declaration it may not be
    bool serialized = false; // avoid recursion during serialization
    Vector<Symbol*> symbols_that_reference_this;
};

struct NamespaceSymbol : public Symbol
{
    constexpr static auto kind = SymbolKind::Namespace;

    constexpr NamespaceSymbol(std::pmr::polymorphic_allocator<> allocator,
                              Symbol* _semantic_parent, String&& _name,
                              std::optional<CXCursor> cursor,
                              String&& _displayName)
        : Symbol(allocator, _semantic_parent, kind, std::move(_name), cursor,
                 std::move(_displayName)),
          symbols(allocator)
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

  protected:
    // cursor must be of type CXCursor_Namespace
    [[nodiscard]] bool visit_children_impl(ClangToGraphMLBuilder::Job& job,
                                           const CXCursor& cursor) final;

  public:
    Vector<Symbol*> symbols;
};

struct ClassSymbol : public Symbol
{
    constexpr static auto kind = SymbolKind::Aggregate;

    // cursor for class symbol is not optional, that way passing
    // semantic_parent, name, cursor is always valid constructor args in
    // template like allocator->new_object(), but we can still handle the
    // case with the root namespace where it has no parent cursor
    constexpr ClassSymbol(std::pmr::polymorphic_allocator<> allocator,
                          Symbol* _semantic_parent, String&& _name,
                          CXCursor cursor, String&& _displayName)
        : Symbol(allocator, _semantic_parent, kind, std::move(_name), cursor,
                 std::move(_displayName)),
          aggregate_kind(get_aggregate_kind_of_cursor(cursor)),
          type_refs(allocator), parent_classes(allocator),
          field_types(allocator), inner_classes(allocator),
          member_functions(allocator), inner_enums(allocator)
    {
    }

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

  protected:
    // cursor must be of type CXCursor_ClassDecl or CXCursor_UnionDecl or
    // CXCursor_StructDecl
    [[nodiscard]] bool visit_children_impl(ClangToGraphMLBuilder::Job& job,
                                           const CXCursor& cursor) final;

    AggregateKind get_aggregate_kind_of_cursor(CXCursor cursor);

  public:
    [[nodiscard]] size_t get_num_symbols_this_references() const final;
    [[nodiscard]] Symbol* get_symbol_this_references(size_t index) const final;

    AggregateKind aggregate_kind;
    Vector<TypeIdentifier> type_refs;
    Vector<TypeIdentifier> parent_classes;
    Vector<TypeIdentifier> field_types;
    Vector<ClassReference> inner_classes;
    Vector<FunctionReference> member_functions;
    Vector<EnumReference> inner_enums;
};

struct EnumTypeSymbol : public Symbol
{
    constexpr static auto kind = SymbolKind::Enum;

    constexpr EnumTypeSymbol(std::pmr::polymorphic_allocator<> allocator,
                             Symbol* _semantic_parent, String&& _name,
                             std::optional<CXCursor> cursor,
                             String&& _displayName)
        : Symbol(allocator, _semantic_parent, kind, std::move(_name), cursor,
                 std::move(_displayName))
    {
    }

  protected:
    // cursor must be of type CXCursor_EnumDecl
    [[nodiscard]] bool visit_children_impl(ClangToGraphMLBuilder::Job& job,
                                           const CXCursor& cursor) final;

  public:
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

    constexpr FunctionSymbol(std::pmr::polymorphic_allocator<> allocator,
                             Symbol* semantic_parent, String&& name,
                             std::optional<CXCursor> cursor,
                             String&& _displayName)
        : Symbol(allocator, semantic_parent, kind, std::move(name), cursor,
                 std::move(_displayName)),
          parameter_types(allocator)
    {
    }

    [[nodiscard]] size_t get_num_symbols_this_references() const final;
    [[nodiscard]] Symbol* get_symbol_this_references(size_t index) const final;

  protected:
    // cursor must be of type CXCursor_FunctionDecl
    [[nodiscard]] bool visit_children_impl(ClangToGraphMLBuilder::Job& job,
                                           const CXCursor& cursor) final;

  public:
    TypeIdentifier return_type;
    // if true, then parameter_types will not include the type of `this`, you
    // get that from .semantic_parent
    bool is_method = false;
    Vector<TypeIdentifier> parameter_types;
};

} // namespace cn

#endif
