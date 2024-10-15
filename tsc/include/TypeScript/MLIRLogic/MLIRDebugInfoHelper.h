#ifndef MLIR_TYPESCRIPT_DEBUGINFOHELPER_H_
#define MLIR_TYPESCRIPT_DEBUGINFOHELPER_H_

#include "TypeScript/TypeScriptOps.h"
#include "TypeScript/LowerToLLVM/LocationHelper.h"

#include "mlir/Dialect/LLVMIR/LLVMAttrs.h"

#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Path.h"

namespace mlir_ts = mlir::typescript;

using llvm::StringRef;
using llvm::SmallString;

namespace typescript
{

class MLIRDebugInfoHelper
{
    mlir::OpBuilder &builder;
    llvm::ScopedHashTable<StringRef, mlir::LLVM::DIScopeAttr> &debugScope;

  public:

    MLIRDebugInfoHelper(
        mlir::OpBuilder &builder, llvm::ScopedHashTable<StringRef, mlir::LLVM::DIScopeAttr> &debugScope)
        : builder(builder), debugScope(debugScope)
    {
    }

    mlir::Location stripMetadata(mlir::Location location) 
    {
        if (auto fusedLoc = location.dyn_cast<mlir::FusedLoc>())
        {
            if (fusedLoc.getMetadata()) 
            {
                return mlir::FusedLoc::get(fusedLoc.getContext(), fusedLoc.getLocations());
            }
        }

        return location;        
    }

    mlir::Location combineWithCurrentScope(mlir::Location location)
    {
        if (auto localScope = dyn_cast_or_null<mlir::LLVM::DIScopeAttr>(debugScope.lookup(DEBUG_SCOPE)))
        {
            return combine(location, localScope);          
        }

        return location;
    }

    mlir::Location combineWithCurrentLexicalBlockScope(mlir::Location location)
    {
        if (auto lexicalBlockScope = dyn_cast_or_null<mlir::LLVM::DILexicalBlockAttr>(debugScope.lookup(DEBUG_SCOPE)))
        {
            return combine(location, lexicalBlockScope);          
        }

        return location;
    }

    mlir::NameLoc combineWithName(mlir::Location location, StringRef name)
    {
        return mlir::NameLoc::get(builder.getStringAttr(name), location);
    }

    mlir::Location combineWithCurrentScopeAndName(mlir::Location location, StringRef name)
    {
        return combineWithCurrentScope(combineWithName(location, name));
    }

    void clearDebugScope() 
    {
        debugScope.insert(DEBUG_SCOPE, mlir::LLVM::DIScopeAttr());
    }

    void setFile(StringRef fileName) {
        // TODO: in file location helper
        SmallString<256> FullName(fileName);
        sys::path::remove_filename(FullName);

        auto file = mlir::LLVM::DIFileAttr::get(builder.getContext(), sys::path::filename(fileName), FullName);

        debugScope.insert(FILE_DEBUG_SCOPE, file);
        debugScope.insert(DEBUG_SCOPE, file);
    }

    mlir::Location getCompileUnit(mlir::Location location, StringRef producerName, bool isOptimized) {

        if (auto file = dyn_cast_or_null<mlir::LLVM::DIFileAttr>(debugScope.lookup(FILE_DEBUG_SCOPE)))
        {
            unsigned sourceLanguage = llvm::dwarf::DW_LANG_C; 
            auto producer = builder.getStringAttr(producerName);
            auto emissionKind = mlir::LLVM::DIEmissionKind::Full;
            auto compileUnit = mlir::LLVM::DICompileUnitAttr::get(builder.getContext(), sourceLanguage, file, producer, isOptimized, emissionKind);        
        
            debugScope.insert(CU_DEBUG_SCOPE, compileUnit);
            debugScope.insert(DEBUG_SCOPE, file);

            return combine(location, compileUnit);
        }

        return location;
    }

    mlir::Location getSubprogram(mlir::Location functionLocation, StringRef functionName, mlir::Location functionBlockLocation) {

        if (auto compileUnitAttr = dyn_cast_or_null<mlir::LLVM::DICompileUnitAttr>(debugScope.lookup(CU_DEBUG_SCOPE)))
        {
            if (auto scopeAttr = dyn_cast_or_null<mlir::LLVM::DIScopeAttr>(debugScope.lookup(DEBUG_SCOPE)))
            {
                auto [line, column] = LocationHelper::getLineAndColumn(functionLocation);
                auto [scopeLine, scopeColumn] = LocationHelper::getLineAndColumn(functionBlockLocation);

                // if (scopeAttr.isa<mlir::LLVM::DILexicalBlockAttr>())
                // {
                //     auto file = dyn_cast<mlir::LLVM::DIFileAttr>(debugScope.lookup(FILE_DEBUG_SCOPE));

                //     // create new scope: DICompositeType
                //     //unsigned tag, StringAttr name, DIFileAttr file, uint32_t line, DIScopeAttr scope, 
                //     //DITypeAttr baseType, DIFlags flags, uint64_t sizeInBits, uint64_t alignInBits, ::llvm::ArrayRef<DINodeAttr> elements
                //     auto compositeTypeAttr = mlir::LLVM::DICompositeTypeAttr::get(
                //         builder.getContext(), llvm::dwarf::DW_TAG_class_type, builder.getStringAttr("nested_function"),
                //         file, line, scopeAttr, mlir::LLVM::DITypeAttr(), mlir::LLVM::DIFlags::TypePassByValue | mlir::LLVM::DIFlags::NonTrivial, 0/*sizeInBits*/, 
                //         8/*alignInBits*/, {/*Add elements here*/});

                //     //debugScope.insert(DEBUG_SCOPE, compositeTypeAttr);
                // }

                auto subprogramFlags = mlir::LLVM::DISubprogramFlags::Definition;
                if (compileUnitAttr.getIsOptimized())
                {
                    subprogramFlags = subprogramFlags | mlir::LLVM::DISubprogramFlags::Optimized;
                }

                // add return types
                auto type = mlir::LLVM::DISubroutineTypeAttr::get(builder.getContext(), llvm::dwarf::DW_CC_normal, {/*Add Types here*/});

                auto funcNameAttr = builder.getStringAttr(functionName);
                auto subprogramAttr = mlir::LLVM::DISubprogramAttr::get(
                    builder.getContext(), compileUnitAttr, scopeAttr, 
                    funcNameAttr, funcNameAttr, 
                    compileUnitAttr.getFile(), line, scopeLine, subprogramFlags, type);   

                debugScope.insert(SUBPROGRAM_DEBUG_SCOPE, subprogramAttr);
                debugScope.insert(DEBUG_SCOPE, subprogramAttr);

                return combine(functionLocation, subprogramAttr);
            }
        }

        return functionLocation;
    }

    void setLexicalBlock(mlir::Location blockLocation) {

        if (auto fileAttr = dyn_cast_or_null<mlir::LLVM::DIFileAttr>(debugScope.lookup(FILE_DEBUG_SCOPE)))
        {
            if (auto scopeAttr = dyn_cast_or_null<mlir::LLVM::DIScopeAttr>(debugScope.lookup(DEBUG_SCOPE)))
            {
                auto [scopeLine, scopeColumn] = LocationHelper::getLineAndColumn(blockLocation);

                auto lexicalBlockAttr = 
                    mlir::LLVM::DILexicalBlockAttr::get(
                        builder.getContext(), 
                        scopeAttr, 
                        fileAttr, 
                        scopeLine, 
                        scopeColumn);      

                debugScope.insert(BLOCK_DEBUG_SCOPE, lexicalBlockAttr);
                debugScope.insert(DEBUG_SCOPE, lexicalBlockAttr);
            }
        }
    }    

private:
    mlir::FusedLoc combine(mlir::Location location, mlir::LLVM::DIScopeAttr scope)
    {
        return mlir::FusedLoc::get(builder.getContext(), {location}, scope);          
    }
};

} // namespace typescript

#endif // MLIR_TYPESCRIPT_DEBUGINFOHELPER_H_