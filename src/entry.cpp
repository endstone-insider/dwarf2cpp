#include "entry.h"

#include <iomanip>
#include <sstream>
#include <unordered_set>

#include <llvm/Demangle/Demangle.h>
#include <spdlog/spdlog.h>

#include "algorithm.hpp"
#include "type_printer.h"

namespace {
std::string to_string(llvm::dwarf::AccessAttribute a)
{
    switch (a) {
    case llvm::dwarf::DW_ACCESS_public:
        return "public";
    case llvm::dwarf::DW_ACCESS_protected:
        return "protected";
    case llvm::dwarf::DW_ACCESS_private:
        return "private";
    default:
        throw std::runtime_error("unknown access");
    }
}
} // namespace

namespace dwarf2cpp {

void Entry::parse(const llvm::DWARFDie &die)
{
    if (auto attr = die.find(llvm::dwarf::DW_AT_accessibility); attr) {
        access_ = static_cast<llvm::dwarf::AccessAttribute>(attr->getAsUnsignedConstant().value());
    }
}

void Typedef::parse(const llvm::DWARFDie &die)
{
    Entry::parse(die);
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto type = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
        type = type.resolveTypeUnitReference();
        llvm::raw_string_ostream os(type_);
        llvm::DWARFTypePrinter type_printer(os);
        type_printer.appendQualifiedName(type);
        if (!type.getShortName()) {
            // check if this is anonymous class defined in place
            std::unique_ptr<Entry> entry;
            switch (type.getTag()) {
            case llvm::dwarf::DW_TAG_class_type:
                entry = std::make_unique<StructLike>(StructLike::Kind::Class);
                break;
            case llvm::dwarf::DW_TAG_enumeration_type:
                entry = std::make_unique<Enum>();
                break;
            case llvm::dwarf::DW_TAG_structure_type:
                entry = std::make_unique<StructLike>(StructLike::Kind::Struct);
                break;
            case llvm::dwarf::DW_TAG_union_type:
                entry = std::make_unique<StructLike>(StructLike::Kind::Union);
                break;
            default:
                break;
            }
            if (entry) {
                entry->parse(type);
                type_ = entry->to_source();
                type_.pop_back(); // remove the trailing semicolon
                is_type_alias_ = false;
            }
        }
    }
}

std::string Typedef::to_source() const
{
    if (is_type_alias_) {
        return "using " + name_ + " = " + type_ + ";";
    }
    return "typedef " + type_ + " " + name_ + ";";
}

void Parameter::parse(const llvm::DWARFDie &die)
{
    Entry::parse(die);
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto type = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
        type = type.resolveTypeUnitReference();
        llvm::raw_string_ostream os(type_);
        llvm::DWARFTypePrinter type_printer(os);
        type_printer.appendQualifiedName(type);
    }
}

std::string Parameter::to_source() const
{
    return type_ + " " + name_;
}

void Function::parse(const llvm::DWARFDie &die)
{
    Entry::parse(die);
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto *buffer = die.getLinkageName(); buffer) {
        linkage_name_ = buffer;
    }
    if (auto type = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
        type = type.resolveTypeUnitReference();
        llvm::raw_string_ostream os(return_type_);
        llvm::DWARFTypePrinter type_printer(os);
        type_printer.appendQualifiedName(type);
    }
    else {
        return_type_ = "void";
    }
    if (die.find(llvm::dwarf::DW_AT_explicit)) {
        is_explicit_ = true;
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_virtuality); attr.has_value()) {
        virtuality_ = static_cast<llvm::dwarf::VirtualityAttribute>(attr->getAsUnsignedConstant().value());
    }
    bool first_param = true;
    for (const auto &child : die.children()) {
        switch (child.getTag()) {
        case llvm::dwarf::DW_TAG_formal_parameter: {
            // Check if this is a const function by checking the first parameter
            // For a const member function, the `this` pointer type will include a const qualifier.
            if (is_member_ && first_param && child.find(llvm::dwarf::DW_AT_artificial)) {
                // it should be a pointer_type for the `this` pointer
                if (auto type = child.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type);
                    type.isValid() && type.getTag() == llvm::dwarf::DW_TAG_pointer_type) {
                    // it should also include a const qualifier, i.e. it should be const_type
                    if (type = type.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type);
                        type.isValid() && type.getTag() == llvm::dwarf::DW_TAG_const_type) {
                        is_const_ = true;
                    }
                }
                is_static_ = false;
            }
            else {
                auto &param = parameters_.emplace_back();
                param.parse(child);
            }
            first_param = false;
            break;
        }
        default:
            // TODO: template params
            break;
        }
    }
}

std::string Function::to_source() const
{
    std::stringstream ss;
    if (is_member_ && is_static_) {
        ss << "static ";
    }
    if (virtuality_ > llvm::dwarf::DW_VIRTUALITY_none) {
        ss << "virtual ";
    }
    if (!linkage_name_.empty() && !startswith(name_, "operator ")) {
        ss << return_type_ << " ";
    }
    if (is_explicit_) {
        ss << "explicit ";
    }
    ss << name_ << "(";
    for (auto it = parameters_.begin(); it != parameters_.end(); ++it) {
        ss << it->to_source();
        if (it < parameters_.end() - 1) {
            ss << ", ";
        }
    }
    ss << ")";
    if (is_const_) {
        ss << " const";
    }
    if (virtuality_ == llvm::dwarf::DW_VIRTUALITY_pure_virtual) {
        ss << " = 0";
    }
    ss << ";";
    return ss.str();
}

void Enum::parse(const llvm::DWARFDie &die)
{
    Entry::parse(die);
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto type = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
        std::string base_type;
        type = type.resolveTypeUnitReference();
        llvm::raw_string_ostream os(base_type);
        llvm::DWARFTypePrinter type_printer(os);
        type_printer.appendQualifiedName(type);
        base_type_ = base_type;
    }
    for (const auto &child : die.children()) {
        if (child.getTag() != llvm::dwarf::DW_TAG_enumerator) {
            continue;
        }
        Enumerator enumerator;
        enumerator.name = child.getShortName();
        enumerator.value = child.find(llvm::dwarf::DW_AT_const_value)->getAsSignedConstant().value();
        enumerators_.push_back(enumerator);
    }
}

std::string Enum::to_source() const
{
    std::stringstream ss;
    ss << "enum ";
    if (!name_.empty()) {
        ss << "class " << name_;
    }
    if (base_type_.has_value()) {
        ss << " : " << base_type_.value();
    }
    ss << " {\n";
    for (const auto &[name, value] : enumerators_) {
        ss << "    " << name << " = " << value << ",\n";
    }
    ss << "};";
    return ss.str();
}

void Field::parse(const llvm::DWARFDie &die)
{
    Entry::parse(die);
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto type = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
        type = type.resolveTypeUnitReference();
        llvm::DWARFDie inner;
        // before name
        {
            llvm::raw_string_ostream os(type_before_);
            llvm::DWARFTypePrinter type_printer(os);
            inner = type_printer.appendQualifiedNameBefore(type);
        }
        // after name
        {
            llvm::raw_string_ostream os(type_after_);
            llvm::DWARFTypePrinter type_printer(os);
            type_printer.appendUnqualifiedNameAfter(type, inner);
        }
        if (!type.getShortName()) {
            // check if this is anonymous class defined in place
            std::unique_ptr<Entry> entry;
            switch (type.getTag()) {
            case llvm::dwarf::DW_TAG_class_type:
                entry = std::make_unique<StructLike>(StructLike::Kind::Class);
                break;
            case llvm::dwarf::DW_TAG_enumeration_type:
                entry = std::make_unique<Enum>();
                break;
            case llvm::dwarf::DW_TAG_structure_type:
                entry = std::make_unique<StructLike>(StructLike::Kind::Struct);
                break;
            case llvm::dwarf::DW_TAG_union_type:
                entry = std::make_unique<StructLike>(StructLike::Kind::Union);
                break;
            default:
                break;
            }
            if (entry) {
                entry->parse(type);
                type_before_ = entry->to_source();
                type_before_.pop_back(); // remove the trailing semicolon
                type_after_.clear();
            }
        }
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_data_member_location); attr.has_value()) {
        member_location_ = attr->getAsUnsignedConstant().value();
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_external); attr.has_value()) {
        is_static = true;
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_const_value); attr.has_value()) {
        if (attr->getForm() == llvm::dwarf::DW_FORM_sdata) {
            default_value_ = attr->getAsSignedConstant().value();
        }
        else {
            default_value_ = attr->getAsUnsignedConstant().value();
        }
    }
}

std::string Field::to_source() const
{
    std::stringstream ss;
    if (is_static) {
        ss << "static ";
    }
    ss << type_before_ << " " << name_ << type_after_;
    if (default_value_.has_value()) {
        ss << " = ";
        if (endswith(type_before_, "float")) {
            auto value = static_cast<std::int32_t>(default_value_.value());
            constexpr auto max_precision{std::numeric_limits<float>::digits10 + 1};
            ss << std::fixed << std::setprecision(max_precision) << *reinterpret_cast<float *>(&value);
        }
        else if (endswith(type_before_, "double")) {
            auto value = default_value_.value();
            constexpr auto max_precision{std::numeric_limits<double>::digits10 + 1};
            ss << std::fixed << std::setprecision(max_precision) << *reinterpret_cast<double *>(&value);
        }
        else {
            ss << default_value_.value();
        }
    }
    ss << ";";

    if (member_location_.has_value()) {
        ss << " // +" << member_location_.value();
    }
    return ss.str();
}

void StructLike::parse(const llvm::DWARFDie &die)
{
    Entry::parse(die);
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_byte_size); attr) {
        byte_size = attr->getAsUnsignedConstant().value();
    }

    std::unordered_set<std::size_t> skipped;
    for (const auto &child : die.children()) {
        if (child.getTag() != llvm::dwarf::DW_TAG_member) {
            continue;
        }
        if (auto type = child.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
            // skip the anonymous class defined by field
            if (!type.getShortName() && (type.getTag() == llvm::dwarf::DW_TAG_structure_type ||
                                         type.getTag() == llvm::dwarf::DW_TAG_union_type ||
                                         type.getTag() == llvm::dwarf::DW_TAG_enumeration_type ||
                                         type.getTag() == llvm::dwarf::DW_TAG_class_type)) {
                skipped.emplace(type.getOffset());
            }
        }
    }

    for (const auto &child : die.children()) {
        std::unique_ptr<Entry> entry;
        std::size_t decl_line = child.getDeclLine();
        bool should_skip = skipped.find(child.getOffset()) != skipped.end();

        switch (child.getTag()) {
        case llvm::dwarf::DW_TAG_inheritance: {
            auto access = kind_ == Kind::Class ? llvm::dwarf::DW_ACCESS_private : llvm::dwarf::DW_ACCESS_public;
            if (auto attr = child.find(llvm::dwarf::DW_AT_accessibility)) {
                access = static_cast<llvm::dwarf::AccessAttribute>(attr->getAsUnsignedConstant().value());
            }
            auto type = child.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type);
            type = type.resolveTypeUnitReference();
            std::string base_type;
            llvm::raw_string_ostream os(base_type);
            llvm::DWARFTypePrinter type_printer(os);
            type_printer.appendQualifiedName(type);
            base_classes_.emplace_back(access, base_type);
            break;
        }
        case llvm::dwarf::DW_TAG_class_type:
            entry = should_skip ? nullptr : std::make_unique<StructLike>(Kind::Class);
            break;
        case llvm::dwarf::DW_TAG_enumeration_type:
            entry = should_skip ? nullptr : std::make_unique<Enum>();
            break;
        case llvm::dwarf::DW_TAG_member:
            entry = std::make_unique<Field>();
            break;
        case llvm::dwarf::DW_TAG_structure_type:
            entry = should_skip ? nullptr : std::make_unique<StructLike>(Kind::Struct);
            break;
        case llvm::dwarf::DW_TAG_typedef:
            entry = std::make_unique<Typedef>();
            break;
        case llvm::dwarf::DW_TAG_union_type:
            entry = should_skip ? nullptr : std::make_unique<StructLike>(Kind::Union);
            break;
        case llvm::dwarf::DW_TAG_subprogram:
            entry = std::make_unique<Function>(true);
            break;
        default:
            break;
        }

        if (entry && decl_line > 0) {
            entry->parse(child);
            members_[decl_line].emplace_back(std::move(entry));
        }
    }
}

std::string StructLike::to_source() const
{
    auto default_access = kind_ == Kind::Class ? llvm::dwarf::DW_ACCESS_private : llvm::dwarf::DW_ACCESS_public;

    std::stringstream ss;
    switch (kind_) {
    case Kind::Struct:
        ss << "struct ";
        break;
    case Kind::Class:
        ss << "class ";
        break;
    case Kind::Union:
        ss << "union ";
        break;
    }
    ss << name_;
    if (!base_classes_.empty()) {
        ss << ": ";
        for (auto i = 0; i < base_classes_.size(); ++i) {
            if (i > 0) {
                ss << ", ";
            }
            const auto &[access, base] = base_classes_[i];
            if (access != default_access) {
                ss << to_string(access) << " ";
            }
            ss << base;
        }
    }
    ss << " {\n";

    auto last_access = default_access;
    for (const auto &[decl_line, member] : members_) {
        for (const auto &m : member) {
            if (auto current_access = m->access().value_or(default_access); current_access != last_access) {
                ss << to_string(current_access) << ":\n";
                last_access = current_access;
            }

            std::stringstream is(m->to_source());
            std::string line;
            while (std::getline(is, line)) {
                ss << "    " << line << "\n";
            }
        }
    }
    ss << "};";
    if (!name_.empty() && byte_size.has_value()) {
        ss << "\n";
        ss << "static_assert(sizeof(" << name_ << ") == " << byte_size.value() << ");\n";
    }
    return ss.str();
}

} // namespace dwarf2cpp
