/*
 * Copyright (c) 2008-2009 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#include "internal.h"

uint64_t 
_dispatch_get_nanoseconds(void)
{
	struct timeval now;
	int r = gettimeofday(&now, NULL);
	dispatch_assert_zero(r);
	dispatch_assert(sizeof(NSEC_PER_SEC) == 8);
	dispatch_assert(sizeof(NSEC_PER_USEC) == 8);
	return now.tv_sec * NSEC_PER_SEC + now.tv_usec * NSEC_PER_USEC;
}

#if defined(__i386__) || defined(__x86_64__)
// x86 currently implements mach time in nanoseconds; this is NOT likely to change
#define _dispatch_time_mach2nano(x) (x)
#define _dispatch_time_nano2mach(x) (x)
#else
static struct _dispatch_host_time_data_s {
	mach_timebase_info_data_t tbi;
	uint64_t safe_numer_math;
	dispatch_once_t pred;
} _dispatch_host_time_data;

static void
_dispatch_get_host_time_init(void *context __attribute__((unused)))
{
	(void)dispatch_assume_zero(mach_timebase_info(
	    &_dispatch_host_time_data.tbi));
	_dispatch_host_time_data.safe_numer_math = DISPATCH_TIME_FOREVER / _dispatch_host_time_data.tbi.numer;
}

static uint64_t
_dispatch_time_mach2nano(uint64_t nsec)
{
	struct _dispatch_host_time_data_s *const data = &_dispatch_host_time_data;
	uint64_t small_tmp = nsec;
#ifdef __LP64__
	__uint128_t big_tmp = nsec;
#else
	long double big_tmp = nsec;
#endif

	dispatch_once_f(&data->pred, NULL, _dispatch_get_host_time_init);

	if (slowpath(data->tbi.numer != data->tbi.denom)) {
		if (nsec < data->safe_numer_math) {
			small_tmp *= data->tbi.numer;
			small_tmp /= data->tbi.denom;
		} else {
			big_tmp *= data->tbi.numer;
			big_tmp /= data->tbi.denom;
			small_tmp = big_tmp;
		}
	}
	return small_tmp;
}

static int64_t
_dispatch_time_nano2mach(int64_t nsec)
{
	struct _dispatch_host_time_data_s *const data = &_dispatch_host_time_data;
#ifdef __LP64__
	__int128_t big_tmp = nsec;
#else
	long double big_tmp = nsec;
#endif

	dispatch_once_f(&data->pred, NULL, _dispatch_get_host_time_init);

	if (fastpath(data->tbi.numer == data->tbi.denom)) {
		return nsec;
	}

	// Multiply by the inverse to convert nsec to Mach absolute time
	big_tmp *= data->tbi.denom;
	big_tmp /= data->tbi.numer;

	if (big_tmp > INT64_MAX) {
		return INT64_MAX;
	}
	if (big_tmp < INT64_MIN) {
		return INT64_MIN;
	}
	return big_tmp;
}
#endif

dispatch_time_t
dispatch_time(dispatch_time_t inval, int64_t delta)
{
	if (inval == DISPATCH_TIME_FOREVER) {
		return DISPATCH_TIME_FOREVER;
	}
	if ((int64_t)inval < 0) {
		// wall clock
		if (delta >= 0) {
			if ((int64_t)(inval -= delta) >= 0) {
				return DISPATCH_TIME_FOREVER;      // overflow
			}
			return inval;
		}
		if ((int64_t)(inval -= delta) >= -1) {
			// -1 is special == DISPATCH_TIME_FOREVER == forever
			return -2;      // underflow
		}
		return inval;
	}
	// mach clock
	delta = _dispatch_time_nano2mach(delta);
   	if (inval == 0) {
		inval = _dispatch_absolute_time();
	}
	if (delta >= 0) {
		if ((int64_t)(inval += delta) <= 0) {
			return DISPATCH_TIME_FOREVER;      // overflow
		}
		return inval;
	}
	if ((int64_t)(inval += delta) < 1) {
		return 1;       // underflow
	}
	return inval;
}

dispatch_time_t
dispatch_walltime(const struct timespec *inval, int64_t delta)
{
	int64_t nsec;
	
	if (inval) {
		nsec = inval->tv_sec * 1000000000ull + inval->tv_nsec;
	} else {
		nsec = _dispatch_get_nanoseconds();
	}

	nsec += delta;
	if (nsec <= 1) {
		// -1 is special == DISPATCH_TIME_FOREVER == forever
		return delta >= 0 ? DISPATCH_TIME_FOREVER : (uint64_t)-2ll;
	}

	return -nsec;
}

uint64_t
_dispatch_timeout(dispatch_time_t when)
{
	uint64_t now;

	if (when == DISPATCH_TIME_FOREVER) {
		return DISPATCH_TIME_FOREVER;
	}
	if (when == 0) {
		return 0;
	}
	if ((int64_t)when < 0) {
		when = -(int64_t)when;
		now = _dispatch_get_nanoseconds();
		return now >= when ? 0 : when - now;
	}
	now = _dispatch_absolute_time();
	return now >= when ? 0 : _dispatch_time_mach2nano(when - now);
}

#if USE_POSIX_SEM
/*
 * Unlike Mach semaphores, POSIX semaphores take an absolute, real time as an
 * argument to sem_timedwait().  This routine converts from dispatch_time_t
 * but assumes the caller has already handled the possibility of
 * DISPATCH_TIME_FOREVER.
 */
struct timespec
_dispatch_timeout_ts(dispatch_time_t when)
{
	struct timespec ts_realtime;
	uint64_t abstime;
	int ret;

	if (when == 0) {
		ret = clock_gettime(CLOCK_REALTIME, &ts_realtime);
		(void)dispatch_assume_zero(ret);
		return (ts_realtime);
	}
	if ((int64_t)when < 0) {
		ret = clock_gettime(CLOCK_REALTIME, &ts_realtime);
		(void)dispatch_assume_zero(ret);
		when = -(int64_t)when + ts_realtime.tv_sec * NSEC_PER_SEC +
		    ts_realtime.tv_nsec;
		ts_realtime.tv_sec = when / NSEC_PER_SEC;
		ts_realtime.tv_nsec = when % NSEC_PER_SEC;
		return (ts_realtime);
	}

	/*
	 * Rebase 'when': (when - abstime) + realtime.
	 *
	 * XXXRW: Should we cache this delta to avoid system calls?
	 */
	abstime = _dispatch_absolute_time();
	ret = clock_gettime(CLOCK_REALTIME, &ts_realtime);
	(void)dispatch_assume_zero(ret);
	printf("now is %d.%lu\n", ts_realtime.tv_sec, ts_realtime.tv_nsec);
	ts_realtime.tv_sec += (when - abstime) / NSEC_PER_SEC;
	ts_realtime.tv_nsec += (when - abstime) % NSEC_PER_SEC;
	return (ts_realtime);
}
#endif
