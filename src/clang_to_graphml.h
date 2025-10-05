#ifndef __CODENODES_CLANG_TO_GRAPHML_H__
#define __CODENODES_CLANG_TO_GRAPHML_H__

#include <memory_resource>
#include <ostream>
#include <span>

namespace cn {
class ClangToGraphMLBuilder
{
  public:
    explicit ClangToGraphMLBuilder(std::pmr::memory_resource& memory_resource);
    ClangToGraphMLBuilder(const ClangToGraphMLBuilder&) = delete;
    ClangToGraphMLBuilder& operator=(const ClangToGraphMLBuilder&) = delete;
    ClangToGraphMLBuilder(ClangToGraphMLBuilder&&) = delete;
    ClangToGraphMLBuilder& operator=(ClangToGraphMLBuilder&&) = delete;
    ~ClangToGraphMLBuilder();

    /// Add a file to parse along with its commandline arguments
    void parse(const char* filename,
               std::span<const char* const> command_args) noexcept;

    /// If there are undefined symbols, or any given job failed perhaps due to
    /// compilation errors, this returns false. Otherwise it writes GraphML XML
    /// to the output stream
    [[nodiscard]] bool finish(std::ostream& output) noexcept;

    struct Job;
    struct PersistentData;

  private:
    std::pmr::polymorphic_allocator<> m_allocator;
    // data that persists between calls to parse
    PersistentData* m_data;
};

} // namespace cn

#endif
