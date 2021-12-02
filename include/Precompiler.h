/*
 * Header for the pre-compiler allowing programmatic access to the LLVM/SPIRV-LLVM binaries for converting OpenCL C
 * source code to LLVM-IR/SPIR-V
 *
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#ifndef PRECOMPILER_H
#define PRECOMPILER_H

#include "Optional.h"
#include "config.h"

#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vc4c
{
    /*
     * The type of input-code determined for the input
     */
    enum class SourceType
    {
        /*
         * Type was not (yet) determined
         */
        UNKNOWN = 0,
        /*
         * OpenCL C source-code
         */
        OPENCL_C = 1,
        /*
         * LLVM IR in textual representation
         */
        LLVM_IR_TEXT = 2,
        /*
         * LLVM IR bit-code
         */
        LLVM_IR_BIN = 3,
        /*
         * SPIR-V in binary representation
         */
        SPIRV_BIN = 4,
        /*
         * SPIR-V in textual representation
         */
        SPIRV_TEXT = 5,
        /*
         * generated machine code in hexadecimal representation
         */
        QPUASM_HEX = 6,
        /*
         * generated machine code in binary representation
         */
        QPUASM_BIN = 7
    };

    bool isSupportedByFrontend(SourceType inputType, Frontend frontend);

    /*
     * RAII object to manage a temporary file.
     *
     * This class guarantees the temporary file to be deleted even if the compilation is cancelled by throwing an
     * exception.
     */
    class TemporaryFile : private NonCopyable
    {
    public:
        /*
         * Creates and manages a new empty temporary file
         */
        explicit TemporaryFile(const std::string& fileTemplate = "/tmp/vc4c-XXXXXX", bool hasStaticLifetime = false);
        /*
         * Creates and manages a new temporary file with fixed file-name and initial content
         */
        explicit TemporaryFile(const std::string& fileName, std::istream& data, bool hasStaticLifetime = false);
        /*
         * Creates and manages a new temporary file with fixed file-name and initial content
         */
        explicit TemporaryFile(const std::string& fileName, const std::vector<char>& data);
        TemporaryFile(const TemporaryFile&) = delete;
        TemporaryFile(TemporaryFile&& other) noexcept;
        ~TemporaryFile();

        TemporaryFile& operator=(const TemporaryFile&) = delete;
        TemporaryFile& operator=(TemporaryFile&&) noexcept = delete;

        void openOutputStream(std::unique_ptr<std::ostream>& ptr) const;
        void openInputStream(std::unique_ptr<std::istream>& ptr) const;

        const std::string fileName;

    private:
        // this temporary file lives as long as the program lives
        bool isStaticTemporary;
    };

    // forward reference to the actual compilation data implementation
    class CompilationDataPrivate;

    /**
     * Handle for compilation input and output data.
     *
     * Abstracts whether the data is located in a (temporary) file or in-memory as well as the actual in-memory storage
     * format.
     */
    class CompilationData
    {
    public:
        /**
         * Creates an empty object. This should e.g. be used for compilation output.
         */
        explicit CompilationData();

        /**
         * Sets the given type and file path as data. This should e.g. be used to compile from files or to compile into
         * a specific file.
         */
        CompilationData(const std::string& fileName, SourceType type);

        /**
         * Sets the given type and raw data. This should e.g. be used to compile from an on-memory data buffer.
         */
        CompilationData(std::istream& rawData, SourceType type);

        explicit CompilationData(std::shared_ptr<CompilationDataPrivate>&& data);
        ~CompilationData();

        SourceType getType() const noexcept;
        Optional<std::string> getFilePath() const;
        std::vector<uint8_t> getRawData() const;

        void readInto(std::ostream& out) const;
        void writeFrom(std::istream& in);

        operator bool() const noexcept;
        const std::shared_ptr<CompilationDataPrivate>& inner() const noexcept;

    private:
        std::shared_ptr<CompilationDataPrivate> data;
    };

    /*
     * The pre-compiler manages and executes the conversion of the input from a various of supported types to a type
     * which can be read by one of the configured compiler front-ends.
     */
    class Precompiler
    {
    public:
        /**
         * Helper-function to easily pre-compile a single input with the given configuration into the given output.
         *
         * \param input The input stream
         * \param output The output-stream
         * \param config The configuration to use for compilation
         * \param options Specify additional compiler-options to pass onto the pre-compiler
         * \param inputFile Can be used by the compiler to speed-up compilation (e.g. by running the pre-compiler with
         * these files instead of needing to write input to a temporary file)
         * \param outputFile The optional output-file to write the pre-compiled code into. If this is specified, the
         * code is compiled into the file, otherwise the output stream is filled with the compiled code
         */
        [[deprecated]] static void precompile(std::istream& input, std::unique_ptr<std::istream>& output,
            Configuration config = {}, const std::string& options = "", const Optional<std::string>& inputFile = {},
            const Optional<std::string>& outputFile = {});
        static CompilationData precompile(
            const CompilationData& input, Configuration config = {}, const std::string& options = "");
        static CompilationData precompile(const CompilationData& input, SourceType outputType,
            Configuration config = {}, const std::string& options = "");

        /*
         * Determines the type of code stored in the given stream.
         *
         * NOTE: This function reads from the stream but resets the cursor back to the beginning.
         */
        static SourceType getSourceType(std::istream& stream);

        /*
         * Links multiple source-code files using a linker provided by the pre-compilers.
         *
         * Returns the SourceType of the linked module
         */
        [[deprecated]] static SourceType linkSourceCode(
            const std::unordered_map<std::istream*, Optional<std::string>>& inputs, std::ostream& output,
            bool includeStandardLibrary = false);
        static CompilationData linkSourceCode(
            const std::vector<CompilationData>& inputs, bool includeStandardLibrary = false);

        /*
         * Returns whether there is a linker available that can link the given input modules
         */
        static bool isLinkerAvailable(const std::unordered_map<std::istream*, Optional<std::string>>& inputs);
        /*
         * Returns whether a linker is available at all in the compiler
         */
        static bool isLinkerAvailable();
    };
} // namespace vc4c

#endif /* PRECOMPILER_H */
