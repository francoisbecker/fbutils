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
#include <queue>
#include <atomic>
#include <condition_variable>
#include <string>
#include <type_traits>
#if __APPLE__
#include <pthread.h>
#endif
#include <fbu/lang_utils.hpp>

namespace fbu
{

/**
 @class ThreadPool
 @brief A simple ThreadPool class that is instantiated with a fixed number of
 threads, a function allows to add jobs, a function allows to wait for jobs
 termination.
 */
class ThreadPool
{
    std::vector<std::thread> mThreads;
    std::atomic_bool mTerminate{false};
    std::atomic_int mNumBusyThreads{0};
    std::condition_variable mJobAvailableCV;
    std::condition_variable mCompletionCV;
    std::queue< std::function<void(void)> > mJobsQueue;
    std::mutex mJobsQueueMutex;
    
public:
    /**
     Constructor.
     @param pNumThreads The number of threads in the ThreadPool. Use 0 for an
     automatic guess of the number of concurrent threads supported by the
     hardware platform. If it can't be determined at runtime, a default number
     of 2 threads is considered.
     @param pName The name to give to the threads, as a prefix followed by a number.
     */
    ThreadPool(int pNumThreads = 0, const std::string& pName = "fbu::ThreadPool")
    : mThreads(pNumThreads != 0
               ? (unsigned)pNumThreads
               : (std::thread::hardware_concurrency() != 0
                  ? std::thread::hardware_concurrency()
                  : 2u))
    {
        int i = 0;
        for (std::thread& t : mThreads)
        {
            std::string lThreadName = pName + " " + std::to_string(i);
            t = std::thread([this, lThreadName]{
#if __APPLE__
                pthread_setname_np(lThreadName.c_str());
#else
#pragma message("Thread name not implemented on this platform yet")
#endif
                this->threadExecLoop();
            });
            ++i;
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
        assert(mNumBusyThreads == 0);
    }
    
    /**
     Add a job.
     */
    template <class F>
    void addJob(F&& pJob)
    {
        std::lock_guard<std::mutex> lGuard(mJobsQueueMutex);
        mJobsQueue.push(std::forward<F>(pJob));
        mJobAvailableCV.notify_one();
    }
    
    /**
     Wait for the jobs to be completed. Call this method prior to destroying the
     ThreadPool.
     */
    void waitForCompletion()
    {
        std::unique_lock<std::mutex> lLock(mJobsQueueMutex);
        while (mJobsQueue.size() > 0 || mNumBusyThreads > 0)
        {
            mCompletionCV.wait(lLock);
        }
    }
    
    size_t getNumThreads() const
    {
        return mThreads.size();
    }
    
    int getNumBusyThreads() const
    {
        return mNumBusyThreads;
    }

private:
    void threadExecLoop()
    {
        while (!mTerminate)
        {
            auto lJob = grabNextJob();
            lJob();
            {
                std::unique_lock<std::mutex> lLock(mJobsQueueMutex);
                --mNumBusyThreads;
                mCompletionCV.notify_all();
            }
        }
    }
    
    std::function<void(void)> grabNextJob()
    {
        std::function<void(void)> lJob;
        std::unique_lock<std::mutex> lLock(mJobsQueueMutex);
        
        mJobAvailableCV.wait(lLock, [this]() -> bool { return (mJobsQueue.size() > 0) || mTerminate; });
        
        ++mNumBusyThreads;
        if (!mTerminate)
        {
            assert(!mJobsQueue.empty());
            lJob.swap(mJobsQueue.front());
            mJobsQueue.pop();
        }
        else
        {
            lJob = []{};
        }
        return lJob;
    }
};

/**
 @class ThreadPoolJobsExecutor
 @brief Manages a list of jobs given to a ThreadPool while allowing to wait for
        all jobs that have been passed through this object to be completed.
 @todo deprecate, use JobCounter instead.
 */
class ThreadPoolJobsExecutor
{
public:
    /**
     Constructor.
     @param pTP The ThreadPool to use.
     */
    ThreadPoolJobsExecutor(ThreadPool& pTP)
    : mTP(pTP)
    {
    }
    
    /**
     Add a job.
     */
    void addJob(std::function<void(void)> pJob)
    {
        {
            std::unique_lock<std::mutex> lLock(mMutex);
            ++mNumJobs;
        }
        mTP.addJob([this, pJob](){
            pJob();
            {
                std::unique_lock<std::mutex> lLock(mMutex);
                --mNumJobs;
                mCompletion.notify_all();
            }
        });
    }
    
    /**
     Wait for all the jobs given to the ThreadPool through this object to be completed.
     */
    void waitForCompletion()
    {
        std::unique_lock<std::mutex> lLock(mMutex);
        mCompletion.wait(lLock, [&](){ return mNumJobs == 0; });
    }
    
private:
    ThreadPool& mTP;
    std::mutex mMutex;
    std::condition_variable mCompletion;
    std::atomic_int mNumJobs{0};
};

/**
 @class JobCounter
 A job counter with a completion waiting method.
 Increment prior to submitting a job to a ThreadPool.
 Decrement at the end of the jobs after unlocking all mutexes on shared data.
 Call waitForCompletion() for waiting for all the jobs to be finished.
 @todo unit test.
 */
class JobCounter : public fbu::lang::NonCopyable
{
public:
    JobCounter() {}
    
    void increment()
    {
        std::unique_lock<std::mutex> lLock(mMutex);
        ++mNumJobs;
    }
    
    void decrement()
    {
        std::unique_lock<std::mutex> lLock(mMutex);
        assert(mNumJobs > 0);
        --mNumJobs;
        mCompletion.notify_all();
    }
    
    void waitForCompletion()
    {
        std::unique_lock<std::mutex> lLock(mMutex);
        mCompletion.wait(lLock, [&]{ return mNumJobs == 0; });
    }
    
private:
    std::mutex mMutex;
    std::condition_variable mCompletion;
    std::atomic_int mNumJobs{0};
};

}

#endif
