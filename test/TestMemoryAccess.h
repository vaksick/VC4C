/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#ifndef VC4C_TEST_MEMORY_ACCESS_H
#define VC4C_TEST_MEMORY_ACCESS_H

#include "TestEmulator.h"

class TestMemoryAccess : public TestEmulator
{
public:
    TestMemoryAccess(const vc4c::Configuration& config = {});

    void testPrivateStorage();
    void testLocalStorage();
    void testConstantStorage();
    void testRegisterStorage();

    void testVPMWrites();
    void testVPMReads();

private:
    void onMismatch(const std::string& expected, const std::string& result);
};

#endif /* VC4C_TEST_MEMORY_ACCESS_H */
