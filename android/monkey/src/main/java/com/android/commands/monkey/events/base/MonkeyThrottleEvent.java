/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.commands.monkey.events.base;

import android.app.IActivityManager;
import android.view.IWindowManager;

import com.android.commands.monkey.events.MonkeyEvent;
import com.android.commands.monkey.utils.Logger;

import java.util.ArrayDeque;
import java.util.Queue;

/**
 * Monkey throttle event. §8.10: pooled via obtain/recycle to reduce allocations.
 */
public class MonkeyThrottleEvent extends MonkeyEvent {
    private static final int MAX_POOL_SIZE = 64;
    private static final Queue<MonkeyThrottleEvent> sPool = new ArrayDeque<>(MAX_POOL_SIZE);

    /* package */ long mThrottle;

    public MonkeyThrottleEvent(long throttle) {
        super(MonkeyEvent.EVENT_TYPE_THROTTLE);
        mThrottle = throttle;
    }

    /** §8.10: Obtain from pool or create new. Call recycle() after inject to return to pool. */
    public static MonkeyThrottleEvent obtain(long throttle) {
        MonkeyThrottleEvent e;
        synchronized (sPool) {
            e = sPool.poll();
        }
        if (e != null) {
            e.mThrottle = throttle;
            return e;
        }
        return new MonkeyThrottleEvent(throttle);
    }

    /** §8.10: Return to pool after inject. Reset eventId so setEventId() can be called again when reused. */
    public static void recycle(MonkeyThrottleEvent e) {
        if (e == null) return;
        e.mThrottle = 0;
        e.resetEventId();
        synchronized (sPool) {
            if (sPool.size() < MAX_POOL_SIZE) sPool.offer(e);
        }
    }

    @Override
    public int injectEvent(IWindowManager iwm, IActivityManager iam, int verbose) {

        if (verbose > 1 && mThrottle > 0) {
            Logger.println("Sleeping for " + mThrottle + " milliseconds");
        }
        try {
            Thread.sleep(mThrottle);
        } catch (InterruptedException e1) {
            Logger.warningPrintln("Monkey interrupted in sleep.");
            return MonkeyEvent.INJECT_FAIL;
        }

        return MonkeyEvent.INJECT_SUCCESS;
    }
}
