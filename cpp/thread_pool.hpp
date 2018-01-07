#ifndef THREAD_POOL_HPP_INCLUDED
#define THREAD_POOL_HPP_INCLUDED

/**
 @file thread_pool.hpp
 @author François Becker
 
MIT License

Copyright (c) 2015-2018 François Becker

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <thread>
#include <vector>
#include <list>

/**
 @class ThreadPool
 @brief A simple ThreadPool class that is instantiated with a fixed number of
 threads, a function allows to add jobs, a function allows to wait for jobs
 termination.
 */
class ThreadPool
{
    std::vector<std::thread> mThreads;
    std::atomic_bool mTerminate;
    std::atomic_int mNumBusyThreads;
    std::condition_variable mJobAvailableCV;
    std::condition_variable mCompletionCV;
    std::list< std::function<void(void)> > mJobsQueue;
    std::mutex mJobsQueueMutex;
    
public:
    /**
     Constructor.
     @param pNumThreads The number of threads in the ThreadPool. Use 0 for an
     automatic guess of the number of concurrent threads supported by the
     hardware platform. If it can't be determined at runtime, a default number
     of 2 threads is considered.
     */
    ThreadPool(int pNumThreads = 0)
    : mThreads(pNumThreads != 0
               ? pNumThreads
               : (std::thread::hardware_concurrency() != 0
                  ? std::thread::hardware_concurrency()
                  : 2))
    , mTerminate(false)
    {
        for (std::thread& t : mThreads)
        {
            t = std::thread([this]{ this->threadExecLoop(); });
        }
    }
    
    /**
     Destructor.
     Note: call waitForCompletion() beforehand, otherwise the ~ThreadPool will
     not wait for all the jobs to be processed.
     */
    ~ThreadPool()
    {
        mTerminate = true;
        mJobAvailableCV.notify_all();
        for (std::thread& t : mThreads)
        {
            t.join();
        }
    }
    
    /**
     Add a job.
     */
    void addJob(std::function<void(void)> pJob)
    {
        std::lock_guard<std::mutex> lGuard(mJobsQueueMutex);
        mJobsQueue.emplace_back(pJob);
        mJobAvailableCV.notify_one();
    }
    
    /**
     Wait for the jobs to be completed. Call this method prior to destroying the
     ThreadPool.
     */
    void waitForCompletion()
    {
        std::unique_lock<std::mutex> lLock(mJobsQueueMutex);
        while (mJobsQueue.size() > 0 && mNumBusyThreads > 0)
        {
            mCompletionCV.wait(lLock);
        }
    }
    
private:
    void threadExecLoop()
    {
        while (!mTerminate)
        {
            auto lJob = grabNextJob();
            lJob();
            --mNumBusyThreads;
            mCompletionCV.notify_all();
        }
    }
    
    std::function<void(void)> grabNextJob()
    {
        std::function<void(void)> lJob;
        std::unique_lock<std::mutex> lLock(mJobsQueueMutex);
        
        mJobAvailableCV.wait(lLock, [this]() -> bool { return mJobsQueue.size() || mTerminate; });
        
        ++mNumBusyThreads;
        if (!mTerminate)
        {
            lJob = mJobsQueue.front();
            mJobsQueue.pop_front();
        }
        else
        {
            lJob = []{};
        }
        return lJob;
    }
};

#endif
