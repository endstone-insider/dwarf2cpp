#include "entry.h"

#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>

#include <llvm/ADT/StringExtras.h>
#include <llvm/Demangle/Demangle.h>
#include <spdlog/spdlog.h>

#include "algorithm.hpp"
#include "context.h"
#include "posixpath.hpp"
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

std::string parse_template_params(const llvm::DWARFDie &die)
{
    std::string result;
    llvm::raw_string_ostream os(result);
    bool first = true;

    auto sep = [&] {
        if (first) {
            os << "template <";
        }
        else {
            os << ", ";
        }
        first = false;
    };

    for (const auto &child : die.children()) {
        switch (child.getTag()) {
        case llvm::dwarf::DW_TAG_template_type_parameter: {
            sep();
            os << "typename " << child.getShortName();
            break;
        }
        case llvm::dwarf::DW_TAG_template_value_parameter: {
            sep();
            auto type = child.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type);
            llvm::DWARFTypePrinter type_printer(os);
            type_printer.appendQualifiedName(type);
            os << " " << child.getShortName();
            break;
        }
        case llvm::dwarf::DW_TAG_GNU_template_parameter_pack: {
            sep();
            os << "typename... " << child.getShortName();
            break;
        }
        case llvm::dwarf::DW_TAG_GNU_template_template_param: {
            sep();
            os << "template<typename> class " << child.getShortName();
            break;
        }
        default:
            break;
        }
    }

    if (!result.empty()) {
        os << ">";
    }
    return result;
}

} // namespace

namespace dwarf2cpp {

void Entry::parse(Context &ctx, const llvm::DWARFDie &die)
{
    // Retrieve the namespace
    std::vector<std::string> namespaces;
    for (auto current = die.getParent(); current.isValid(); current = current.getParent()) {
        if (current.getTag() == llvm::dwarf::DW_TAG_namespace) {
            if (auto *name = current.getShortName(); name) {
                namespaces.emplace_back(name);
            }
            else {
                namespaces.emplace_back("");
            }
        }
        else {
            break;
        }
    }
    if (!namespaces.empty()) {
        std::reverse(namespaces.begin(), namespaces.end());
        namespaces_ = std::move(namespaces);
    }

    // Get accessibility (if set)
    if (const auto attr = die.find(llvm::dwarf::DW_AT_accessibility); attr) {
        access_ = static_cast<llvm::dwarf::AccessAttribute>(attr->getAsUnsignedConstant().value());
    }
}

void Typedef::parse(Context &ctx, const llvm::DWARFDie &die)
{
    Entry::parse(ctx, die);
    if (auto *buffer = die.getShortName(); buffer) {
        std::string name = buffer;
        names_.emplace_back(name);
        const auto last = std::unique(names_.begin(), names_.end());
        names_.erase(last, names_.end());
    }
    if (type_.empty()) {
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
                    entry->parse(ctx, type);
                    type_ = entry->to_source();
                    type_.pop_back(); // remove the trailing semicolon
                    is_type_alias_ = false;
                }
            }
        }
    }
}

std::string Typedef::to_source() const
{
    if (is_type_alias_) {
        return "using " + names_[0] + " = " + type_ + ";";
    }

    return "typedef " + type_ + " " + llvm::join(names_.begin(), names_.end(), ",") + ";";
}

void Parameter::parse(Context &ctx, const llvm::DWARFDie &die)
{
    Entry::parse(ctx, die);
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

void Function::parse(Context &ctx, const llvm::DWARFDie &die)
{
    Entry::parse(ctx, die);
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto *buffer = die.getLinkageName(); buffer) {
        linkage_name_ = buffer;
    }
    if (auto type = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
        std::string return_type;
        type = type.resolveTypeUnitReference();
        llvm::raw_string_ostream os(return_type);
        llvm::DWARFTypePrinter type_printer(os);
        type_printer.appendQualifiedName(type);
        return_type_ = return_type;
    }
    else {
        return_type_ = "void";
    }
    if (die.find(llvm::dwarf::DW_AT_explicit)) {
        is_explicit_ = true;
    }
    // If the member function entry has been declared as deleted, then that entry has a
    // DW_AT_deleted attribute
    if (die.find(llvm::dwarf::DW_AT_defaulted)) {
        is_defaulted_ = true;
    }
    // If the member function has been declared as defaulted, then the entry has a
    // DW_AT_defaulted attribute whose integer constant value indicates whether, and
    // if so, how, that member is defaulted.
    if (die.find(llvm::dwarf::DW_AT_deleted)) {
        is_deleted_ = true;
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_virtuality); attr.has_value()) {
        virtuality_ = static_cast<llvm::dwarf::VirtualityAttribute>(attr->getAsUnsignedConstant().value());
    }
    if (auto parameters = parse_parameters(ctx, die); !parameters.empty()) {
        parameters_ = parameters;
    }
    if (auto template_params = parse_template_params(die); !template_params.empty()) {
        template_params_ = template_params;
    }
}

std::vector<Parameter> Function::parse_parameters(Context &ctx, const llvm::DWARFDie &die)
{
    std::vector<Parameter> result;
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
                auto &param = result.emplace_back();
                param.parse(ctx, child);
            }
            first_param = false;
            break;
        }
        default:
            break;
        }
    }
    return result;
}

std::string Function::to_source() const
{
    std::stringstream ss;
    if (!template_params_.empty()) {
        ss << "// " << template_params_ << "\n";
    }
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
    if (is_defaulted_) {
        ss << " = default";
    }
    if (is_deleted_) {
        ss << " = delete";
    }
    ss << ";";
    return ss.str();
}

void Enum::parse(Context &ctx, const llvm::DWARFDie &die)
{
    Entry::parse(ctx, die);
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
    if (enumerators_.empty()) {
        parse_children(die);
    }
    if (die.find(llvm::dwarf::DW_AT_enum_class)) {
        is_enum_class_ = true;
    }
}

void Enum::parse_children(const llvm::DWARFDie &die)
{
    for (const auto &child : die.children()) {
        if (child.getTag() != llvm::dwarf::DW_TAG_enumerator) {
            continue;
        }
        Enumerator enumerator;
        enumerator.name = child.getShortName();

        auto value = child.find(llvm::dwarf::DW_AT_const_value);
        if (value->getForm() == llvm::dwarf::DW_FORM_sdata) {
            enumerator.value = value->getAsSignedConstant().value();
        }
        else {
            enumerator.value = value->getAsUnsignedConstant().value();
        }
        enumerators_.push_back(enumerator);
    }
}

std::string Enum::to_source() const
{
    std::stringstream ss;
    ss << "enum ";
    if (is_enum_class_) {
        ss << "class ";
    }
    ss << name_;
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

void Field::parse(Context &ctx, const llvm::DWARFDie &die)
{
    Entry::parse(ctx, die);
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
                entry->parse(ctx, type);
                type_before_ = entry->to_source();
                type_before_.pop_back(); // remove the trailing semicolon
                type_after_.clear();
            }
        }
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_data_member_location); attr.has_value()) {
        member_location_ = attr->getAsUnsignedConstant().value();
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_bit_size); attr.has_value()) {
        bit_size_ = attr->getAsUnsignedConstant().value();
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_external); attr.has_value()) {
        is_static_ = true;
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_mutable); attr.has_value()) {
        is_mutable_ = true;
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
    if (is_static_) {
        ss << "static ";
    }
    if (is_mutable_) {
        ss << "mutable ";
    }
    ss << type_before_ << " " << name_ << type_after_;
    if (bit_size_.has_value()) {
        ss << " : " << bit_size_.value();
    }
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
        else if (endswith(type_before_, "char")) {
            auto val = default_value_.value();
            if ((val & ~0xFFu) == ~0xFFu) {
                val &= 0xFFu;
            }
            if (val < 127 && val >= 32) {
                ss << "'" << static_cast<char>(default_value_.value()) << "'";
            }
            else if (val < 256) {
                ss << "'\\x" << std::setw(2) << std::setfill('0') << std::hex << val << PRIx64 << "'";
            }
            else if (val <= 0xFFFF) {
                ss << "'\\u" << std::setw(4) << std::setfill('0') << std::hex << val << PRIx64 << "'";
            }
            else {
                ss << "'\\U" << std::setw(8) << std::setfill('0') << std::hex << val << PRIx64 << "'";
            }
        }
        else if (endswith(type_before_, "bool")) {
            ss << (default_value_.value() ? "true" : "false");
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

void StructLike::parse(Context &ctx, const llvm::DWARFDie &die)
{
    Entry::parse(ctx, die);
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_byte_size); attr) {
        byte_size = attr->getAsUnsignedConstant().value();
    }

    std::string decl_file = die.getDeclFile(llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath);
    std::replace(decl_file.begin(), decl_file.end(), '\\', '/');
    decl_file = posixpath::normpath(decl_file);

    std::unordered_set<std::size_t> skipped;
    for (auto child : die.children()) {
        child = child.resolveTypeUnitReference();
        if (child.getTag() != llvm::dwarf::DW_TAG_member) {
            continue;
        }
        if (auto type = child.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
            type = type.resolveTypeUnitReference();
            // skip the anonymous class defined by field
            if (!type.getShortName() && (type.getTag() == llvm::dwarf::DW_TAG_structure_type ||
                                         type.getTag() == llvm::dwarf::DW_TAG_union_type ||
                                         type.getTag() == llvm::dwarf::DW_TAG_enumeration_type ||
                                         type.getTag() == llvm::dwarf::DW_TAG_class_type)) {
                skipped.emplace(type.getOffset());
            }
        }
    }

    decltype(base_classes_) base_classes;
    decltype(members_) members;
    for (auto child : die.children()) {
        child = child.resolveTypeUnitReference();

        // Check if the parent has a declaration file (decl_file) set
        // - If the parent has decl_file set, but the child doesn't, we assume the child has the same declaration
        //   context as the parent.
        // - If the parent has decl_file set, but the child has a different one, this might indicate a template
        //   specialization (skip processing for now).
        if (!decl_file.empty()) {
            std::string child_decl_file =
                child.getDeclFile(llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath);
            std::replace(child_decl_file.begin(), child_decl_file.end(), '\\', '/');
            child_decl_file = posixpath::normpath(child_decl_file);
            if (!child_decl_file.empty() && child_decl_file != decl_file) {
                // TODO: handle template specialization
                continue;
            }
        }

        bool should_skip = skipped.find(child.getOffset()) != skipped.end();
        std::unique_ptr<Entry> entry;
        switch (child.getTag()) {
        case llvm::dwarf::DW_TAG_inheritance: {
            // An inheritance entry may have a DW_AT_accessibility attribute. If no
            // accessibility attribute is present, private access is assumed for an entry of a class
            // and public access is assumed for an entry of a struct, union or interface
            auto access = kind_ == Kind::Class ? llvm::dwarf::DW_ACCESS_private : llvm::dwarf::DW_ACCESS_public;
            if (auto attr = child.find(llvm::dwarf::DW_AT_accessibility)) {
                access = static_cast<llvm::dwarf::AccessAttribute>(attr->getAsUnsignedConstant().value());
            }

            // If the class referenced by the inheritance entry serves as a C++ virtual base class,
            // the inheritance entry has a DW_AT_virtuality attribute.
            auto virtuality = llvm::dwarf::VirtualityAttribute::DW_VIRTUALITY_none;
            if (auto attr = child.find(llvm::dwarf::DW_AT_virtuality)) {
                virtuality = static_cast<llvm::dwarf::VirtualityAttribute>(attr->getAsUnsignedConstant().value());
            }

            // An inheritance entry has a DW_AT_type attribute whose value is a reference to
            // the debugging information entry describing the class or interface from which the
            // parent class or structure of the inheritance entry is derived, extended or
            // implementing
            auto type = child.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type);
            type = type.resolveTypeUnitReference();
            std::string base_type;
            llvm::raw_string_ostream os(base_type);
            if (virtuality > llvm::dwarf::VirtualityAttribute::DW_VIRTUALITY_none) {
                os << "virtual ";
            }
            llvm::DWARFTypePrinter type_printer(os);
            type_printer.appendQualifiedName(type);
            base_classes.emplace_back(access, base_type);
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

        std::size_t decl_line = child.getDeclLine();
        if (entry && decl_line > 0 && members[decl_line].size() <= 16) {
            entry->parse(ctx, child);
            members[decl_line].emplace_back(std::move(entry));
        }
    }

    for (auto &[decl_line, member] : members) {
        // Remove consecutive (adjacent) duplicates
        auto last = std::unique(member.begin(), member.end(),
                                [](const auto &lhs, const auto &rhs) { return lhs->to_source() == rhs->to_source(); });
        member.erase(last, member.end());

        if (auto it = members_.find(decl_line); it != members_.end()) {
            it->second = std::move(member);
        }
        else {
            members_[decl_line] = std::move(member);
        }
    }

    if (!base_classes.empty()) {
        base_classes_ = base_classes;
    }

    if (auto template_params = parse_template_params(die); !template_params.empty()) {
        template_params_ = template_params;
    }
}

std::string StructLike::to_source() const
{
    auto default_access = kind_ == Kind::Class ? llvm::dwarf::DW_ACCESS_private : llvm::dwarf::DW_ACCESS_public;

    std::stringstream ss;
    if (!template_params_.empty()) {
        ss << "// " << template_params_ << "\n";
    }
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
    bool first_line = true;
    for (const auto &[decl_line, member] : members_) {
        for (const auto &m : member) {
            if (auto current_access = m->access().value_or(default_access); current_access != last_access) {
                if (!first_line) {
                    ss << "\n";
                }
                ss << to_string(current_access) << ":\n";
                last_access = current_access;
            }

            std::stringstream is(m->to_source());
            std::string line;
            while (std::getline(is, line)) {
                ss << "    " << line << "\n";
            }
        }
        first_line = false;
    }
    ss << "};";
    if (!name_.empty() && byte_size.has_value()) {
        ss << "\n";
        ss << "static_assert(sizeof(" << name_ << ") == " << byte_size.value() << ");";
    }
    return ss.str();
}

} // namespace dwarf2cpp
