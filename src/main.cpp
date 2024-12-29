#include <filesystem>
#include <fstream>

#include <argparse/argparse.hpp>
#include <spdlog/fmt/std.h>
#include <spdlog/spdlog.h>

#include "context.h"
#include "entry.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "posixpath.hpp"
#include "source_file.h"

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    // Initialize the argument parser
    argparse::ArgumentParser program("parse_dwarf");

    program.add_argument("file_path").help("Path to the DWARF debug file").required();

    // Parse the arguments
    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    // Get the file path from the parsed arguments
    auto file_path = program.get<std::string>("file_path");

    // Create a memory buffer from the file
    spdlog::info("Reading file: {}", file_path);
    auto buffer_or_err = llvm::MemoryBuffer::getFile(file_path);
    if (!buffer_or_err) {
        spdlog::error("Failed to create memory buffer: {}", buffer_or_err.getError().message());
        return 1;
    }

    // Parse the object file
    spdlog::info("Parsing object file...");
    const auto &buffer = *buffer_or_err;
    auto obj_or_err = llvm::object::ObjectFile::createObjectFile(buffer->getMemBufferRef());
    if (!obj_or_err) {
        spdlog::error("Failed to parse object file: {}", toString(obj_or_err.takeError()));
        return 1;
    }

    // Create a DWARF context
    spdlog::info("Creating DWARF context...");
    const auto &obj = *obj_or_err;
    auto dwarf_context = llvm::DWARFContext::create(*obj);

    // Iterate over the compile units (CUs)
    spdlog::info("Iterating over compile units...");
    auto num_compile_units = dwarf_context->getNumCompileUnits();
    auto i = 0;
    dwarf2cpp::Context ctx;
    for (const auto &cu : dwarf_context->compile_units()) {
        auto cu_die = cu->getUnitDIE(false);
        if (!cu_die.isValid()) {
            spdlog::warn("Invalid compile unit found.");
            continue;
        }

        auto name = cu_die.getShortName();
        spdlog::info("[{}/{}] Parsing compile unit {}", ++i, num_compile_units, name);

        ctx.update(cu_die);
    }

    spdlog::info("Build dir: {}", ctx.base_dir());
    spdlog::info("Finished parsing DWARF debug file.");

    spdlog::info("Writing header files to the output folder");
    for (const auto &[filename, content] : ctx.source_files()) {
        if (posixpath::commonpath({filename, ctx.base_dir()}) != ctx.base_dir()) {
            // skip non-project files, e.g. standard lib
            continue;
        }

        auto relpath = posixpath::relpath(filename, ctx.base_dir());
        fs::path output_file = fs::path("output") / relpath;
        create_directories(output_file.parent_path());

        spdlog::info("Writing to {}", output_file);
        std::ofstream out(output_file.string());
        out << content;
    }

    return 0;
}
