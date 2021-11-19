/*
 * Majority of this code is copied from slang/driver
 */
#include <fstream>
#include <iostream>

#include "slang/compilation/Compilation.h"
#include "slang/diagnostics/DeclarationsDiags.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/LookupDiags.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/symbols/ASTSerializer.h"
#include "slang/symbols/CompilationUnitSymbols.h"
#include "slang/symbols/InstanceSymbols.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/Json.h"
#include "slang/text/SourceManager.h"
#include "slang/util/CommandLine.h"
#include "slang/util/OS.h"
#include "slang/util/String.h"
#include "slang/util/Version.h"
#include "../src/builder.hh"

using namespace slang;

namespace xsim {
static constexpr auto warningColor = fmt::terminal_color::bright_yellow;
static constexpr auto errorColor = fmt::terminal_color::bright_red;

SourceBuffer readSource(SourceManager& sourceManager, const std::string& file) {
    SourceBuffer buffer = sourceManager.readSource(widen(file));
    if (!buffer) {
        OS::printE(fg(errorColor), "error: ");
        OS::printE("no such file or directory: '{}'\n", file);
    }
    return buffer;
}

bool loadAllSources(Compilation& compilation, SourceManager& sourceManager,
                    const std::vector<SourceBuffer>& buffers, const Bag& options, bool singleUnit,
                    bool onlyLint, const std::vector<std::string>& libraryFiles,
                    const std::vector<std::string>& libDirs,
                    const std::vector<std::string>& libExts) {
    if (singleUnit) {
        auto tree = SyntaxTree::fromBuffers(buffers, sourceManager, options);
        if (onlyLint) tree->isLibrary = true;

        compilation.addSyntaxTree(tree);
    } else {
        for (const SourceBuffer& buffer : buffers) {
            auto tree = SyntaxTree::fromBuffer(buffer, sourceManager, options);
            if (onlyLint) tree->isLibrary = true;

            compilation.addSyntaxTree(tree);
        }
    }

    bool ok = true;
    for (auto& file : libraryFiles) {
        SourceBuffer buffer = readSource(sourceManager, file);
        if (!buffer) {
            ok = false;
            continue;
        }

        auto tree = SyntaxTree::fromBuffer(buffer, sourceManager, options);
        tree->isLibrary = true;
        compilation.addSyntaxTree(tree);
    }

    if (libDirs.empty()) return ok;

    std::vector<fs::path> directories;
    directories.reserve(libDirs.size());
    for (auto& dir : libDirs) directories.emplace_back(widen(dir));

    flat_hash_set<string_view> uniqueExtensions;
    uniqueExtensions.emplace(".v"sv);
    uniqueExtensions.emplace(".sv"sv);
    for (auto& ext : libExts) uniqueExtensions.emplace(ext);

    std::vector<fs::path> extensions;
    for (auto ext : uniqueExtensions) extensions.emplace_back(widen(ext));

    // If library directories are specified, see if we have any unknown instantiations
    // or package names for which we should search for additional source files to load.
    flat_hash_set<string_view> knownNames;
    auto addKnownNames = [&](const std::shared_ptr<SyntaxTree>& tree) {
        auto& meta = tree->getMetadata();
        for (auto& [n, _] : meta.nodeMap) {
            auto decl = &n->as<ModuleDeclarationSyntax>();
            string_view name = decl->header->name.valueText();
            if (!name.empty()) knownNames.emplace(name);
        }

        for (auto classDecl : meta.classDecls) {
            string_view name = classDecl->name.valueText();
            if (!name.empty()) knownNames.emplace(name);
        }
    };

    auto findMissingNames = [&](const std::shared_ptr<SyntaxTree>& tree,
                                flat_hash_set<string_view>& missing) {
        auto& meta = tree->getMetadata();
        for (auto name : meta.globalInstances) {
            if (knownNames.find(name) == knownNames.end()) missing.emplace(name);
        }

        for (auto idName : meta.classPackageNames) {
            string_view name = idName->identifier.valueText();
            if (!name.empty() && knownNames.find(name) == knownNames.end()) missing.emplace(name);
        }

        for (auto importDecl : meta.packageImports) {
            for (auto importItem : importDecl->items) {
                string_view name = importItem->package.valueText();
                if (!name.empty() && knownNames.find(name) == knownNames.end())
                    missing.emplace(name);
            }
        }
    };

    for (auto& tree : compilation.getSyntaxTrees()) addKnownNames(tree);

    flat_hash_set<string_view> missingNames;
    for (auto& tree : compilation.getSyntaxTrees()) findMissingNames(tree, missingNames);

    // Keep loading new files as long as we are making forward progress.
    flat_hash_set<string_view> nextMissingNames;
    while (true) {
        for (auto name : missingNames) {
            SourceBuffer buffer;
            for (auto& dir : directories) {
                fs::path path(dir);
                path /= name;

                for (auto& ext : extensions) {
                    path.replace_extension(ext);
                    if (!sourceManager.isCached(path)) {
                        buffer = sourceManager.readSource(path);
                        if (buffer) break;
                    }
                }

                if (buffer) break;
            }

            if (buffer) {
                auto tree = SyntaxTree::fromBuffer(buffer, sourceManager, options);
                tree->isLibrary = true;
                compilation.addSyntaxTree(tree);

                addKnownNames(tree);
                findMissingNames(tree, nextMissingNames);
            }
        }

        if (nextMissingNames.empty()) break;

        missingNames = std::move(nextMissingNames);
        nextMissingNames.clear();
    }

    return ok;
}

class Compiler {
public:
    Compilation& compilation;
    slang::DiagnosticEngine diagEngine;
    std::shared_ptr<TextDiagnosticClient> diagClient;
    bool quiet = false;

    explicit Compiler(Compilation& compilation)
        : compilation(compilation), diagEngine(*compilation.getSourceManager()) {
        diagClient = std::make_shared<TextDiagnosticClient>();
        diagEngine.addClient(diagClient);
    }

    void setDiagnosticOptions(const std::vector<std::string>& warningOptions,
                              bool ignoreUnknownModules, bool allowUseBeforeDeclare) {
        Diagnostics optionDiags = diagEngine.setWarningOptions(warningOptions);
        Diagnostics pragmaDiags = diagEngine.setMappingsFromPragmas();
        if (ignoreUnknownModules)
            diagEngine.setSeverity(diag::UnknownModule, DiagnosticSeverity::Ignored);
        if (allowUseBeforeDeclare)
            diagEngine.setSeverity(diag::UsedBeforeDeclared, DiagnosticSeverity::Ignored);

        for (auto& diag : optionDiags) diagEngine.issue(diag);

        for (auto& diag : pragmaDiags) diagEngine.issue(diag);
    }

    bool run() {
        auto topInstances = compilation.getRoot().topInstances;
        if (!quiet && !topInstances.empty()) {
            OS::print(fg(warningColor), "Top level design units:\n");
            for (auto inst : topInstances) OS::print("    {}\n", inst->name);
            OS::print("\n");
        }

        for (auto& diag : compilation.getAllDiagnostics()) diagEngine.issue(diag);

        bool succeeded = diagEngine.getNumErrors() == 0;

        std::string diagStr = diagClient->getString();
        OS::printE("{}", diagStr);

        if (!quiet && !succeeded) {
            if (diagStr.size() > 1) OS::print("\n");

            OS::print("{} error{}, {} warning{}\n", diagEngine.getNumErrors(),
                      diagEngine.getNumErrors() == 1 ? "" : "s", diagEngine.getNumWarnings(),
                      diagEngine.getNumWarnings() == 1 ? "" : "s");
        }

        return succeeded;
    }
};

template <typename TArgs>
int driverMain(int argc, TArgs argv, bool suppressColorsStdout, bool suppressColorsStderr) try {
    CommandLine cmdLine;

    // General
    optional<bool> showHelp;
    optional<bool> showVersion;
    optional<bool> quiet;
    cmdLine.add("-h,--help", showHelp, "Display available options");
    cmdLine.add("--version", showVersion, "Display version information and exit");
    cmdLine.add("-q,--quiet", quiet, "Suppress non-essential output");

    // Include paths
    std::vector<std::string> includeDirs;
    std::vector<std::string> includeSystemDirs;
    std::vector<std::string> libDirs;
    std::vector<std::string> libExts;
    cmdLine.add("-I,--include-directory", includeDirs, "Additional include search paths", "<dir>");
    cmdLine.add("--isystem", includeSystemDirs, "Additional system include search paths", "<dir>");
    cmdLine.add("-y,--libdir", libDirs,
                "Library search paths, which will be searched for missing modules", "<dir>");
    cmdLine.add("-Y,--libext", libExts, "Additional library file extensions to search", "<ext>");

    // Preprocessor
    optional<bool> includeComments;
    optional<bool> includeDirectives;
    optional<uint32_t> maxIncludeDepth;
    std::vector<std::string> defines;
    std::vector<std::string> undefines;
    cmdLine.add("-D,--define-macro", defines,
                "Define <macro> to <value> (or 1 if <value> ommitted) in all source files",
                "<macro>=<value>");
    cmdLine.add("-U,--undefine-macro", undefines,
                "Undefine macro name at the start of all source files", "<macro>");
    cmdLine.add("--comments", includeComments, "Include comments in preprocessed output (with -E)");
    cmdLine.add("--directives", includeDirectives,
                "Include compiler directives in preprocessed output (with -E)");
    cmdLine.add("--max-include-depth", maxIncludeDepth,
                "Maximum depth of nested include files allowed", "<depth>");

    // Parsing
    optional<uint32_t> maxLexerErrors;
    cmdLine.add("--max-lexer-errors", maxLexerErrors,
                "Maximum number of errors that can occur during lexing before the rest of the file "
                "is skipped",
                "<count>");

    // Compilation
    optional<std::string> minTypMax;
    std::vector<std::string> topModules;
    std::vector<std::string> paramOverrides;
    cmdLine.add("-T,--timing", minTypMax,
                "Select which value to consider in min:typ:max expressions", "min|typ|max");
    cmdLine.add("--top", topModules,
                "One or more top-level modules to instantiate "
                "(instead of figuring it out automatically)",
                "<name>");
    cmdLine.add("-G", paramOverrides,
                "One or more parameter overrides to apply when"
                "instantiating top-level modules",
                "<name>=<value>");

    // Diagnostics control
    optional<bool> colorDiags;
    optional<bool> diagColumn;
    optional<bool> diagLocation;
    optional<bool> diagSourceLine;
    optional<bool> diagOptionName;
    optional<bool> diagIncludeStack;
    optional<bool> diagMacroExpansion;
    optional<bool> diagHierarchy;
    optional<bool> ignoreUnknownModules;
    optional<bool> allowUseBeforeDeclare;
    optional<uint32_t> errorLimit;
    std::vector<std::string> warningOptions;
    cmdLine.add("-W", warningOptions, "Control the specified warning", "<warning>");
    cmdLine.add("--color-diagnostics", colorDiags,
                "Always print diagnostics in color."
                "If this option is unset, colors will be enabled if a color-capable "
                "terminal is detected.");
    cmdLine.add("--diag-column", diagColumn, "Show column numbers in diagnostic output.");
    cmdLine.add("--diag-location", diagLocation, "Show location information in diagnostic output.");
    cmdLine.add("--diag-source", diagSourceLine,
                "Show source line or caret info in diagnostic output.");
    cmdLine.add("--diag-option", diagOptionName, "Show option names in diagnostic output.");
    cmdLine.add("--diag-include-stack", diagIncludeStack,
                "Show include stacks in diagnostic output.");
    cmdLine.add("--diag-macro-expansion", diagMacroExpansion,
                "Show macro expansion backtraces in diagnostic output.");
    cmdLine.add("--diag-hierarchy", diagHierarchy,
                "Show hierarchy locations in diagnostic output.");
    cmdLine.add("--error-limit", errorLimit,
                "Limit on the number of errors that will be printed. Setting this to zero will "
                "disable the limit.",
                "<limit>");
    cmdLine.add("--ignore-unknown-modules", ignoreUnknownModules,
                "Don't issue an error for instantiations of unknown modules, "
                "interface, and programs.");
    cmdLine.add("--allow-use-before-declare", allowUseBeforeDeclare,
                "Don't issue an error for use of names before their declarations.");

    // simulation
    optional<uint32_t> optimizationLevel;
    optional<bool> runAfterCompilation;
    cmdLine.add("-O", optimizationLevel, "Optimization level");
    cmdLine.add("-R,--run", runAfterCompilation, "Run after compilation");

    // File list
    optional<bool> singleUnit;
    std::vector<std::string> sourceFiles;
    cmdLine.add("--single-unit", singleUnit, "Treat all input files as a single compilation unit");
    cmdLine.setPositional(sourceFiles, "files");

    std::vector<std::string> libraryFiles;
    cmdLine.add("-v", libraryFiles,
                "One or more library files, which are separate compilation units "
                "where modules are not automatically instantiated.",
                "<filename>");

    if (!cmdLine.parse(argc, argv)) {
        for (auto& err : cmdLine.getErrors()) OS::printE("{}\n", err);
        return 1;
    }

    if (showHelp == true) {
        OS::print("{}", cmdLine.getHelpText("slang SystemVerilog compiler"));
        return 0;
    }

    if (showVersion == true) {
        OS::print("slang version {}.{}.{}\n", VersionInfo::getMajor(), VersionInfo::getMinor(),
                  VersionInfo::getRevision());
        return 0;
    }

    bool showColors;
    if (colorDiags)
        showColors = *colorDiags;
    else
        showColors = !suppressColorsStderr && OS::fileSupportsColors(stderr);

    if (showColors) {
        OS::setStderrColorsEnabled(true);
        if (!suppressColorsStdout && OS::fileSupportsColors(stdout))
            OS::setStdoutColorsEnabled(true);
    }

    bool anyErrors = false;
    SourceManager sourceManager;
    for (const std::string& dir : includeDirs) {
        try {
            sourceManager.addUserDirectory(string_view(dir));
        } catch (const std::exception&) {
            OS::printE(fg(errorColor), "error: ");
            OS::printE("include directory '{}' does not exist\n", dir);
            anyErrors = true;
        }
    }

    for (const std::string& dir : includeSystemDirs) {
        try {
            sourceManager.addSystemDirectory(string_view(dir));
        } catch (const std::exception&) {
            OS::printE(fg(errorColor), "error: ");
            OS::printE("include directory '{}' does not exist\n", dir);
            anyErrors = true;
        }
    }

    PreprocessorOptions ppoptions;
    ppoptions.predefines = defines;
    ppoptions.undefines = undefines;
    ppoptions.predefineSource = "<command-line>";
    if (maxIncludeDepth.has_value()) ppoptions.maxIncludeDepth = *maxIncludeDepth;

    LexerOptions loptions;
    if (maxLexerErrors.has_value()) loptions.maxErrors = *maxLexerErrors;

    ParserOptions poptions;

    CompilationOptions coptions;
    coptions.suppressUnused = true;
    if (errorLimit.has_value()) coptions.errorLimit = *errorLimit * 2;

    for (auto& name : topModules) coptions.topModules.emplace(name);
    for (auto& opt : paramOverrides) coptions.paramOverrides.emplace_back(opt);

    if (minTypMax.has_value()) {
        if (minTypMax == "min")
            coptions.minTypMax = MinTypMax::Min;
        else if (minTypMax == "typ")
            coptions.minTypMax = MinTypMax::Typ;
        else if (minTypMax == "max")
            coptions.minTypMax = MinTypMax::Max;
        else {
            OS::printE(fg(errorColor), "error: ");
            OS::printE("invalid value for timing option: '{}'", *minTypMax);
            return 1;
        }
    }

    Bag options;
    options.set(ppoptions);
    options.set(loptions);
    options.set(poptions);
    options.set(coptions);

    std::vector<SourceBuffer> buffers;
    for (const std::string& file : sourceFiles) {
        SourceBuffer buffer = readSource(sourceManager, file);
        if (!buffer) {
            anyErrors = true;
            continue;
        }

        buffers.push_back(buffer);
    }

    if (anyErrors) return 2;

    if (buffers.empty()) {
        OS::printE(fg(errorColor), "error: ");
        OS::printE("no input files\n");
        return 3;
    }

    try {
        Compilation compilation(options);
        anyErrors = !loadAllSources(compilation, sourceManager, buffers, options,
                                    singleUnit == true, false, libraryFiles, libDirs, libExts);

        Compiler compiler(compilation);
        compiler.quiet = quiet == true;

        auto& diag = *compiler.diagClient;
        diag.showColors(showColors);
        diag.showColumn(diagColumn.value_or(true));
        diag.showLocation(diagLocation.value_or(true));
        diag.showSourceLine(diagSourceLine.value_or(true));
        diag.showOptionName(diagOptionName.value_or(true));
        diag.showIncludeStack(diagIncludeStack.value_or(true));
        diag.showMacroExpansion(diagMacroExpansion.value_or(true));
        diag.showHierarchyInstance(diagHierarchy.value_or(true));

        compiler.diagEngine.setErrorLimit((int)errorLimit.value_or(20));
        compiler.setDiagnosticOptions(warningOptions, ignoreUnknownModules == true,
                                      allowUseBeforeDeclare == true);

        anyErrors |= !compiler.run();

        if (!anyErrors) {
            // compile simulation
            xsim::BuildOptions b_opt;
            if (optimizationLevel) {
                b_opt.debug_build = (*optimizationLevel) == 0;
            }
            if (runAfterCompilation) {
                b_opt.run_after_build = true;
            }
            xsim::Builder builder(b_opt);
            builder.build(&compilation);
        }

    } catch (const std::exception& e) {
        OS::printE("internal compiler error: {}\n", e.what());
        return 4;
    }

    return anyErrors ? 1 : 0;
} catch (const std::exception& e) {
    OS::printE("{}\n", e.what());
    return 5;
}
}  // namespace xsim

int main(int argc, char** argv) { return xsim::driverMain(argc, argv, false, false); }
