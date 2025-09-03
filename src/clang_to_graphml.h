#ifndef __CODENODES_TRANSLATION_UNIT_H__
#define __CODENODES_TRANSLATION_UNIT_H__

#include <fstream>
#include <memory_resource>
#include <span>
#include <vector>

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
    void parse(const char* filename, std::span<const char* const> command_args) noexcept;

    /// If there are undefined symbols, or any given job failed perhaps due to
    /// compilation errors, this returns false. Otherwise it writes GraphML XML
    /// to the output stream
    [[nodiscard]] bool finish(std::ostream& output) noexcept;

  private:
    struct Job;
    struct Data;
    std::pmr::memory_resource& m_resource; // backs the other allocators
    std::pmr::polymorphic_allocator<> m_allocator;
    std::pmr::vector<Job*> m_jobs;
    Data* m_data;
};

} // namespace cn

#endif
