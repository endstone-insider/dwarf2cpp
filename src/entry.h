#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <llvm/DebugInfo/DWARF/DWARFDie.h>

namespace dwarf2cpp {

class Context;

class Entry {
public:
    virtual ~Entry() = default;
    virtual void parse(const llvm::DWARFDie &die);
    [[nodiscard]] virtual std::string to_source() const = 0;

    [[nodiscard]] std::vector<std::string> namespaces() const
    {
        return namespaces_;
    }

    [[nodiscard]] std::optional<llvm::dwarf::AccessAttribute> access() const
    {
        return access_;
    }

private:
    std::vector<std::string> namespaces_;
    std::optional<llvm::dwarf::AccessAttribute> access_;
};

class Typedef : public Entry {
public:
    using Entry::Entry;
    void parse(const llvm::DWARFDie &die) override;
    [[nodiscard]] std::string to_source() const override;

private:
    std::vector<std::string> names_;
    std::string type_;
    bool is_type_alias_{true};
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
    explicit Function(bool is_member) : is_member_(is_member) {}
    void parse(const llvm::DWARFDie &die) override;
    [[nodiscard]] std::string to_source() const override;

private:
    void parse_children(const llvm::DWARFDie &die);

    std::string name_;
    std::string linkage_name_;
    std::string return_type_;
    std::vector<Parameter> parameters_;
    std::string template_params_;
    bool is_const_{false};
    bool is_member_{false};
    bool is_static_{true};
    bool is_explicit_{false};
    bool is_defaulted_{false};
    bool is_deleted_{false};
    llvm::dwarf::VirtualityAttribute virtuality_{llvm::dwarf::DW_VIRTUALITY_none};
};

class Enum : public Entry {
public:
    struct Enumerator {
        std::string name;
        std::int64_t value;
    };
    using Entry::Entry;
    void parse(const llvm::DWARFDie &die) override;
    [[nodiscard]] std::string to_source() const override;

private:
    void parse_children(const llvm::DWARFDie &die);

    std::string name_;
    std::optional<std::string> base_type_;
    std::vector<Enumerator> enumerators_;
    bool is_enum_class_{false};
};

class Field : public Entry {
public:
    using Entry::Entry;
    void parse(const llvm::DWARFDie &die) override;
    [[nodiscard]] std::string to_source() const override;

private:
    std::string type_before_;
    std::string name_;
    std::string type_after_;
    std::optional<std::size_t> member_location_;
    std::optional<std::size_t> bit_size_;
    bool is_static_{false};
    bool is_mutable_{false};
    std::optional<std::int64_t> default_value_;
};

class StructLike : public Entry {
public:
    enum class Kind {
        Struct,
        Class,
        Union,
    };
    explicit StructLike(Kind kind) : kind_(kind) {}
    void parse(const llvm::DWARFDie &die) override;
    [[nodiscard]] std::string to_source() const override;

private:
    std::string name_;
    Kind kind_;
    std::map<std::size_t, std::vector<std::unique_ptr<Entry>>> members_;
    std::optional<std::size_t> byte_size;
    std::vector<std::pair<llvm::dwarf::AccessAttribute, std::string>> base_classes_;
    std::string template_params_;
};

} // namespace dwarf2cpp
