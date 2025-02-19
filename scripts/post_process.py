import os
import shutil
import subprocess
from pathlib import Path
import argparse
import glob
from tqdm.contrib.concurrent import process_map
import re


def process_file(args):
    """
    Process a single file. Replace this with your file handling logic.
    """
    file_path, input_path, output_path = args

    with open(file_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Replace every occurrence of 'std::__1::' with 'std::'
    content = content.replace("std::__1::", "std::")
    content = content.replace("std::__ndk1::", "std::")
    content = content.replace(
        "std::basic_string<char, std::char_traits<char>, std::allocator<char> >",
        "std::string",
    )
    content = content.replace(
        "std::basic_string_view<char, std::char_traits<char> >",
        "std::string_view",
    )
    content = content.replace(
        "std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<long long, std::ratio<1L, 1000000000L> > >",
        "std::chrono::steady_clock::time_point",
    )
    content = re.sub(r"\(lambda at .+?\)", "Lambda", content)

    pattern_repl = [
        # std::unique_ptr
        (
            r"std::unique_ptr<(.+?),\s*std::default_delete<\1\s*>\s*>",
            r"std::unique_ptr<\1>",
        ),
        # std::vector
        (r"std::vector<(.+?),\s*std::allocator<\1\s*>\s*>", r"std::vector<\1>"),
        # std::list
        (r"std::list<(.+?),\s*std::allocator<\1\s*>\s*>", r"std::list<\1>"),
        # std::deque
        (r"std::deque<(.+?),\s*std::allocator<\1\s*>\s*>", r"std::deque<\1>"),
        # std::queue
        (r"std::queue<(.+?),\s*std::deque<\1\s*>\s*>", r"std::queue<\1>"),
        # std::unordered_map
        (
            r"std::unordered_map<(.+?),\s*(.+?),\s*"
            r"std::hash<\1\s*>,\s*"
            r"std::equal_to<\1\s*>,\s*"
            r"std::allocator<std::pair<const \1,\s*\2\s*>\s*>\s*>",
            r"std::unordered_map<\1, \2>",
        ),
        # std::unordered_set
        (
            r"std::unordered_set<(.+?),\s*std::hash<\1\s*>,\s*"
            r"std::equal_to<\1\s*>,\s*std::allocator<\1\s*>\s*>",
            r"std::unordered_set<\1>",
        ),
        # std::map
        (
            r"std::map<(.+?),\s*(.+?),\s*std::less<\1\s*>,\s*"
            r"std::allocator<std::pair<const \1,\s*\2\s*>\s*>\s*>",
            r"std::map<\1, \2>",
        ),
        # std::set
        (
            r"std::set<(.+?),\s*std::less<\1\s*>,\s*std::allocator<\1\s*>\s*>",
            r"std::set<\1>",
        ),
        # gsl::span
        (r"gsl::span<(.+),\s*\d+UL>", r"gsl::span<\1>"),
        # glm::vec
        (r"glm::vec<(\d),\s*float,\s*\(glm::qualifier\)0>", r"glm::vec\1"),
        # glm::mat
        (r"glm::mat<(\d),\s*(\d),\s*float,\s*\(glm::qualifier\)0>", r"glm::mat\1x\2"),
    ]

    while True:
        output = content
        for pattern, repl in pattern_repl:
            output = re.sub(pattern, repl, output)

        if output == content:
            break

        content = output

    # Calculate relative path
    relative_path = file_path.relative_to(input_path)

    # Construct the full output path
    result_file = output_path / relative_path

    # Ensure the directory of the result file exists
    result_file.parent.mkdir(parents=True, exist_ok=True)

    # Example file processing logic
    with result_file.open("w") as f:
        f.write(content)

    # Call clang-format on the file
    clang_format_file = os.path.join(os.path.dirname(__file__), ".clang-format")
    try:
        subprocess.run(
            ["clang-format", "-i", f"-style=file:{clang_format_file}", result_file],
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        print("Error occurred: ", e.stderr)


def main():
    # Set up argument parsing
    parser = argparse.ArgumentParser(
        description="Process files in a specified directory using multiprocessing."
    )
    parser.add_argument(
        "--input",
        "-i",
        required=True,
        type=str,
        help="Path to the input directory containing the files to process.",
    )
    parser.add_argument(
        "--output",
        "-o",
        required=True,
        type=str,
        help="Path to the output directory to store results.",
    )
    args = parser.parse_args()

    # Ensure the output directory exists
    output_path = Path(args.output)
    if output_path.exists():
        shutil.rmtree(output_path)
    else:
        output_path.mkdir(parents=True, exist_ok=True)

    # Find all files in the input directory
    input_files = glob.glob(str(Path(args.input) / "**" / "*.*"), recursive=True)

    if not input_files:
        print("No files found in the input directory.")
        return

    # Create tuples of file path, input path, and output path for processing
    files_with_paths = [
        (Path(file), Path(args.input), Path(args.output))
        for file in input_files
        if os.path.isfile(file)
    ]

    process_map(process_file, files_with_paths, desc="Processing files", chunksize=1)
    print("Processing complete!")


if __name__ == "__main__":
    main()
