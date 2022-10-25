#include <ctime>
#include <unistd.h>
#include <stdlib.h>
#include <iostream>
#include <functional>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>
#include <thread>
#include <chrono>

using namespace std;
using namespace chrono;

using Value = string;
constexpr int SHARD = 16;

Value get_value(int key) {
#ifndef BASELINE
    usleep(1);
#endif
    /* return key; */
    string s = to_string(key + 10000);
    return s + s + s;
}

template <class MC>
class DummyL2 {
    private:
        MC &t;
    public:
        DummyL2(MC& tt): t(tt) {}
        void lock() {
            t.lock();
        }
        void unlock() {
            t.unlock();
        }

        Value &read(int key) {
            return t.read(key);
        }
};

class MainCacheRWLock {
    private:
        unordered_map<int, Value> map;
        shared_mutex slock;
    public:
        MainCacheRWLock() {}

        void lock() {}
        void unlock() {}

        Value &read(int key) {
            {
                shared_lock<shared_mutex> rlock(slock);
                auto it = map.find(key);
                if (it != map.end()) {
                    return it->second;
                }
            }
            auto v = get_value(key); // may be called multi times?
            unique_lock<shared_mutex> wlock(slock);
            auto p = map.insert(make_pair(key, move(v)));
            return p.first->second;
        }

        DummyL2<MainCacheRWLock> get_l2() {
            return DummyL2(*this);
        }
};

class MainCacheSharded {
    private:
        MainCacheRWLock shard[SHARD];
    public:
        MainCacheSharded() {}

        void lock() {}
        void unlock() {}

        Value &read(int key) {
            int slot = key % SHARD;
            return shard[slot].read(key);
        }

        DummyL2<MainCacheSharded> get_l2() {
            return DummyL2(*this);
        }

};

class MainCacheSingleLock {
    private:
        unordered_map<int, Value> map;
        mutex mut;
    public:
        MainCacheSingleLock() {}

        void lock() {
            mut.lock();
        }
        void unlock() {
            mut.unlock();
        }

        Value &read(int key) {
            auto it = map.find(key);
            if (it != map.end()) {
                return it->second;
            }
            auto p = map.insert(make_pair(key, get_value(key)));
            return p.first->second;
        }

        DummyL2<MainCacheSingleLock> get_l2() {
            return DummyL2(*this);
        }
};

template <class MC>
class L2Cache {
    private:
        MC &main_cache;
        unordered_map<int, Value> map;
    public:
        void lock() { }
        void unlock() { }
        L2Cache(MC& c): main_cache(c) {}

        Value &read(int key) {
            auto it = map.find(key);
            if (it != map.end()) {
                return it->second;
            }
            // fetch from main cache
            auto p = map.insert(make_pair(key, main_cache.read(key)));
            return p.first->second;
        }
};

template <class MC>
class MainCacheWithL2 {
    private:
        MC main_cache;
    public:
        MainCacheWithL2() {}
        L2Cache<MC> get_l2() {
            return L2Cache<MC>{main_cache};
        }
};

template <class T>
void batch_access(T& l2_cache, int from, int to, int *cost) {
    microseconds start = duration_cast< microseconds >( system_clock::now().time_since_epoch());

    l2_cache.lock();
    for (int i = from; i < to; ++i) {
        l2_cache.read(i);
    }
    l2_cache.unlock();

    microseconds end = duration_cast< microseconds >( system_clock::now().time_since_epoch());
    *cost = end.count() - start.count();
}

void reorder(int* a, int* b) {
    if (*a > *b) {
        int tmp = *a;
        *a = *b;
        *b = tmp;
    }
}

template <class T>
void multi_thread_access(T &cache1, T &cache2, const char* name, int from1, int to1, int from2, int to2) {
    cout << "  ===start " << name << " exp===" << endl;
    int cost1, cost2;
    thread t1([&]() { batch_access(cache1, from1, to1, &cost1); });
    thread t2([&]() { batch_access(cache2, from2, to2, &cost2); });
    t1.join();
    t2.join();
    reorder(&cost1, &cost2);
    cout << "  thread #1 ends, cost=" << cost1 << endl;
    cout << "  thread #2 ends, cost=" << cost2 << endl;
    cout << "  ===end " << name << " exp===" << endl << endl;
}

constexpr int SIZE = 10000;
constexpr int REPEAT = 3;

template <class T>
void access_multi_times(const char* name, int from1, int to1, int from2, int to2, int repeat) {
    T cache;
    auto cache1 = cache.get_l2();
    auto cache2 = cache.get_l2();
    cout << "=======start " << name << " multi times exp=======" << endl;
    for (int i = 0; i < repeat; i++) {
        multi_thread_access(cache1, cache2, name, from1, to1, from2, to2);
    }
    cout << endl;
}

template <class T>
void exp_access_same_data_multi_times(const char* name) {
    access_multi_times<T>(name, 0, SIZE, 0, SIZE, REPEAT);
}

template <class T>
void exp_access_diff_data_multi_times(const char* name) {
    access_multi_times<T>(name, 0, SIZE, SIZE, SIZE * 2, REPEAT);
}

int main(int argc, char *argv[]) {
    srand(time(NULL));

    exp_access_same_data_multi_times<MainCacheRWLock>("rw same");
    exp_access_same_data_multi_times<MainCacheSharded>("sharded same");
    exp_access_same_data_multi_times<MainCacheWithL2<MainCacheRWLock>>("l2 same");
    exp_access_same_data_multi_times<MainCacheWithL2<MainCacheSharded>>("l2 same sharded");
    exp_access_same_data_multi_times<MainCacheSingleLock>("single same");

    exp_access_diff_data_multi_times<MainCacheRWLock>("rw diff");
    exp_access_diff_data_multi_times<MainCacheSharded>("sharded diff");
    exp_access_diff_data_multi_times<MainCacheWithL2<MainCacheRWLock>>("l2 diff");
    exp_access_diff_data_multi_times<MainCacheWithL2<MainCacheSharded>>("l2 diff sharded");
    exp_access_diff_data_multi_times<MainCacheSingleLock>("single diff");
}

