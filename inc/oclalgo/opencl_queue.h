/*! @file opencl_queue.h
 *  @brief Class for simple OpenCL API usage
 *  @author Senin Dmitry <d.senin@samsung.com>
 *  @version 1.0
 *
 *  @section Notes
 *  Use OpencCL C++ Wrapper API. Create one in-order OpenCL command queue.
 *
 *  @section Copyright
 *  Copyright 2013 Samsung R&D Institute Russia
 */

#ifndef OCLALGO_OPENCLQUEUE_H_
#define OCLALGO_OPENCLQUEUE_H_

#define __CL_ENABLE_EXCEPTIONS

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <tuple>
#include <type_traits>
#include <string>
#include <fstream>
#include <iostream>
#include <CL/cl.hpp>

namespace oclalgo {

enum DataType { IN, OUT, IN_OUT, LOCALE, VAR };

template<typename T, oclalgo::DataType DT>
struct cl_data_t {
  cl_data_t()
      : host_ptr(0),
        size(0) {
  }

  cl_data_t(const T& hostPtr, size_t size)
      : host_ptr(hostPtr),
        size(size) {
  }

  T host_ptr;
  size_t size;
  static constexpr oclalgo::DataType io_type = DT;
};

/**
 * @brief Class for synchronization OpenCLQueue task with host thread
 */
template<typename T>
class cl_future {
 public:
  cl_future(const cl::Event& event, const std::vector<cl::Buffer>& buffers,
            const T& outVal)
      : event_(event),
        buffers_(buffers),
        out_val_(outVal) {
  }

  cl_future(const cl_future&) = delete;
  cl_future& operator=(const cl_future&) = delete;

  cl_future(cl_future&& other)
      : event_(other.event_),
        buffers_(other.buffers_),
        out_val_(other.out_val_) {
  }

  virtual ~cl_future() = default;

  /**
   * @brief Stop host thread and wait the end of OpenCL task,
   * then return the refreshed host data
   */
  virtual T get() {
    event_.wait();
    return out_val_;
  }
  /**
   * @brief Stop host thread and wait the end of OpenCL task
   */
  virtual void wait() const {
    event_.wait();
  }

 private:
  cl::Event event_;
  std::vector<cl::Buffer> buffers_;
  T out_val_;
};

/**
 * @brief Class for simple execution of OpenCL kernels
 *
 * Use OpenCL C++ Wrapper API. Provide a OpenCL task synchronization by cl_future<Args...>
 * objects, which have similar interface and functionality like std::future
 */
class OpenCLQueue {
 public:
  OpenCLQueue(const std::string& platformName, const std::string& deviceName);
  OpenCLQueue(const OpenCLQueue&) = delete;
  OpenCLQueue& operator=(const OpenCLQueue&) = delete;

  /**
   * @brief Add task in OpenCL in-order queue
   */
  template<typename First, typename ... Tail>
  auto AddTask(
      const std::string& pathToProgram,
      const std::string& kernelName,
      const cl::NDRange& offset,
      const cl::NDRange& global,
      const cl::NDRange& local,
      const First& clData,
      const Tail& ... args)
      -> cl_future<decltype(std::tuple_cat(ReturnOutData(clData), ComposeOutTuple(args...)))>;

  static std::string OpenCLInfo(bool completeInfo);
  static std::string StatusStr(cl_int status);

 private:
  static std::string PlatformInfo(const std::vector<cl::Platform>& platforms,
                                  size_t platformId, bool completeInfo);
  static std::string DeviceInfo(const std::vector<cl::Device>& devices,
                                size_t deviceId, bool completeInfo);

  template<typename First, typename ... Tail>
  void SetKernelArgs(uint32_t argIndex, cl::Kernel* kernel,
                     std::vector<cl::Buffer>* buffers, const First& clData,
                     const Tail&... args) const;
  template<typename First>
  void SetKernelArgs(uint32_t argIndex, cl::Kernel* kernel,
                     std::vector<cl::Buffer>* buffers,
                     const First& clData) const;
  template<typename First, typename ... Tail>
  void GetResults(uint32_t argIndex, const std::vector<cl::Buffer>& buffers,
                  cl::Event* event, const First& clData,
                  const Tail&... args) const;
  template<typename First>
  void GetResults(uint32_t argIndex, const std::vector<cl::Buffer>& buffers,
                  cl::Event* event, const First& clData) const;

  size_t platform_id_;
  size_t device_id_;
  std::vector<cl::Platform> platforms_;
  std::vector<cl::Device> devices_;
  cl::Context context_;
  cl::CommandQueue queue_;
  std::unordered_map<std::string, cl::Program> programs_;
  std::unordered_map<std::string, cl::Kernel> kernels_;
};

template<typename T>
typename std::enable_if<T::io_type != OUT && T::io_type != IN_OUT,
std::tuple<>>::type ReturnOutData(const T&) {
  return std::tuple<>();
}

template<typename T>
typename std::enable_if<T::io_type == OUT || T::io_type == IN_OUT,
std::tuple<T>>::type ReturnOutData(const T& data) {
   return std::make_tuple(data);
}

template<typename First>
auto ComposeOutTuple(const First& data) -> decltype(ReturnOutData(data)) {
  return ReturnOutData(data);
}

template<typename First, typename ... Tail>
auto ComposeOutTuple(const First& data, const Tail& ... args)
    -> decltype(std::tuple_cat(ReturnOutData(data), ComposeOutTuple(args...))) {
  return std::tuple_cat(ReturnOutData(data), ComposeOutTuple(args...));
}

template<typename First, typename ... Tail>
auto OpenCLQueue::AddTask(
    const std::string& pathToProgram,
    const std::string& kernelName,
    const cl::NDRange& offset,
    const cl::NDRange& global,
    const cl::NDRange& local,
    const First& clData,
    const Tail& ... args)
    -> cl_future<decltype(std::tuple_cat(ReturnOutData(clData), ComposeOutTuple(args...)))> {

  // load source code
  bool build_flag = false;
  if (programs_.find(pathToProgram) == programs_.end()) {
    std::ifstream source_file(pathToProgram);
    std::string source_code(std::istreambuf_iterator<char>(source_file),
                            (std::istreambuf_iterator<char>()));
    cl::Program::Sources cl_source(
        1, std::make_pair(source_code.c_str(), source_code.length() + 1));

    // build program from source code
    programs_[pathToProgram] = cl::Program(context_, cl_source);
    try {
      programs_[pathToProgram].build( { devices_[device_id_] }, "-D BLOCK_SIZE=2");
    } catch (const cl::Error& e) {
      std::cout << "Build log:" << std::endl
          << programs_[pathToProgram].
          getBuildInfo<CL_PROGRAM_BUILD_LOG>(devices_[device_id_]) << std::endl;
      throw(e);
    }
    build_flag = true;
  }
  // create OpenCL kernel
  auto kernel_id_str = pathToProgram + "; " + kernelName;
  if (build_flag || kernels_.find(kernel_id_str) == kernels_.end()) {
    kernels_[kernel_id_str] = cl::Kernel(programs_[pathToProgram],
                                         kernelName.c_str());
  }

  // fill OpenCL command queue
  std::vector<cl::Buffer> buffers;
  cl::Event event;
  SetKernelArgs(0, &kernels_[kernel_id_str], &buffers, clData, args...);
  queue_.enqueueNDRangeKernel(kernels_[kernel_id_str], offset, global, local,
                              nullptr, &event);
  GetResults(0, buffers, &event, clData, args...);

  // providing output data into the cl_future object
  auto t = ComposeOutTuple(clData, args...);
  return cl_future<decltype(t)>(event, buffers, t);
}

template<typename First, typename ... Tail>
void OpenCLQueue::SetKernelArgs(uint32_t argIndex, cl::Kernel* kernel,
                                std::vector<cl::Buffer>* buffers,
                                const First& clData,
                                const Tail&... args) const {
  SetKernelArgs(argIndex, kernel, buffers, clData);
  SetKernelArgs(++argIndex, kernel, buffers, args...);
}

template<typename First>
void OpenCLQueue::SetKernelArgs(uint32_t argIndex, cl::Kernel* kernel,
                                std::vector<cl::Buffer>* buffers,
                                const First& clData) const {
  cl::Buffer buffer;
  switch (First::io_type) {
    case oclalgo::IN:
      buffer = cl::Buffer(context_, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                          clData.size, clData.host_ptr);
      buffers->push_back(buffer);
      kernel->setArg(argIndex, buffer);
      break;
    case oclalgo::IN_OUT:
      buffer = cl::Buffer(context_, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                          clData.size, clData.host_ptr);
      buffers->push_back(buffer);
      kernel->setArg(argIndex, buffer);
      break;
    case oclalgo::OUT:
      buffer = cl::Buffer(context_, CL_MEM_WRITE_ONLY, clData.size);
      buffers->push_back(buffer);
      kernel->setArg(argIndex, buffer);
      break;
    case oclalgo::LOCALE:
      kernel->setArg(argIndex, cl::Local(clData.size));
      break;
    case oclalgo::VAR:
      kernel->setArg(argIndex, *clData.host_ptr);
      break;
  }
}

template<typename First, typename ... Tail>
void OpenCLQueue::GetResults(uint32_t argIndex,
                             const std::vector<cl::Buffer>& buffers,
                             cl::Event* event, const First& clData,
                             const Tail&... args) const {
  GetResults(argIndex, buffers, event, clData);
  GetResults(++argIndex, buffers, event, args...);
}

template<typename First>
void OpenCLQueue::GetResults(uint32_t argIndex,
                             const std::vector<cl::Buffer>& buffers,
                             cl::Event* event, const First& clData) const {
  if (First::io_type == oclalgo::OUT || First::io_type == oclalgo::IN_OUT) {
    queue_.enqueueReadBuffer(buffers[argIndex], CL_FALSE, 0, clData.size,
                             clData.host_ptr, NULL, event);
  }
}

} // namespace oclalgo

#endif // OCLALGO_OPENCLQUEUE_H_