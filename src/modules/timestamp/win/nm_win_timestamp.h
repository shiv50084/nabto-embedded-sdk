#ifndef _NP_WIN_TIMESTAMP_H_
#define _NP_WIN_TIMESTAMP_H_

#include <platform/np_platform.h>
#include <nabto_types.h>

bool nm_win_ts_passed_or_now(np_timestamp* ts);

bool nm_win_ts_less_or_equal(np_timestamp* t1, np_timestamp* t2);

void nm_win_ts_set_future_timestamp(np_timestamp* ts, uint32_t ms);

void nm_win_ts_now(np_timestamp* ts);

uint32_t nm_win_ts_difference(np_timestamp* t1, np_timestamp* t2);

uint32_t nm_win_ts_now_ms(void);

#endif // _NP_WIN_TIMESTAMP_H_