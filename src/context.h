#pragma once

#include <llvm/DebugInfo/DWARF/DWARFUnit.h>

#include "source_file.h"

namespace dwarf2cpp {

class Context {
public:
    Context() = default;

    void update(const llvm::DWARFDie &unit_die);
    [[nodiscard]] std::string base_dir() const;
    [[nodiscard]] const std::unordered_map<std::string, SourceFile> &source_files() const;

private:
    void parse_children(const llvm::DWARFDie &die, std::vector<std::string> &namespaces);

    std::string base_dir_;
    std::unordered_map<std::string, SourceFile> source_files_;
};

}; // namespace dwarf2cpp
