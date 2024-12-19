#pragma once

#include <map>
#include <memory>
#include <string>

#include "entry.h"

namespace dwarf2cpp {

class SourceFile {
public:
    void add(std::size_t line, std::unique_ptr<Entry> new_entry);
    [[nodiscard]] std::string to_source() const;
    friend std::ostream &operator<<(std::ostream &os, const SourceFile &sf);

private:
    std::map<std::size_t, std::unique_ptr<Entry>> lines_;
};

} // namespace dwarf2cpp
