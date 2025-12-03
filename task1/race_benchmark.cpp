// g++ -std=c++20 -pthread -O2 race_benchmark.cpp -o race_benchmark

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <semaphore>
#include <condition_variable>
#include <barrier>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>

using namespace std;
using namespace std::chrono_literals;

// ==================== Конфигурация ====================
const int DEFAULT_THREADS = 8;
const int CHARS_COUNT = 10;
const int BENCH_ITERATIONS = 5;
const bool SHOW_RACE_OUTPUT = true;

// ==================== Базовый класс ====================
class SyncPrimitive {
public:
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual string name() const = 0;
    virtual ~SyncPrimitive() = default;
};

// ==================== Реализации примитивов ====================
class MutexPrimitive : public SyncPrimitive {
private:
    mutex mtx;
public:
    void lock() override { mtx.lock(); }
    void unlock() override { mtx.unlock(); }
    string name() const override { return "Mutex"; }
};

class SemaphorePrimitive : public SyncPrimitive {
private:
    binary_semaphore sem{1};
public:
    void lock() override { sem.acquire(); }
    void unlock() override { sem.release(); }
    string name() const override { return "Semaphore"; }
};

class SemaphoreSlim : public SyncPrimitive {
private:
    atomic<int> count;
public:
    explicit SemaphoreSlim(int initial = 1) : count(initial) {}
    void lock() override {
        while (true) {
            int expected = count.load(memory_order_acquire);
            if (expected > 0 && count.compare_exchange_strong(expected, expected - 1,
                memory_order_acquire)) {
                return;
                }
                this_thread::yield();
        }
    }
    void unlock() override { count.fetch_add(1, memory_order_release); }
    string name() const override { return "SemaphoreSlim"; }
};

class SpinLockPrimitive : public SyncPrimitive {
private:
    atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void lock() override {
        while (flag.test_and_set(memory_order_acquire)) {}
    }
    void unlock() override { flag.clear(memory_order_release); }
    string name() const override { return "SpinLock"; }
};

class SpinWaitPrimitive : public SyncPrimitive {
private:
    atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void lock() override {
        int attempts = 0;
        while (flag.test_and_set(memory_order_acquire)) {
            if (++attempts > 100) {
                this_thread::yield();
                attempts = 0;
            }
        }
    }
    void unlock() override { flag.clear(memory_order_release); }
    string name() const override { return "SpinWait"; }
};

class MonitorPrimitive : public SyncPrimitive {
private:
    mutable mutex mtx;
    mutable condition_variable cv;
    bool locked = false;
public:
    void lock() override {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this]() { return !locked; });
        locked = true;
    }
    void unlock() override {
        unique_lock<mutex> lock(mtx);
        locked = false;
        cv.notify_one();
    }
    string name() const override { return "Monitor"; }
};

// ==================== BarrierPrimitive ====================
class BarrierPrimitive : public SyncPrimitive {
private:
    mutable mutex mtx;
    mutable condition_variable cv;
    int waiting_threads;
    int total_threads;
    int generation;

public:
    explicit BarrierPrimitive(int threads)
    : waiting_threads(0), total_threads(threads), generation(0) {}

    void lock() override {
        unique_lock<mutex> lock(mtx);
        int gen = generation;

        if (++waiting_threads == total_threads) {
            waiting_threads = 0;
            generation++;
            cv.notify_all();
        } else {
            cv.wait(lock, [this, gen]() { return gen != generation; });
        }
    }

    void unlock() override {
    }

    string name() const override { return "Barrier"; }

    void arrive_and_wait() {
        lock();
    }
};

// ==================== Генератор случайных символов ====================
class RandomCharGenerator {
private:
    random_device rd;
    mt19937 gen;
    uniform_int_distribution<int> dist;
public:
    RandomCharGenerator() : gen(rd()), dist(33, 126) {}
    char generate() { return static_cast<char>(dist(gen)); }
    string generate_string(int count) {
        string result;
        for (int i = 0; i < count; ++i) result += generate();
        return result;
    }
};

// ==================== Демонстрация гонки ====================
void demonstrate_race_with_barrier(SyncPrimitive& prim, int thread_count = DEFAULT_THREADS) {
    cout << "\n=== " << prim.name() << " Race with Barrier Start ===" << endl;

    BarrierPrimitive start_barrier(thread_count);
    vector<thread> threads;
    vector<string> results(thread_count);
    mutex output_mutex;

    auto worker = [&](int id) {
        RandomCharGenerator rng;
        string chars = rng.generate_string(CHARS_COUNT);

        start_barrier.arrive_and_wait();

        prim.lock();
        {
            lock_guard<mutex> lock(output_mutex);
            cout << "Thread " << setw(2) << id << " >> ";
            for (char c : chars) {
                cout << c << " ";
                cout.flush();
                this_thread::sleep_for(2ms);
            }
            cout << endl;
        }
        prim.unlock();

        results[id] = chars;
    };

    for (int i = 0; i < thread_count; ++i) threads.emplace_back(worker, i);
    for (auto& t : threads) t.join();

    cout << "\nGenerated strings:" << endl;
    for (int i = 0; i < thread_count; ++i) {
        cout << "  " << i << ": " << results[i] << endl;
    }
}

// ==================== Демонстрация чистой Barrier синхронизации ====================
void demonstrate_pure_barrier(int thread_count = DEFAULT_THREADS) {
    cout << "\n=== Pure Barrier Synchronization Demo ===" << endl;
    cout << "Threads: " << thread_count << endl;

    BarrierPrimitive barrier(thread_count);
    vector<thread> threads;
    atomic<int> ready_count{0};

    auto worker = [&](int id) {
        this_thread::sleep_for(chrono::milliseconds(id * 50));
        cout << "Thread " << id << " готов к старту" << endl;
        ready_count++;

        barrier.arrive_and_wait();

        cout << "Thread " << id << " СТАРТ!" << endl;

        this_thread::sleep_for(100ms);
        cout << "Thread " << id << " финишировал" << endl;
    };

    for (int i = 0; i < thread_count; ++i) threads.emplace_back(worker, i);
    for (auto& t : threads) t.join();
}

// ==================== Бенчмарк ====================
long long run_benchmark(SyncPrimitive& prim, int num_threads, int work_iterations, int work_in_cs = 100) {
    vector<thread> threads;
    atomic<int> counter{0};
    RandomCharGenerator rng;

    auto worker = [&](int id) {
        for (int i = 0; i < work_iterations; ++i) {
            volatile int compute = 0;
            for (int j = 0; j < 50; ++j) compute += j * id;

            prim.lock();
            for (int j = 0; j < work_in_cs; ++j) {
                char c = rng.generate();
                counter += c;
            }
            prim.unlock();

            for (int j = 0; j < 25; ++j) compute -= j;
        }
    };

    auto start = chrono::high_resolution_clock::now();
    for (int i = 0; i < num_threads; ++i) threads.emplace_back(worker, i);
    for (auto& t : threads) t.join();
    auto end = chrono::high_resolution_clock::now();

    return chrono::duration_cast<chrono::microseconds>(end - start).count();
}

// ==================== Сравнительный анализ всех примитивов ====================
void comparative_analysis(int thread_count = DEFAULT_THREADS) {
    cout << "\n" << string(60, '=') << endl;
    cout << "COMPARATIVE ANALYSIS (" << thread_count << " THREADS)" << endl;
    cout << string(60, '=') << endl;

    MutexPrimitive mutex;
    SemaphorePrimitive semaphore;
    SemaphoreSlim semaphore_slim;
    SpinLockPrimitive spinlock;
    SpinWaitPrimitive spinwait;
    MonitorPrimitive monitor;
    BarrierPrimitive barrier(thread_count);

    vector<SyncPrimitive*> primitives = {
        &mutex, &semaphore, &semaphore_slim, &spinlock, &spinwait, &monitor, &barrier
    };

    vector<pair<string, long long>> results;
    cout << "\nBenchmarking..." << endl;

    for (auto prim : primitives) {
        long long total_time = 0;

        if (prim->name() == "Barrier") {
            vector<thread> threads;
            atomic<long long> barrier_time{0};

            auto barrier_worker = [&](int id) {
                auto start = chrono::high_resolution_clock::now();
                for (int i = 0; i < 100; ++i) {
                    prim->lock();
                    volatile int work = 0;
                    for (int j = 0; j < 50; ++j) work += j;
                    prim->unlock();
                }
                auto end = chrono::high_resolution_clock::now();
                barrier_time += chrono::duration_cast<chrono::microseconds>(end - start).count();
            };

            for (int i = 0; i < thread_count; ++i) {
                threads.emplace_back(barrier_worker, i);
            }
            for (auto& t : threads) {
                t.join();
            }

            total_time = barrier_time / thread_count;
        } else {
            run_benchmark(*prim, thread_count, 10);
            for (int i = 0; i < BENCH_ITERATIONS; ++i) {
                total_time += run_benchmark(*prim, thread_count, 50);
            }
            total_time /= BENCH_ITERATIONS;
        }

        results.emplace_back(prim->name(), total_time);
        cout << "  " << setw(15) << left << prim->name()
        << ": " << total_time << " μs" << endl;
    }

    sort(results.begin(), results.end(),
         [](auto& a, auto& b) { return a.second < b.second; });

    long long best_time = results[0].second;

    cout << "\nResults (sorted by performance):" << endl;
    cout << string(50, '-') << endl;
    for (const auto& [name, time] : results) {
        double relative = (static_cast<double>(time) / best_time) * 100;
        int bars = static_cast<int>(40 * best_time / time);
        cout << setw(15) << left << name
        << setw(10) << time << " μs"
        << setw(10) << fixed << setprecision(1) << relative << "%"
        << " " << string(max(1, bars), '#') << endl;
    }

    ofstream file("results_threads_" + to_string(thread_count) + ".csv");
    file << "Primitive,Time(μs)\n";
    for (const auto& [name, time] : results) file << name << "," << time << "\n";
    file.close();
}

// ==================== Тест масштабируемости ====================
void scalability_test() {
    cout << "\n" << string(60, '=') << endl;
    cout << "SCALABILITY ANALYSIS" << endl;
    cout << string(60, '=') << endl;

    MutexPrimitive mutex;
    vector<int> thread_counts = {1, 2, 4, 8, 16, 32};

    cout << "\n" << setw(8) << "Threads"
    << setw(12) << "Mutex(μs)"
    << setw(12) << "SpinLock(μs)"
    << setw(12) << "Barrier(μs)"
    << setw(15) << "Speedup" << endl;
    cout << string(59, '-') << endl;

    SpinLockPrimitive spinlock;
    long long single_thread_time = 0;

    for (int threads : thread_counts) {
        run_benchmark(mutex, threads, 10);
        run_benchmark(spinlock, threads, 10);

        BarrierPrimitive barrier(threads);
        long long barrier_time = 0;
        {
            vector<thread> barrier_threads;
            atomic<long long> total_barrier_time{0};

            auto barrier_worker = [&](int id) {
                auto start = chrono::high_resolution_clock::now();
                for (int i = 0; i < 100; ++i) {
                    barrier.arrive_and_wait();
                    volatile int work = 0;
                    for (int j = 0; j < 50; ++j) work += j;
                }
                auto end = chrono::high_resolution_clock::now();
                total_barrier_time += chrono::duration_cast<chrono::microseconds>(end - start).count();
            };

            for (int i = 0; i < threads; ++i) {
                barrier_threads.emplace_back(barrier_worker, i);
            }
            for (auto& t : barrier_threads) {
                t.join();
            }
            barrier_time = total_barrier_time / threads;
        }

        long long mutex_time = run_benchmark(mutex, threads, 100);
        long long spinlock_time = run_benchmark(spinlock, threads, 100);

        if (threads == 1) single_thread_time = mutex_time;

        double speedup = static_cast<double>(single_thread_time) / mutex_time;

        cout << setw(8) << threads
        << setw(12) << mutex_time
        << setw(12) << spinlock_time
        << setw(12) << barrier_time
        << setw(15) << fixed << setprecision(2) << speedup << endl;
    }
}

// ==================== Тест длины критической секции ====================
void critical_section_length_test() {
    cout << "\n" << string(60, '=') << endl;
    cout << "CRITICAL SECTION LENGTH IMPACT" << endl;
    cout << string(60, '=') << endl;

    MutexPrimitive mutex;
    SpinLockPrimitive spinlock;
    vector<int> cs_lengths = {1, 10, 100, 1000};

    cout << "\n" << setw(10) << "CS Work"
    << setw(15) << "Mutex(μs)"
    << setw(15) << "SpinLock(μs)"
    << setw(15) << "Barrier(μs)"
    << setw(15) << "Mutex/Spin Ratio" << endl;
    cout << string(70, '-') << endl;

    for (int length : cs_lengths) {
        long long mutex_time = run_benchmark(mutex, 8, 50, length);
        long long spinlock_time = run_benchmark(spinlock, 8, 50, length);

        BarrierPrimitive barrier(8);
        long long barrier_time = 0;
        {
            vector<thread> threads;
            atomic<long long> total_barrier_time{0};

            auto worker = [&](int id) {
                auto start = chrono::high_resolution_clock::now();
                for (int i = 0; i < 50; ++i) {
                    barrier.arrive_and_wait();
                    volatile int work = 0;
                    for (int j = 0; j < length; ++j) work += j;
                }
                auto end = chrono::high_resolution_clock::now();
                total_barrier_time += chrono::duration_cast<chrono::microseconds>(end - start).count();
            };

            for (int i = 0; i < 8; ++i) threads.emplace_back(worker, i);
            for (auto& t : threads) t.join();
            barrier_time = total_barrier_time / 8;
        }

        double ratio = static_cast<double>(mutex_time) / spinlock_time;

        cout << setw(10) << length
        << setw(15) << mutex_time
        << setw(15) << spinlock_time
        << setw(15) << barrier_time
        << setw(15) << fixed << setprecision(2) << ratio << endl;
    }
}

// ==================== Главная функция ====================
int main() {
    srand(time(nullptr));

    cout << "SYNCHRONIZATION PRIMITIVES BENCHMARK SUITE" << endl;
    cout << "==========================================" << endl;
    cout << "All 7 primitives: Mutex, Semaphore, SemaphoreSlim," << endl;
    cout << "SpinLock, SpinWait, Monitor, Barrier" << endl;
    cout << string(50, '=') << endl;

    if (SHOW_RACE_OUTPUT) {
        MutexPrimitive mutex;
        demonstrate_race_with_barrier(mutex, 4);
        demonstrate_pure_barrier(4);
    }

    comparative_analysis(DEFAULT_THREADS);
    scalability_test();
    critical_section_length_test();

    cout << "\n" << string(60, '=') << endl;
    cout << "ALL TESTS COMPLETED" << endl;
    cout << "Results saved to CSV files" << endl;
    cout << string(60, '=') << endl;

    return 0;
}
