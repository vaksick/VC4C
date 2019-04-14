/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#include "IntermediateInstruction.h"

#include "../GlobalValues.h"
#include "../asm/Instruction.h"
#include "../periphery/VPM.h"

#include "log.h"

using namespace vc4c;
using namespace vc4c::intermediate;

static bool isDerivedFromMemory(const Local* local)
{
    auto base = local->getBase(true);
    if(base->residesInMemory() || base->is<Parameter>())
        return true;
    bool allSourcesDerivedFromMemory = true;

    local->forUsers(LocalUse::Type::WRITER, [&](const LocalUser* user) {
        if(dynamic_cast<const intermediate::MoveOperation*>(user) != nullptr && user->getArgument(0)->checkLocal())
        {
            if(!isDerivedFromMemory(user->getArgument(0)->local()))
                allSourcesDerivedFromMemory = false;
        }
        else if(dynamic_cast<const intermediate::MemoryInstruction*>(user) != nullptr)
            return;
        else if(auto op = dynamic_cast<const intermediate::Operation*>(user))
        {
            if(op->op != OP_ADD && op->op != OP_SUB)
                allSourcesDerivedFromMemory = false;
            else if(op->getFirstArg().checkLocal() && op->getFirstArg().type.getPointerType() &&
                !op->assertArgument(1).type.getPointerType())
            {
                return;
            }
            else if(op->assertArgument(1).checkLocal() && !op->getFirstArg().type.getPointerType() &&
                op->assertArgument(1).type.getPointerType())
            {
                return;
            }
            else
                allSourcesDerivedFromMemory = false;
        }
        else
        {
            // unknown / not handled operation, assume worst
            CPPLOG_LAZY(
                logging::Level::DEBUG, log << "Unhandled source of pointer: " << user->to_string() << logging::endl);
            allSourcesDerivedFromMemory = false;
        }
    });

    return allSourcesDerivedFromMemory;
}

static void checkMemoryLocation(const Value& val)
{
    if(!val.type.getPointerType())
        throw CompilationError(CompilationStep::LLVM_2_IR, "Operand needs to be a pointer", val.to_string());
    /*
     * TODO some memory locations are not recognized:
     * Pointers with a dynamically set memory location cannot be recognized at compile-time and therefore this check
     * will always fail for them
     */
    if(!val.checkLocal() || !isDerivedFromMemory(val.local()))
        throw CompilationError(CompilationStep::LLVM_2_IR,
            "Operand needs to refer to a memory location or a parameter containing one", val.to_string());
}

static void checkLocalValue(const Value& val)
{
    if(val.checkLocal() &&
        (val.local()->residesInMemory() ||
            (val.local()->is<Parameter>() && (val.local()->type.getPointerType() || val.local()->type.getArrayType()))))
        throw CompilationError(
            CompilationStep::LLVM_2_IR, "Operand needs to be a local value (local, register)", val.to_string());
}

static void checkSingleValue(const Value& val)
{
    if(val.getLiteralValue() == Literal(1u))
        return;
    throw CompilationError(CompilationStep::LLVM_2_IR, "Operand needs to the constant one", val.to_string());
}

MemoryInstruction::MemoryInstruction(const MemoryOperation op, Value&& dest, Value&& src, Value&& numEntries) :
    IntermediateInstruction(std::move(dest)), op(op)
{
    setArgument(0, std::move(src));
    setArgument(1, std::move(numEntries));

    if(numEntries != INT_ONE)
    {
        if(op != MemoryOperation::COPY && op != MemoryOperation::FILL)
            throw CompilationError(
                CompilationStep::LLVM_2_IR, "Can only use the entry count for copying or filling memory", to_string());
    }
}

std::string MemoryInstruction::to_string() const
{
    switch(op)
    {
    case MemoryOperation::COPY:
        return std::string("copy ") + (getNumEntries().to_string() + " entries from ") +
            (getSource().to_string() + " into ") + getDestination().to_string();
    case MemoryOperation::FILL:
        return std::string("fill ") + (getDestination().to_string() + " with ") +
            (getNumEntries().to_string() + " copies of ") + getSource().to_string();
    case MemoryOperation::READ:
        return (getDestination().to_string() + " = load memory at ") + getSource().to_string();
    case MemoryOperation::WRITE:
        return std::string("store ") + (getSource().to_string() + " into ") + getDestination().to_string();
    }
    throw CompilationError(
        CompilationStep::GENERAL, "Unknown memory operation type", std::to_string(static_cast<unsigned>(op)));
}

qpu_asm::DecoratedInstruction MemoryInstruction::convertToAsm(const FastMap<const Local*, Register>& registerMapping,
    const FastMap<const Local*, std::size_t>& labelMapping, std::size_t instructionIndex) const
{
    throw CompilationError(CompilationStep::OPTIMIZER, "There should be no more memory operations", to_string());
}

bool MemoryInstruction::isNormalized() const
{
    return false;
}

bool MemoryInstruction::hasSideEffects() const
{
    return true;
}

IntermediateInstruction* MemoryInstruction::copyFor(Method& method, const std::string& localPrefix) const
{
    return (new MemoryInstruction(op, renameValue(method, getDestination(), localPrefix),
                renameValue(method, getSource(), localPrefix), renameValue(method, getNumEntries(), localPrefix)))
        ->copyExtrasFrom(this);
}

const Value& MemoryInstruction::getSource() const
{
    return assertArgument(0);
}

const Value& MemoryInstruction::getDestination() const
{
    return getOutput().value();
}

const Value& MemoryInstruction::getNumEntries() const
{
    return assertArgument(1);
}

static bool canMoveIntoVPM(const Value& val, bool isMemoryAddress)
{
    if(isMemoryAddress)
    {
        checkMemoryLocation(val);
        // Checks whether the memory-address can be lowered into VPM
        const Local* base = val.local()->getBase(true);
        if(base->type.getElementType().getStructType() ||
            (base->type.getElementType().getArrayType() &&
                base->type.getElementType().getArrayType()->elementType.getStructType()))
            // cannot lower structs/arrays of structs into VPM
            return false;
        const DataType inVPMType = periphery::VPM::getVPMStorageType(base->type.getElementType());
        if(inVPMType.getPhysicalWidth() > VPM_DEFAULT_SIZE)
            return false;
        if(auto global = base->as<Global>())
            /*
             * Constant globals can be moved into VPM (actually completely into constant values), since they do not
             * change. Non-constant globals on the other side cannot be moved to the VPM, since they might lose their
             * values in the next work-group. Local memory is mapped by LLVM into globals with __local address space,
             * but can be lowered to VPM, since it is only used within one work-group
             */
            return global->isConstant ||
                (base->type.getPointerType() && base->type.getPointerType()->addressSpace == AddressSpace::LOCAL);
        if(base->is<Parameter>())
            /*
             * Since parameter are used outside of the kernel execution (host-side), they cannot be lowered into VPM.
             * XXX The only exception are __local parameter, which are not used outside of the work-group and can
             * therefore handled as local values.
             */
            return false;
        if(base->is<StackAllocation>())
            // the stack can always be lowered into VPM (if it fits!)
            // TODO could be optimized by determining the actual number of work-items in work-group
            return (inVPMType.getPhysicalWidth() * NUM_QPUS /* number of stacks */) < VPM_DEFAULT_SIZE;
        // for any other value, do not lower
        return false;
    }

    /*
     * Checks whether the local value (local, register) can be lifted into VPM.
     * This can be useful for operations performing memory-copy without QPU-side access to skip the steps of loading
     * into QPU and writing back to VPM.
     */
    if(!val.checkLocal())
        // any non-local cannot be moved to VPM
        return false;

    return std::all_of(val.local()->getUsers().begin(), val.local()->getUsers().end(), [](const auto& pair) -> bool {
        // TODO enable if handled correctly by optimizations (e.g. combination of read/write into copy)
        return false; // return dynamic_cast<const MemoryInstruction*>(pair.first) != nullptr;
    });
}

bool MemoryInstruction::canMoveSourceIntoVPM() const
{
    if(op == MemoryOperation::READ || op == MemoryOperation::WRITE)
        checkSingleValue(getNumEntries());
    return canMoveIntoVPM(getSource(), op == MemoryOperation::COPY || op == MemoryOperation::READ);
}

bool MemoryInstruction::canMoveDestinationIntoVPM() const
{
    if(op == MemoryOperation::READ || op == MemoryOperation::WRITE)
        checkSingleValue(getNumEntries());
    return canMoveIntoVPM(getDestination(), op != MemoryOperation::READ);
}

bool MemoryInstruction::accessesConstantGlobal() const
{
    switch(op)
    {
    case MemoryOperation::COPY:
        checkMemoryLocation(getSource());
        checkMemoryLocation(getDestination());
        return (getSource().local()->getBase(true)->is<Global>() &&
                   getSource().local()->getBase(true)->as<Global>()->isConstant) ||
            (getDestination().local()->getBase(true)->is<Global>() &&
                getDestination().local()->getBase(true)->as<Global>()->isConstant);
    case MemoryOperation::FILL:
        checkMemoryLocation(getDestination());
        return getDestination().local()->getBase(true)->is<Global>() &&
            getDestination().local()->getBase(true)->as<Global>()->isConstant;
    case MemoryOperation::READ:
        checkMemoryLocation(getSource());
        return getSource().local()->getBase(true)->is<Global>() &&
            getSource().local()->getBase(true)->as<Global>()->isConstant;
    case MemoryOperation::WRITE:
        checkMemoryLocation(getDestination());
        return getDestination().local()->getBase(true)->is<Global>() &&
            getDestination().local()->getBase(true)->as<Global>()->isConstant;
    }
    return false;
}

bool MemoryInstruction::accessesStackAllocation() const
{
    switch(op)
    {
    case MemoryOperation::COPY:
        checkMemoryLocation(getSource());
        checkMemoryLocation(getDestination());
        return getSource().local()->getBase(true)->is<StackAllocation>() ||
            getDestination().local()->getBase(true)->is<StackAllocation>();
    case MemoryOperation::FILL:
        checkMemoryLocation(getDestination());
        return getDestination().local()->getBase(true)->is<StackAllocation>();
    case MemoryOperation::READ:
        checkMemoryLocation(getSource());
        return getSource().local()->getBase(true)->is<StackAllocation>();
    case MemoryOperation::WRITE:
        checkMemoryLocation(getDestination());
        return getDestination().local()->getBase(true)->is<StackAllocation>();
    }
    return false;
}

static bool isGlobalWithLocalAddressSpace(const Local* local)
{
    return local->is<Global>() && local->type.getPointerType()->addressSpace == AddressSpace::LOCAL;
}

bool MemoryInstruction::accessesLocalMemory() const
{
    switch(op)
    {
    case MemoryOperation::COPY:
        checkMemoryLocation(getSource());
        checkMemoryLocation(getDestination());
        return isGlobalWithLocalAddressSpace(getSource().local()->getBase(true)) ||
            isGlobalWithLocalAddressSpace(getDestination().local()->getBase(true));
    case MemoryOperation::FILL:
        checkMemoryLocation(getDestination());
        return isGlobalWithLocalAddressSpace(getDestination().local()->getBase(true));
    case MemoryOperation::READ:
        checkMemoryLocation(getSource());
        return isGlobalWithLocalAddressSpace(getSource().local()->getBase(true));
    case MemoryOperation::WRITE:
        checkMemoryLocation(getDestination());
        return isGlobalWithLocalAddressSpace(getDestination().local()->getBase(true));
    }
    return false;
}

DataType MemoryInstruction::getSourceElementType(bool sizedType) const
{
    switch(op)
    {
    case MemoryOperation::COPY:
    {
        checkMemoryLocation(getSource());
        DataType elementType = getSource().type.getElementType();
        if(!sizedType)
            // simple pointed-to type
            return elementType;
        // sized pointed-to type
        if(!getNumEntries().isLiteralValue())
            throw CompilationError(CompilationStep::GENERAL,
                "Cannot calculate type-size from dynamically sized memory-operation", to_string());
        return elementType.toArrayType(getNumEntries().getLiteralValue()->unsignedInt());
    }
    case MemoryOperation::FILL:
        // local value
        checkLocalValue(getSource());
        return getSource().type;
    case MemoryOperation::READ:
        // pointed-to type
        checkMemoryLocation(getSource());
        checkSingleValue(getNumEntries());
        return getSource().type.getElementType();
    case MemoryOperation::WRITE:
        // local value
        checkLocalValue(getSource());
        checkSingleValue(getNumEntries());
        return getSource().type;
    }
    throw CompilationError(
        CompilationStep::GENERAL, "Unknown memory operation type", std::to_string(static_cast<unsigned>(op)));
}

DataType MemoryInstruction::getDestinationElementType(bool sizedType) const
{
    switch(op)
    {
    case MemoryOperation::COPY:
        FALL_THROUGH
    case MemoryOperation::FILL:
    {
        checkMemoryLocation(getDestination());
        DataType elementType = getDestination().type.getElementType();
        if(!sizedType)
            // simple pointed-to type
            return elementType;
        // sized pointed-to type
        if(!getNumEntries().isLiteralValue())
            throw CompilationError(CompilationStep::GENERAL,
                "Cannot calculate type-size from dynamically sized memory-operation", to_string());
        return elementType.toArrayType(getNumEntries().getLiteralValue()->unsignedInt());
    }
    case MemoryOperation::READ:
        // local value
        checkLocalValue(getDestination());
        checkSingleValue(getNumEntries());
        return getDestination().type;
    case MemoryOperation::WRITE:
        // pointed-to type
        checkMemoryLocation(getDestination());
        checkSingleValue(getNumEntries());
        return getDestination().type.getElementType();
    }
    throw CompilationError(
        CompilationStep::GENERAL, "Unknown memory operation type", std::to_string(static_cast<unsigned>(op)));
}

FastSet<const Local*> MemoryInstruction::getMemoryAreas() const
{
    FastSet<const Local*> res;
    switch(op)
    {
    case MemoryOperation::COPY:
        checkMemoryLocation(getSource());
        checkMemoryLocation(getDestination());
        res.emplace(getSource().local()->getBase(true));
        res.emplace(getDestination().local()->getBase(true));
        break;
    case MemoryOperation::FILL:
        checkMemoryLocation(getDestination());
        res.emplace(getDestination().local()->getBase(true));
        break;
    case MemoryOperation::READ:
        checkMemoryLocation(getSource());
        res.emplace(getSource().local()->getBase(true));
        break;
    case MemoryOperation::WRITE:
        checkMemoryLocation(getDestination());
        res.emplace(getDestination().local()->getBase(true));
        break;
    }
    return res;
}
