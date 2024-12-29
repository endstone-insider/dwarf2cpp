#pragma once

#include <map>
#include <memory>
#include <string>

#include "entry.h"

namespace dwarf2cpp {

class SourceFile : public Entry {
public:
    Entry *add(const llvm::DWARFDie &die);
    Entry *get(const llvm::DWARFDie &die);
    [[nodiscard]] std::string to_source() const override;
    friend std::ostream &operator<<(std::ostream &os, const SourceFile &sf);

private:
    std::map<std::size_t, std::vector<std::unique_ptr<Entry>>> lines_;
    std::unordered_map<std::size_t, std::unordered_map<std::string, Entry *>> lookup_map_;
};

} // namespace dwarf2cpp
