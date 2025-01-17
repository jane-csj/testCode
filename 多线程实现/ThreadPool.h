#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <functional>

class ThreadPool{
public:
    ThreadPool(const ThreadPool& )= delete;
    ThreadPool& operator=(const ThreadPool&)=delete;
    static ThreadPool& instance(){
        static ThreadPool ins;
        return ins;
    }
    using Task = std::packaged_task<void()>;
    ~ThreadPool(){
        stop();
    }
    
    // 提交任务
    template<class F, class... Args>
    auto commit(F&& f, Args&&... args)->std::future<decltype(f(args...))>{
        using Retype = decltype(f(args...));
        // 如果线程池停止了，就直接返回一个空的future对象
        if(stop_.load()){
            return std::future<Retype>{};
        }
        // 创建一个packaged_task对象
        auto task = std::make_shared<std::packaged_task<Retype()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<Retype> ret = task->get_future();
        // 将任务提交到任务队列中
        {
            std::lock_guard<std::mutex> cv_mt(cv_mt_);
            tasks_.emplace([task]{(*task)();});
        }
        // 通知线程池有任务可执行
        cv_lock_.notify_one();
        return ret;
    }
    int idleThreadCount(){
        return thread_num_;
    }
private:
    ThreadPool(unsigned int num = 5)
    :stop_(false)
    {
       { 
            if(num < 1)
                thread_num_ = 1;
            else
                thread_num_ = num;
        }
        start();
        
    }
    void start(){
        for(unsigned int i = 0; i < thread_num_; ++i){
            pool_.emplace_back([this](){
                while (!this->stop_.load())
                {
                    Task task;
                    {
                        std::unique_lock<std::mutex> cv_mt(cv_mt_);
                        this->cv_lock_.wait(cv_mt,[this]{
                            return this->stop_.load() || !this->tasks_.empty();
                        });
                        if(this->tasks_.empty())
                            return;
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    this->thread_num_--;
                    task();
                    this->thread_num_++;
                }
                
            });
        }
    }
    void stop(){
        stop_.store(true);
        cv_lock_.notify_all();
        for(auto& td : pool_){
            if(td.joinable()){
                std::cout << "join thread " << td.get_id() << std::endl;
                td.join();
            }
        }
    }
private:
    std::mutex cv_mt_;
    std::condition_variable cv_lock_;
    std::atomic_bool stop_;
    std::atomic_int thread_num_;
    std::queue<Task> tasks_;
    std::vector<std::thread> pool_;
};
