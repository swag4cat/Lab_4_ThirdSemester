// g++ -std=c++17 -pthread -O2 bank_operations.cpp -o bank_operations

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <string>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <cmath>

using namespace std;
using namespace std::chrono;

// ==================== Структуры данных ====================

enum class OperationType { DEPOSIT, WITHDRAWAL, TRANSFER, PAYMENT };

struct Date {
    int day, month, year;

    bool operator<(const Date& other) const {
        if (year != other.year) return year < other.year;
        if (month != other.month) return month < other.month;
        return day < other.day;
    }

    bool operator>=(const Date& other) const { return !(*this < other); }
    bool operator<=(const Date& other) const { return *this < other || (year == other.year && month == other.month && day == other.day); }

    string to_string() const {
        char buffer[11];
        snprintf(buffer, sizeof(buffer), "%02d.%02d.%04d", day, month, year);
        return string(buffer);
    }
};

struct BankOperation {
    string card_holder;
    string card_number;
    Date date;
    OperationType operation;
    double amount;

    string to_string() const {
        char buffer[256];
        const char* op_str = "";
        switch(operation) {
            case OperationType::DEPOSIT: op_str = "DEPOSIT"; break;
            case OperationType::WITHDRAWAL: op_str = "WITHDRAWAL"; break;
            case OperationType::TRANSFER: op_str = "TRANSFER"; break;
            case OperationType::PAYMENT: op_str = "PAYMENT"; break;
        }
        snprintf(buffer, sizeof(buffer), "%s | %s | %s | %s | %.2f RUB",
                card_holder.c_str(), card_number.c_str(), date.to_string().c_str(),
                op_str, amount);
        return string(buffer);
    }
};

// ==================== Генератор данных ====================

vector<BankOperation> generate_operations(int count) {
    vector<BankOperation> operations;
    operations.reserve(count);

    mt19937 gen(12345);
    uniform_int_distribution<int> day_dist(1, 28);
    uniform_int_distribution<int> month_dist(1, 12);
    uniform_int_distribution<int> year_dist(2023, 2024);
    uniform_real_distribution<double> amount_dist(100.0, 50000.0);
    uniform_int_distribution<int> op_dist(0, 3);
    uniform_int_distribution<int> name_dist(0, 4);

    const char* first_names[] = {"Ivan", "Petr", "Alex", "Maria", "Olga"};
    const char* last_names[] = {"Ivanov", "Petrov", "Sidorov", "Smirnov", "Kuznetsov"};

    for (int i = 0; i < count; i++) {
        BankOperation op;

        // ФИО
        int name_idx = name_dist(gen);
        op.card_holder = string(last_names[name_idx]) + " " + first_names[name_idx];

        // Номер карты
        char card_num[20];
        unsigned long rand1 = gen() % 10000;
        unsigned long rand2 = gen() % 10000;
        snprintf(card_num, sizeof(card_num), "****%04lu****%04lu", rand1, rand2);
        op.card_number = card_num;

        // Дата
        op.date = {day_dist(gen), month_dist(gen), year_dist(gen)};

        // Тип операции
        op.operation = static_cast<OperationType>(op_dist(gen));

        // Сумма
        op.amount = amount_dist(gen);

        operations.push_back(op);
    }

    return operations;
}

// ==================== Создание нагрузки ====================

void do_work() {
    volatile double result = 0.0;
    for (int j = 0; j < 1000; j++) {
        result += sin(j * 0.01) * cos(j * 0.01);
    }
}

// ==================== Однопоточная обработка ====================

double process_single_thread(const vector<BankOperation>& operations,
                            OperationType target_operation,
                            const Date& start_date,
                            const Date& end_date) {
    double total = 0.0;

    for (const auto& op : operations) {
        do_work();

        if (op.operation == target_operation &&
            op.date >= start_date &&
            op.date <= end_date) {
            total += op.amount;
        }
    }

    return total;
}

// ==================== Многопоточная обработка ====================

double process_multi_thread(const vector<BankOperation>& operations,
                           OperationType target_operation,
                           const Date& start_date,
                           const Date& end_date,
                           int num_threads) {

    if (num_threads <= 1) {
        return process_single_thread(operations, target_operation, start_date, end_date);
    }

    vector<thread> threads;
    vector<double> partial_sums(num_threads, 0.0);
    size_t chunk_size = operations.size() / num_threads;

    auto worker = [&](int thread_id, size_t start_idx, size_t end_idx) {
        double local_sum = 0.0;

        for (size_t i = start_idx; i < end_idx && i < operations.size(); i++) {
            do_work();

            const auto& op = operations[i];
            if (op.operation == target_operation &&
                op.date >= start_date &&
                op.date <= end_date) {
                local_sum += op.amount;
            }
        }

        partial_sums[thread_id] = local_sum;
    };

    // Запускаем потоки
    for (int i = 0; i < num_threads; i++) {
        size_t start = i * chunk_size;
        size_t end = (i == num_threads - 1) ? operations.size() : start + chunk_size;
        threads.emplace_back(worker, i, start, end);
    }

    // Ждем завершения
    for (auto& t : threads) {
        t.join();
    }

    // Суммируем результаты
    double total = 0.0;
    for (double sum : partial_sums) {
        total += sum;
    }

    return total;
}

// ==================== Тест масштабируемости ====================

void run_scalability_test() {
    cout << "\n" << string(60, '=') << endl;
    cout << "SCALABILITY TEST" << endl;
    cout << string(60, '=') << endl;

    auto operations = generate_operations(50000); // 50k операций

    OperationType target_operation = OperationType::PAYMENT;
    Date start_date = {1, 1, 2023};
    Date end_date = {31, 12, 2023};

    // Однопоточный
    cout << "\nCalculating single-threaded reference..." << endl;
    auto start_single = high_resolution_clock::now();
    double result_single = process_single_thread(operations, target_operation, start_date, end_date);
    auto end_single = high_resolution_clock::now();
    auto time_single = duration_cast<milliseconds>(end_single - start_single);

    cout << "Single-threaded: " << time_single.count() << " ms, Result: "
         << fixed << setprecision(2) << result_single << " RUB" << endl;

    cout << "\n" << setw(8) << "Threads"
         << setw(12) << "Time(ms)"
         << setw(12) << "Speedup"
         << setw(15) << "Efficiency" << endl;
    cout << string(47, '-') << endl;

    vector<int> thread_counts = {1, 2, 4, 8};

    for (int threads : thread_counts) {
        auto start = high_resolution_clock::now();
        double result = process_multi_thread(operations, target_operation, start_date, end_date, threads);
        auto end = high_resolution_clock::now();
        auto time = duration_cast<milliseconds>(end - start);

        if (time.count() > 0) {
            double speedup = (double)time_single.count() / time.count();
            double efficiency = (speedup / threads) * 100;

            cout << fixed << setprecision(2);
            cout << setw(8) << threads
                 << setw(12) << time.count()
                 << setw(12) << speedup
                 << setw(15) << efficiency << "%" << endl;
        }
    }
}

// ==================== Тест разных операций ====================

void test_different_operations() {
    cout << "\n" << string(60, '=') << endl;
    cout << "ANALYSIS OF DIFFERENT OPERATION TYPES" << endl;
    cout << string(60, '=') << endl;

    auto operations = generate_operations(30000);

    Date start_date = {1, 1, 2023};
    Date end_date = {31, 12, 2023};

    vector<pair<OperationType, string>> op_types = {
        {OperationType::DEPOSIT, "DEPOSIT"},
        {OperationType::WITHDRAWAL, "WITHDRAWAL"},
        {OperationType::TRANSFER, "TRANSFER"},
        {OperationType::PAYMENT, "PAYMENT"}
    };

    cout << "\n" << setw(12) << "Operation"
         << setw(15) << "Count"
         << setw(20) << "Total Amount"
         << setw(15) << "Avg Amount" << endl;
    cout << string(62, '-') << endl;

    for (const auto& [op_type, op_name] : op_types) {
        int count = 0;
        double total = 0.0;

        // Быстрый подсчет
        for (const auto& op : operations) {
            if (op.operation == op_type &&
                op.date >= start_date &&
                op.date <= end_date) {
                count++;
                total += op.amount;
            }
        }

        double avg = count > 0 ? total / count : 0.0;

        cout << fixed << setprecision(2);
        cout << setw(12) << op_name
             << setw(15) << count
             << setw(20) << total << " RUB"
             << setw(15) << avg << " RUB" << endl;
    }
}

// ==================== Основной тест ====================

void run_main_test() {
    const int DATA_SIZE = 30000;
    const int NUM_THREADS = 4;

    OperationType target_operation = OperationType::PAYMENT;
    Date start_date = {1, 1, 2023};
    Date end_date = {31, 12, 2023};

    cout << "==================================" << endl;
    cout << "BANK OPERATIONS PROCESSING SYSTEM" << endl;
    cout << "==================================" << endl;

    // Генерация данных
    cout << "\nGenerating " << DATA_SIZE << " operations..." << endl;
    auto operations = generate_operations(DATA_SIZE);
    cout << "Data generation complete." << endl;

    // Однопоточная обработка
    cout << "\n1. SINGLE-THREADED PROCESSING" << endl;
    cout << "-----------------------------" << endl;

    auto start_single = high_resolution_clock::now();
    double result_single = process_single_thread(operations, target_operation, start_date, end_date);
    auto end_single = high_resolution_clock::now();
    auto time_single = duration_cast<milliseconds>(end_single - start_single);

    cout << "Time: " << time_single.count() << " ms" << endl;
    cout << "Total amount: " << fixed << setprecision(2) << result_single << " RUB" << endl;

    // Многопоточная обработка
    cout << "\n2. MULTI-THREADED PROCESSING (" << NUM_THREADS << " threads)" << endl;
    cout << "---------------------------------" << endl;

    auto start_multi = high_resolution_clock::now();
    double result_multi = process_multi_thread(operations, target_operation, start_date, end_date, NUM_THREADS);
    auto end_multi = high_resolution_clock::now();
    auto time_multi = duration_cast<milliseconds>(end_multi - start_multi);

    cout << "Time: " << time_multi.count() << " ms" << endl;
    cout << "Total amount: " << fixed << setprecision(2) << result_multi << " RUB" << endl;

    // Сравнение
    cout << "\n3. COMPARISON RESULTS" << endl;
    cout << "---------------------" << endl;

    if (time_multi.count() > 0) {
        double speedup = (double)time_single.count() / time_multi.count();
        double efficiency = (speedup / NUM_THREADS) * 100;

        cout << fixed << setprecision(2);
        cout << "Speedup: " << speedup << "x" << endl;
        cout << "Efficiency: " << efficiency << "%" << endl;

        // Визуализация
        cout << "\nPerformance visualization:" << endl;
        int bars = 40;
        int single_bars = bars;
        int multi_bars = max(1, (int)(bars * time_single.count() / max((long long)time_multi.count(), 1LL)));

        cout << "  Single: " << string(single_bars, '#') << " " << time_single.count() << " ms" << endl;
        cout << "  Multi : " << string(min(multi_bars, 50), '#') << " " << time_multi.count() << " ms" << endl;
    }

    // Проверка корректности
    cout << "\n4. CORRECTNESS CHECK" << endl;
    cout << "-------------------" << endl;

    double diff = abs(result_single - result_multi);
    if (diff < 0.01) {
        cout << "  Results match perfectly!" << endl;
        cout << "  Difference: " << scientific << setprecision(2) << diff << " RUB" << endl;
    } else {
        cout << "  Results don't match!" << endl;
        cout << fixed << setprecision(2);
        cout << "  Single: " << result_single << " RUB" << endl;
        cout << "  Multi:  " << result_multi << " RUB" << endl;
    }
}

// ==================== Главная функция ====================

int main() {
    try {
        run_main_test();
        run_scalability_test();
        test_different_operations();

        cout << "\n" << string(50, '=') << endl;
        cout << "PROGRAM COMPLETED SUCCESSFULLY" << endl;
        cout << string(50, '=') << endl;

    } catch (const exception& e) {
        cout << "\nERROR: " << e.what() << endl;
        return 1;
    }

    return 0;
}
