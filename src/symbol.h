#ifndef __SYMBOL_H__
#define __SYMBOL_H__

#include "aliases.h"
#include "type_identifier.h"

namespace cn {

enum class SymbolType : uint8_t
{
    Namespace,
    Class,
    Function,
    Enum,
    Union,
    Variable,
};

struct Symbol
{
    Symbol() = delete;
    constexpr Symbol(Symbol* _semantic_parent, SymbolType _type, String&& _name)
        : semantic_parent(_semantic_parent), type(_type), name(std::move(_name))
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

    SymbolType type;
    String name;
    Symbol* semantic_parent;
    Vector<Symbol*> symbols_that_reference_this;
};

struct FunctionSymbol;
struct EnumTypeSymbol;
struct ClassSymbol;

struct NamespaceSymbol : public Symbol
{
    constexpr explicit NamespaceSymbol(Symbol* semantic_parent, String&& _name)
        : Symbol(semantic_parent, SymbolType::Namespace, std::move(_name))
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

    Vector<Symbol*> symbols;
};

struct ClassSymbol : public Symbol
{
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

    Vector<ClassSymbol> parent_classes;
    Vector<ClassSymbol> inner_classes;
    Vector<FunctionReference> member_functions;
    Vector<EnumReference> inner_enums;
};

struct EnumTypeSymbol : public Symbol
{
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
    TypeIdentifier return_type;
    // if true, then parameter_types will not include the type of `this`, you
    // get that from .semantic_parent
    bool is_method = false;
    Vector<TypeIdentifier> parameter_types;
};

} // namespace cn

#endif
