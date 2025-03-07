/*
Copyright (c) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
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

/**
 * @addtogroup hipStreamCreateWithPriority hipStreamCreateWithPriority
 * @{
 * @ingroup StreamTest
 * `hipStreamCreateWithPriority (hipStream_t *stream, unsigned int flags, int priority)` -
 * begins graph capture on a stream
 */

#include <hip_test_kernels.hh>
#include <atomic>
#include <vector>
#include "streamCommon.hh" // NOLINT

#define MEMCPYSIZE1 (64*1024*1024)
#define MEMCPYSIZE2 (1024*1024)
#define NUMITERS   2
#define GRIDSIZE   1024
#define BLOCKSIZE  256
#define TOTALTHREADS 16

namespace hipStreamCreateWithPriorityTest {

std::atomic<int> g_thTestPassed(1);
// helper rountine to initialize memory
template <typename T>
void mem_init(T* buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    buf[i] = i;
  }
}

// kernel to copy n elements from src to dst
template <typename T>
__global__ void memcpy_kernel(T* dst, T* src, size_t n) {
  int num = gridDim.x * blockDim.x;
  int id = blockDim.x * blockIdx.x + threadIdx.x;

  for (size_t i = id; i < n; i += num) {
    dst[i] = src[i];
  }
}

/**
 * Scenario: Create a stream for all available priority levels
 * and queue tasks in each of these streams and default stream.
 * Validate the calculated results.
 */
void funcTestsForAllPriorityLevelsWrtNullStrm(unsigned int flags,
                                        bool deviceSynchronize) {
  int priority;
  int priority_low{};
  int priority_high{};
  size_t size = MEMCPYSIZE2 * sizeof(int);
  // Test is to get the Stream Priority Range
  HIP_CHECK(hipDeviceGetStreamPriorityRange(&priority_low, &priority_high));

  // Check if priorities are indeed supported
  if (priority_low == priority_high) {
    WARN("Stream priority range not supported. Skipping test.");
    return;
  }

  int numOfPriorities = priority_low - priority_high;
  INFO("numOfPriorities = " << numOfPriorities);
  const int arr_size = numOfPriorities + 1;
  // 0 idx is for default stream
  hipStream_t *stream = reinterpret_cast<hipStream_t*>(
                       malloc(arr_size*sizeof(hipStream_t)));
  REQUIRE(stream != nullptr);
  stream[0] = 0;
  int count = 1;
  // Create a stream for each of the priority levels
  for (priority = priority_high; priority < priority_low; priority++) {
    HIP_CHECK(hipStreamCreateWithPriority(&stream[count++],
            flags, priority));
  }
  // Allocate memory
  int **A_d = reinterpret_cast<int**>(malloc(arr_size*sizeof(int*)));
  int **C_d = reinterpret_cast<int**>(malloc(arr_size*sizeof(int*)));
  int **A_h = reinterpret_cast<int**>(malloc(arr_size*sizeof(int*)));
  int **C_h = reinterpret_cast<int**>(malloc(arr_size*sizeof(int*)));

  REQUIRE(A_d != nullptr);
  REQUIRE(C_d != nullptr);
  REQUIRE(A_h != nullptr);
  REQUIRE(C_h != nullptr);

  for (int idx = 0; idx < arr_size; idx++) {
    A_h[idx] = reinterpret_cast<int*>(malloc(size));
    REQUIRE(A_h[idx] != nullptr);
    C_h[idx] = reinterpret_cast<int*>(malloc(size));
    REQUIRE(C_h[idx] != nullptr);
    HIP_CHECK(hipMalloc(&A_d[idx], size));
    HIP_CHECK(hipMalloc(&C_d[idx], size));
  }

  // Initialize host memory
  constexpr int initVal = 2;
  for (int idx = 0; idx < arr_size; idx++) {
    for (int idy = 0; idy < MEMCPYSIZE2; idy++) {
      A_h[idx][idy] = initVal;
    }
  }

  // Launch task on each stream
  for (int idx = 0; idx < arr_size; idx++) {
    HIP_CHECK(hipMemcpyAsync(A_d[idx], A_h[idx], size,
            hipMemcpyHostToDevice, stream[idx]));
    hipLaunchKernelGGL((HipTest::vector_square), dim3(GRIDSIZE),
                        dim3(BLOCKSIZE), 0, stream[idx], A_d[idx],
                        C_d[idx], MEMCPYSIZE2);
    HIP_CHECK(hipGetLastError());
    HIP_CHECK(hipMemcpyAsync(C_h[idx], C_d[idx], size,
            hipMemcpyDeviceToHost, stream[idx]));
  }

  if (deviceSynchronize) {
    HIP_CHECK(hipDeviceSynchronize());
  }

  // Validate the output of each queue
  for (int idx = 0; idx < arr_size; idx++) {
    if (!deviceSynchronize) {
      HIP_CHECK(hipStreamSynchronize(stream[idx]));
    }
    for (int idy = 0; idy < MEMCPYSIZE2; idy++) {
      if (C_h[idx][idy] != A_h[idx][idy] * A_h[idx][idy]) {
        INFO("Data mismatch at idx:" << idx << " idy:" << idy);
        REQUIRE(false);
      }
    }
  }

  // Deallocate memory
  for (int idx = 0; idx < arr_size; idx++) {
    HIP_CHECK(hipFree(reinterpret_cast<void*>(C_d[idx])));
    HIP_CHECK(hipFree(reinterpret_cast<void*>(A_d[idx])));
    free(C_h[idx]);
    free(A_h[idx]);
  }

  // Destroy the stream for each of the priority levels
  count = 1;
  for (priority = priority_high; priority < priority_low; priority++) {
    HIP_CHECK(hipStreamDestroy(stream[count++]));
  }
  free(stream);
  free(A_d);
  free(C_d);
  free(A_h);
  free(C_h);
}

/**
 * Scenario: Queue tasks in each of these streams and default stream.
 * Validate the calculated results.
 */
void queueTasksInStreams(std::vector<hipStream_t> *stream,
                         const int arrsize) {
  size_t size = MEMCPYSIZE2 * sizeof(int);
  // Allocate memory
  int **A_d = reinterpret_cast<int**>(malloc(arrsize*sizeof(int *)));
  int **C_d = reinterpret_cast<int**>(malloc(arrsize*sizeof(int *)));
  int **A_h = reinterpret_cast<int**>(malloc(arrsize*sizeof(int *)));
  int **C_h = reinterpret_cast<int**>(malloc(arrsize*sizeof(int *)));

  HIPASSERT(A_d != nullptr);
  HIPASSERT(C_d != nullptr);
  HIPASSERT(A_h != nullptr);
  HIPASSERT(C_h != nullptr);

  for (int idx = 0; idx < arrsize; idx++) {
    A_h[idx] = reinterpret_cast<int*>(malloc(size));
    HIPASSERT(A_h[idx] != nullptr);
    C_h[idx] = reinterpret_cast<int*>(malloc(size));
    HIPASSERT(C_h[idx] != nullptr);
    HIP_CHECK(hipMalloc(&A_d[idx], size));
    HIP_CHECK(hipMalloc(&C_d[idx], size));
  }
  // Initialize host memory
  constexpr int initVal = 2;
  for (int idx = 0; idx < arrsize; idx++) {
    for (int idy = 0; idy < MEMCPYSIZE2; idy++) {
      A_h[idx][idy] = initVal;
    }
  }
  // Launch task on each stream
  for (int idx = 0; idx < arrsize; idx++) {
    HIP_CHECK(hipMemcpyAsync(A_d[idx], A_h[idx], size,
             hipMemcpyHostToDevice, (*stream)[idx]));
    hipLaunchKernelGGL((HipTest::vector_square), dim3(GRIDSIZE),
                        dim3(BLOCKSIZE), 0, (*stream)[idx], A_d[idx],
                        C_d[idx], MEMCPYSIZE2);
    HIP_CHECK(hipGetLastError());
    HIP_CHECK(hipMemcpyAsync(C_h[idx], C_d[idx], size,
             hipMemcpyDeviceToHost, (*stream)[idx]));
  }

  bool isPassed = true;
  // Validate the output of each queue
  for (int idx = 0; idx < arrsize; idx++) {
    HIP_CHECK(hipStreamSynchronize((*stream)[idx]));
    for (int idy = 0; idy < MEMCPYSIZE2; idy++) {
      if (C_h[idx][idy] != A_h[idx][idy] * A_h[idx][idy]) {
        UNSCOPED_INFO("Data mismatch at idx:" << idx << " idy:" << idy);
        isPassed = false;
        break;
      }
    }
    if (false == isPassed) break;
  }
  // Deallocate memory
  for (int idx = 0; idx < arrsize; idx++) {
    HIP_CHECK(hipFree(reinterpret_cast<void*>(C_d[idx])));
    HIP_CHECK(hipFree(reinterpret_cast<void*>(A_d[idx])));
    free(C_h[idx]);
    free(A_h[idx]);
  }
  free(A_d);
  free(C_d);
  free(A_h);
  free(C_h);
  g_thTestPassed &= static_cast<int>(isPassed);
}

/**
 * Scenario:
 * Common streams used across multiple threads:Create a stream for each
 * priority level (flag = hipStreamDefault/hipStreamNonBlocking)
 * and 1 default stream.
 * Launch memcpy and kernel tasks on these streams from multiple threads
 * (use 16 threads). Validate all the results.
*/
bool runFuncTestsForAllPriorityLevelsMultThread(unsigned int flags) {
  bool TestPassed = true;
  std::thread T[TOTALTHREADS];
  int priority;
  int priority_low;
  int priority_high;
  std::vector<hipStream_t> stream_set{};
  hipStream_t stream{};
  // Test is to get the Stream Priority Range
  HIP_CHECK(hipDeviceGetStreamPriorityRange(&priority_low, &priority_high));

  // Check if priorities are indeed supported
  if (priority_low == priority_high) {
    WARN("Stream priority range not supported. Skipping test.");
    return true;
  }

  int numOfPriorities = priority_low - priority_high + 1;
  INFO("numOfPriorities : " << numOfPriorities);

  // Create a stream for each of the priority levels
  for (priority = priority_high; priority <= priority_low; priority++) {
    HIP_CHECK(hipStreamCreateWithPriority(&stream, flags,
                                         priority));
    stream_set.push_back(stream);
  }

  for (int i = 0; i < TOTALTHREADS; i++) {
    T[i] = std::thread(queueTasksInStreams,
                       &stream_set, numOfPriorities);
  }

  for (int i=0; i < TOTALTHREADS; i++) {
    T[i].join();
  }
  if (g_thTestPassed) {
    TestPassed = true;
  } else {
    TestPassed = false;
  }

  // Destroy the stream for each of the priority levels
  size_t set_size = stream_set.size();
  for (int i = 0; i < set_size; i++) {
    HIP_CHECK(hipStreamDestroy(stream_set[i]));
  }
  return TestPassed;
}


template <typename T>
bool validateStreamPrioritiesWithEvents() {
  size_t size = NUMITERS * MEMCPYSIZE1;

  // get the range of priorities available
  #define OP(x) \
     int priority_##x; \
     bool enable_priority_##x = false;
  OP(low)
  OP(normal)
  OP(high)
  #undef OP
  HIP_CHECK(hipDeviceGetStreamPriorityRange(&priority_low, &priority_high));

  INFO("HIP stream priority range - low: " << priority_low << ",high: "
                                         << priority_high << ",normal: "
                                         << (priority_low + priority_high)/2);
  // Check if priorities are indeed supported
  if (priority_low == priority_high) {
    WARN("Stream priority range not supported. Skipping test.");
    return true;
  }

  // Enable/disable priorities based on number of available priority levels
  enable_priority_low = true;
  enable_priority_high = true;
  if ((priority_low - priority_high) > 1) {
    enable_priority_normal = true;
  }
  if (enable_priority_normal) {
    priority_normal = ((priority_low + priority_high) / 2);
  }
  // create streams with highest and lowest available priorities
  #define OP(x)\
    hipStream_t stream_##x;\
    if (enable_priority_##x) {\
      HIP_CHECK(hipStreamCreateWithPriority(&stream_##x, \
              hipStreamDefault, priority_##x));\
    }
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  // allocate and initialise host source and destination buffers
  #define OP(x) \
    T* src_h_##x; \
    T* dst_h_##x; \
    if (enable_priority_##x) { \
      src_h_##x = reinterpret_cast<T*>(malloc(size)); \
      REQUIRE(src_h_##x != nullptr); \
      mem_init<T>(src_h_##x, (size / sizeof(T))); \
      dst_h_##x = reinterpret_cast<T*>(malloc(size)); \
      REQUIRE(dst_h_##x != nullptr); \
      memset(dst_h_##x, 0, size); \
    }
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  // allocate and initialize device source and destination buffers
  #define OP(x) \
    T* src_d_##x; \
    T* dst_d_##x; \
    if (enable_priority_##x) { \
      HIP_CHECK(hipMalloc(&src_d_##x, size)); \
      HIP_CHECK( \
        hipMemcpy(src_d_##x, src_h_##x, size, hipMemcpyHostToDevice)); \
      HIP_CHECK(hipMalloc(&dst_d_##x, size)); \
    }
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  // create events for measuring time spent in kernel execution
  #define OP(x) \
    hipEvent_t event_start_##x; \
    hipEvent_t event_end_##x; \
    if (enable_priority_##x) { \
      HIP_CHECK(hipEventCreate(&event_start_##x)); \
      HIP_CHECK(hipEventCreate(&event_end_##x)); \
    }
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  // record start events for each of the priority streams
  #define OP(x) \
    if (enable_priority_##x) { \
      HIP_CHECK(\
      hipEventRecord(event_start_##x, stream_##x)); \
    }
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  // launch kernels repeatedly on each of the prioritiy streams
  for (int i = 0; i < static_cast<int>(size); i += MEMCPYSIZE1) {
    int j = i / sizeof(T);
    #define OP(x) \
      if (enable_priority_##x) { \
        hipLaunchKernelGGL((memcpy_kernel<T>), dim3(GRIDSIZE), \
        dim3(BLOCKSIZE), 0, stream_##x, dst_d_##x + j, src_d_##x + j, \
        (MEMCPYSIZE1 / sizeof(T))); \
        HIP_CHECK(hipGetLastError()); \
      }
    OP(low)
    OP(normal)
    OP(high)
    #undef OP
  }

  // record end events for each of the priority streams
  #define OP(x) \
    if (enable_priority_##x) { \
      HIP_CHECK(hipEventRecord(event_end_##x, stream_##x)); \
    }
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  // synchronize events for each of the priority streams
  #define OP(x) \
    if (enable_priority_##x) { \
      HIP_CHECK(hipEventSynchronize(event_end_##x)); \
    }
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  // compute time spent for memcpy in each stream
  #define OP(x) \
    float time_spent_##x; \
    if (enable_priority_##x) { \
      HIP_CHECK(hipEventElapsedTime(&time_spent_##x, \
              event_start_##x, event_end_##x)); \
      INFO("time spent for memcpy in " << #x << \
      " priority stream: " << time_spent_##x << " ms"); \
    }
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  // sanity check
  #define OP(x) \
    if (enable_priority_##x) { \
      HIP_CHECK(hipMemcpy(dst_h_##x, dst_d_##x, size, \
                         hipMemcpyDeviceToHost)); \
      if (memcmp(dst_h_##x, src_h_##x, size) != 0) { \
        REQUIRE(false); \
      } \
    }
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  // destroy stream
  #define OP(x) \
    if (enable_priority_##x) { \
      HIP_CHECK(hipStreamDestroy(stream_##x)); \
    }
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  // validate that stream priorities are working as expected
  #define OP(x, y) \
      if (enable_priority_##x && enable_priority_##y) { \
          if ((1.05f * time_spent_##x) < time_spent_##y) { \
            INFO("time_spent_##x : " << time_spent_##x << \
            "time_spent_##y : " << time_spent_##y); \
            REQUIRE(false); \
          } \
      }
  OP(low, normal)
  OP(normal, high)
  OP(low, high)
  #undef OP

  // free host & device memory
  #define OP(x) \
    free(src_h_##x); \
    free(dst_h_##x); \
    HIP_CHECK(hipFree(src_d_##x)); \
    HIP_CHECK(hipFree(dst_d_##x));
  OP(low)
  OP(normal)
  OP(high)
  #undef OP

  return true;
}

#define LOW_PRIORITY_STREAMCOUNT 2
#define HIGH_PRIORITY_STREAMCOUNT 2
#define NORMAL_PRIORITY_STREAMCOUNT 2
template <typename T>
void TestForMultipleStreamWithPriority(void) {
  size_t size = NUMITERS * MEMCPYSIZE1;
  // get the range of priorities available
  #define OP(x) \
      int priority_##x; \
      bool enable_priority_##x = false;
  OP(low)
  OP(normal)
  OP(high)
  #undef OP
  HIP_CHECK(hipDeviceGetStreamPriorityRange(&priority_low, &priority_high));
  INFO("HIP stream priority range - low: " << priority_low << ",high: "
                                         << priority_high << ",normal: "
                                         << (priority_low + priority_high)/2);

  // Check if priorities are indeed supported
  // Enable/disable priorities based on number of available priority levels
  enable_priority_low = true;
  enable_priority_high = true;
  if ((priority_low - priority_high) > 1) {
    enable_priority_normal = true;
  }
  if (enable_priority_normal) {
    priority_normal = ((priority_low + priority_high) / 2);
  }
  // create streams with low priority
  hipStream_t stream_low[LOW_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < LOW_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_low) {
      HIP_CHECK(hipStreamCreateWithPriority(&stream_low[i],
               hipStreamDefault, priority_low));
    }
  }
  // create streams with normal priority
  hipStream_t stream_normal[NORMAL_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < NORMAL_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_normal) {
      HIP_CHECK(hipStreamCreateWithPriority(&stream_normal[i],
               hipStreamDefault, priority_normal));
    }
  }
  // create streams with high priority
  hipStream_t stream_high[HIGH_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < HIGH_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_high) {
      HIP_CHECK(hipStreamCreateWithPriority(&stream_high[i],
               hipStreamDefault, priority_high));
    }
  }
  // allocate and initialise host source and destination buffers for
  // low streams
  T* src_h_low[LOW_PRIORITY_STREAMCOUNT];
  T* dst_h_low[LOW_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < LOW_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_low) {
      src_h_low[i] = reinterpret_cast<T*>(malloc(size));
      REQUIRE(src_h_low[i] != nullptr);
      mem_init<T>(src_h_low[i], (size / sizeof(T)));
      dst_h_low[i] = reinterpret_cast<T*>(malloc(size));
      REQUIRE(dst_h_low[i] != nullptr);
      memset(dst_h_low[i], 0, size);
    }
  }
  // allocate and initialise host source and destination buffers for
  // normal streams
  T* src_h_normal[NORMAL_PRIORITY_STREAMCOUNT];
  T* dst_h_normal[NORMAL_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < NORMAL_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_normal) {
      src_h_normal[i] = reinterpret_cast<T*>(malloc(size));
      REQUIRE(src_h_normal[i] != nullptr);
      mem_init<T>(src_h_normal[i], (size / sizeof(T)));
      dst_h_normal[i] = reinterpret_cast<T*>(malloc(size));
      REQUIRE(dst_h_normal[i] != nullptr);
      memset(dst_h_normal[i], 0, size);
    }
  }
  // allocate and initialise host source and destination buffers for
  // high streams
  T* src_h_high[HIGH_PRIORITY_STREAMCOUNT];
  T* dst_h_high[HIGH_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < HIGH_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_high) {
      src_h_high[i] = reinterpret_cast<T*>(malloc(size));
      REQUIRE(src_h_high[i] != nullptr);
      mem_init<T>(src_h_high[i], (size / sizeof(T)));
      dst_h_high[i] = reinterpret_cast<T*>(malloc(size));
      REQUIRE(dst_h_high[i] != nullptr);
      memset(dst_h_high[i], 0, size);
    }
  }
  // allocate and initialize device source and destination buffers for
  // low streams
  T* src_d_low[LOW_PRIORITY_STREAMCOUNT];
  T* dst_d_low[LOW_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < LOW_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_low) {
      HIP_CHECK(hipMalloc(&src_d_low[i], size));
      HIP_CHECK(hipMemcpy(src_d_low[i], src_h_low[i], size,
                hipMemcpyHostToDevice));
      HIP_CHECK(hipMalloc(&dst_d_low[i], size));
    }
  }
  // allocate and initialize device source and destination buffers for
  // normal streams
  T* src_d_normal[NORMAL_PRIORITY_STREAMCOUNT];
  T* dst_d_normal[NORMAL_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < NORMAL_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_normal) {
      HIP_CHECK(hipMalloc(&src_d_normal[i], size));
      HIP_CHECK(hipMemcpy(src_d_normal[i], src_h_normal[i], size,
                hipMemcpyHostToDevice));
      HIP_CHECK(hipMalloc(&dst_d_normal[i], size));
    }
  }
  // allocate and initialize device source and destination buffers for
  // high streams
  T* src_d_high[HIGH_PRIORITY_STREAMCOUNT];
  T* dst_d_high[HIGH_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < HIGH_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_high) {
      HIP_CHECK(hipMalloc(&src_d_high[i], size));
      HIP_CHECK(hipMemcpy(src_d_high[i], src_h_high[i], size,
                hipMemcpyHostToDevice));
      HIP_CHECK(hipMalloc(&dst_d_high[i], size));
    }
  }

  hipEvent_t event_start_low[LOW_PRIORITY_STREAMCOUNT];
  hipEvent_t event_end_low[LOW_PRIORITY_STREAMCOUNT];
  // create events for measuring time spent in kernel execution for low
  // priority streams
  for (int i = 0; i < LOW_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_low) {
      HIP_CHECK(hipEventCreate(&event_start_low[i]));
      HIP_CHECK(hipEventCreate(&event_end_low[i]));
    }
  }

  hipEvent_t event_start_normal[NORMAL_PRIORITY_STREAMCOUNT];
  hipEvent_t event_end_normal[NORMAL_PRIORITY_STREAMCOUNT];
  // create events for measuring time spent in kernel execution for
  // normal priority streams
  for (int i = 0; i < NORMAL_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_normal) {
      HIP_CHECK(hipEventCreate(&event_start_normal[i]));
      HIP_CHECK(hipEventCreate(&event_end_normal[i]));
    }
  }

  hipEvent_t event_start_high[HIGH_PRIORITY_STREAMCOUNT];
  hipEvent_t event_end_high[HIGH_PRIORITY_STREAMCOUNT];
  // create events for measuring time spent in kernel execution for
  // high priority streams
  for (int i = 0; i < HIGH_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_high) {
      HIP_CHECK(hipEventCreate(&event_start_high[i]));
      HIP_CHECK(hipEventCreate(&event_end_high[i]));
    }
  }
  // record start events for each of the low priority streams
  for (int i = 0; i < LOW_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_low) {
      HIP_CHECK(hipEventRecord(event_start_low[i], stream_low[i]));
    }
  }
  // record start events for each of the normal priority streams
  for (int i = 0; i < NORMAL_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_normal) {
      HIP_CHECK(hipEventRecord(event_start_normal[i], stream_normal[i]));
    }
  }
  // record start events for each of the high priority streams
  for (int i = 0; i < HIGH_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_high) {
      HIP_CHECK(hipEventRecord(event_start_high[i], stream_high[i]));
    }
  }
  // launch kernels repeatedly on each of the low prioritiy stream
  for (int k = 0; k < LOW_PRIORITY_STREAMCOUNT; ++k) {
    for (int i = 0; i < size; i += MEMCPYSIZE1) {
      int j = i / sizeof(T);
      if (enable_priority_low) {
        hipLaunchKernelGGL((memcpy_kernel<T>), dim3(GRIDSIZE), dim3(BLOCKSIZE),
        0, stream_low[k], dst_d_low[k] + j, src_d_low[k] + j,
        (MEMCPYSIZE1 / sizeof(T)));
      }
    }
  }
  // launch kernels repeatedly on each of the normal prioritiy stream
  for (int k = 0; k < NORMAL_PRIORITY_STREAMCOUNT; ++k) {
    for (int i = 0; i < size; i += MEMCPYSIZE1) {
      int j = i / sizeof(T);
      if (enable_priority_normal) {
        hipLaunchKernelGGL((memcpy_kernel<T>), dim3(GRIDSIZE), dim3(BLOCKSIZE),
        0, stream_normal[k], dst_d_normal[k] + j, src_d_normal[k] + j,
       (MEMCPYSIZE1 / sizeof(T)));
      }
    }
  }
  // launch kernels repeatedly on each of the high prioritiy stream
  for (int k = 0; k < HIGH_PRIORITY_STREAMCOUNT; ++k) {
    for (int i = 0; i < size; i += MEMCPYSIZE1) {
      int j = i / sizeof(T);
      if (enable_priority_high) {
        hipLaunchKernelGGL((memcpy_kernel<T>), dim3(GRIDSIZE), dim3(BLOCKSIZE),
        0, stream_high[k], dst_d_high[k] + j, src_d_high[k] + j,
       (MEMCPYSIZE1 / sizeof(T)));
      }
    }
  }
  // record end events for each of the low priority streams
  for (int i = 0; i < LOW_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_low) {
      HIP_CHECK(hipEventRecord(event_end_low[i], stream_low[i]));
    }
  }
  // record end events for each of the normal priority streams
  for (int i = 0; i < NORMAL_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_normal) {
      HIP_CHECK(hipEventRecord(event_end_normal[i], stream_normal[i]));
    }
  }
  // record end events for each of the high priority streams
  for (int i = 0; i < HIGH_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_high) {
      HIP_CHECK(hipEventRecord(event_end_high[i], stream_high[i]));
    }
  }
  // synchronize events for each of the low priority streams
  for (int i = 0; i < LOW_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_low) {
      HIP_CHECK(hipEventSynchronize(event_end_low[i]));
    }
  }
  // synchronize events for each of the normal priority streams
  for (int i = 0; i < NORMAL_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_normal) {
      HIP_CHECK(hipEventSynchronize(event_end_normal[i]));
    }
  }
  // synchronize events for each of the high priority streams
  for (int i = 0; i < HIGH_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_high) {
      HIP_CHECK(hipEventSynchronize(event_end_high[i]));
    }
  }
  // compute time spent for memcpy in each low stream
  float time_spent_low[LOW_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < LOW_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_low) {
      HIP_CHECK(hipEventElapsedTime(&time_spent_low[i], event_start_low[i],
                event_end_low[i]));
    }
  }
  // compute time spent for memcpy in each normal stream
  float time_spent_normal[NORMAL_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < NORMAL_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_normal) {
      HIP_CHECK(hipEventElapsedTime(&time_spent_normal[i],
                event_start_normal[i], event_end_normal[i]));
    }
  }
  // compute time spent for memcpy in each high stream
  float time_spent_high[HIGH_PRIORITY_STREAMCOUNT];
  for (int i = 0; i < HIGH_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_high) {
      HIP_CHECK(hipEventElapsedTime(&time_spent_high[i], event_start_high[i],
                event_end_high[i]));
    }
  }
  // sanity check for low priority streams
  for (int i = 0; i < LOW_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_low) {
      HIP_CHECK(hipMemcpy(dst_h_low[i], dst_d_low[i], size,
                hipMemcpyDeviceToHost));
      REQUIRE(memcmp(dst_h_low[i], src_h_low[i], size) == 0);
    }
  }
  // sanity check for normal priority streams
  for (int i = 0; i < NORMAL_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_normal) {
      HIP_CHECK(hipMemcpy(dst_h_normal[i], dst_d_normal[i], size,
                hipMemcpyDeviceToHost));
      REQUIRE(memcmp(dst_h_normal[i], src_h_normal[i], size) == 0);
    }
  }
  // sanity check for high priority streams
  for (int i = 0; i < HIGH_PRIORITY_STREAMCOUNT; ++i) {
    if (enable_priority_high) {
      HIP_CHECK(hipMemcpy(dst_h_high[i], dst_d_high[i], size,
                hipMemcpyDeviceToHost));
      REQUIRE(memcmp(dst_h_high[i], src_h_high[i], size) == 0);
    }
  }
}
}  // namespace hipStreamCreateWithPriorityTest

/**
 * Test Description
 * ------------------------
 *    - Create streams with default flag for all available priority levels and
 * queue tasks in each of these streams, perform device synchronize and validate
 * behavior.
 *    - Create streams with non-blocking flag for all available priority levels
 * and queue tasks in each of these streams, perform stream synchronize and
 * validate behavior.
 *    - Create streams with default flag for all available priority levels and
 * queue tasks in each of these streams, perform stream synchronize and validate
 * behavior.
 *    - Create streams with non-blocking flag for all available priority levels
 * and queue tasks in each of these streams, perform device synchronize and validate
 * behavior.
 * Test source
 * ------------------------
 *    - catch\unit\stream\hipStreamCreateWithPriority.cc
 * Test requirements
 * ------------------------
 *    - HIP_VERSION >= 5.2
 */
TEST_CASE("Unit_hipStreamCreateWithPriority_FunctionalForAllPriorities") {
  SECTION("Default flag and device synchronize") {
    hipStreamCreateWithPriorityTest::
    funcTestsForAllPriorityLevelsWrtNullStrm(hipStreamDefault, true);
  }

  SECTION("Stream non-blocking flag and stream synchronize") {
    hipStreamCreateWithPriorityTest::
    funcTestsForAllPriorityLevelsWrtNullStrm(hipStreamNonBlocking, false);
  }

  SECTION("Default flag and stream synchronize") {
    hipStreamCreateWithPriorityTest::
    funcTestsForAllPriorityLevelsWrtNullStrm(hipStreamDefault, false);
  }

  SECTION("Stream non-blocking flag and device synchronize") {
    hipStreamCreateWithPriorityTest::
    funcTestsForAllPriorityLevelsWrtNullStrm(hipStreamNonBlocking, true);
  }
}

/**
 * Test Description
 * ------------------------
 *    - Create a stream for each priority level with default flag, Launch
 * memcpy and kernel tasks on these streams from multiple threads. Validate
 * all the results.
 * ------------------------
 *    - catch\unit\stream\hipStreamCreateWithPriority.cc
 * Test requirements
 * ------------------------
 *    - HIP_VERSION >= 5.2
 */
TEST_CASE("Unit_hipStreamCreateWithPriority_MulthreadDefaultflag") {
  bool TestPassed = true;
  TestPassed = hipStreamCreateWithPriorityTest::
  runFuncTestsForAllPriorityLevelsMultThread(hipStreamDefault);
  REQUIRE(TestPassed);
}

/**
 * Test Description
 * ------------------------
 *    - Create a stream for each priority level with non-blocking flag, Launch
 * memcpy and kernel tasks on these streams from multiple threads. Validate all
 * the results.
 * ------------------------
 *    - catch\unit\stream\hipStreamCreateWithPriority.cc
 * Test requirements
 * ------------------------
 *    - HIP_VERSION >= 5.2
 */
TEST_CASE("Unit_hipStreamCreateWithPriority_MulthreadNonblockingflag") {
  bool TestPassed = true;
  TestPassed = hipStreamCreateWithPriorityTest::
  runFuncTestsForAllPriorityLevelsMultThread(hipStreamNonBlocking);
  REQUIRE(TestPassed);
}

/**
 * Test Description
 * ------------------------
 *    - Validates functionality of hipStreamCreateWithPriority when stream = nullptr
 *    - Validates functionality of hipStreamCreateWithPriority when flag = 0xffffffff
 * ------------------------
 *    - catch\unit\stream\hipStreamCreateWithPriority.cc
 * Test requirements
 * ------------------------
 *    - HIP_VERSION >= 5.2
 */
TEST_CASE("Unit_hipStreamCreateWithPriority_NegTst") {
  hipStream_t stream{nullptr};
  int priority_low{0};
  int priority_high{0};

  // Test is to get the Stream Priority Range
  HIP_CHECK(hipDeviceGetStreamPriorityRange(&priority_low, &priority_high));
  // Check if priorities are indeed supported
  if (priority_low == priority_high) {
    WARN("Stream priority range not supported. Skipping test.");
    return;  // exit the test since priorities are not supported
  }

  SECTION("stream = nullptr test") {
    REQUIRE(hipErrorInvalidValue ==
        hipStreamCreateWithPriority(nullptr, hipStreamDefault, priority_low));
  }

  SECTION("flag value invalid test") {
    REQUIRE(hipErrorInvalidValue ==
        hipStreamCreateWithPriority(&stream, 0xffffffff, priority_low));
  }
}

/**
 * Test Description
 * ------------------------
 *    - Set and Get Priority Value
 * ------------------------
 *    - catch\unit\stream\hipStreamCreateWithPriority.cc
 * Test requirements
 * ------------------------
 *    - HIP_VERSION >= 5.2
 */
TEST_CASE("Unit_hipStreamCreateWithPriority_CheckPriorityVal") {
  int id = GENERATE(range(0, HipTest::getDeviceCount()));

  HIP_CHECK(hipSetDevice(id));

  int priority_low = 0, priority_high = 0;
  HIP_CHECK(hipDeviceGetStreamPriorityRange(&priority_low, &priority_high));
  hipStream_t stream{nullptr};

  SECTION("Setting high priority") {
    HIP_CHECK(hipStreamCreateWithPriority(&stream, hipStreamDefault,
              priority_high));
    REQUIRE(stream != nullptr);
    REQUIRE(hip::checkStreamPriorityAndFlags(stream, priority_high));
  }

  SECTION("Setting low priority") {
    HIP_CHECK(hipStreamCreateWithPriority(&stream, hipStreamDefault,
              priority_low));
    REQUIRE(stream != nullptr);
    REQUIRE(hip::checkStreamPriorityAndFlags(stream, priority_low));
  }

  SECTION("Setting lowest possible priority") {
    HIP_CHECK(hipStreamCreateWithPriority(&stream, hipStreamDefault,
              std::numeric_limits<int>::max()));
    REQUIRE(stream != nullptr);
    REQUIRE(hip::checkStreamPriorityAndFlags(stream, priority_low));
  }

  SECTION("Setting highest possible priority") {
    HIP_CHECK(hipStreamCreateWithPriority(&stream, hipStreamDefault,
              std::numeric_limits<int>::min()));
    REQUIRE(stream != nullptr);
    REQUIRE(hip::checkStreamPriorityAndFlags(stream, priority_high));
  }

  SECTION("Setting flags to hipStreamNonBlocking") {
    HIP_CHECK(hipStreamCreateWithPriority(&stream, hipStreamNonBlocking,
              priority_high));
    REQUIRE(stream != nullptr);
    REQUIRE(hip::checkStreamPriorityAndFlags(stream, priority_high,
            hipStreamNonBlocking));
  }

  HIP_CHECK(hipStreamDestroy(stream));
}

/**
 * Test Description
 * ------------------------
 *    - Validate stream priorities with event after classifying them as low,
 * medium and high.
 * ------------------------
 *    - catch\unit\stream\hipStreamCreateWithPriority.cc
 * Test requirements
 * ------------------------
 *    - HIP_VERSION >= 5.2
 */
TEST_CASE("Unit_hipStreamCreateWithPriority_ValidateWithEvents") {
  bool TestPassed = true;
  TestPassed =
  hipStreamCreateWithPriorityTest::validateStreamPrioritiesWithEvents<int>();
  REQUIRE(TestPassed);
}

/**
 * Test Description
 * ------------------------
 *    - Test that create Multiple streams with priority low, normal and high
 * then these streams used to launch the kernel in random seq high,normal,low.
 * ------------------------
 *    - catch\unit\stream\hipStreamCreateWithPriority.cc
 * Test requirements
 * ------------------------
 *    - HIP_VERSION >= 5.2
 */
TEST_CASE("Unit_hipStreamCreateWithPriority_TestMultipleStreamWithPriority") {
  hipStreamCreateWithPriorityTest::TestForMultipleStreamWithPriority<int>();
}
