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
            unique_lock<shared_mutex> wlock(slock);
            auto it = map.find(key);
            if (it != map.end()) {
                return it->second;
            }

            auto v = get_value(key);
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
        L2Cache(MC& c): main_cache(c) { }

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

template <class MC>
class MainCacheWithThreadLocalL2 {
    private:
        MC main_cache;
    public:
        void lock() { main_cache.lock(); }
        void unlock() { main_cache.unlock(); }

        MainCacheWithThreadLocalL2() {}

        Value &read(int key) {
            thread_local L2Cache<MC> l2(main_cache);
            return l2.read(key);
        }

        DummyL2<MainCacheWithThreadLocalL2> get_l2() {
            return DummyL2(*this);
        }
};

constexpr int SIZE = 10000;

template <class T>
void batch_access(T& l2_cache, int from, int repeat, vector<int> &cost) {
    for(int i = 0; i < repeat; i++) {
        microseconds start = duration_cast< microseconds >( system_clock::now().time_since_epoch());

        int real_from = from * (i + 1);
        int to = real_from + SIZE;

        l2_cache.lock();
        for (int i = real_from; i < to; ++i) {
            l2_cache.read(i);
        }
        l2_cache.unlock();

        microseconds end = duration_cast< microseconds >( system_clock::now().time_since_epoch());
        int c = end.count() - start.count();
        cost.push_back(c);
    }
}

void reorder(int* a, int* b) {
    if (*a > *b) {
        int tmp = *a;
        *a = *b;
        *b = tmp;
    }
}

template <class T>
void multi_thread_access(T &cache1, T &cache2, const string& name, int from1, int from2, int repeat) {
    vector<int> cost1_v, cost2_v;
    thread t1([&]() { batch_access(cache1, from1, repeat, cost1_v); });
    thread t2([&]() { batch_access(cache2, from2, repeat, cost2_v); });
    t1.join();
    t2.join();

    for (int i = 0; i < cost1_v.size(); i++) {
        int cost1 = cost1_v[i], cost2 = cost2_v[i];
        reorder(&cost1, &cost2);
        cout << "  ===start " << name << " exp===" << endl;
        cout << "  thread #1 ends, cost=" << cost1 << endl;
        cout << "  thread #2 ends, cost=" << cost2 << endl;
        cout << "  ===end " << name << " exp===" << endl << endl;
    }
}

constexpr int REPEAT = 3;

template <class T>
void access_multi_times(const string& name, int from1, int from2, int repeat) {
    T cache;
    auto cache1 = cache.get_l2();
    auto cache2 = cache.get_l2();
    cout << "=======start " << name << " multi times exp=======" << endl;
    multi_thread_access(cache1, cache2, name, from1, from2, REPEAT);
    cout << endl;
}

template <class T>
void exp_access_same_data_multi_times(const string& name) {
    access_multi_times<T>(name, 0, 0, REPEAT);
}

template <class T>
void exp_access_diff_data_multi_times(const string& name) {
    access_multi_times<T>(name, 0, SIZE, REPEAT);
}

int main(int argc, char *argv[]) {
    string arg = argv[1];
    if (arg == "rw same") {
        exp_access_same_data_multi_times<MainCacheRWLock>(arg);
    } else if (arg == "sharded same") {
        exp_access_same_data_multi_times<MainCacheSharded>(arg);
    } else if (arg == "l2 same") {
        exp_access_same_data_multi_times<MainCacheWithL2<MainCacheRWLock>>(arg);
    } else if (arg == "l2 sharded same") {
        exp_access_same_data_multi_times<MainCacheWithL2<MainCacheSharded>>(arg);
    } else if (arg == "l2 local same") {
        exp_access_same_data_multi_times<MainCacheWithThreadLocalL2<MainCacheRWLock>>(arg);
    } else if (arg == "single same") {
        exp_access_same_data_multi_times<MainCacheSingleLock>(arg);
    } else if (arg == "rw diff") {
        exp_access_diff_data_multi_times<MainCacheRWLock>(arg);
    } else if (arg == "sharded diff") {
        exp_access_diff_data_multi_times<MainCacheSharded>(arg);
    } else if (arg == "l2 diff") {
        exp_access_diff_data_multi_times<MainCacheWithL2<MainCacheRWLock>>(arg);
    } else if (arg == "l2 sharded diff") {
        exp_access_diff_data_multi_times<MainCacheWithL2<MainCacheSharded>>(arg);
    } else if (arg == "l2 local diff") {
        exp_access_diff_data_multi_times<MainCacheWithThreadLocalL2<MainCacheRWLock>>(arg);
    } else if (arg == "single diff") {
        exp_access_diff_data_multi_times<MainCacheSingleLock>(arg);
    }
}

