/*
   Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.
   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
 */
/*
AtomicAdd on FineGrainMemory
1. The following test scenario verifies
unsafeatomicAdd on fineGrain memory with -mno-unsafe-fp-atomics flag
This testcase works only on gfx90a, gfx940, gfx941, gfx942.
*/

#include<hip_test_common.hh>
#include<hip_test_checkers.hh>
#include<hip_test_features.hh>

#define INC_VAL 10
#define INITIAL_VAL 5
template<typename T>
static __global__ void AtomicCheck(T* Ad, T* result) {
  T inc_val = 10;
  *result = unsafeAtomicAdd(Ad, inc_val);
}


/*unsafeatomicAdd API for the fine grained memory variable
  with -mno-unsafe-fp-atomics flag
Input: Ad{5}, INC_VAL{10}
Output: unsafeatomicAdd API would return 0 and the 0/P is 5
        Generate the assembly file and check whether
        atomic add instruction is generated
        or not */

TEMPLATE_TEST_CASE("Unit_unsafeAtomicAdd_CoherentwithnoUnsafeflag", "",
                   float, double) {
  hipDeviceProp_t prop;
  int device;
  HIP_CHECK(hipGetDevice(&device));
  HIP_CHECK(hipGetDeviceProperties(&prop, device));
  std::string gfxName(prop.gcnArchName);
  if (CheckIfFeatSupported(CTFeatures::CT_FEATURE_FINEGRAIN_HWSUPPORT, gfxName)) {
    if (prop.canMapHostMemory != 1) {
      SUCCEED("Does not support HostPinned Memory");
    } else {
      TestType *A_h{nullptr}, *result{nullptr};
      TestType *A_d{nullptr}, *result_d{nullptr};
      HIP_CHECK(hipHostMalloc(reinterpret_cast<void**>(&A_h), sizeof(TestType),
                              hipHostMallocCoherent));
      A_h[0] = INITIAL_VAL;
      HIP_CHECK(hipHostMalloc(reinterpret_cast<void**>(&result),
                              sizeof(TestType),
                              hipHostMallocCoherent));
      result[0] = INITIAL_VAL;
      HIP_CHECK(hipHostGetDevicePointer(reinterpret_cast<void**>(&A_d),
                                        A_h, 0));
      HIP_CHECK(hipHostGetDevicePointer(reinterpret_cast<void**>(&result_d),
                                        result, 0));
      hipLaunchKernelGGL(AtomicCheck<TestType>, dim3(1), dim3(1),
                         0, 0, A_d,
                         result_d);
      HIP_CHECK(hipGetLastError()); 
      HIP_CHECK(hipDeviceSynchronize());
      bool testResult;

      if ((std::is_same<TestType, float>::value)) {
        testResult = HipTest::assemblyFile_Verification<TestType>(
        "unsafeAtomicAdd_Coherent_withnounsafeflag-hip-amdgcn(.*)\\.s",
        "global_atomic_add_f32");
        REQUIRE(testResult == true);
      } else {
        testResult = HipTest::assemblyFile_Verification<TestType>(
        "unsafeAtomicAdd_Coherent_withnounsafeflag-hip-amdgcn(.*)\\.s",
        "global_atomic_add_f64");
        REQUIRE(testResult == true);
      }

      REQUIRE(A_h[0] == INITIAL_VAL);
      REQUIRE(result[0] == 0);
      HIP_CHECK(hipHostFree(A_h));
      HIP_CHECK(hipHostFree(result));
    }
  } else {
    SUCCEED("Memory model feature is only supported for gfx90a, gfx940, gfx941, gfx942, Hence"
             "skipping the testcase for this GPU " << device);
  }
}
