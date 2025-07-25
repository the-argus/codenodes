#include <glaze/glaze.hpp>
#include <print>

#include "compile_command_entry.h"

static_assert(glz::reflectable<cn::CompileCommandEntry>);
namespace cn {

std::optional<std::vector<CompileCommandEntry>>
parse_compile_commands_json_file(const std::string_view& file) noexcept
{
    // read entire file into string
    std::vector<CompileCommandEntry> compile_commands;
    {
        std::string buffer{};
        // about a million bytes
        buffer.reserve(1024UL * 1024UL);
        auto read_err = glz::read_file_json(compile_commands, file, buffer);

        if (read_err) {
            std::println(stderr, "Error parsing compile commands: {}",
                         glz::format_error(read_err, buffer));
            return {};
        }
    }

    return std::move(compile_commands);
}

} // namespace cn
