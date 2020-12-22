/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */
#ifndef VC4C_CONTROLFLOWLOOP_H
#define VC4C_CONTROLFLOWLOOP_H 1

#include "../Expression.h"
#include "../Graph.h"
#include "../InstructionWalker.h"
#include "../performance.h"
#include "../tools/SmallSet.h"
#include "Optional.h"

#include <memory>
#include <string>

namespace vc4c
{
    class BasicBlock;

    namespace analysis
    {
        class CFGRelation;
        using CFGNode = Node<BasicBlock*, CFGRelation, Directionality::BIDIRECTIONAL>;
        class DataDependencyGraph;

        /**
         * "Variable i in loop L is called induction variable of L if each time i changes value in L,it is
         * incremented/decremented by loop-invariant value."
         * - https://www.cs.princeton.edu/courses/archive/spring03/cs320/notes/loops.pdf, slide 29
         *
         * We especially care about induction variables, since the loop iteration variable is usually an induction
         * variable. Example: i in for(int i = 0; i < xxx; ++i)
         */
        struct InductionVariable
        {
            // The local associated with this induction variable
            Local* local;
            // The initial value assignment to the induction variable
            const intermediate::IntermediateInstruction* initialAssignment;
            // The expression calculating the actual value change for every loop iteration
            const intermediate::Operation* inductionStep;
            // The condition to hold to repeat the loop as the pair of comparison and the compared-to value. This might
            // not be valid for all induction variables, only for loop iteration variables
            Optional<std::pair<const char*, Value>> repeatCondition = {};
            // Whether the repeat condition is checked on the induction variable itself before the step is applied
            // (true) or after the step is applied (false, default)
            bool conditionCheckedBeforeStep = false;

            std::string to_string() const;

            /**
             * Returns the initial value.
             *
             * NOTE: This does not have to be smaller than the upper bound!
             */
            Optional<Literal> getLowerBound() const;

            /**
             * Returns the value at which to abort the induction.
             *
             * NOTE: This does not have to be larger than the lower bound!
             * NOTE: This value does not necessarily be included in the range!
             */
            Optional<Literal> getUpperBound() const;

            /**
             * Returns the step for each iteration.
             */
            Optional<Literal> getStep() const;

            /**
             * Returns the total range between the lower and the upper bounds
             */
            Optional<unsigned> getRange() const;

            /**
             * Returns the static number of iterations using the lower and upper bounds and the iteration step.
             */
            Optional<unsigned> getIterationCount() const;
        };

        /**
         * A natural loop in the control-flow represented by the basic-blocks taking part in it
         *
         * A natural loop is defined as:
         *   "The natural loop of a back edge (m -> n), where n dominates m, is the set of nodes x such that n dominates
         *   x and there is a path from x to m not containing n"
         * Source: https://www.cs.colostate.edu/~mstrout/CS553Fall09/Slides/lecture12-control.ppt.pdf#page=6
         *
         * NOTE: A control-flow loop can only be used within the life-time of the ControlFlowGraph it is created from!
         */
        class ControlFlowLoop : public FastSet<const CFGNode*>
        {
        public:
            ControlFlowLoop(const CFGNode::EdgeType* backEdge) : backEdge(backEdge) {}

            /*
             * Returns the basic-block(s) in the CFG preceding the first node in the loop, the node from which the loop
             * is entered.
             *
             * NOTE: The single return value variant returns NULL if there are multiple predecessors!
             */
            const CFGNode* findPredecessor() const;
            tools::SmallSortedPointerSet<const CFGNode*> findPredecessors() const;

            /*
             * Returns the basic-block in the CFG following the last node in the loop, the node into which this loop
             * exits into.
             *
             * NOTE: The single return value variant returns NULL if there are multiple successors!
             */
            const CFGNode* findSuccessor() const;
            tools::SmallSortedPointerSet<const CFGNode*> findSuccessors() const;

            /*
             * Returns the InstructionWalker for the given instruction, if it is within the loop.
             */
            Optional<InstructionWalker> findInLoop(const intermediate::IntermediateInstruction* inst) const;

            /*
             * Returns whether this loop includes other loop and doesn't equal it.
             */
            bool includes(const ControlFlowLoop& other) const;

            /*
             * Returns the list of induction variables of this loop.
             *
             * The parameter includeIterationInformation defines whether to try to find additional information which is
             * only useful when trying to determine the loop iteration variable.
             */
            FastAccessList<InductionVariable> findInductionVariables(
                const DataDependencyGraph& dependencyGraph, bool includeIterationInformation) const;

            /*
             * Returns this loop's header, or a nullptr if the header could not be deduced.
             *
             * The loop header is the only node inside the loop which has direct predecessor nodes that are not inside
             * the loop.
             */
            const CFGNode* getHeader() const;

            /**
             * Returns this loop's tail, or a nullptr if the tail could not be deduced.
             *
             * The loop tail is the node from which the actual looping back-jump to the loop header is executed.
             */
            const CFGNode* getTail() const;

            /**
             * Iterates through all the instructions in the loop and returns loop invariant instructions that do not
             * depend on any dynamically calculated value inside the loop, i.e. all instructions that calculate the same
             * value independent of the loop iteration.
             *
             * NOTE: Not all instructions marked as invariant can be moved, since some might have side-effects which
             * change the behavior of the program is moved!
             *
             * @return the list of marked loop invariant instructions
             */
            FastSet<InstructionWalker> findLoopInvariants();

            /**
             * Returns whether at least one edge in this loop is part of the work-group loop optimization and therefore
             * this loop is a version of the work-group loop.
             */
            bool isWorkGroupLoop() const;

            bool operator==(const ControlFlowLoop& other) const noexcept;
            bool operator!=(const ControlFlowLoop& other) const noexcept;

            std::string to_string() const;

        private:
            const CFGNode::EdgeType* backEdge;
        };

        /*
         * A relation in the control-flow-loop
         */
        struct LoopInclusion
        {
        };

        struct LoopInclusionTreeNodeBase
        {
            virtual ~LoopInclusionTreeNodeBase() noexcept;

            LoopInclusionTreeNodeBase* findRoot(Optional<int> depth);
            unsigned int getLongestPathToRoot() const;
            bool hasCFGNodeInChildren(const CFGNode* node) const;

            std::string to_string() const;
        };

        using LoopInclusionTreeNode =
            Node<const ControlFlowLoop*, LoopInclusion, Directionality::DIRECTED, LoopInclusionTreeNodeBase>;
        using LoopInclusionTreeEdge = LoopInclusionTreeNode::EdgeType;

        LoopInclusionTreeNode* castToTreeNode(LoopInclusionTreeNodeBase* base);
        const LoopInclusionTreeNode* castToTreeNode(const LoopInclusionTreeNodeBase* base);

        /*
         * The trees represents inclusion relation of control-flow loops. This may have multiple trees.
         */
        using LoopInclusionTree = Graph<const ControlFlowLoop*, LoopInclusionTreeNode>;

        /**
         * Create the tree of loop inclusions from the given list of detected control flow loops.
         *
         * A loop A includes another loop B if A includes all basic blocks that are part of B and A != B.
         *
         * Examples:
         *
         * loop A {
         *     loop B {
         *         loop C {
         *         }
         *     }
         *     loop D {
         *     }
         * }
         *
         * to:
         *       +-+
         *  +----+A+
         *  |    +++
         *  |     |
         *  v     v
         * +-+   +++   +-+
         * |D|   |B+-->+C|
         * +-+   +-+   +-+
         */
        std::unique_ptr<LoopInclusionTree> createLoopInclusingTree(const FastAccessList<ControlFlowLoop>& loops);
    } // namespace analysis
} // namespace vc4c

#endif /* VC4C_CONTROLFLOWLOOP_H */
