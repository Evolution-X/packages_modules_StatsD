// Copyright (C) 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/metrics/duration_helper/MaxDurationTracker.h"
#include "src/condition/ConditionWizard.h"
#include "metrics_test_helper.h"
#include "tests/statsd_test_util.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <set>
#include <unordered_map>
#include <vector>

using namespace android::os::statsd;
using namespace testing;
using android::sp;
using std::set;
using std::unordered_map;
using std::vector;

#ifdef __ANDROID__

namespace android {
namespace os {
namespace statsd {

const ConfigKey kConfigKey(0, 12345);

const int TagId = 1;
const int64_t metricId = 123;
const optional<UploadThreshold> emptyThreshold;

const MetricDimensionKey eventKey = getMockedMetricDimensionKey(TagId, 0, "1");
const HashableDimensionKey conditionKey = getMockedDimensionKey(TagId, 4, "1");
const HashableDimensionKey key1 = getMockedDimensionKey(TagId, 1, "1");
const HashableDimensionKey key2 = getMockedDimensionKey(TagId, 1, "2");
const int64_t bucketSizeNs = 30 * 1000 * 1000 * 1000LL;

TEST(MaxDurationTrackerTest, TestSimpleMaxDuration) {
    const MetricDimensionKey eventKey = getMockedMetricDimensionKey(TagId, 0, "1");
    const HashableDimensionKey key1 = getMockedDimensionKey(TagId, 1, "1");
    const HashableDimensionKey key2 = getMockedDimensionKey(TagId, 1, "2");


    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    unordered_map<MetricDimensionKey, vector<DurationBucket>> buckets;

    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketEndTimeNs = bucketStartTimeNs + bucketSizeNs;
    int64_t bucketNum = 0;

    int64_t metricId = 1;
    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, -1, false, bucketStartTimeNs,
                               bucketNum, bucketStartTimeNs, bucketSizeNs, false, false, {});

    tracker.noteStart(key1, true, bucketStartTimeNs, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    // Event starts again. This would not change anything as it already starts.
    tracker.noteStart(key1, true, bucketStartTimeNs + 3, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    // Stopped.
    tracker.noteStop(key1, bucketStartTimeNs + 10, false);

    // Another event starts in this bucket.
    tracker.noteStart(key2, true, bucketStartTimeNs + 20, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    tracker.noteStop(key2, bucketStartTimeNs + 40, false /*stop all*/);

    tracker.flushIfNeeded(bucketStartTimeNs + bucketSizeNs + 1, emptyThreshold, &buckets);
    EXPECT_TRUE(buckets.find(eventKey) != buckets.end());
    ASSERT_EQ(1u, buckets[eventKey].size());
    EXPECT_EQ(20LL, buckets[eventKey][0].mDuration);
}

TEST(MaxDurationTrackerTest, TestStopAll) {
    const MetricDimensionKey eventKey = getMockedMetricDimensionKey(TagId, 0, "1");
    const HashableDimensionKey key1 = getMockedDimensionKey(TagId, 1, "1");
    const HashableDimensionKey key2 = getMockedDimensionKey(TagId, 1, "2");

    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    unordered_map<MetricDimensionKey, vector<DurationBucket>> buckets;

    int64_t bucketSizeNs = 30 * 1000 * 1000 * 1000LL;
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketEndTimeNs = bucketStartTimeNs + bucketSizeNs;
    int64_t bucketNum = 0;

    int64_t metricId = 1;
    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, -1, false, bucketStartTimeNs,
                               bucketNum, bucketStartTimeNs, bucketSizeNs, false, false, {});

    tracker.noteStart(key1, true, bucketStartTimeNs + 1, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);

    // Another event starts in this bucket.
    tracker.noteStart(key2, true, bucketStartTimeNs + 20, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    tracker.flushIfNeeded(bucketStartTimeNs + bucketSizeNs + 40, emptyThreshold, &buckets);
    tracker.noteStopAll(bucketStartTimeNs + bucketSizeNs + 40);
    EXPECT_TRUE(tracker.mInfos.empty());
    EXPECT_TRUE(buckets.find(eventKey) == buckets.end());

    tracker.flushIfNeeded(bucketStartTimeNs + 3 * bucketSizeNs + 40, emptyThreshold, &buckets);
    EXPECT_TRUE(buckets.find(eventKey) != buckets.end());
    ASSERT_EQ(1u, buckets[eventKey].size());
    EXPECT_EQ(bucketSizeNs + 40 - 1, buckets[eventKey][0].mDuration);
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs, buckets[eventKey][0].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs, buckets[eventKey][0].mBucketEndNs);
}

TEST(MaxDurationTrackerTest, TestCrossBucketBoundary) {
    const MetricDimensionKey eventKey = getMockedMetricDimensionKey(TagId, 0, "1");
    const HashableDimensionKey key1 = getMockedDimensionKey(TagId, 1, "1");
    const HashableDimensionKey key2 = getMockedDimensionKey(TagId, 1, "2");
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    unordered_map<MetricDimensionKey, vector<DurationBucket>> buckets;

    int64_t bucketSizeNs = 30 * 1000 * 1000 * 1000LL;
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketEndTimeNs = bucketStartTimeNs + bucketSizeNs;
    int64_t bucketNum = 0;

    int64_t metricId = 1;
    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, -1, false, bucketStartTimeNs,
                               bucketNum, bucketStartTimeNs, bucketSizeNs, false, false, {});

    // The event starts.
    tracker.noteStart(DEFAULT_DIMENSION_KEY, true, bucketStartTimeNs + 1, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);

    // Starts again. Does not DEFAULT_DIMENSION_KEY anything.
    tracker.noteStart(DEFAULT_DIMENSION_KEY, true, bucketStartTimeNs + bucketSizeNs + 1,
                      ConditionKey(), StatsdStats::kDimensionKeySizeHardLimitMin);

    // The event stops at early 4th bucket.
    // Notestop is called from DurationMetricProducer's onMatchedLogEvent, which calls
    // flushIfneeded.
    tracker.flushIfNeeded(bucketStartTimeNs + (3 * bucketSizeNs) + 20, emptyThreshold, &buckets);
    tracker.noteStop(DEFAULT_DIMENSION_KEY, bucketStartTimeNs + (3 * bucketSizeNs) + 20,
                     false /*stop all*/);
    EXPECT_TRUE(buckets.find(eventKey) == buckets.end());

    tracker.flushIfNeeded(bucketStartTimeNs + 4 * bucketSizeNs, emptyThreshold, &buckets);
    ASSERT_EQ(1u, buckets[eventKey].size());
    EXPECT_EQ((3 * bucketSizeNs) + 20 - 1, buckets[eventKey][0].mDuration);
    EXPECT_EQ(bucketStartTimeNs + 3 * bucketSizeNs, buckets[eventKey][0].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + 4 * bucketSizeNs, buckets[eventKey][0].mBucketEndNs);
}

TEST(MaxDurationTrackerTest, TestCrossBucketBoundary_nested) {
    const MetricDimensionKey eventKey = getMockedMetricDimensionKey(TagId, 0, "1");
    const HashableDimensionKey key1 = getMockedDimensionKey(TagId, 1, "1");
    const HashableDimensionKey key2 = getMockedDimensionKey(TagId, 1, "2");
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    unordered_map<MetricDimensionKey, vector<DurationBucket>> buckets;

    int64_t bucketSizeNs = 30 * 1000 * 1000 * 1000LL;
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketEndTimeNs = bucketStartTimeNs + bucketSizeNs;
    int64_t bucketNum = 0;

    int64_t metricId = 1;
    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, -1, true, bucketStartTimeNs,
                               bucketNum, bucketStartTimeNs, bucketSizeNs, false, false, {});

    // 2 starts
    tracker.noteStart(DEFAULT_DIMENSION_KEY, true, bucketStartTimeNs + 1, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    tracker.noteStart(DEFAULT_DIMENSION_KEY, true, bucketStartTimeNs + 10, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    // one stop
    tracker.noteStop(DEFAULT_DIMENSION_KEY, bucketStartTimeNs + 20, false /*stop all*/);

    tracker.flushIfNeeded(bucketStartTimeNs + (2 * bucketSizeNs) + 1, emptyThreshold, &buckets);
    // Because of nesting, still not stopped.
    EXPECT_TRUE(buckets.find(eventKey) == buckets.end());

    // real stop now.
    tracker.noteStop(DEFAULT_DIMENSION_KEY,
                     bucketStartTimeNs + (2 * bucketSizeNs) + 5, false);
    tracker.flushIfNeeded(bucketStartTimeNs + (3 * bucketSizeNs) + 1, emptyThreshold, &buckets);

    ASSERT_EQ(1u, buckets[eventKey].size());
    EXPECT_EQ(2 * bucketSizeNs + 5 - 1, buckets[eventKey][0].mDuration);
}

TEST(MaxDurationTrackerTest, TestMaxDurationWithCondition) {
    const HashableDimensionKey conditionDimKey = key1;

    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    ConditionKey conditionKey1;
    MetricDimensionKey eventKey = getMockedMetricDimensionKey(TagId, 1, "1");
    conditionKey1[StringToId("APP_BACKGROUND")] = conditionDimKey;

    /**
    Start in first bucket, stop in second bucket. Condition turns on and off in the first bucket
    and again turns on and off in the second bucket.
    */
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketEndTimeNs = bucketStartTimeNs + bucketSizeNs;
    int64_t eventStartTimeNs = bucketStartTimeNs + 1 * NS_PER_SEC;
    int64_t conditionStarts1 = bucketStartTimeNs + 11 * NS_PER_SEC;
    int64_t conditionStops1 = bucketStartTimeNs + 14 * NS_PER_SEC;
    int64_t conditionStarts2 = bucketStartTimeNs + bucketSizeNs + 5 * NS_PER_SEC;
    int64_t conditionStops2 = conditionStarts2 + 10 * NS_PER_SEC;
    int64_t eventStopTimeNs = conditionStops2 + 8 * NS_PER_SEC;

    int64_t metricId = 1;
    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, 1, false, bucketStartTimeNs,
                               0, bucketStartTimeNs, bucketSizeNs, true, false, {});
    EXPECT_TRUE(tracker.mAnomalyTrackers.empty());

    tracker.noteStart(key1, false, eventStartTimeNs, conditionKey1,
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    tracker.noteConditionChanged(key1, true, conditionStarts1);
    tracker.noteConditionChanged(key1, false, conditionStops1);
    unordered_map<MetricDimensionKey, vector<DurationBucket>> buckets;
    tracker.flushIfNeeded(bucketStartTimeNs + bucketSizeNs + 1, emptyThreshold, &buckets);
    ASSERT_EQ(0U, buckets.size());

    tracker.noteConditionChanged(key1, true, conditionStarts2);
    tracker.noteConditionChanged(key1, false, conditionStops2);
    tracker.noteStop(key1, eventStopTimeNs, false);
    tracker.flushIfNeeded(bucketStartTimeNs + 2 * bucketSizeNs + 1, emptyThreshold, &buckets);
    ASSERT_EQ(1U, buckets.size());
    vector<DurationBucket> item = buckets.begin()->second;
    ASSERT_EQ(1UL, item.size());
    EXPECT_EQ((int64_t)(13LL * NS_PER_SEC), item[0].mDuration);
}

TEST(MaxDurationTrackerTest, TestAnomalyDetection) {
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    ConditionKey conditionKey1;
    MetricDimensionKey eventKey = getMockedMetricDimensionKey(TagId, 2, "maps");
    conditionKey1[StringToId("APP_BACKGROUND")] = conditionKey;

    unordered_map<MetricDimensionKey, vector<DurationBucket>> buckets;

    int64_t bucketSizeNs = 30 * 1000 * 1000 * 1000LL;
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketEndTimeNs = bucketStartTimeNs + bucketSizeNs;
    int64_t bucketNum = 0;
    int64_t eventStartTimeNs = 13000000000;
    int64_t durationTimeNs = 2 * 1000;

    int64_t metricId = 1;
    Alert alert;
    alert.set_id(101);
    alert.set_metric_id(1);
    alert.set_trigger_if_sum_gt(40 * NS_PER_SEC);
    alert.set_num_buckets(2);
    const int32_t refPeriodSec = 45;
    alert.set_refractory_period_secs(refPeriodSec);
    sp<AlarmMonitor> alarmMonitor;
    sp<DurationAnomalyTracker> anomalyTracker =
        new DurationAnomalyTracker(alert, kConfigKey, alarmMonitor);
    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, 1, false, bucketStartTimeNs,
                               bucketNum, bucketStartTimeNs, bucketSizeNs, true, false,
                               {anomalyTracker});

    tracker.noteStart(key1, true, eventStartTimeNs, conditionKey1,
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    sp<const InternalAlarm> alarm = anomalyTracker->mAlarms.begin()->second;
    EXPECT_EQ((long long)(53ULL * NS_PER_SEC), (long long)(alarm->timestampSec * NS_PER_SEC));

    // Remove the anomaly alarm when the duration is no longer fully met.
    tracker.noteConditionChanged(key1, false, eventStartTimeNs + 15 * NS_PER_SEC);
    ASSERT_EQ(0U, anomalyTracker->mAlarms.size());

    // Since the condition was off for 10 seconds, the anomaly should trigger 10 sec later.
    tracker.noteConditionChanged(key1, true, eventStartTimeNs + 25 * NS_PER_SEC);
    ASSERT_EQ(1U, anomalyTracker->mAlarms.size());
    alarm = anomalyTracker->mAlarms.begin()->second;
    EXPECT_EQ((long long)(63ULL * NS_PER_SEC), (long long)(alarm->timestampSec * NS_PER_SEC));
}

// This tests that we correctly compute the predicted time of an anomaly assuming that the current
// state continues forward as-is.
TEST(MaxDurationTrackerTest, TestAnomalyPredictedTimestamp) {
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    ConditionKey conditionKey1;
    MetricDimensionKey eventKey = getMockedMetricDimensionKey(TagId, 2, "maps");
    conditionKey1[StringToId("APP_BACKGROUND")] = conditionKey;
    ConditionKey conditionKey2;
    conditionKey2[StringToId("APP_BACKGROUND")] = getMockedDimensionKey(TagId, 4, "2");

    unordered_map<MetricDimensionKey, vector<DurationBucket>> buckets;

    /**
     * Suppose we have two sub-dimensions that we're taking the MAX over. In the first of these
     * nested dimensions, we enter the pause state after 3 seconds. When we resume, the second
     * dimension has already been running for 4 seconds. Thus, we have 40-4=36 seconds remaining
     * before we trigger the anomaly.
     */
    int64_t bucketSizeNs = 30 * 1000 * 1000 * 1000LL;
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketEndTimeNs = bucketStartTimeNs + bucketSizeNs;
    int64_t bucketNum = 0;
    int64_t eventStartTimeNs = bucketStartTimeNs + 5 * NS_PER_SEC;  // Condition is off at start.
    int64_t conditionStarts1 = bucketStartTimeNs + 11 * NS_PER_SEC;
    int64_t conditionStops1 = bucketStartTimeNs + 14 * NS_PER_SEC;
    int64_t conditionStarts2 = bucketStartTimeNs + 20 * NS_PER_SEC;
    int64_t eventStartTimeNs2 = conditionStarts2 - 4 * NS_PER_SEC;

    int64_t metricId = 1;
    Alert alert;
    alert.set_id(101);
    alert.set_metric_id(1);
    alert.set_trigger_if_sum_gt(40 * NS_PER_SEC);
    alert.set_num_buckets(2);
    const int32_t refPeriodSec = 45;
    alert.set_refractory_period_secs(refPeriodSec);
    sp<AlarmMonitor> alarmMonitor;
    sp<DurationAnomalyTracker> anomalyTracker =
        new DurationAnomalyTracker(alert, kConfigKey, alarmMonitor);
    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, 1, false, bucketStartTimeNs,
                               bucketNum, bucketStartTimeNs, bucketSizeNs, true, false,
                               {anomalyTracker});

    tracker.noteStart(key1, false, eventStartTimeNs, conditionKey1,
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    tracker.noteConditionChanged(key1, true, conditionStarts1);
    tracker.noteConditionChanged(key1, false, conditionStops1);
    tracker.noteStart(key2, true, eventStartTimeNs2, conditionKey2,
                      StatsdStats::kDimensionKeySizeHardLimitMin);  // Condition is on already.
    tracker.noteConditionChanged(key1, true, conditionStarts2);
    ASSERT_EQ(1U, anomalyTracker->mAlarms.size());
    auto alarm = anomalyTracker->mAlarms.begin()->second;
    int64_t anomalyFireTimeSec = alarm->timestampSec;
    EXPECT_EQ(conditionStarts2 + 36 * NS_PER_SEC,
            (long long)anomalyFireTimeSec * NS_PER_SEC);

    // Now we test the calculation now that there's a refractory period.
    // At the correct time, declare the anomaly. This will set a refractory period. Make sure it
    // gets correctly taken into account in future predictAnomalyTimestampNs calculations.
    std::unordered_set<sp<const InternalAlarm>, SpHash<InternalAlarm>> firedAlarms({alarm});
    anomalyTracker->informAlarmsFired(anomalyFireTimeSec * NS_PER_SEC, firedAlarms);
    ASSERT_EQ(0u, anomalyTracker->mAlarms.size());
    int64_t refractoryPeriodEndsSec = anomalyFireTimeSec + refPeriodSec;
    EXPECT_EQ(anomalyTracker->getRefractoryPeriodEndsSec(eventKey), refractoryPeriodEndsSec);

    // Now stop and start again. Make sure the new predictAnomalyTimestampNs takes into account
    // the refractory period correctly.
    int64_t eventStopTimeNs = anomalyFireTimeSec * NS_PER_SEC + 10;
    tracker.noteStop(key1, eventStopTimeNs, false);
    tracker.noteStop(key2, eventStopTimeNs, false);
    tracker.noteStart(key1, true, eventStopTimeNs + 1000000, conditionKey1,
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    // Anomaly is ongoing, but we're still in the refractory period.
    ASSERT_EQ(1U, anomalyTracker->mAlarms.size());
    alarm = anomalyTracker->mAlarms.begin()->second;
    EXPECT_EQ(refractoryPeriodEndsSec, (long long)(alarm->timestampSec));

    // Makes sure it is correct after the refractory period is over.
    tracker.noteStop(key1, eventStopTimeNs + 2000000, false);
    int64_t justBeforeRefPeriodNs = (refractoryPeriodEndsSec - 2) * NS_PER_SEC;
    tracker.noteStart(key1, true, justBeforeRefPeriodNs, conditionKey1,
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    alarm = anomalyTracker->mAlarms.begin()->second;
    EXPECT_EQ(justBeforeRefPeriodNs + 40 * NS_PER_SEC,
                (unsigned long long)(alarm->timestampSec * NS_PER_SEC));
}

// Suppose that within one tracker there are two dimensions A and B.
// Suppose A starts, then B starts, and then A stops. We still need to set an anomaly based on the
// elapsed duration of B.
TEST(MaxDurationTrackerTest, TestAnomalyPredictedTimestamp_UpdatedOnStop) {
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    ConditionKey conditionKey1;
    MetricDimensionKey eventKey = getMockedMetricDimensionKey(TagId, 2, "maps");
    conditionKey1[StringToId("APP_BACKGROUND")] = conditionKey;
    ConditionKey conditionKey2;
    conditionKey2[StringToId("APP_BACKGROUND")] = getMockedDimensionKey(TagId, 4, "2");

    unordered_map<MetricDimensionKey, vector<DurationBucket>> buckets;

    /**
     * Suppose we have two sub-dimensions that we're taking the MAX over. In the first of these
     * nested dimensions, are started for 8 seconds. When we stop, the other nested dimension has
     * been started for 5 seconds. So we can only allow 35 more seconds from now.
     */
    int64_t bucketSizeNs = 30 * 1000 * 1000 * 1000LL;
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketEndTimeNs = bucketStartTimeNs + bucketSizeNs;
    int64_t bucketNum = 0;
    int64_t eventStartTimeNs1 = bucketStartTimeNs + 5 * NS_PER_SEC;  // Condition is off at start.
    int64_t eventStopTimeNs1 = bucketStartTimeNs + 13 * NS_PER_SEC;
    int64_t eventStartTimeNs2 = bucketStartTimeNs + 8 * NS_PER_SEC;

    int64_t metricId = 1;
    Alert alert;
    alert.set_id(101);
    alert.set_metric_id(1);
    alert.set_trigger_if_sum_gt(40 * NS_PER_SEC);
    alert.set_num_buckets(2);
    const int32_t refPeriodSec = 45;
    alert.set_refractory_period_secs(refPeriodSec);
    sp<AlarmMonitor> alarmMonitor;
    sp<DurationAnomalyTracker> anomalyTracker =
        new DurationAnomalyTracker(alert, kConfigKey, alarmMonitor);
    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, 1, false, bucketStartTimeNs,
                               bucketNum, bucketStartTimeNs, bucketSizeNs, true, false,
                               {anomalyTracker});

    tracker.noteStart(key1, true, eventStartTimeNs1, conditionKey1,
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    tracker.noteStart(key2, true, eventStartTimeNs2, conditionKey2,
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    tracker.noteStop(key1, eventStopTimeNs1, false);
    ASSERT_EQ(1U, anomalyTracker->mAlarms.size());
    auto alarm = anomalyTracker->mAlarms.begin()->second;
    EXPECT_EQ(eventStopTimeNs1 + 35 * NS_PER_SEC,
              (unsigned long long)(alarm->timestampSec * NS_PER_SEC));
}

TEST(MaxDurationTrackerTest, TestUploadThreshold) {
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    unordered_map<MetricDimensionKey, vector<DurationBucket>> buckets;

    int64_t bucketSizeNs = 30 * 1000 * 1000 * 1000LL;
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketNum = 0;
    int64_t eventStartTimeNs = bucketStartTimeNs + 1;
    int64_t event2StartTimeNs = bucketStartTimeNs + bucketSizeNs + 1;
    int64_t thresholdDurationNs = 2000;

    UploadThreshold threshold;
    threshold.set_gt_int(thresholdDurationNs);

    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, 1, false, bucketStartTimeNs,
                               bucketNum, bucketStartTimeNs, bucketSizeNs, false, false, {});

    // Duration below the gt_int threshold should not be added to past buckets.
    tracker.noteStart(key1, true, eventStartTimeNs, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    tracker.noteStop(key1, eventStartTimeNs + thresholdDurationNs, false);
    tracker.flushIfNeeded(eventStartTimeNs + bucketSizeNs + 1, threshold, &buckets);
    EXPECT_TRUE(buckets.find(eventKey) == buckets.end());

    // Duration above the gt_int threshold should be added to past buckets.
    tracker.noteStart(key1, true, event2StartTimeNs, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    tracker.noteStop(key1, event2StartTimeNs + thresholdDurationNs + 1, false);
    tracker.flushIfNeeded(event2StartTimeNs + bucketSizeNs + 1, threshold, &buckets);
    EXPECT_TRUE(buckets.find(eventKey) != buckets.end());
    ASSERT_EQ(1u, buckets[eventKey].size());
    EXPECT_EQ(thresholdDurationNs + 1, buckets[eventKey][0].mDuration);
}

TEST(MaxDurationTrackerTest, TestNoAccumulatingDuration) {
    const MetricDimensionKey eventKey = getMockedMetricDimensionKey(TagId, 0, "1");
    const HashableDimensionKey key1 = getMockedDimensionKey(TagId, 1, "1");
    const HashableDimensionKey key2 = getMockedDimensionKey(TagId, 1, "2");

    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    unordered_map<MetricDimensionKey, vector<DurationBucket>> buckets;

    int64_t bucketSizeNs = 30 * 1000 * 1000 * 1000LL;
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketEndTimeNs = bucketStartTimeNs + bucketSizeNs;
    int64_t bucketNum = 0;

    int64_t metricId = 1;
    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, -1, false, bucketStartTimeNs,
                               bucketNum, bucketStartTimeNs, bucketSizeNs, false, false, {});

    tracker.noteStart(key1, true, bucketStartTimeNs + 1, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    tracker.noteStop(key1, bucketStartTimeNs + 50, false);
    EXPECT_TRUE(tracker.hasAccumulatedDuration());
    EXPECT_FALSE(tracker.hasStartedDuration());

    tracker.noteStart(key1, true, bucketStartTimeNs + 100, ConditionKey(),
                      StatsdStats::kDimensionKeySizeHardLimitMin);
    EXPECT_TRUE(tracker.hasStartedDuration());
    tracker.noteConditionChanged(key1, false, bucketStartTimeNs + 150);
    EXPECT_TRUE(tracker.hasAccumulatedDuration());
    EXPECT_FALSE(tracker.hasStartedDuration());

    tracker.noteStop(key1, bucketStartTimeNs + 200, true);
    tracker.flushIfNeeded(bucketEndTimeNs + 1, emptyThreshold, &buckets);
    EXPECT_FALSE(tracker.hasAccumulatedDuration());
}

class MaxDurationTrackerTest_DimLimit : public Test {
protected:
    ~MaxDurationTrackerTest_DimLimit() {
        StatsdStats::getInstance().reset();
    }
};

TEST_F(MaxDurationTrackerTest_DimLimit, TestDimLimit) {
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    MaxDurationTracker tracker(kConfigKey, metricId, eventKey, wizard, -1 /* conditionIndex */,
                               false /* nesting */, 0 /* currentBucketStartNs */, 0 /* bucketNum */,
                               0 /* startTimeNs */, bucketSizeNs, false /* conditionSliced */,
                               false /* fullLink */, {} /* anomalyTrackers */);

    const size_t dimensionHardLimit = 900;
    for (int i = 1; i <= dimensionHardLimit; i++) {
        const HashableDimensionKey key = getMockedDimensionKey(TagId, i, "maps");
        tracker.noteStart(key, false /* condition */, i /* eventTime */, ConditionKey(),
                          dimensionHardLimit);
    }
    ASSERT_FALSE(tracker.mHasHitGuardrail);
    const HashableDimensionKey key = getMockedDimensionKey(TagId, dimensionHardLimit + 1, "maps");
    tracker.noteStart(key, false /* condition */, dimensionHardLimit + 1 /* eventTime */,
                      ConditionKey(), dimensionHardLimit);
    EXPECT_TRUE(tracker.mHasHitGuardrail);
}

}  // namespace statsd
}  // namespace os
}  // namespace android
#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif
