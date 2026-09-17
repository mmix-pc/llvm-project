//===- MMIX.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "CGCall.h"
#include "CodeGenModule.h"
#include "TargetInfo.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/Type.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>

using namespace clang;
using namespace clang::CodeGen;

namespace {

static bool isDeferredMMIXBoundaryType(QualType Ty) {
  return Ty->isRecordType() || Ty->isArrayType() || Ty->isAtomicType() ||
         Ty->isReferenceType();
}

static bool isSupportedMMIXCXXMemberPointerType(QualType Ty) {
  return Ty->isMemberPointerType();
}

static bool isSupportedMMIXScalarType(const ASTContext &Context, QualType Ty,
                                      bool AllowVoid) {
  if (Ty->isVoidType())
    return AllowVoid;

  if (isSupportedMMIXCXXMemberPointerType(Ty))
    return true;

  if (Ty->isIntegralOrEnumerationType()) {
    if (Ty->isBitIntType())
      return false;
    return Context.getTypeSize(Ty) <= 128;
  }

  if (Ty->isNullPtrType())
    return true;

  if (const auto *PT = Ty->getAs<PointerType>())
    return Context.getTargetAddressSpace(
               PT->getPointeeType().getAddressSpace()) == 0;

  return Ty->isSpecificBuiltinType(BuiltinType::Float) ||
         Ty->isSpecificBuiltinType(BuiltinType::Double) ||
         Ty->isSpecificBuiltinType(BuiltinType::LongDouble);
}

static bool isUnsupportedMMIXScalarType(const ASTContext &Context,
                                        QualType Ty, bool AllowVoid) {
  if (isDeferredMMIXBoundaryType(Ty))
    return false;
  return !isSupportedMMIXScalarType(Context, Ty, AllowVoid);
}

static bool isSupportedMMIXComplexType(QualType Ty) {
  const auto *ComplexTy = Ty->getAs<ComplexType>();
  if (!ComplexTy)
    return false;

  QualType ElementTy = ComplexTy->getElementType();
  return ElementTy->isSpecificBuiltinType(BuiltinType::Float) ||
         ElementTy->isSpecificBuiltinType(BuiltinType::Double) ||
         ElementTy->isSpecificBuiltinType(BuiltinType::LongDouble);
}

static bool isSupportedMMIXFixedVectorType(const ASTContext &Context,
                                           QualType Ty);

static bool isUnsupportedMMIXBoundaryScalarType(const ASTContext &Context,
                                                QualType Ty, bool AllowVoid) {
  return !isSupportedMMIXComplexType(Ty) &&
         !isSupportedMMIXFixedVectorType(Context, Ty) &&
         isUnsupportedMMIXScalarType(Context, Ty, AllowVoid);
}

enum class MMIXCXXFeature {
  Exceptions,
  AllocationForm,
  DeallocationForm,
  Coroutines,
};

static StringRef getMMIXCXXFeatureName(MMIXCXXFeature Feature) {
  switch (Feature) {
  case MMIXCXXFeature::Exceptions:
    return "exceptions";
  case MMIXCXXFeature::AllocationForm:
    return "allocation form";
  case MMIXCXXFeature::DeallocationForm:
    return "deallocation form";
  case MMIXCXXFeature::Coroutines:
    return "coroutines";
  }
  llvm_unreachable("unknown MMIX C++ feature");
}

static bool diagnoseUnsupportedMMIXCXXFeature(CodeGenModule &CGM,
                                              SourceLocation Loc,
                                              MMIXCXXFeature Feature) {
  unsigned DiagID = CGM.getDiags().getCustomDiagID(
      DiagnosticsEngine::Error,
      "MMIX does not support C++ %0");
  CGM.getDiags().Report(Loc, DiagID) << getMMIXCXXFeatureName(Feature);
  return true;
}

static bool isSupportedMMIXCXXNewExpr(const CXXNewExpr &E, bool IsLinux) {
  const FunctionDecl *OperatorNew = E.getOperatorNew();
  if (!OperatorNew)
    return false;
  if (OperatorNew->isReservedGlobalPlacementOperator())
    return true;
  // Linux also supplies standard nothrow overloads for arrays and aligned objects.
  bool IsNothrow = false;
  if (IsLinux && E.getNumPlacementArgs() == 1 &&
      OperatorNew->isReplaceableGlobalAllocationFunction(nullptr, &IsNothrow) &&
      IsNothrow)
    return true;
  // Allocator adapters use a reference to report failure without throwing.
  // Single objects use the ordinary initialization path. Arrays of scalar or
  // trivially destructible record elements need no destructor cookie.
  const CXXRecordDecl *Record = E.getAllocatedType()->getAsCXXRecordDecl();
  if (!isa<CXXMethodDecl>(OperatorNew) && !E.passAlignment() &&
      (E.getAllocatedType()->isScalarType() ||
       (Record && (!E.isArray() || Record->hasTrivialDestructor()))) &&
      E.getNumPlacementArgs() == 1 &&
      OperatorNew->getNumParams() == 2 && !OperatorNew->isVariadic() &&
      OperatorNew->getParamDecl(1)->getType()->isLValueReferenceType() &&
      OperatorNew->getType()->castAs<FunctionProtoType>()->isNothrow())
    return true;
  // Typed single-object placement is used by runtimes with caller-owned storage.
  if (isa<CXXMethodDecl>(OperatorNew) && !E.isArray() && !E.passAlignment() &&
      E.getNumPlacementArgs() == 1 && OperatorNew->getNumParams() == 2) {
    QualType StorageType = OperatorNew->getParamDecl(1)->getType();
    if (StorageType->isPointerType() &&
        OperatorNew->getASTContext().hasSameType(
            StorageType->getPointeeType(), E.getAllocatedType()))
      return true;
  }
  return E.getNumPlacementArgs() == 0 &&
         (!E.passAlignment() || IsLinux) &&
         OperatorNew->isReplaceableGlobalAllocationFunction();
}

static bool isSupportedMMIXCXXDeleteExpr(const CXXDeleteExpr &E,
                                       bool AllowAlignedAllocation) {
  const FunctionDecl *OperatorDelete = E.getOperatorDelete();
  if (!OperatorDelete)
    return false;
  UnsignedOrNone AlignmentParam = std::nullopt;
  bool IsNothrow = false;
  return OperatorDelete->isReplaceableGlobalAllocationFunction(&AlignmentParam,
                                                               &IsNothrow) &&
         (!AlignmentParam || AllowAlignedAllocation) && !IsNothrow;
}

static bool isMMIXNativeAtomicStorageType(const ASTContext &Context,
                                          QualType Ty) {
  if (Ty->isIncompleteType() ||
      Context.getTargetAddressSpace(Ty.getAddressSpace()) != 0)
    return false;

  uint64_t Size = Context.getTypeSize(Ty);
  uint64_t Align = Context.getTypeAlign(Ty);
  return (Size == 8 || Size == 16 || Size == 32 || Size == 64) && Align >= Size;
}

static bool isSupportedMMIXAtomicRMWType(const ASTContext &Context, QualType Ty) {
  if (const auto *AT = Ty->getAs<AtomicType>())
    Ty = AT->getValueType();
  // Scalar i128 support does not supply a wide atomic RMW runtime ABI.
  return (Ty->isIntegerType() || Ty->isPointerType()) &&
         Context.getTypeSize(Ty) <= 64;
}

enum class MMIXUnsupportedObjectKind {
  None,
  Vector,
  AddressSpace,
};

static bool isSupportedMMIXFixedVectorType(const ASTContext &Context,
                                           QualType Ty) {
  const auto *VT = Ty->getAs<VectorType>();
  if (!VT || VT->getVectorKind() != VectorKind::Generic ||
      !llvm::isPowerOf2_64(VT->getNumElements()))
    return false;

  QualType ElementTy = VT->getElementType();
  uint64_t ElementBits = Context.getTypeSize(ElementTy);
  uint64_t LaneBits = ElementTy->isBooleanType() ? 1 : ElementBits;
  bool SupportedElement =
      ElementTy->isBooleanType() && VT->getNumElements() >= 8;
  if (!ElementTy->isBooleanType() && ElementTy->isIntegerType() &&
      !ElementTy->isBitIntType())
    SupportedElement |= ElementBits == 8 || ElementBits == 16 ||
                        ElementBits == 32 || ElementBits == 64;
  SupportedElement |= ElementTy->isSpecificBuiltinType(BuiltinType::Float) ||
                      ElementTy->isSpecificBuiltinType(BuiltinType::Double) ||
                      ElementTy->isSpecificBuiltinType(BuiltinType::LongDouble);
  if (!SupportedElement || LaneBits * uint64_t(VT->getNumElements()) > 64)
    return false;

  uint64_t Size = Context.getTypeSize(Ty);
  uint64_t Align = Context.getTypeAlign(Ty);
  return (Size == 8 || Size == 16 || Size == 32 || Size == 64) && Align == Size;
}

static MMIXUnsupportedObjectKind
classifyUnsupportedMMIXObjectType(const ASTContext &Context, QualType Ty,
                                  bool AllowSupportedVector = true) {
  if (Ty.isNull())
    return MMIXUnsupportedObjectKind::None;

  QualType OriginalTy = Ty;
  Ty = Ty.getCanonicalType();
  if (Context.getTargetAddressSpace(Ty.getAddressSpace()) != 0)
    return MMIXUnsupportedObjectKind::AddressSpace;
  if (const auto *AT = Ty->getAs<AtomicType>()) {
    return classifyUnsupportedMMIXObjectType(Context, AT->getValueType(),
                                             /*AllowSupportedVector=*/false);
  }
  if (Ty->isVectorType())
    return AllowSupportedVector &&
                   isSupportedMMIXFixedVectorType(Context, OriginalTy)
               ? MMIXUnsupportedObjectKind::None
               : MMIXUnsupportedObjectKind::Vector;

  if (const auto *PT = Ty->getAs<PointerType>()) {
    if (Context.getTargetAddressSpace(PT->getPointeeType().getAddressSpace()) !=
        0)
      return MMIXUnsupportedObjectKind::AddressSpace;
    return MMIXUnsupportedObjectKind::None;
  }

  if (const auto *RT = Ty->getAs<ReferenceType>())
    return classifyUnsupportedMMIXObjectType(Context, RT->getPointeeType(),
                                             AllowSupportedVector);

  if (const auto *AT = Context.getAsArrayType(Ty))
    return classifyUnsupportedMMIXObjectType(Context, AT->getElementType(),
                                             AllowSupportedVector);

  if (const auto *RT = Ty->getAs<RecordType>()) {
    for (const FieldDecl *Field : RT->getDecl()->fields()) {
      MMIXUnsupportedObjectKind Kind = classifyUnsupportedMMIXObjectType(
          Context, Field->getType(), AllowSupportedVector);
      if (Kind != MMIXUnsupportedObjectKind::None)
        return Kind;
    }
  }

  return MMIXUnsupportedObjectKind::None;
}

static StringRef
getMMIXUnsupportedObjectDescription(MMIXUnsupportedObjectKind Kind) {
  switch (Kind) {
  case MMIXUnsupportedObjectKind::Vector:
    return "vector value";
  case MMIXUnsupportedObjectKind::AddressSpace:
    return "nonzero-address-space value";
  case MMIXUnsupportedObjectKind::None:
    llvm_unreachable("expected an unsupported MMIX object kind");
  }
  llvm_unreachable("invalid MMIX object kind");
}

static bool diagnoseUnsupportedMMIXObject(CodeGenModule &CGM,
                                          SourceLocation Loc, QualType Ty) {
  MMIXUnsupportedObjectKind Kind =
      classifyUnsupportedMMIXObjectType(CGM.getContext(), Ty);
  if (Kind == MMIXUnsupportedObjectKind::None)
    return false;

  unsigned DiagID = CGM.getDiags().getCustomDiagID(
      DiagnosticsEngine::Error,
      "MMIX GNU ABI does not support %0 CodeGen involving type %1");
  CGM.getDiags().Report(Loc, DiagID)
      << getMMIXUnsupportedObjectDescription(Kind) << Ty;
  return true;
}

static bool isMMIXAtomicBuiltinName(StringRef Name) {
  return Name.starts_with("__atomic_") || Name.starts_with("__sync_") ||
         Name.starts_with("__c11_atomic_") ||
         Name.starts_with("__scoped_atomic_");
}

static bool isSupportedMMIXAtomicFenceBuiltin(unsigned BuiltinID) {
  switch (BuiltinID) {
  case Builtin::BI__atomic_thread_fence:
  case Builtin::BI__atomic_signal_fence:
  case Builtin::BI__c11_atomic_thread_fence:
  case Builtin::BI__c11_atomic_signal_fence:
  case Builtin::BI__scoped_atomic_thread_fence:
    return true;
  default:
    return false;
  }
}

static bool isSupportedMMIXLegacySyncBuiltinName(StringRef Name) {
  if (Name == "__sync_synchronize")
    return true;

  static constexpr llvm::StringLiteral Operations[] = {
      "__sync_fetch_and_add",        "__sync_fetch_and_sub",
      "__sync_fetch_and_or",         "__sync_fetch_and_and",
      "__sync_fetch_and_xor",        "__sync_fetch_and_nand",
      "__sync_add_and_fetch",        "__sync_sub_and_fetch",
      "__sync_or_and_fetch",         "__sync_and_and_fetch",
      "__sync_xor_and_fetch",        "__sync_nand_and_fetch",
      "__sync_val_compare_and_swap", "__sync_bool_compare_and_swap",
      "__sync_lock_test_and_set",    "__sync_lock_release"};
  return llvm::any_of(Operations,
                      [Name](StringRef Op) { return Name.starts_with(Op); });
}

class MMIXCodeGenBoundaryVisitor
    : public RecursiveASTVisitor<MMIXCodeGenBoundaryVisitor> {
  CodeGenModule &CGM;

  bool diagnoseType(SourceLocation Loc, StringRef Description, QualType Ty) {
    unsigned DiagID = CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Error,
        "MMIX GNU ABI does not support %0 CodeGen involving type %1");
    CGM.getDiags().Report(Loc, DiagID) << Description << Ty;
    return false;
  }

  bool diagnoseObjectType(SourceLocation Loc, QualType Ty) {
    return !diagnoseUnsupportedMMIXObject(CGM, Loc, Ty);
  }

  bool diagnoseExtendedScalarOperation(SourceLocation Loc, QualType Ty) {
    if (!Ty->isScalarType() ||
        !isUnsupportedMMIXBoundaryScalarType(CGM.getContext(), Ty,
                                             /*AllowVoid=*/true))
      return true;
    return diagnoseType(Loc, "extended scalar operation", Ty);
  }

  bool diagnoseAtomicOperation(SourceLocation Loc, StringRef Operation) {
    unsigned DiagID = CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Error,
        "MMIX GNU ABI does not support atomic operation %0");
    CGM.getDiags().Report(Loc, DiagID) << Operation;
    return false;
  }

  bool diagnoseAtomicStorage(SourceLocation Loc, StringRef Operation,
                             QualType Ty) {
    const ASTContext &Context = CGM.getContext();
    if (Context.getTargetAddressSpace(Ty.getAddressSpace()) != 0) {
      unsigned DiagID = CGM.getDiags().getCustomDiagID(
          DiagnosticsEngine::Error,
          "MMIX atomic operation %0 requires address space zero storage");
      CGM.getDiags().Report(Loc, DiagID) << Operation;
      return false;
    }

    return true;
  }

public:
  explicit MMIXCodeGenBoundaryVisitor(CodeGenModule &CGM) : CGM(CGM) {}

  bool VisitVarDecl(VarDecl *VD) {
    return diagnoseObjectType(VD->getLocation(), VD->getType());
  }

  bool VisitCXXThrowExpr(CXXThrowExpr *E) {
    if (CGM.getCodeGenOpts().hasDWARFExceptions())
      return true;
    return !diagnoseUnsupportedMMIXCXXFeature(CGM, E->getExprLoc(),
                                              MMIXCXXFeature::Exceptions);
  }

  bool VisitCoroutineBodyStmt(CoroutineBodyStmt *S) {
    return !diagnoseUnsupportedMMIXCXXFeature(CGM, S->getBeginLoc(),
                                              MMIXCXXFeature::Coroutines);
  }

  bool VisitCXXNewExpr(CXXNewExpr *E) {
    // The Linux runtime supplies the aligned replaceable allocation operators.
    if (isSupportedMMIXCXXNewExpr(*E, CGM.getTarget().getTriple().isOSLinux()))
      return true;
    return !diagnoseUnsupportedMMIXCXXFeature(CGM, E->getExprLoc(),
                                              MMIXCXXFeature::AllocationForm);
  }

  bool VisitCXXDeleteExpr(CXXDeleteExpr *E) {
    if (isSupportedMMIXCXXDeleteExpr(*E, CGM.getTarget().getTriple().isOSLinux()))
      return true;
    return !diagnoseUnsupportedMMIXCXXFeature(CGM, E->getExprLoc(),
                                              MMIXCXXFeature::DeallocationForm);
  }

  bool VisitExpr(Expr *E) {
    return diagnoseObjectType(E->getExprLoc(), E->getType());
  }

  bool VisitBinaryOperator(BinaryOperator *E) {
    if (E->isCompoundAssignmentOp() && E->getLHS()->getType()->isAtomicType() &&
        !isSupportedMMIXAtomicRMWType(CGM.getContext(), E->getLHS()->getType()))
      return diagnoseAtomicOperation(E->getExprLoc(), E->getOpcodeStr());
    return diagnoseExtendedScalarOperation(E->getExprLoc(), E->getType()) &&
           diagnoseExtendedScalarOperation(E->getExprLoc(),
                                           E->getLHS()->getType()) &&
           diagnoseExtendedScalarOperation(E->getExprLoc(),
                                           E->getRHS()->getType());
  }

  bool VisitUnaryOperator(UnaryOperator *E) {
    if (E->isIncrementDecrementOp() &&
        E->getSubExpr()->getType()->isAtomicType() &&
        !isSupportedMMIXAtomicRMWType(CGM.getContext(), E->getSubExpr()->getType()))
      return diagnoseAtomicOperation(
          E->getExprLoc(), UnaryOperator::getOpcodeStr(E->getOpcode()));
    return diagnoseExtendedScalarOperation(E->getExprLoc(), E->getType()) &&
           diagnoseExtendedScalarOperation(E->getExprLoc(),
                                           E->getSubExpr()->getType());
  }

  bool VisitCastExpr(CastExpr *E) {
    return diagnoseExtendedScalarOperation(E->getExprLoc(), E->getType()) &&
           diagnoseExtendedScalarOperation(E->getExprLoc(),
                                           E->getSubExpr()->getType());
  }

  bool VisitCallExpr(CallExpr *E) {
    unsigned BuiltinID = E->getBuiltinCallee();
    if (!BuiltinID)
      return true;

    std::string Name = CGM.getContext().BuiltinInfo.getName(BuiltinID);
    if (!isMMIXAtomicBuiltinName(Name))
      return true;
    if (isSupportedMMIXAtomicFenceBuiltin(BuiltinID))
      return true;
    if (Name == "__atomic_always_lock_free")
      return true;
    if (Name == "__atomic_is_lock_free" || Name == "__c11_atomic_is_lock_free")
      return true;
    if (isSupportedMMIXLegacySyncBuiltinName(Name)) {
      if (Name == "__sync_synchronize")
        return true;

      QualType StorageTy = E->getArg(0)->getType()->getPointeeType();
      if (isSupportedMMIXAtomicRMWType(CGM.getContext(), StorageTy) &&
          isMMIXNativeAtomicStorageType(CGM.getContext(), StorageTy))
        return true;
    }

    unsigned DiagID = CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Error,
        "MMIX GNU ABI does not support atomic builtin %0");
    CGM.getDiags().Report(E->getExprLoc(), DiagID) << Name;
    return false;
  }

  bool VisitAtomicExpr(AtomicExpr *E) {
    switch (E->getOp()) {
    case AtomicExpr::AO__c11_atomic_init:
    case AtomicExpr::AO__c11_atomic_load:
    case AtomicExpr::AO__c11_atomic_store:
    case AtomicExpr::AO__atomic_load:
    case AtomicExpr::AO__atomic_load_n:
    case AtomicExpr::AO__atomic_store:
    case AtomicExpr::AO__atomic_store_n:
    case AtomicExpr::AO__c11_atomic_exchange:
    case AtomicExpr::AO__atomic_exchange:
    case AtomicExpr::AO__atomic_exchange_n:
    case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
    case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
    case AtomicExpr::AO__atomic_compare_exchange:
    case AtomicExpr::AO__atomic_compare_exchange_n:
    case AtomicExpr::AO__atomic_test_and_set:
    case AtomicExpr::AO__atomic_clear:
    case AtomicExpr::AO__scoped_atomic_load:
    case AtomicExpr::AO__scoped_atomic_load_n:
    case AtomicExpr::AO__scoped_atomic_store:
    case AtomicExpr::AO__scoped_atomic_store_n:
    case AtomicExpr::AO__scoped_atomic_exchange:
    case AtomicExpr::AO__scoped_atomic_exchange_n:
    case AtomicExpr::AO__scoped_atomic_compare_exchange:
    case AtomicExpr::AO__scoped_atomic_compare_exchange_n:
      break;
    case AtomicExpr::AO__c11_atomic_fetch_add:
    case AtomicExpr::AO__c11_atomic_fetch_sub:
    case AtomicExpr::AO__c11_atomic_fetch_and:
    case AtomicExpr::AO__c11_atomic_fetch_or:
    case AtomicExpr::AO__c11_atomic_fetch_xor:
    case AtomicExpr::AO__c11_atomic_fetch_nand:
    case AtomicExpr::AO__c11_atomic_fetch_min:
    case AtomicExpr::AO__c11_atomic_fetch_max:
    case AtomicExpr::AO__atomic_fetch_add:
    case AtomicExpr::AO__atomic_fetch_sub:
    case AtomicExpr::AO__atomic_fetch_and:
    case AtomicExpr::AO__atomic_fetch_or:
    case AtomicExpr::AO__atomic_fetch_xor:
    case AtomicExpr::AO__atomic_fetch_nand:
    case AtomicExpr::AO__atomic_fetch_min:
    case AtomicExpr::AO__atomic_fetch_max:
    case AtomicExpr::AO__atomic_add_fetch:
    case AtomicExpr::AO__atomic_sub_fetch:
    case AtomicExpr::AO__atomic_and_fetch:
    case AtomicExpr::AO__atomic_or_fetch:
    case AtomicExpr::AO__atomic_xor_fetch:
    case AtomicExpr::AO__atomic_nand_fetch:
    case AtomicExpr::AO__atomic_min_fetch:
    case AtomicExpr::AO__atomic_max_fetch:
    case AtomicExpr::AO__scoped_atomic_fetch_add:
    case AtomicExpr::AO__scoped_atomic_fetch_sub:
    case AtomicExpr::AO__scoped_atomic_fetch_and:
    case AtomicExpr::AO__scoped_atomic_fetch_or:
    case AtomicExpr::AO__scoped_atomic_fetch_xor:
      if (!isSupportedMMIXAtomicRMWType(CGM.getContext(), E->getValueType()))
        return diagnoseAtomicOperation(E->getExprLoc(), E->getOpAsString());
      break;
    default:
      return diagnoseAtomicOperation(E->getExprLoc(), E->getOpAsString());
    }

    QualType StorageTy = E->getPtr()->getType()->getPointeeType();
    return diagnoseAtomicStorage(E->getExprLoc(), E->getOpAsString(),
                                 StorageTy);
  }
};

enum class MMIXGCCModeKind { Scalar, Block, AlignmentOnlyBlock };

struct MMIXGCCMode {
  MMIXGCCModeKind Kind;
  uint64_t SizeInBits;
  uint64_t AlignInBits;
  bool IsInteger;
};

static MMIXGCCMode getMMIXGCCIntegerMode(uint64_t SizeInBits) {
  if (SizeInBits == 8 || SizeInBits == 16 || SizeInBits == 32 ||
      SizeInBits == 64 || SizeInBits == 128)
    return {MMIXGCCModeKind::Scalar, SizeInBits,
            std::min(SizeInBits, uint64_t(64)), true};
  return {MMIXGCCModeKind::Block, SizeInBits, 0, false};
}

// Mirror the part of GCC compute_record_mode that distinguishes scalar record
// modes from BLKmode for the frozen MMIX C result boundary. AlignmentOnlyBlock
// represents GCC's TYPE_NO_FORCE_BLK case, which does not force an enclosing
// record to remain BLKmode.
static MMIXGCCMode getMMIXGCCTypeMode(const ASTContext &Context, QualType Ty) {
  if (Ty->isIncompleteType())
    return {MMIXGCCModeKind::Block, 0, 0, false};

  uint64_t Size = Context.getTypeSize(Ty);
  if (const auto *RT = Ty->getAsCanonical<RecordType>()) {
    const RecordDecl *RD = RT->getDecl()->getDefinitionOrSelf();
    MMIXGCCMode WholeField = {MMIXGCCModeKind::Block, 0, 0, false};

    for (const FieldDecl *Field : RD->fields()) {
      QualType FieldTy = Field->getType();
      uint64_t FieldSize;
      if (Field->isBitField())
        FieldSize = Field->getBitWidthValue();
      else if (FieldTy->isIncompleteArrayType())
        FieldSize = 0;
      else if (FieldTy->isIncompleteType())
        return {MMIXGCCModeKind::Block, Size, 0, false};
      else
        FieldSize = Context.getTypeSize(FieldTy);

      if (FieldSize == 0)
        continue;

      MMIXGCCMode FieldMode = getMMIXGCCTypeMode(Context, FieldTy);
      if (FieldMode.Kind == MMIXGCCModeKind::Block)
        return {MMIXGCCModeKind::Block, Size, 0, false};

      if (FieldSize == Size && FieldMode.Kind == MMIXGCCModeKind::Scalar &&
          (!RD->isUnion() || FieldMode.IsInteger) &&
          FieldMode.SizeInBits > WholeField.SizeInBits)
        WholeField = FieldMode;
    }

    MMIXGCCMode Mode = WholeField.Kind == MMIXGCCModeKind::Scalar
                           ? WholeField
                           : getMMIXGCCIntegerMode(Size);
    if (Mode.Kind == MMIXGCCModeKind::Block)
      return Mode;
    if (Context.getTypeAlign(Ty) < Mode.AlignInBits)
      return {MMIXGCCModeKind::AlignmentOnlyBlock, Size, 0, false};
    return Mode;
  }

  if (const auto *AT = Context.getAsConstantArrayType(Ty)) {
    if (AT->getSize().isOne()) {
      MMIXGCCMode ElementMode =
          getMMIXGCCTypeMode(Context, AT->getElementType());
      if (ElementMode.Kind != MMIXGCCModeKind::Scalar)
        return {MMIXGCCModeKind::Block, Size, 0, false};
      return ElementMode;
    }
    return getMMIXGCCIntegerMode(Size);
  }

  bool IsInteger = Ty->isIntegralOrEnumerationType() || Ty->isPointerType();
  return {MMIXGCCModeKind::Scalar, Size, std::min(Size, uint64_t(64)),
          IsInteger};
}

static void diagnoseUnsupportedMMIXScalar(CodeGenModule &CGM,
                                          SourceLocation Loc,
                                          StringRef ValueKind, QualType Ty) {
  unsigned DiagID = CGM.getDiags().getCustomDiagID(
      DiagnosticsEngine::Error,
      "MMIX GNU ABI does not support %0 type %1");
  CGM.getDiags().Report(Loc, DiagID) << ValueKind << Ty;
}

static bool diagnoseUnsupportedMMIXAggregateArgument(CodeGenModule &CGM,
                                                     SourceLocation Loc,
                                                     QualType Ty) {
  ASTContext &Context = CGM.getContext();
  if (!Ty->isRecordType())
    return false;

  StringRef Reason;
  if (Ty->isIncompleteType())
    Reason = "incomplete";
  else if (Ty->isVariablyModifiedType())
    Reason = "variable-size";
  else if (Context.getTypeAlign(Ty) > 64)
    Reason = "over-aligned";
  else
    return false;

  unsigned DiagID = CGM.getDiags().getCustomDiagID(
      DiagnosticsEngine::Error,
      "MMIX GNU ABI does not support %0 aggregate argument type %1");
  CGM.getDiags().Report(Loc, DiagID) << Reason << Ty;
  return true;
}

static bool diagnoseUnsupportedMMIXAggregateResult(CodeGenModule &CGM,
                                                   SourceLocation Loc,
                                                   QualType Ty) {
  ASTContext &Context = CGM.getContext();
  if (!Ty->isRecordType())
    return false;

  StringRef Reason;
  if (Ty->isIncompleteType())
    Reason = "incomplete";
  else if (Ty->isVariablyModifiedType())
    Reason = "variable-size";
  else if (Context.getTypeAlign(Ty) > 64)
    Reason = "over-aligned";
  else
    return false;

  unsigned DiagID = CGM.getDiags().getCustomDiagID(
      DiagnosticsEngine::Error,
      "MMIX GNU ABI does not support %0 aggregate return type %1");
  CGM.getDiags().Report(Loc, DiagID) << Reason << Ty;
  return true;
}

static bool diagnoseUnsupportedMMIXVariadicSignature(CodeGenModule &CGM,
                                                      SourceLocation Loc,
                                                      const FunctionDecl *FD) {
  if (!FD || !FD->isVariadic() || FD->getNumParams() == 0)
    return false;

  for (const ParmVarDecl *Param : FD->parameters()) {
    if (!isSupportedMMIXFixedVectorType(CGM.getContext(), Param->getType()))
      continue;
    unsigned DiagID = CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Error,
        "MMIX GNU ABI does not support vector parameters in variadic "
        "functions");
    CGM.getDiags().Report(Loc, DiagID);
    return true;
  }

  QualType LastNamedType = FD->getParamDecl(FD->getNumParams() - 1)->getType();
  // C++ empty records use the ordinary Ignore/indirect classification. The
  // variadic cursor follows actual ABI slots, not the final source parameter.
  // Keep the GNU C empty-record boundary separate from this C++ support.
  if (LastNamedType->getAsCXXRecordDecl())
    return false;
  if (!isEmptyRecord(CGM.getContext(), LastNamedType, /*AllowArrays=*/true))
    return false;

  unsigned DiagID = CGM.getDiags().getCustomDiagID(
      DiagnosticsEngine::Error,
      "MMIX GNU ABI does not support an empty final named parameter in a "
      "variadic function");
  CGM.getDiags().Report(Loc, DiagID);
  return true;
}

static void diagnoseUnsupportedMMIXVariadicCallArguments(
    CodeGenModule &CGM, SourceLocation Loc, const FunctionDecl *Callee,
    const CallArgList &Args) {
  if (!Callee || !Callee->isVariadic())
    return;

  unsigned FixedArgumentCount = Callee->getNumParams();
  for (unsigned I = FixedArgumentCount; I != Args.size(); ++I) {
    QualType Ty = Args[I].getType();
    if (!isSupportedMMIXFixedVectorType(CGM.getContext(), Ty))
      continue;
    unsigned DiagID = CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Error,
        "MMIX GNU ABI does not support variadic vector argument type %0");
    CGM.getDiags().Report(Loc, DiagID) << Ty;
    return;
  }
}

class MMIXABIInfo : public DefaultABIInfo {
public:
  explicit MMIXABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  llvm::Value *createCoercedLoad(Address Src, const ABIArgInfo &AI,
                                 CodeGenFunction &CGF) const override;
  void createCoercedStore(llvm::Value *Val, Address Dst,
                          const ABIArgInfo &AI, bool DestIsVolatile,
                          CodeGenFunction &CGF) const override;

private:
  ABIArgInfo classifyAggregateArgument(QualType Ty) const;
  ABIArgInfo classifyAggregateReturn(QualType Ty) const;
  ABIArgInfo classifyReturnType(QualType Ty) const;
  ABIArgInfo classifyArgumentType(QualType Ty) const;
  void computeInfo(CGFunctionInfo &FI) const override;
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

class MMIXTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  explicit MMIXTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<MMIXABIInfo>(CGT)) {}

  void checkFunctionABI(CodeGenModule &CGM,
                        const FunctionDecl *FD) const override;
  void checkFunctionCallABI(CodeGenModule &CGM, SourceLocation CallLoc,
                            const FunctionDecl *, const FunctionDecl *Callee,
                            const CallArgList &Args,
                            QualType ReturnType) const override;
  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGenModule &CGM) const override;
  StringRef getLLVMSyncScopeStr(const LangOptions &LangOpts, SyncScope Scope,
                                llvm::AtomicOrdering Ordering) const override;
};

} // namespace

StringRef MMIXTargetCodeGenInfo::getLLVMSyncScopeStr(
    const LangOptions &LangOpts, SyncScope Scope,
    llvm::AtomicOrdering Ordering) const {
  switch (Scope) {
  case SyncScope::SystemScope:
  case SyncScope::DeviceScope:
  case SyncScope::WorkgroupScope:
  case SyncScope::ClusterScope:
  case SyncScope::WavefrontScope:
  case SyncScope::SingleScope:
    return "";
  default:
    return TargetCodeGenInfo::getLLVMSyncScopeStr(LangOpts, Scope, Ordering);
  }
}

void MMIXTargetCodeGenInfo::setTargetAttributes(const Decl *D,
                                                llvm::GlobalValue *,
                                                CodeGenModule &CGM) const {
  const auto *VD = dyn_cast_or_null<VarDecl>(D);
  if (!VD || !VD->hasGlobalStorage())
    return;

  diagnoseUnsupportedMMIXObject(CGM, VD->getLocation(), VD->getType());
}

ABIArgInfo MMIXABIInfo::classifyReturnType(QualType Ty) const {
  if (Ty->isVoidType())
    return ABIArgInfo::getIgnore();
  if (isSupportedMMIXFixedVectorType(getContext(), Ty)) {
    llvm::IntegerType *CoerceTy =
        llvm::IntegerType::get(getVMContext(), getContext().getTypeSize(Ty));
    return ABIArgInfo::getDirect(CoerceTy);
  }
  if (isSupportedMMIXComplexType(Ty)) {
    QualType ElementTy = Ty->castAs<ComplexType>()->getElementType();
    if (ElementTy->isSpecificBuiltinType(BuiltinType::Float))
      return ABIArgInfo::getDirect(llvm::Type::getInt64Ty(getVMContext()));
    return ABIArgInfo::getDirect();
  }
  if (isUnsupportedMMIXScalarType(getContext(), Ty, /*AllowVoid=*/true))
    return ABIArgInfo::getDirect();
  if (isAggregateTypeForABI(Ty))
    return classifyAggregateReturn(Ty);
  return ABIArgInfo::getDirect();
}

ABIArgInfo MMIXABIInfo::classifyAggregateReturn(QualType Ty) const {
  if (isEmptyRecord(getContext(), Ty, /*AllowArrays=*/true))
    return ABIArgInfo::getIgnore();

  MMIXGCCMode Mode = getMMIXGCCTypeMode(getContext(), Ty);
  if (Mode.Kind != MMIXGCCModeKind::Scalar || Mode.SizeInBits > 64)
    return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                   /*ByVal=*/false);

  llvm::IntegerType *CoerceTy =
      llvm::IntegerType::get(getVMContext(), Mode.SizeInBits);
  if (llvm::isPowerOf2_64(Mode.SizeInBits))
    return ABIArgInfo::getDirect(CoerceTy);

  return ABIArgInfo::getTargetSpecific(llvm::Type::getInt64Ty(getVMContext()),
                                       /*Offset=*/0, /*Padding=*/nullptr,
                                       /*CanBeFlattened=*/false);
}

ABIArgInfo MMIXABIInfo::classifyAggregateArgument(QualType Ty) const {
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(
        Ty, getDataLayout().getAllocaAddrSpace(),
        /*ByVal=*/RAA == CGCXXABI::RAA_DirectInMemory);

  if (isEmptyRecord(getContext(), Ty, /*AllowArrays=*/true))
    return ABIArgInfo::getIgnore();

  if (const auto *RT = Ty->getAsCanonical<RecordType>()) {
    const RecordDecl *RD = RT->getDecl()->getDefinitionOrSelf();
    if (!isa<CXXRecordDecl>(RD) && !RD->canPassInRegisters())
      return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace());
  }

  uint64_t Size = getContext().getTypeSize(Ty);
  if (Size > 64)
    return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace());

  llvm::IntegerType *CoerceTy = llvm::IntegerType::get(getVMContext(), Size);
  if (llvm::isPowerOf2_64(Size) && Size < 64)
    return ABIArgInfo::getNoExtend(CoerceTy);
  if (Size == 64)
    return ABIArgInfo::getDirect(CoerceTy);

  return ABIArgInfo::getTargetSpecific(
      llvm::Type::getInt64Ty(getVMContext()), /*Offset=*/0, /*Padding=*/nullptr,
      /*CanBeFlattened=*/false);
}

ABIArgInfo MMIXABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);
  if (isSupportedMMIXFixedVectorType(getContext(), Ty)) {
    unsigned Size = getContext().getTypeSize(Ty);
    llvm::IntegerType *CoerceTy =
        llvm::IntegerType::get(getVMContext(), Size);
    return Size < 64 ? ABIArgInfo::getNoExtend(CoerceTy)
                     : ABIArgInfo::getDirect(CoerceTy);
  }
  if (isSupportedMMIXComplexType(Ty)) {
    QualType ElementTy = Ty->castAs<ComplexType>()->getElementType();
    if (ElementTy->isSpecificBuiltinType(BuiltinType::Float))
      return ABIArgInfo::getDirect(llvm::Type::getInt64Ty(getVMContext()));
    return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace());
  }
  if (isUnsupportedMMIXScalarType(getContext(), Ty, /*AllowVoid=*/false))
    return ABIArgInfo::getDirect();
  if (isAggregateTypeForABI(Ty))
    return classifyAggregateArgument(Ty);

  if (Ty->isIntegralOrEnumerationType() && getContext().getTypeSize(Ty) < 64)
    return ABIArgInfo::getExtend(Ty, CGT.ConvertType(Ty));
  return ABIArgInfo::getDirect();
}

llvm::Value *MMIXABIInfo::createCoercedLoad(Address Src, const ABIArgInfo &AI,
                                            CodeGenFunction &CGF) const {
  assert(AI.isTargetSpecific() && AI.getCoerceToType()->isIntegerTy(64));
  uint64_t Size = getDataLayout().getTypeAllocSize(Src.getElementType());
  assert(Size > 0 && Size < 8 && !llvm::isPowerOf2_64(Size));

  llvm::Value *Result = llvm::ConstantInt::get(CGF.Int64Ty, 0);
  Address ByteSrc = Src.withElementType(CGF.Int8Ty);
  for (uint64_t I = 0; I != Size; ++I) {
    Address ByteAddr = CGF.Builder.CreateConstInBoundsByteGEP(
        ByteSrc, CharUnits::fromQuantity(I));
    llvm::Value *Byte = CGF.Builder.CreateLoad(ByteAddr);
    Byte = CGF.Builder.CreateZExt(Byte, CGF.Int64Ty);
    unsigned Shift = 8 * (Size - I - 1);
    if (Shift)
      Byte = CGF.Builder.CreateShl(Byte, Shift);
    Result = CGF.Builder.CreateOr(Result, Byte);
  }
  return Result;
}

void MMIXABIInfo::createCoercedStore(llvm::Value *Val, Address Dst,
                                     const ABIArgInfo &AI,
                                     bool DestIsVolatile,
                                     CodeGenFunction &CGF) const {
  assert(AI.isTargetSpecific() && Val->getType()->isIntegerTy(64));
  uint64_t Size = getDataLayout().getTypeAllocSize(Dst.getElementType());
  assert(Size > 0 && Size < 8 && !llvm::isPowerOf2_64(Size));

  Address ByteDst = Dst.withElementType(CGF.Int8Ty);
  for (uint64_t I = 0; I != Size; ++I) {
    unsigned Shift = 8 * (Size - I - 1);
    llvm::Value *Byte = Val;
    if (Shift)
      Byte = CGF.Builder.CreateLShr(Byte, Shift);
    Byte = CGF.Builder.CreateTrunc(Byte, CGF.Int8Ty);
    Address ByteAddr = CGF.Builder.CreateConstInBoundsByteGEP(
        ByteDst, CharUnits::fromQuantity(I));
    CGF.Builder.CreateStore(Byte, ByteAddr, DestIsVolatile);
  }
}

void MMIXABIInfo::computeInfo(CGFunctionInfo &FI) const {
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  for (auto &Arg : FI.arguments())
    Arg.info = classifyArgumentType(Arg.type);
}

RValue MMIXABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                              QualType Ty, AggValueSlot Slot) const {
  if (Ty->isAtomicType() ||
      (!isSupportedMMIXComplexType(Ty) &&
       isUnsupportedMMIXScalarType(getContext(), Ty, /*AllowVoid=*/false))) {
    unsigned DiagID = CGF.CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Error,
        "MMIX GNU ABI does not support va_arg type %0");
    SourceLocation Loc =
        CGF.CurCodeDecl ? CGF.CurCodeDecl->getLocation() : SourceLocation();
    CGF.CGM.getDiags().Report(Loc, DiagID) << Ty;

    if (const auto *ComplexTy = Ty->getAs<ComplexType>()) {
      llvm::Type *ElementTy = CGF.ConvertType(ComplexTy->getElementType());
      llvm::Value *Poison = llvm::PoisonValue::get(ElementTy);
      return RValue::getComplex(Poison, Poison);
    }
    return RValue::get(llvm::PoisonValue::get(CGF.ConvertType(Ty)));
  }

  if (isSupportedMMIXComplexType(Ty)) {
    QualType ElementTy = Ty->castAs<ComplexType>()->getElementType();
    bool IsIndirect =
        !ElementTy->isSpecificBuiltinType(BuiltinType::Float);
    return emitVoidPtrVAArg(
        CGF, VAListAddr, Ty, IsIndirect, getContext().getTypeInfoInChars(Ty),
        CharUnits::fromQuantity(8), /*AllowHigherAlign=*/false, Slot,
        /*ForceRightAdjust=*/true);
  }

  if (isAggregateTypeForABI(Ty)) {
    ABIArgInfo AI = classifyAggregateArgument(Ty);
    if (AI.isIgnore())
      return Slot.asRValue();

    return emitVoidPtrVAArg(
        CGF, VAListAddr, Ty, /*IsIndirect=*/AI.isIndirect(),
        getContext().getTypeInfoInChars(Ty), CharUnits::fromQuantity(8),
        /*AllowHigherAlign=*/false, Slot, /*ForceRightAdjust=*/true);
  }

  return emitVoidPtrVAArg(
      CGF, VAListAddr, Ty, /*IsIndirect=*/false,
      getContext().getTypeInfoInChars(Ty), CharUnits::fromQuantity(8),
      /*AllowHigherAlign=*/false, Slot, /*ForceRightAdjust=*/true);
}

void MMIXTargetCodeGenInfo::checkFunctionABI(CodeGenModule &CGM,
                                             const FunctionDecl *FD) const {
  if (FD->hasAttr<NakedAttr>() || FD->hasAttr<TargetAttr>()) {
    StringRef Attribute = FD->hasAttr<NakedAttr>() ? "naked" : "target";
    unsigned DiagID = CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Error,
        "MMIX does not support the '%0' function attribute");
    CGM.getDiags().Report(FD->getLocation(), DiagID) << Attribute;
    return;
  }

  if (FD->getNumParams() != 0)
    diagnoseUnsupportedMMIXVariadicSignature(
        CGM, FD->getParamDecl(FD->getNumParams() - 1)->getLocation(), FD);

  ASTContext &Context = CGM.getContext();
  QualType ReturnType = FD->getReturnType();
  if (!(isDeferredMMIXBoundaryType(ReturnType) &&
        diagnoseUnsupportedMMIXObject(CGM, FD->getLocation(), ReturnType)) &&
      !diagnoseUnsupportedMMIXAggregateResult(CGM, FD->getLocation(),
                                              ReturnType) &&
      isUnsupportedMMIXBoundaryScalarType(Context, ReturnType,
                                          /*AllowVoid=*/true))
    diagnoseUnsupportedMMIXScalar(CGM, FD->getLocation(), "return", ReturnType);

  for (const ParmVarDecl *Param : FD->parameters()) {
    QualType Ty = Param->getType();
    if (isDeferredMMIXBoundaryType(Ty) &&
        diagnoseUnsupportedMMIXObject(CGM, Param->getLocation(), Ty))
      continue;
    if (diagnoseUnsupportedMMIXAggregateArgument(CGM, Param->getLocation(), Ty))
      continue;
    if (isUnsupportedMMIXBoundaryScalarType(Context, Ty,
                                            /*AllowVoid=*/false))
      diagnoseUnsupportedMMIXScalar(CGM, Param->getLocation(), "argument", Ty);
  }

  if (const Stmt *Body = FD->getBody()) {
    MMIXCodeGenBoundaryVisitor Visitor(CGM);
    Visitor.TraverseStmt(const_cast<Stmt *>(Body));
  }
}

void MMIXTargetCodeGenInfo::checkFunctionCallABI(
    CodeGenModule &CGM, SourceLocation CallLoc, const FunctionDecl *,
    const FunctionDecl *Callee, const CallArgList &Args,
    QualType ReturnType) const {
  diagnoseUnsupportedMMIXVariadicSignature(CGM, CallLoc, Callee);
  diagnoseUnsupportedMMIXVariadicCallArguments(CGM, CallLoc, Callee, Args);

  ASTContext &Context = CGM.getContext();
  if (!(isDeferredMMIXBoundaryType(ReturnType) &&
        diagnoseUnsupportedMMIXObject(CGM, CallLoc, ReturnType)) &&
      !diagnoseUnsupportedMMIXAggregateResult(CGM, CallLoc, ReturnType) &&
      isUnsupportedMMIXBoundaryScalarType(Context, ReturnType,
                                          /*AllowVoid=*/true))
    diagnoseUnsupportedMMIXScalar(CGM, CallLoc, "return", ReturnType);

  for (const CallArg &Arg : Args) {
    QualType Ty = Arg.getType();
    if (isDeferredMMIXBoundaryType(Ty) &&
        diagnoseUnsupportedMMIXObject(CGM, CallLoc, Ty))
      continue;
    if (diagnoseUnsupportedMMIXAggregateArgument(CGM, CallLoc, Ty))
      continue;
    if (isUnsupportedMMIXBoundaryScalarType(Context, Ty,
                                            /*AllowVoid=*/false))
      diagnoseUnsupportedMMIXScalar(CGM, CallLoc, "argument", Ty);
  }
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createMMIXTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<MMIXTargetCodeGenInfo>(CGM.getTypes());
}
