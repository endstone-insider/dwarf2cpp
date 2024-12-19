#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
    // Initialize the argument parser
    argparse::ArgumentParser program("parse_dwarf");

    program.add_argument("file_path")
            .help("Path to the DWARF debug file")
            .required();

    // Parse the arguments
    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
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
    for (const auto &cu: dwarf_context->compile_units()) {
        auto cu_die = cu->getUnitDIE(false);
        if (cu_die.isValid()) {
            spdlog::info("[{}/{}] Parsing compile unit {}", ++i, num_compile_units,
                         cu_die.getName(llvm::DINameKind::ShortName));
        } else {
            spdlog::warn("Invalid compile unit found.");
        }
    }

    spdlog::info("Finished parsing DWARF debug file.");
    return 0;
}
