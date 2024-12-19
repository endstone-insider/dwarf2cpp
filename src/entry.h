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

class Parameter : public Entry {
public:
    using Entry::Entry;
    void parse(const llvm::DWARFDie &die) override;
    [[nodiscard]] std::string to_source() const override;

private:
    std::string name_;
    std::string type_;
};

class Function : public Entry {
public:
    explicit Function(bool is_member, std::vector<std::string> namespaces = {})
        : Entry(std::move(namespaces)), is_member_(is_member)
    {
    }
    void parse(const llvm::DWARFDie &die) override;
    [[nodiscard]] std::string to_source() const override;

private:
    std::string name_;
    std::string linkage_name_;
    std::string return_type_;
    std::vector<Parameter> parameters_;
    bool is_const_{false};
    bool is_member_{false};
    bool is_static_{true};
    bool is_explicit_{false};
    llvm::dwarf::VirtualityAttribute virtuality_{llvm::dwarf::DW_VIRTUALITY_none};
    // std::optional<dw::access> access_;
};

} // namespace dwarf2cpp
