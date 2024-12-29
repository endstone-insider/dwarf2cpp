#include "context.h"

#include <filesystem>

#include "posixpath.hpp"

namespace fs = std::filesystem;

namespace dwarf2cpp {

void Context::update(const llvm::DWARFDie &unit_die)
{
    if (!unit_die.isValid()) {
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

    parse_children(unit_die);
}

std::string Context::base_dir() const
{
    return base_dir_;
}

const std::unordered_map<std::string, SourceFile> &Context::source_files() const
{
    return source_files_;
}

Entry *Context::get(const llvm::DWARFDie &index)
{
    const auto die = index.resolveTypeUnitReference();
    auto &map = die.getDwarfUnit()->isTypeUnit() ? type_entries_ : info_entries_;

    if (const auto it = map.find(die.getOffset()); it == map.end()) {
        if (!die.find(llvm::dwarf::DW_AT_name) || !die.find(llvm::dwarf::DW_AT_decl_file) ||
            !die.find(llvm::dwarf::DW_AT_decl_line)) {
            return nullptr;
        }

        // Parse the DIE if we haven't and add the entry to the corresponding source file
        std::unique_ptr<Entry> entry;
        switch (die.getTag()) {
        case llvm::dwarf::DW_TAG_class_type:
            entry = std::make_unique<StructLike>(StructLike::Kind::Class);
            if (auto *buffer = die.getShortName(); buffer) {
                if (std::string(buffer) == "AABB") {
                    spdlog::warn("AABB at {}", die.getOffset());
                }
            }
            break;
        case llvm::dwarf::DW_TAG_enumeration_type:
            entry = std::make_unique<Enum>();
            break;
        case llvm::dwarf::DW_TAG_structure_type:
            entry = std::make_unique<StructLike>(StructLike::Kind::Struct);
            break;
        case llvm::dwarf::DW_TAG_typedef:
            entry = std::make_unique<Typedef>();
            break;
        case llvm::dwarf::DW_TAG_union_type:
            entry = std::make_unique<StructLike>(StructLike::Kind::Union);
            break;
        case llvm::dwarf::DW_TAG_subprogram:
            entry = std::make_unique<Function>(false);
            break;
        default:
            return nullptr;
        }

        entry->parse(die);

        auto decl_file = die.getDeclFile(llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath);
        std::replace(decl_file.begin(), decl_file.end(), '\\', '/');
        decl_file = posixpath::normpath(decl_file);
        auto &sf = source_files_[decl_file];
        sf.add(die.getDeclLine(), entry.get());
        map[die.getOffset()] = std::move(entry);
    }

    return map.at(die.getOffset()).get();
}

void Context::parse_children(const llvm::DWARFDie &die) // NOLINT(*-no-recursion)
{
    for (const auto &child : die.children()) {
        if (child.getTag() == llvm::dwarf::DW_TAG_namespace) {
            parse_children(child);
            continue;
        }

        if (auto *entry = get(child); entry && child.resolveTypeUnitReference() != child) {
            entry->parse(child);
        }
    }
}

} // namespace dwarf2cpp
