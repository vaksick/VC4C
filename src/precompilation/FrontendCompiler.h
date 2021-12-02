/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#ifndef VC4C_FRONTEND_COMPILER
#define VC4C_FRONTEND_COMPILER

#include "CompilationData.h"
#include "CompilationError.h"
#include "Optional.h"
#include "Precompiler.h"
#include "tool_paths.h"

#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

namespace vc4c
{
    namespace precompilation
    {
        template <SourceType Type>
        struct SourceTypeToRepresentation
        {
            using type = std::string;
        };

        template <>
        struct SourceTypeToRepresentation<SourceType::LLVM_IR_BIN>
        {
            using type = llvm::Module;
        };

        template <>
        struct SourceTypeToRepresentation<SourceType::SPIRV_BIN>
        {
            using type = std::vector<uint32_t>;
        };

        template <SourceType Type>
        class PrecompilationSource;

        template <SourceType Type>
        class PrecompilationResult : private NonCopyable
        {
        public:
            explicit PrecompilationResult() : data(nullptr) {}

            explicit PrecompilationResult(std::unique_ptr<TypedCompilationData<Type>>&& data) : data(std::move(data))
            {
                if(!this->data)
                    throw CompilationError(CompilationStep::PRECOMPILATION, "Result has no data!");
            }

            explicit PrecompilationResult(const std::string& file) :
                PrecompilationResult(std::make_unique<FileCompilationData<Type>>(file))
            {
            }

            explicit PrecompilationResult(TemporaryFile&& file) :
                PrecompilationResult(std::make_unique<TemporaryFileCompilationData<Type>>(std::move(file)))
            {
            }

            PrecompilationResult(const PrecompilationResult&) = delete;
            PrecompilationResult(PrecompilationResult&&) noexcept = default;

            PrecompilationResult& operator=(const PrecompilationResult&) = delete;
            PrecompilationResult& operator=(PrecompilationResult&&) noexcept = default;

            std::string getOutputPath(const std::string& defaultPath) const noexcept
            {
                return getFilePath().value_or(defaultPath);
            }

            Optional<std::string> getFilePath() const noexcept
            {
                return data ? data->getFilePath() : Optional<std::string>{};
            }

            std::unique_ptr<std::ostream> getBufferWriter(bool force = false)
            {
                if(!force && dynamic_cast<const FileCompilationData<Type>*>(data.get()))
                    // prefer using the file instead of writing via a file stream
                    return nullptr;
                return data ? data->writeStream() : nullptr;
            }

            TypedCompilationData<Type>& inner() noexcept
            {
                return *data;
            }

            std::unique_ptr<TypedCompilationData<Type>> release() noexcept
            {
                return std::move(data);
            }

            std::string to_string() const
            {
                return data ? data->to_string() : "(empty)";
            }

            operator bool() const noexcept
            {
                return data != nullptr;
            }

        protected:
            std::unique_ptr<TypedCompilationData<Type>> data;

            friend class PrecompilationSource<Type>;
        };

        template <SourceType Type>
        class PrecompilationSource : private NonCopyable
        {
        public:
            explicit PrecompilationSource(std::shared_ptr<TypedCompilationData<Type>>&& data) : data(std::move(data))
            {
                if(!this->data)
                    throw CompilationError(CompilationStep::PRECOMPILATION, "Source has no data!");
            }

            explicit PrecompilationSource(std::istream& s) :
                PrecompilationSource(std::make_unique<RawCompilationData<Type>>(s))
            {
            }

            explicit PrecompilationSource(const std::string& file) :
                PrecompilationSource(std::make_unique<FileCompilationData<Type>>(file))
            {
            }

            explicit PrecompilationSource(PrecompilationResult<Type>&& res) : PrecompilationSource(std::move(res.data))
            {
            }

            PrecompilationSource(const PrecompilationSource&) = delete;
            PrecompilationSource(PrecompilationSource&&) noexcept = default;

            PrecompilationSource& operator=(const PrecompilationSource&) = delete;
            PrecompilationSource& operator=(PrecompilationSource&&) noexcept = default;

            std::string getInputPath(const std::string& defaultPath) const noexcept
            {
                return getFilePath().value_or(defaultPath);
            }

            Optional<std::string> getFilePath() const noexcept
            {
                return data->getFilePath();
            }

            std::unique_ptr<std::istream> getBufferReader(bool force = false) const
            {
                if(!force && dynamic_cast<const FileCompilationData<Type>*>(data.get()))
                    // prefer using the file instead of reading it into memory here
                    return nullptr;
                return data->readStream();
            }

            const TypedCompilationData<Type>& inner() const noexcept
            {
                return *data;
            }

            std::string to_string() const
            {
                return data ? data->to_string() : "(empty)";
            }

        private:
            std::shared_ptr<TypedCompilationData<Type>> data;
        };

        template <SourceType InType, SourceType OutType>
        using PrecompilationStep = std::function<PrecompilationResult<OutType>(
            PrecompilationSource<InType>&&, const std::string&, PrecompilationResult<OutType>&&)>;

        template <SourceType Type>
        using LinkStep = std::function<PrecompilationResult<Type>(
            std::vector<PrecompilationSource<Type>>&&, const std::string&, PrecompilationResult<Type>&&)>;

        using OpenCLSource = PrecompilationSource<SourceType::OPENCL_C>;
        using LLVMIRSource = PrecompilationSource<SourceType::LLVM_IR_BIN>;
        using LLVMIRTextSource = PrecompilationSource<SourceType::LLVM_IR_TEXT>;
        using SPIRVSource = PrecompilationSource<SourceType::SPIRV_BIN>;
        using SPIRVTextSource = PrecompilationSource<SourceType::SPIRV_TEXT>;
        using LLVMIRResult = PrecompilationResult<SourceType::LLVM_IR_BIN>;
        using LLVMIRTextResult = PrecompilationResult<SourceType::LLVM_IR_TEXT>;
        using SPIRVResult = PrecompilationResult<SourceType::SPIRV_BIN>;
        using SPIRVTextResult = PrecompilationResult<SourceType::SPIRV_TEXT>;

        LLVMIRResult compileOpenCLWithPCH(
            OpenCLSource&& source, const std::string& userOptions, LLVMIRResult&& desiredOutput = LLVMIRResult{});
        LLVMIRResult compileOpenCLWithDefaultHeader(
            OpenCLSource&& source, const std::string& userOptions, LLVMIRResult&& desiredOutput = LLVMIRResult{});
        LLVMIRResult linkInStdlibModule(
            LLVMIRSource&& source, const std::string& userOptions, LLVMIRResult&& desiredOutput = LLVMIRResult{});
        LLVMIRTextResult compileOpenCLToLLVMText(OpenCLSource&& source, const std::string& userOptions,
            LLVMIRTextResult&& desiredOutput = LLVMIRTextResult{});
        SPIRVResult compileLLVMToSPIRV(
            LLVMIRSource&& source, const std::string& userOptions, SPIRVResult&& desiredOutput = SPIRVResult{});
        SPIRVResult assembleSPIRV(
            SPIRVTextSource&& source, const std::string& userOptions, SPIRVResult&& desiredOutput = SPIRVResult{});
        SPIRVTextResult compileLLVMToSPIRVText(
            LLVMIRSource&& source, const std::string& userOptions, SPIRVTextResult&& desiredOutput = SPIRVTextResult{});
        SPIRVTextResult disassembleSPIRV(
            SPIRVSource&& source, const std::string& userOptions, SPIRVTextResult&& desiredOutput = SPIRVTextResult{});
        LLVMIRTextResult disassembleLLVM(LLVMIRSource&& source, const std::string& userOptions,
            LLVMIRTextResult&& desiredOutput = LLVMIRTextResult{});
        LLVMIRResult assembleLLVM(
            LLVMIRTextSource&& source, const std::string& userOptions, LLVMIRResult&& desiredOutput = LLVMIRResult{});
        LLVMIRResult linkLLVMModules(std::vector<LLVMIRSource>&& sources, const std::string& userOptions,
            LLVMIRResult&& desiredOutput = LLVMIRResult{});
        SPIRVResult linkSPIRVModules(std::vector<SPIRVSource>&& sources, const std::string& userOptions,
            SPIRVResult&& desiredOutput = SPIRVResult{});
        LLVMIRResult optimizeLLVMIR(
            LLVMIRSource&& source, const std::string& userOptions, LLVMIRResult&& desiredOutput = LLVMIRResult{});
        LLVMIRResult compileOpenCLAndLinkModule(
            OpenCLSource&& source, const std::string& userOptions, LLVMIRResult&& desiredOutput = LLVMIRResult{});
        SPIRVResult compileOpenCLToSPIRV(
            OpenCLSource&& source, const std::string& userOptions, SPIRVResult&& desiredOutput = SPIRVResult{});
        SPIRVTextResult compileOpenCLToSPIRVText(
            OpenCLSource&& source, const std::string& userOptions, SPIRVTextResult&& desiredOutput = SPIRVTextResult{});

        /*
         * General version of compiling OpenCL C source to to LLVM binary module with the standard-library included.
         * Depending on the compilation options, the standard-library PCH is included or the standard-library module is
         * linked in.
         */
        LLVMIRResult compileOpenCLToLLVMIR(
            OpenCLSource&& source, const std::string& userOptions, LLVMIRResult&& desiredOutput = LLVMIRResult{});

        /**
         * Tries to find the location of the tool executable with the given name.
         *
         * First looks up the preferredPath and if the tool does not exist at that path (or if the path is not set),
         * tries to look up the tool in the $PATH environment variable.
         */
        Optional<std::string> findToolLocation(
            const std::string& name, const std::string& preferredPath = "", bool skipPathLookup = false);

        /*
         * Container for the paths used to look up the VC4CL OpenCL C standard-library implementation files
         */
        struct StdlibFiles
        {
            // The path to the defines.h header file, empty if not found. This is always required
            std::string configurationHeader;
            // The path to the pre-compiled header (PCH), empty if not found. Only required for SPIR-V front-end
            std::string precompiledHeader;
            // The path to the pre-compiled LLVM module, empty if not found. Only required for LLVM module front-end
            std::string llvmModule;
            // The path to the pre-compiled SPIR-V module, empty if not found. Only required for SPIR-V front-end
            std::string spirvModule;
        };

        /*
         * Determines and returns the paths to the VC4CL OpenCL C standard library files to be used for compilations
         *
         * The optional parameter specifies additional folder to look up the required files. If it is not given, only
         * the default locations will be searched.
         *
         * NOTE: The locations of the files are cached, therefore only the first call has any effect of specifying the
         * locations.
         */
        const StdlibFiles& findStandardLibraryFiles(const std::vector<std::string>& additionalFolders = {});

        /*
         * Pre-compiles the given VC4CL OpenCL C standard-library file (the VC4CLStdLib.h header) into a PCH and an LLVM
         * module and stores them in the given output folder.
         */
        void precompileStandardLibraryFiles(const std::string& sourceFile, const std::string& destinationFolder);
    } /* namespace precompilation */
} /* namespace vc4c */

#endif /* VC4C_FRONTEND_COMPILER */
