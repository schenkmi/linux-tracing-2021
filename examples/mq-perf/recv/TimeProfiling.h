#pragma once

#include <chrono>
#include <vector>
#include <map>
#include <algorithm>
#include <numeric>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <iomanip>

#define MAX_TIMESTAMPS  1000000 /* we may capture that much TimeItems */

using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<Clock>;

class TimeItem
{
    public:
        TimeItem()
            :
            captureTP(Clock::now())
        {
        }

        TimeItem(int64_t ts)
            :
            captureTP(Clock::now()),
            timestamp(ts)
        {
        }

        TimeItem(const TimeItem &rhs)
        {
            *this = rhs;
        }

        TimeItem& operator= (const TimeItem& rhs)
        {
            if (&rhs != this) {
                captureTP = rhs.captureTP;
                timestamp = rhs.timestamp;
            }

            return *this;
        }

        TimeItem(TimeItem&& other)
        {
            // the std::move function converts the lvalue other to an rvalue.
            // we can then use the move assignment operator.
            *this = std::move(other);
        }

        TimeItem& operator=(TimeItem&& other)
        {
            if (this != &other)
            {
                // use std::exchange
                captureTP = std::exchange(other.captureTP, { });
                timestamp = std::exchange(other.timestamp, { 0 });
            }

            return *this;
        }

        int getElapsed()
        {
            TimePoint sent(std::chrono::nanoseconds(this->timestamp));
            std::chrono::duration<double, std::micro> elapsed = this->captureTP - sent;
            return nearbyint(elapsed.count());
        }

    public:
        TimePoint captureTP;
        int64_t timestamp = 0;
};

/**
 * STL definitions.
 */
using TimeItemVector = std::vector<TimeItem>;
using TimeItemVectorIt = TimeItemVector::iterator;
using TimeItemVectorConstIt = TimeItemVector::const_iterator;
using HistogramMap = std::multimap<uint32_t /* us */, double>;

/**
 * Normal time profiling as class
 */
class TimeProfiling
{
    public:
        TimeProfiling(const uint32_t maxSize = MAX_TIMESTAMPS)
            : m_maxSize{maxSize}
        {
            /* min and max */
            m_startTimePoint = TimePoint::min();
            m_endTimePoint = TimePoint::max();
            m_timeItems = new TimeItem[m_maxSize];
        }

        virtual ~TimeProfiling()
        {
        }

        void configure(int startDelaySec, int durationSec)
        {
            m_startDelaySec = startDelaySec;
            m_durationSec = durationSec;
        }

        void start()
        {
            m_startTimePoint = TimePoint(Clock::now() + std::chrono::seconds(m_startDelaySec));
            m_endTimePoint = (m_durationSec > 0) ? TimePoint(m_startTimePoint + std::chrono::seconds(m_durationSec)) : TimePoint::max();
        }

        inline void add(TimeItem&& item)
        {
            if ((item.captureTP > m_startTimePoint) && (item.captureTP < m_endTimePoint)) {
                if (m_index < m_maxSize) {
                    m_timeItems[m_index] = item;
                    m_index++;
                }
            }
        }

        inline int addLatency(TimeItem&& item)
        {
            int ret = 0;

            if ((item.captureTP > m_startTimePoint) && (item.captureTP < m_endTimePoint)) {
                ret = item.getElapsed();
                if (m_index < m_maxSize) {
                    m_timeItems[m_index] = item;
                    m_index++;
                }
            }

            return ret;
        }

        void process(size_t safety = 0)
        {
            std::vector<double> latencyVec;
            m_histogramMap.clear();

            const size_t eleInVec = m_index;
            size_t start = (eleInVec - 1) > safety ? safety : 0;
            size_t stop = eleInVec > safety ? (eleInVec - safety) : eleInVec;

            std::cout << "safety : " << safety << " start : " << start << " stop : " << stop << " total elements : " << eleInVec << std::endl;

            if (eleInVec < 2) {
                return;
            }

            // creates a histogram
            for (size_t cnt = start; cnt < stop; cnt++) {
                TimePoint sent(std::chrono::nanoseconds(m_timeItems[cnt].timestamp));
                std::chrono::duration<double, std::micro> elapsed = m_timeItems[cnt].captureTP - sent;
                m_histogramMap.emplace(std::make_pair(nearbyint(elapsed.count()), elapsed.count()));
                latencyVec.push_back(elapsed.count());
                m_minLatency = m_minLatency > elapsed.count() ? elapsed.count() : m_minLatency;
                m_maxLatency = m_maxLatency < elapsed.count() ? elapsed.count() : m_maxLatency;
            }

            // average calc
            const size_t sz = latencyVec.size();
            const double mean = std::accumulate(latencyVec.begin(), latencyVec.end(), 0.0) / sz;
            m_avgLatency = mean;

            // Now calculate the variance & deviation
            m_varianceLatency = std::accumulate(latencyVec.begin(), latencyVec.end(), 0.0,
                                                [&mean, &sz](double accumulator, const double& val)
                                                {
                                                    return accumulator + ((val - mean)*(val - mean) / (sz - 1));
                                                });
            m_deviationLatency = sqrt(m_varianceLatency);

            // median
            std::sort(latencyVec.begin(), latencyVec.end(), std::less<double>());
            auto it = latencyVec.cbegin();
            std::advance(it, latencyVec.size() / 2);

            // Middle or average of two middle values
            if (latencyVec.size() % 2 == 0) {
                const auto it2 = it--;
                m_medLatency = double((*it) + (*it2)) / 2;    // data[n/2 - 1] AND data[n/2]
            }
            else {
                m_medLatency = (*it);
            }
        }

        void dump()
        {
            std::cout << "min latency          : " << std::fixed << std::setprecision(3) << std::setw(9) << m_minLatency << " us" << std::endl;
            std::cout << "max latency          : " << std::fixed << std::setprecision(3) << std::setw(9) << m_maxLatency << " us" << std::endl;
            std::cout << "average of latency   : " << std::fixed << std::setprecision(3) << std::setw(9) << m_avgLatency << " us" << std::endl;
            std::cout << "median of latency    : " << std::fixed << std::setprecision(3) << std::setw(9) << m_medLatency << " us" << std::endl;
            std::cout << "variance of latency  : " << std::fixed << std::setprecision(3) << std::setw(9) << m_varianceLatency << " us" << std::endl;
            std::cout << "deviation of latency : " << std::fixed << std::setprecision(3) << std::setw(9) << m_deviationLatency << " us" << std::endl;

            std::cout << "Histogram" << std::endl;
            for (auto it = m_histogramMap.begin(), end = m_histogramMap.end(); it != end; it = m_histogramMap.upper_bound(it->first)) {
                std::cout << it->first << " : " << m_histogramMap.count(it->first) << std::endl;
            }
        }

    private:
        int m_index = 0;
        TimeItem * m_timeItems; /* remark: must be pre-allocated to avoid outliers due to memory allocation */
        int m_startDelaySec = 0;
        int m_durationSec = 0;
        TimePoint m_startTimePoint;
        TimePoint m_endTimePoint;
        HistogramMap m_histogramMap;
        double m_avgLatency = 0.0;
        double m_medLatency = 0.0;
        double m_varianceLatency = 0.0;
        double m_deviationLatency = 0.0;
        double m_minLatency = std::numeric_limits<double>::max();
        double m_maxLatency = 0.0;
        const uint32_t m_maxSize;
};

