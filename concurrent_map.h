#pragma once
#include <future>
#include <map>
#include <mutex>
#include <vector>

template <typename Key, typename Value>
class ConcurrentMap {
public:
    struct Access {
        ~Access() {
            mutex.unlock();
        }

        std::mutex& mutex;
        Value& ref_to_value;
    };

public:
    static_assert(std::is_integral_v<Key>, "ConcurrentMap supports only integer keys");

public:
    explicit ConcurrentMap(size_t bucket_count)
        : buckets_(std::vector<Bucket>(bucket_count)) {
    }

public:
    Access operator[](const Key& key) {
        const size_t bucket_index = key % buckets_.size();
        buckets_[bucket_index].mutex.lock();

        return { buckets_[bucket_index].mutex, buckets_[bucket_index].map_[key] };
    }

    std::map<Key, Value> BuildOrdinaryMap() {
        std::map<Key, Value> result;

        for (auto& bucket : buckets_) {
            bucket.mutex.lock();
            result.insert(bucket.map_.begin(), bucket.map_.end());
            bucket.mutex.unlock();
        }

        return result;
    }

private:
    struct Bucket {
        std::mutex mutex;
        std::map<Key, Value> map_;
    };

private:
    std::vector<Bucket> buckets_;
};