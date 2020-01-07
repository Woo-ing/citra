#pragma once
#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>

template <typename key_type, typename data_type, size_t _size>
class cache_buffer {
public:
    cache_buffer() : p(0), l(-1) {}
    void clear() {
        p = 0;
        l = -1;
    }

    inline data_type& cache_data(const key_type key, const data_type& data) {
        keys[p] = key;
        data_type& ret = datas[p];
        ret = data;

        if (p < (int)_size - 1)
            ++p;
        else
            p = 0;
        if (l < (int)_size - 1)
            ++l;
        return ret;
    }

    data_type* hit(const key_type key) {
        for (int i = l; i >= 0; --i) {
            if (keys[i] == key) {
                return &datas[i];
            }
        }
        return NULL;
    }
    key_type keys[_size];
    data_type datas[_size];
    int p;
    int l;
};

template <typename DomainT, typename CodomainT>
class cache_map : protected boost::icl::interval_map<DomainT, CodomainT> {
public:
    typedef cache_map<DomainT,CodomainT> type;
    typedef boost::icl::interval_map<DomainT, CodomainT> base_map;

    typedef ICL_INTERVAL_INSTANCE(ICL_INTERVAL_DEFAULT, DomainT, ICL_COMPARE_DEFAULT) interval_type;

    typedef typename base_map::segment_type segment_type;
    typedef typename base_map::element_type element_type;
    typedef typename base_map::const_iterator const_iterator;
    typedef typename base_map::iterator iterator;

public:
    cache_map() : base_map() {}

    void clear_caches() {
        caches_0.clear();
        caches_1.clear();
        caches_2.clear();
        caches_4.clear();
        caches_8.clear();
    }
    void add(const segment_type& interval_value_pair) {
        base_map::add(interval_value_pair);
        clear_caches();
    }

    void set(const segment_type& interval_value_pair) {
        base_map::set(interval_value_pair);
        clear_caches();
    }

    void subtract(const segment_type& interval_value_pair) {
        base_map::subtract(interval_value_pair);
        clear_caches();
    }
    void erase(const interval_type& interval) {
        base_map::erase(interval);
        clear_caches();
    }

    auto operator-=(const boost::icl::interval_set<DomainT>& interval) {
        clear_caches();
        return (base_map&)(*this) -= interval;
    }

    bool empty() {
        return base_map::empty();
    }

    iterator begin() {
        return base_map::begin();
    }

    iterator end() {
        return base_map::end();
    }

    typedef std::pair<const_iterator, const_iterator> cache_range;
    typedef std::pair<interval_type, cache_range> cache_data;
    typedef cache_buffer<u32, cache_range, 4> cache_buff;

    cache_range& equal_range(const interval_type& interval) const {
        u32 l = interval.lower();
        u32 u = interval.upper();
        cache_buff& c = caches(u - l);
        cache_range* pData = c.hit(u - l);
        if (pData != NULL)
            return *pData;

        return c.cache_data(l, base_map::equal_range(interval));
    }

    template <u32 _bytes>
    cache_range* hit(DomainT addr) const {
        return caches<_bytes>().hit(addr);
    }

    cache_buff& caches(u32 bytes) const {
        switch (bytes) {
        case 1:
            return caches<1>();
        case 2:
            return caches<2>();
        case 4:
            return caches<4>();
        case 8:
            return caches<8>();
        default:
            return caches_0;
        }
    }

    template <u32 _bytes>
    cache_buff& caches() const {
        return caches_0;
    }
    template <>
    cache_buff& caches<1>() const {
        return caches_1;
    }
    template <>
    cache_buff& caches<2>() const {
        return caches_2;
    }
    template <>
    cache_buff& caches<4>() const {
        return caches_4;
    }
    template <>
    cache_buff& caches<8>() const {
        return caches_8;
    }

    mutable cache_buff caches_1;
    mutable cache_buff caches_2;
    mutable cache_buff caches_4;
    mutable cache_buff caches_8;
    mutable cache_buff caches_0;
};
