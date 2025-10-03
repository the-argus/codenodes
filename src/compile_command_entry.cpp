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

struct CompileCommandEntrySeparatedArgsSchema
{
    std::string directory;
    std::vector<std::string> arguments;
    std::string file;
    std::string output;
};

std::optional<std::vector<CompileCommandEntry>>
parse_compile_commands_json_file_separated_args(
    const std::string_view& file) noexcept
{
    // read entire file into string
    std::vector<CompileCommandEntrySeparatedArgsSchema> compile_commands;
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

    std::vector<CompileCommandEntry> actual_output_format;
    actual_output_format.reserve(compile_commands.size());

    std::transform(compile_commands.begin(), compile_commands.end(),
                   std::back_inserter(actual_output_format),
                   [](CompileCommandEntrySeparatedArgsSchema& separated) {
                       std::string concatenated;
                       for (auto& arg : separated.arguments) {
                           concatenated += std::move(arg);
                           if (&arg != &separated.arguments.back()) {
                               concatenated += " ";
                           }
                       }
                       return CompileCommandEntry{
                           .directory = std::move(separated.directory),
                           .command = std::move(concatenated),
                           .file = std::move(separated.file),
                           .output = std::move(separated.output),
                       };
                   });

    return std::move(actual_output_format);
}

} // namespace cn
