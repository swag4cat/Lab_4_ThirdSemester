// g++ -std=c++17 -pthread -O2 bankers_algorithm.cpp -o bankers

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <random>

using namespace std;
using namespace std::chrono;

class BankersAlgorithm {
private:
    int processes;
    int resources;
    vector<vector<int>> max_matrix;
    vector<vector<int>> allocation_matrix;
    vector<vector<int>> need_matrix;
    vector<int> available;
    vector<bool> finished;
    mutable mutex mtx;

public:
    BankersAlgorithm(int p, int r)
    : processes(p), resources(r),
    max_matrix(p, vector<int>(r)),
    allocation_matrix(p, vector<int>(r)),
    need_matrix(p, vector<int>(r)),
    available(r),
    finished(p, false) {}

    void initialize() {
        cout << "\n=== Инициализация данных ===" << endl;

        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<int> dist(1, 10);

        for (int i = 0; i < processes; i++) {
            for (int j = 0; j < resources; j++) {
                max_matrix[i][j] = dist(gen);
            }
        }

        for (int i = 0; i < processes; i++) {
            for (int j = 0; j < resources; j++) {
                allocation_matrix[i][j] = dist(gen) % (max_matrix[i][j] + 1);
                need_matrix[i][j] = max_matrix[i][j] - allocation_matrix[i][j];
            }
        }

        for (int j = 0; j < resources; j++) {
            available[j] = dist(gen) + 5;
        }

        cout << "Данные инициализированы:" << endl;
        cout << "Процессы: " << processes << endl;
        cout << "Ресурсы: " << resources << endl;
    }

    bool is_safe_state(vector<int>& safe_sequence) const {
        vector<int> work = available;
        vector<bool> finish = finished;
        safe_sequence.clear();

        for (int k = 0; k < processes; k++) {
            bool found = false;

            for (int i = 0; i < processes; i++) {
                if (!finish[i]) {
                    bool can_allocate = true;

                    for (int j = 0; j < resources; j++) {
                        if (need_matrix[i][j] > work[j]) {
                            can_allocate = false;
                            break;
                        }
                    }

                    if (can_allocate) {
                        for (int j = 0; j < resources; j++) {
                            work[j] += allocation_matrix[i][j];
                        }

                        finish[i] = true;
                        safe_sequence.push_back(i);
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                return false;
            }
        }

        return true;
    }

    bool request_resources(int process_id, const vector<int>& request) {
        lock_guard<mutex> lock(mtx);

        cout << "\nПроцесс P" << process_id << " запрашивает ресурсы: ";
        for (int val : request) cout << val << " ";
        cout << endl;

        for (int j = 0; j < resources; j++) {
            if (request[j] > need_matrix[process_id][j]) {
                cout << "Ошибка: Запрос превышает потребность процесса" << endl;
                return false;
            }
        }

        for (int j = 0; j < resources; j++) {
            if (request[j] > available[j]) {
                cout << "Ожидание: Недостаточно доступных ресурсов" << endl;
                return false;
            }
        }

        for (int j = 0; j < resources; j++) {
            available[j] -= request[j];
            allocation_matrix[process_id][j] += request[j];
            need_matrix[process_id][j] -= request[j];
        }

        vector<int> safe_seq;
        if (is_safe_state(safe_seq)) {
            cout << "Ресурсы выделены. Состояние безопасное." << endl;
            cout << "Безопасная последовательность: ";
            for (int p : safe_seq) cout << "P" << p << " ";
            cout << endl;
            return true;
        } else {
            cout << "Состояние небезопасное. Откат изменений..." << endl;
            for (int j = 0; j < resources; j++) {
                available[j] += request[j];
                allocation_matrix[process_id][j] -= request[j];
                need_matrix[process_id][j] += request[j];
            }
            return false;
        }
    }

    void release_resources(int process_id, vector<int>& release) {  // Убрал const
        lock_guard<mutex> lock(mtx);

        cout << "\nПроцесс P" << process_id << " освобождает ресурсы: ";
        for (int val : release) cout << val << " ";
        cout << endl;

        for (int j = 0; j < resources; j++) {
            if (release[j] > allocation_matrix[process_id][j]) {
                cout << "Предупреждение: Попытка освободить больше, чем выделено" << endl;
                release[j] = allocation_matrix[process_id][j];
            }

            allocation_matrix[process_id][j] -= release[j];
            need_matrix[process_id][j] += release[j];
            available[j] += release[j];
        }

        cout << "Ресурсы освобождены." << endl;
    }

    void print_state() const {
        lock_guard<mutex> lock(mtx);

        cout << "\n" << string(40, '=') << endl;
        cout << "       ТЕКУЩЕЕ СОСТОЯНИЕ СИСТЕМЫ" << endl;
        cout << string(40, '=') << endl;

        cout << "\nДоступные ресурсы: ";
        for (int val : available) cout << setw(3) << val << " ";
        cout << endl;

        cout << "\nМаксимальные потребности (Max):" << endl;
        for (int i = 0; i < processes; i++) {
            cout << "P" << i << ": ";
            for (int j = 0; j < resources; j++) {
                cout << setw(3) << max_matrix[i][j] << " ";
            }
            cout << endl;
        }

        cout << "\nВыделенные ресурсы (Allocation):" << endl;
        for (int i = 0; i < processes; i++) {
            cout << "P" << i << ": ";
            for (int j = 0; j < resources; j++) {
                cout << setw(3) << allocation_matrix[i][j] << " ";
            }
            cout << endl;
        }

        cout << "\nТекущие потребности (Need):" << endl;
        for (int i = 0; i < processes; i++) {
            cout << "P" << i << ": ";
            for (int j = 0; j < resources; j++) {
                cout << setw(3) << need_matrix[i][j] << " ";
            }
            cout << endl;
        }

        vector<int> safe_seq;
        bool safe = is_safe_state(safe_seq);

        cout << "\nСостояние системы: ";
        if (safe) {
            cout << "БЕЗОПАСНОЕ" << endl;
            cout << "Безопасная последовательность: ";
            for (int p : safe_seq) cout << "P" << p << " ";
            cout << endl;
        } else {
            cout << "НЕБЕЗОПАСНОЕ (возможен deadlock)" << endl;
        }
    }

    void run_processes(int iterations) {
        vector<thread> threads;

        auto process_worker = [this, iterations](int process_id) {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<int> req_dist(0, 3);
            uniform_int_distribution<int> iter_dist(1, 5);

            for (int iter = 0; iter < iterations; iter++) {
                this_thread::sleep_for(milliseconds(100 * (process_id + 1)));

                vector<int> request(resources);
                for (int j = 0; j < resources; j++) {
                    request[j] = req_dist(gen);
                }

                bool granted = request_resources(process_id, request);

                if (granted) {
                    this_thread::sleep_for(milliseconds(200));

                    vector<int> release(resources);
                    for (int j = 0; j < resources; j++) {
                        release[j] = req_dist(gen) % (request[j] + 1);
                    }
                    release_resources(process_id, release);
                }

                this_thread::sleep_for(milliseconds(100));
            }

            cout << "Процесс P" << process_id << " завершил работу." << endl;
        };

        for (int i = 0; i < processes; i++) {
            threads.emplace_back(process_worker, i);
        }

        for (auto& t : threads) {
            t.join();
        }
    }
};

// ==================== Демонстрация ====================

void demonstrate_bankers_algorithm() {
    cout << "======================================" << endl;
    cout << "АЛГОРИТМ БАНКИРА (BANKER'S ALGORITHM)" << endl;
    cout << "======================================" << endl;

    const int PROCESSES = 5;
    const int RESOURCES = 3;
    const int ITERATIONS = 3;

    BankersAlgorithm banker(PROCESSES, RESOURCES);

    banker.initialize();
    banker.print_state();

    cout << "\n" << string(50, '=') << endl;
    cout << "         ЗАПУСК ПРОЦЕССОВ (" << ITERATIONS << " итераций)" << endl;
    cout << string(50, '=') << endl;

    auto start = high_resolution_clock::now();
    banker.run_processes(ITERATIONS);
    auto end = high_resolution_clock::now();

    banker.print_state();

    auto duration = duration_cast<milliseconds>(end - start);
    cout << "\n" << string(40, '=') << endl;
    cout << "        СТАТИСТИКА ВЫПОЛНЕНИЯ" << endl;
    cout << string(40, '=') << endl;
    cout << "Время выполнения: " << duration.count() << " ms" << endl;
    cout << "Процессов: " << PROCESSES << endl;
    cout << "Типов ресурсов: " << RESOURCES << endl;
    cout << "Итераций на процесс: " << ITERATIONS << endl;
}

// ==================== Простой тест ====================

void simple_test() {
    cout << "\n\n" << string(40, '=') << endl;
    cout << "        ПРОСТОЙ ТЕСТ АЛГОРИТМА" << endl;
    cout << string(40, '=') << endl;

    BankersAlgorithm banker(3, 2);

    // Ручная установка значений для теста
    banker.initialize();

    // Тестовые запросы
    vector<int> request1 = {1, 0};
    vector<int> request2 = {0, 1};
    vector<int> request3 = {2, 1};

    cout << "\nТест 1: Безопасный запрос" << endl;
    banker.request_resources(0, request1);

    cout << "\nТест 2: Еще один запрос" << endl;
    banker.request_resources(1, request2);

    cout << "\nТест 3: Возможный небезопасный запрос" << endl;
    banker.request_resources(2, request3);

    banker.print_state();
}

// ==================== Главная функция ====================

int main() {
    try {
        demonstrate_bankers_algorithm();
        simple_test();

        cout << "\n" << string(40, '=') << endl;
        cout << "       ПРОГРАММА ЗАВЕРШЕНА УСПЕШНО" << endl;
        cout << string(40, '=') << endl;

    } catch (const exception& e) {
        cout << "\nОШИБКА: " << e.what() << endl;
        return 1;
    }

    return 0;
}
