#ifndef __CODENODES_COMPILE_COMMANDS_ENTRY_H__
#define __CODENODES_COMPILE_COMMANDS_ENTRY_H__

#include <optional>
#include <string>
#include <vector>

namespace cn {

struct CompileCommandEntry
{
    std::string directory;
    std::string command;
    std::string file;
    std::string output;
};

std::optional<std::vector<CompileCommandEntry>>
parse_compile_commands_json_file(const std::string_view& file) noexcept;

std::optional<std::vector<CompileCommandEntry>>
parse_compile_commands_json_file_separated_args(
    const std::string_view& file) noexcept;

} // namespace cn

#endif
