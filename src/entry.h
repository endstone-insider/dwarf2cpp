#pragma once

#include <optional>
#include <string>
#include <vector>

#include <llvm/DebugInfo/DWARF/DWARFDie.h>

namespace dwarf2cpp {

class Entry {
public:
    using namespace_list = std::vector<std::string>;
    explicit Entry(namespace_list namespaces = {}) : namespaces_(std::move(namespaces)){};
    virtual ~Entry() = default;
    virtual void parse(const llvm::DWARFDie &die) = 0;
    [[nodiscard]] virtual std::string to_source() const = 0;
    [[nodiscard]] namespace_list namespaces() const
    {
        return namespaces_;
    }

private:
    namespace_list namespaces_;
};

class Typedef : public Entry {
public:
    using Entry::Entry;
    void parse(const llvm::DWARFDie &die) override;
    [[nodiscard]] std::string to_source() const override;

private:
    std::string name_;
    std::string type_;
};

} // namespace dwarf2cpp
