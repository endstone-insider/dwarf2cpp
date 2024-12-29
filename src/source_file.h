#pragma once

#include <map>
#include <memory>
#include <string>

#include "entry.h"

namespace dwarf2cpp {

class SourceFile : public Entry {
public:
    void add(std::size_t decl_line, Entry *entry);
    [[nodiscard]] std::string to_source() const override;
    friend std::ostream &operator<<(std::ostream &os, const SourceFile &sf);

private:
    std::map<std::size_t, std::vector<Entry *>> lines_;
};

} // namespace dwarf2cpp
