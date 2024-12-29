#include "context.h"

#include <filesystem>

#include "posixpath.hpp"

namespace fs = std::filesystem;

namespace dwarf2cpp {

void Context::update(const llvm::DWARFDie &unit_die)
{
    if (unit_die.isValid()) {
        return;
    }
    auto name = unit_die.getShortName();
    auto comp_dir = unit_die.getDwarfUnit()->getCompilationDir();
    if (!name || !comp_dir) {
        return;
    }

    auto base_dir = posixpath::commonpath({name, comp_dir});
    if (base_dir_.empty()) {
        base_dir_ = base_dir;
    }
    base_dir_ = posixpath::commonpath({base_dir, base_dir_});

    std::vector<std::string> namespaces;
    parse_children(unit_die, namespaces);
}

std::string Context::base_dir() const
{
    return base_dir_;
}

const std::unordered_map<std::string, SourceFile> &Context::source_files() const
{
    return source_files_;
}

void Context::parse_children(const llvm::DWARFDie &die, std::vector<std::string> &namespaces) // NOLINT(*-no-recursion)
{
    for (const auto &child : die.children()) {
        const auto tag = child.getTag();

        if (tag == llvm::dwarf::DW_TAG_namespace) {
            std::string name;
            if (auto *buffer = child.getShortName(); buffer) {
                name = buffer;
            }
            namespaces.push_back(name);
            parse_children(child, namespaces);
            namespaces.pop_back();
            continue;
        }

        auto child_type = child.resolveTypeUnitReference();
        if (!child_type.find(llvm::dwarf::DW_AT_name) || !child_type.find(llvm::dwarf::DW_AT_decl_file) ||
            !child_type.find(llvm::dwarf::DW_AT_decl_line)) {
            continue;
        }

        auto decl_line = child_type.getDeclLine();
        auto decl_file = child_type.getDeclFile(llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath);
        std::replace(decl_file.begin(), decl_file.end(), '\\', '/');
        decl_file = posixpath::normpath(decl_file);

        if (tag == llvm::dwarf::DW_TAG_subprogram) {
            auto entry = std::make_unique<Function>(false, namespaces);
            entry->parse(child_type);
            files[decl_file].add(decl_line, std::move(entry));
            continue;
        }

        if (tag == llvm::dwarf::DW_TAG_typedef) {
            auto entry = std::make_unique<Typedef>(namespaces);
            entry->parse(child_type);
            files[decl_file].add(decl_line, std::move(entry));
            continue;
        }

        Entry *existing_entry = nullptr;
        if (auto it = files.find(decl_file); it != files.end()) {
            existing_entry = files[decl_file].get(decl_line);
        }

        if (!existing_entry) {
            std::unique_ptr<dwarf2cpp::Entry> entry;
            switch (tag) {
            case llvm::dwarf::DW_TAG_class_type: {
                entry = std::make_unique<dwarf2cpp::StructLike>(dwarf2cpp::StructLike::Kind::Class, namespaces);
                break;
            }
            case llvm::dwarf::DW_TAG_enumeration_type: {
                entry = std::make_unique<dwarf2cpp::Enum>(namespaces);
                break;
            }
            case llvm::dwarf::DW_TAG_structure_type: {
                entry = std::make_unique<dwarf2cpp::StructLike>(dwarf2cpp::StructLike::Kind::Struct, namespaces);
                break;
            }
            case llvm::dwarf::DW_TAG_union_type: {
                entry = std::make_unique<dwarf2cpp::StructLike>(dwarf2cpp::StructLike::Kind::Union, namespaces);
                break;
            }
            default:
                break;
            }

            if (entry) {
                files[decl_file].add(decl_line, std::move(entry));
                existing_entry = files[decl_file].get(decl_line);
            }
        }

        if (existing_entry) {
            existing_entry->parse(child_type);
            if (child_type != child) {
                existing_entry->parse(child);
            }
        }
    }
}

} // namespace dwarf2cpp
