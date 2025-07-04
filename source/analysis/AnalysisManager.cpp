//------------------------------------------------------------------------------
// AnalysisManager.cpp
// Central manager for analyzing ASTs
//
// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include "slang/analysis/AnalysisManager.h"

#include "AnalysisScopeVisitor.h"

#include "slang/ast/ASTDiagMap.h"
#include "slang/ast/Compilation.h"

namespace slang::analysis {

using namespace ast;

static const Scope& getAsScope(const Symbol& symbol) {
    switch (symbol.kind) {
        case SymbolKind::Instance: {
            auto& inst = symbol.as<InstanceSymbol>();
            if (auto body = inst.getCanonicalBody())
                return *body;
            return inst.body;
        }
        case SymbolKind::CheckerInstance:
            return symbol.as<CheckerInstanceSymbol>().body;
        default:
            return symbol.as<Scope>();
    }
}

const AnalyzedScope* PendingAnalysis::tryGet() const {
    return analysisManager->getAnalyzedScope(getAsScope(*symbol));
}

Diagnostic& AnalysisContext::addDiag(const Symbol& symbol, DiagCode code, SourceLocation location) {
    return diagnostics.add(symbol, code, location);
}

Diagnostic& AnalysisContext::addDiag(const Symbol& symbol, DiagCode code, SourceRange sourceRange) {
    return diagnostics.add(symbol, code, sourceRange);
}

AnalysisManager::AnalysisManager(AnalysisOptions options) :
    options(options), threadPool(options.numThreads) {

    workerStates.reserve(threadPool.get_thread_count() + 1);
    for (size_t i = 0; i < threadPool.get_thread_count() + 1; i++)
        workerStates.emplace_back(*this);
}

AnalyzedDesign AnalysisManager::analyze(const Compilation& compilation) {
    SLANG_ASSERT(compilation.isFinalized());
    SLANG_ASSERT(compilation.isFrozen());

    if (compilation.hasFatalErrors())
        return {};

    // Analyze all compilation units first.
    auto& root = compilation.getRootNoFinalize();
    for (auto unit : root.compilationUnits)
        analyzeScopeAsync(*unit);
    wait();

    // Go back through and collect all of the units that were analyzed.
    AnalyzedDesign result(compilation);
    for (auto unit : root.compilationUnits) {
        auto scope = getAnalyzedScope(*unit);
        SLANG_ASSERT(scope);
        result.compilationUnits.push_back(scope);
    }

    // Collect all packages into our result object.
    for (auto package : compilation.getPackages()) {
        // Skip the built-in "std" package.
        if (package->name == "std")
            continue;

        auto scope = getAnalyzedScope(*package);
        SLANG_ASSERT(scope);
        result.packages.push_back(scope);
    }

    for (auto instance : root.topInstances)
        result.topInstances.emplace_back(analyzeSymbol(*instance));
    wait();

    // Finalize all drivers that are applied through modport ports.
    auto& state = getState();
    driverTracker.propagateModportDrivers(state.context, state.driverAlloc);

    // Report on unused definitions.
    if (hasFlag(AnalysisFlags::CheckUnused)) {
        for (auto def : compilation.getUnreferencedDefinitions()) {
            if (!def->name.empty() && def->name != "_"sv && !hasUnusedAttrib(compilation, *def)) {
                state.context.addDiag(*def, diag::UnusedDefinition, def->location)
                    << def->getKindString();
            }
        }
    }

    return result;
}

const AnalyzedScope& AnalysisManager::analyzeScopeBlocking(
    const Scope& scope, const AnalyzedProcedure* parentProcedure) {

    auto& state = getState();
    auto& result = *state.scopeAlloc.emplace(scope);

    AnalysisScopeVisitor visitor(state, result, parentProcedure);
    for (auto& member : scope.members())
        member.visit(visitor);

    return result;
}

const AnalyzedScope* AnalysisManager::getAnalyzedScope(const Scope& scope) {
    const AnalyzedScope* result = nullptr;
    analyzedScopes.cvisit(&scope, [&result](auto& item) {
        if (item.second)
            result = *item.second;
    });
    return result;
}

const AnalyzedProcedure* AnalysisManager::getAnalyzedSubroutine(
    const SubroutineSymbol& symbol) const {

    const AnalyzedProcedure* result = nullptr;
    analyzedSubroutines.cvisit(&symbol, [&result](auto& item) { result = item.second.get(); });
    return result;
}

void AnalysisManager::addAnalyzedSubroutine(const SubroutineSymbol& symbol,
                                            std::unique_ptr<AnalyzedProcedure> procedure) {
    auto& state = getState();
    driverTracker.add(state.context, state.driverAlloc, *procedure);
    analyzedSubroutines.try_emplace(&symbol, std::move(procedure));
}

DriverList AnalysisManager::getDrivers(const ValueSymbol& symbol) const {
    return driverTracker.getDrivers(symbol);
}

Diagnostics AnalysisManager::getDiagnostics(const SourceManager* sourceManager) {
    wait();

    ASTDiagMap diagMap;
    for (auto& state : workerStates) {
        for (auto& diag : state.context.diagnostics) {
            bool _;
            diagMap.add(diag, _);
        }
    }

    return diagMap.coalesce(sourceManager);
}

PendingAnalysis AnalysisManager::analyzeSymbol(const Symbol& symbol) {
    analyzeScopeAsync(getAsScope(symbol));

    // If this is an instance with a canonical body, record that
    // relationship in our map.
    if (symbol.kind == SymbolKind::Instance) {
        auto& inst = symbol.as<InstanceSymbol>();
        if (inst.getCanonicalBody()) {
            auto& state = getState();
            driverTracker.noteNonCanonicalInstance(state.context, state.driverAlloc, inst);
        }
    }

    return PendingAnalysis(*this, symbol);
}

void AnalysisManager::analyzeScopeAsync(const Scope& scope) {
    // Kick off a new analysis task if we haven't already seen
    // this scope before.
    if (analyzedScopes.try_emplace(&scope, std::nullopt)) {
        threadPool.detach_task([this, &scope] {
            SLANG_TRY {
                auto& result = analyzeScopeBlocking(scope);
                analyzedScopes.visit(&scope, [&result](auto& item) { item.second = &result; });
            }
            SLANG_CATCH(...) {
                std::unique_lock<std::mutex> lock(mutex);
                pendingException = std::current_exception();
            }
        });
    }
}

AnalysisManager::WorkerState& AnalysisManager::getState() {
    return workerStates[BS::this_thread::get_index().value_or(workerStates.size() - 1)];
}

void AnalysisManager::wait() {
    threadPool.wait();
    if (pendingException)
        std::rethrow_exception(pendingException);
}

} // namespace slang::analysis
