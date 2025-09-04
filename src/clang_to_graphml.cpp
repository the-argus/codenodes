#include <cassert>
#include <clang-c/Index.h>
#include <cstdio>
#include <span>
#include <unordered_map>
#include <utility>

#include "clang_to_graphml.h"

// using std::pmr but these should all be backed by monotonic buffers
template <typename T> using Vector = std::pmr::vector<T>;
using String = std::pmr::string;
template <typename K, typename V> using Map = std::pmr::unordered_map<K, V>;
using Allocator = std::pmr::polymorphic_allocator<>;
using MemoryResource = std::pmr::memory_resource;
// resource user per parse job
using JobResource = std::pmr::unsynchronized_pool_resource;

template <typename T> class OwningPointer
{
  public:
    constexpr T* operator->() noexcept { return ptr; }
    constexpr T& operator*() noexcept { return *ptr; }
    constexpr explicit operator bool() noexcept { return ptr; }

    template <typename... ConstructorArgs>
        requires std::is_constructible_v<T, ConstructorArgs...>
    explicit constexpr OwningPointer(Allocator& _allocator,
                                     ConstructorArgs&&... args)
        : allocator(std::addressof(_allocator)),
          ptr(_allocator.new_object<T>(std::forward<ConstructorArgs>(args)...))
    {
    }

    constexpr OwningPointer(OwningPointer&& other) noexcept
        : ptr(std::exchange(other.ptr, nullptr)), allocator(other.allocator)
    {
    }

    constexpr OwningPointer& operator=(OwningPointer&& other) noexcept
    {
        allocator->delete_object(ptr);
        ptr = std::exchange(other.ptr, nullptr);
        allocator = other.allocator;
    }

    OwningPointer(const OwningPointer&) = delete;
    OwningPointer& operator=(const OwningPointer&) = delete;

    constexpr ~OwningPointer()
    {
        if (ptr) {
            allocator->delete_object(ptr);
        }
    }

  private:
    T* ptr;
    Allocator* allocator;
};

struct OwningCXString : private CXString
{
    constexpr static OwningCXString clang_getCursorUSR(const CXCursor& cursor)
    {
        return OwningCXString(::clang_getCursorUSR(cursor));
    }

    constexpr static OwningCXString
    clang_getCursorSpelling(const CXCursor& cursor)
    {
        return OwningCXString(::clang_getCursorSpelling(cursor));
    }

    constexpr static OwningCXString clang_getFileName(const CXFile& cursor)
    {
        return OwningCXString(::clang_getFileName(cursor));
    }

    constexpr static OwningCXString
    clang_getCursorDisplayName(const CXCursor& cursor)
    {
        return OwningCXString(::clang_getCursorDisplayName(cursor));
    }

    constexpr static OwningCXString
    clang_formatDiagnostic(CXDiagnostic diagnostic,
                           CXDiagnosticDisplayOptions display_options)
    {
        return OwningCXString(
            ::clang_formatDiagnostic(diagnostic, display_options));
    }

    constexpr String copy_to_string(Allocator& allocator)
    {
        return {c_str(), allocator};
    }

    OwningCXString() = delete;
    constexpr explicit OwningCXString(CXString nonowning) : CXString(nonowning)
    {
    }

    constexpr OwningCXString(OwningCXString&& other) noexcept : CXString(other)
    {
        other.data = nullptr;
        other.private_flags = 0;
    }

    constexpr OwningCXString& operator=(OwningCXString&& other) noexcept
    {
        data = std::exchange(other.data, nullptr);
        private_flags = std::exchange(other.private_flags, 0);
        return *this;
    }

    OwningCXString(const OwningCXString& other) = delete;
    OwningCXString& operator=(const OwningCXString& other) = delete;

    const char* c_str() noexcept { return clang_getCString(*this); }
    constexpr std::string_view view() noexcept
    {
        return {clang_getCString(*this)};
    }

    constexpr ~OwningCXString() { clang_disposeString(*this); }
};

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

enum class PrimitiveTypeType : uint8_t
{
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float,
    Double,
    Bool,
    Char, // distinct from int8?
    Nullptr,
    Unknown, // some weird implementation defined long double or something?
};

enum class ReferenceKind : uint8_t
{
    RValue,
    LValue,
};

struct Symbol;

/// not a primitive type or pointer or reference or array or type alias
struct UserDefinedTypeIdentifier
{
    Symbol* symbol;
};

struct CArrayTypeIdentifier
{
    std::variant<UserDefinedTypeIdentifier, PrimitiveTypeType> contents_type;
    size_t size{};
};

using ConcreteTypeIdentifier =
    std::variant<PrimitiveTypeType, UserDefinedTypeIdentifier,
                 CArrayTypeIdentifier>;

struct PointerTypeIdentifier
{
    // either a pointer to another pointer, or a pointer to a concrete thing
    std::variant<ConcreteTypeIdentifier, PointerTypeIdentifier*> pointee_type;
};

using NonReferenceTypeIdentifier =
    std::variant<PointerTypeIdentifier, ConcreteTypeIdentifier>;

struct ReferenceTypeIdentifier
{
    bool is_const;
    ReferenceKind kind;
    NonReferenceTypeIdentifier referenced_type;
};

struct TypeIdentifier
{
    [[nodiscard]] Symbol* try_get_symbol() const
    {
        return std::visit(
            [](const auto& iden) { return ref_or_nonref_visitor(iden); },
            variant);
    }

    // the sum of all human knowledge
    std::variant<ReferenceTypeIdentifier, NonReferenceTypeIdentifier> variant;

  private:
    static Symbol*
    user_def_or_primitive_recursive_visitor(const PrimitiveTypeType& iden)
    {
        return primitive_or_user_def_or_carray_recursive_visitor(iden);
    }

    static Symbol* user_def_or_primitive_recursive_visitor(
        const UserDefinedTypeIdentifier& iden)
    {
        return primitive_or_user_def_or_carray_recursive_visitor(iden);
    }

    static Symbol* primitive_or_user_def_or_carray_recursive_visitor(
        const PrimitiveTypeType& /*type*/)
    {
        return nullptr;
    }

    static Symbol* primitive_or_user_def_or_carray_recursive_visitor(
        const UserDefinedTypeIdentifier& iden)
    {
        return iden.symbol;
    }

    static Symbol* primitive_or_user_def_or_carray_recursive_visitor(
        const CArrayTypeIdentifier& iden)
    {
        return std::visit(
            [](const auto& iden) {
                return user_def_or_primitive_recursive_visitor(iden);
            },
            iden.contents_type);
    }

    static Symbol*
    pointee_type_recursive_visitor(PointerTypeIdentifier* pointer_iden)
    {
        return concrete_or_pointer_recursive_visitor(*pointer_iden);
    }

    static Symbol*
    pointee_type_recursive_visitor(const ConcreteTypeIdentifier& concrete_iden)
    {
        return concrete_or_pointer_recursive_visitor(concrete_iden);
    }

    static Symbol* concrete_or_pointer_recursive_visitor(
        const PointerTypeIdentifier& pointer_iden)
    {
        return std::visit(
            [](const auto& iden) {
                return pointee_type_recursive_visitor(iden);
            },
            pointer_iden.pointee_type);
    }

    static Symbol* concrete_or_pointer_recursive_visitor(
        const ConcreteTypeIdentifier& concrete_iden)
    {
        return std::visit(
            [](const auto& iden) {
                return primitive_or_user_def_or_carray_recursive_visitor(iden);
            },
            concrete_iden);
    }

    static Symbol* ref_or_nonref_visitor(const ReferenceTypeIdentifier& ref)
    {
        return ref_or_nonref_visitor(ref.referenced_type);
    }

    static Symbol*
    ref_or_nonref_visitor(const NonReferenceTypeIdentifier& nonref)
    {
        return std::visit(
            [](const auto& iden) {
                return concrete_or_pointer_recursive_visitor(iden);
            },
            nonref);
    }
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

struct ClangToGraphMLBuilder::PersistentData
{
    std::vector<OwningPointer<Job>> finished_jobs;
    // all symbols by their unique id
    Map<String, Symbol*> symbols_by_usr;
    // forest of definitions
    NamespaceSymbol global_namespace{nullptr, String{}};

    Symbol* try_get_symbol(const String& usr)
    {
        if (usr.empty()) {
            return nullptr;
        }
        if (symbols_by_usr.contains(usr)) {
            auto* out = symbols_by_usr[usr];
            assert(out);
            return out;
        }
        return nullptr;
    }
};

struct ClangToGraphMLBuilder::Job
{
    explicit Job(MemoryResource* res, PersistentData* data)
        : shared_data(data), resource(res), allocator(&resource)
    {
    }

    void run(const char* filename,
             std::span<const char* const> command_args) noexcept;

    static enum CXChildVisitResult
    cursor_visitor(CXCursor current_cursor, CXCursor parent, void* userdata);

    // for use with clang_getInclusions... could be useful for eliminating
    // symbols after a certain inclusion depth
    // static void inclusion_visitor(CXFile included_file,
    //                               CXSourceLocation* inclusion_stack,
    //                               unsigned include_len,
    //                               CXClientData client_data);

    PersistentData* shared_data;
    Allocator allocator;
    JobResource resource;
};

ClangToGraphMLBuilder::ClangToGraphMLBuilder(MemoryResource& memory_resource)
    : m_resource(memory_resource), m_allocator(&memory_resource),
      m_data(m_allocator.new_object<PersistentData>())
{
}

ClangToGraphMLBuilder::~ClangToGraphMLBuilder()
{
    m_allocator.delete_object(m_data);
}

void ClangToGraphMLBuilder::parse(
    const char* filename, std::span<const char* const> command_args) noexcept
{
    OwningPointer<Job> job(m_allocator, &m_resource, m_data);
    job->run(filename, command_args);
    m_data->finished_jobs.emplace_back(std::move(job));
}

enum CXChildVisitResult
ClangToGraphMLBuilder::Job::cursor_visitor(CXCursor current_cursor,
                                           CXCursor parent, void* userdata)
{
    auto* job = static_cast<Job*>(userdata);
    PersistentData* data = job->shared_data;
    auto usr = OwningCXString::clang_getCursorUSR(current_cursor);
    auto display = OwningCXString::clang_getCursorDisplayName(current_cursor);
    if (usr.view().length() > 0) {
        // std::ignore = fprintf(
        //     stdout, "found cursor with USR: %s \nand display name: %s\n",
        //     usr.c_str(), display.c_str());
    }

    enum CXCursorKind kind = clang_getCursorKind(current_cursor);

    switch (kind) {
    case CXCursorKind::CXCursor_FunctionDecl: {
        break;
    }
    case CXCursorKind::CXCursor_CXXMethod: {
        // clang_Cursor_getNumArguments
        // clang_Cursor_isDynamicCall()
        break;
    }
    case CXCursorKind::CXCursor_CallExpr: {
        CXCursor function = clang_getCursorReferenced(current_cursor);
        enum CXCursorKind kind = clang_getCursorKind(function);

        auto is_function =
            kind == CXCursor_CXXMethod || kind == CXCursor_FunctionDecl ||
            kind == CXCursor_Constructor || kind == CXCursor_Destructor ||
            kind == CXCursor_ConversionFunction;

        if (!is_function) {
            auto name = [&]() -> std::optional<const char*> {
                switch (kind) {
                case CXCursor_FirstInvalid:
                    return {};
                    return "CXCursor_FirstInvalid";
                case CXCursor_VarDecl:
                    return "CXCursor_VarDecl";
                case CXCursor_ParmDecl:
                    return "CXCursor_ParmDecl";
                case CXCursor_FieldDecl:
                    return "CXCursor_FieldDecl";
                default:
                    return {};
                }
            }();

            // this should never happen
            if (!name) {
                break;
                auto callsym =
                    OwningCXString::clang_getCursorSpelling(current_cursor);
                auto defsym = OwningCXString::clang_getCursorSpelling(function);
                std::ignore = fprintf(
                    stderr,
                    "Found callexpr which does not reference a function: "
                    "callsym is %s and defsym is %s with kind %d named "
                    "%s\n",
                    callsym.c_str(), defsym.c_str(), static_cast<int>(kind),
                    name.value_or("unknown"));
                break;
            }

            // CXSourceLocation location =
            // clang_getCursorLocation(current_cursor);

            // CXFile file{};
            // uint32_t line{};
            // clang_getSpellingLocation(location, &file, &line, nullptr,
            // nullptr);

            // if (clang_Location_isInSystemHeader(location) != 0) {
            //     break;
            // }

            // // to get containing class or namespace:
            // clang_getCursorSemanticParent(current_cursor);

            // printf("callexpr %s is of type %s in file %s line %u\n",
            //        OwningCXString::clang_getCursorDisplayName(current_cursor)
            //            .c_str(),
            //        name.value(),
            //        OwningCXString::clang_getFileName(file).c_str(), line);

            break;
        }

        break;
    }
    default:
        // do nothing
        break;
    }

    return CXChildVisit_Recurse;
}

void ClangToGraphMLBuilder::Job::run(
    const char* filename, std::span<const char* const> command_args) noexcept
{
    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit =
        clang_parseTranslationUnit(index, filename, command_args.data(),
                                   static_cast<int>(command_args.size()),
                                   nullptr, 0, CXTranslationUnit_None);

    if (unit == nullptr) {
        std::ignore =
            fprintf(stderr, "Unable to parse translation unit %s, aborting.\n",
                    filename);
        clang_disposeTranslationUnit(unit);
        clang_disposeIndex(index);
        return;
    }

    // warn for diagnostics
    CXDiagnosticSet diagnostics = clang_getDiagnosticSetFromTU(unit);
    const size_t num_diagnostic = clang_getNumDiagnostics(unit);
    for (size_t i = 0; i < num_diagnostic; ++i) {
        CXDiagnostic diagnostic = clang_getDiagnosticInSet(diagnostic, i);

        std::ignore = fprintf(
            stderr, "DIAGNOSTIC - Encountered while parsing %s: %s\n", filename,
            OwningCXString::clang_formatDiagnostic(
                diagnostic,
                CXDiagnosticDisplayOptions::CXDiagnostic_DisplaySourceLocation)
                .c_str());
    }

    CXCursor cursor = clang_getTranslationUnitCursor(unit);

    clang_visitChildren(cursor, // Root cursor
                        Job::cursor_visitor,
                        nullptr // userdata
    );

    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
}

bool ClangToGraphMLBuilder::finish(std::ostream& output) noexcept
{
    return false;
}

} // namespace cn
