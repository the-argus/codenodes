#ifndef __CODENODES_TRANSLATION_UNIT_H__
#define __CODENODES_TRANSLATION_UNIT_H__

#include <optional>
#include <span>
#include <string_view>

namespace cn {

struct TranslationUnit
{
    [[nodiscard]] static std::optional<TranslationUnit>
    parse(const char* filename,
          std::span<const std::string_view> command_args) noexcept;
};

} // namespace cn

#endif
