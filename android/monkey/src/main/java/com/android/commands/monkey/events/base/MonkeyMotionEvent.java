/*
 * Copyright (c) 2020 Bytedance Inc.
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
 */

package com.android.commands.monkey.events.base;

import android.app.IActivityManager;
import android.graphics.Rect;
import android.hardware.input.InputManager;
import android.os.SystemClock;
import android.util.SparseArray;
import android.view.IWindowManager;
import android.view.MotionEvent;

import com.android.commands.monkey.events.MonkeyEvent;
import com.android.commands.monkey.framework.AndroidDevice;
import com.android.commands.monkey.utils.Logger;

/**
 * @author Zhao Zhang
 */

/**
 * Monkey motion event (touch / optional future mouse).
 * Performance: reuse PointerProperties[] and PointerCoords[] to avoid allocation per getEvent() (see SCRCPY_VS_FASTBOT_OPTIMIZATION_ANALYSIS §3).
 * Future mouse (SCRCPY_VS_FASTBOT_OPTIMIZATION_ANALYSIS §二.5): for SOURCE_MOUSE on API 23+, send ACTION_DOWN then ACTION_BUTTON_PRESS; on release ACTION_BUTTON_RELEASE then ACTION_UP; use InputManager.setActionButton(MotionEvent, actionButton) via reflection.
 */
public abstract class MonkeyMotionEvent extends MonkeyEvent {

    /** Max pointers for reuse arrays (obtain(long, long, int, int, int[], PointerCoords[], ...)). */
    private static final int MAX_POINTERS = 10;

    private static final ThreadLocal<int[]> sPointerIds = ThreadLocal.withInitial(() -> new int[MAX_POINTERS]);

    private static final ThreadLocal<MotionEvent.PointerCoords[]> sPointerCoords = ThreadLocal.withInitial(() -> {
        MotionEvent.PointerCoords[] c = new MotionEvent.PointerCoords[MAX_POINTERS];
        for (int i = 0; i < MAX_POINTERS; i++) {
            c[i] = new MotionEvent.PointerCoords();
        }
        return c;
    });

    // statusBarHeight / bottomBarHeight from AndroidDevice (cached, invalidated on rotation).

    private long mDownTime;
    private long mEventTime;
    private int mAction;
    private SparseArray<MotionEvent.PointerCoords> mPointers;
    private int mMetaState;
    private float mXPrecision;
    private float mYPrecision;
    private int mDeviceId;
    private int mSource;
    private int mFlags;
    private int mEdgeFlags;
    private int type = 0;

    // If true, this is an intermediate step (more verbose logging, only)
    private boolean mIntermediateNote;

    protected MonkeyMotionEvent(int type, int source, int action) {
        super(type);
        mSource = source;
        mDownTime = -1;
        mEventTime = -1;
        mAction = action;
        mPointers = new SparseArray<MotionEvent.PointerCoords>();
        mXPrecision = 1;
        mYPrecision = 1;
    }

    public MonkeyMotionEvent addPointer(int id, float x, float y) {
        return addPointer(id, x, y, 0, 0);
    }

    public MonkeyMotionEvent addPointer(int id, float x, float y, float pressure, float size) {
        // SCRCPY_VS_FASTBOT_OPTIMIZATION_ANALYSIS §三.1: clip to focused display bounds so (x,y) is in device display space.
        Rect bounds = AndroidDevice.getDisplayBounds(AndroidDevice.getFocusedDisplayId());
        int statusBarHeight = AndroidDevice.getStatusBarHeight();
        int bottomBarHeight = AndroidDevice.getBottomBarHeight();
        if (bounds != null) {
            int maxY = bounds.height() - 1;
            if (bottomBarHeight > maxY) bottomBarHeight = maxY;
        }
        MotionEvent.PointerCoords c = new MotionEvent.PointerCoords();
        c.x = x;
        if (y <= statusBarHeight) {
            y = statusBarHeight + 1;
        } else if (y >= bottomBarHeight) {
            if (type == 1) y = bottomBarHeight - 1;
        }
        c.y = y;
        c.pressure = pressure;
        c.size = size;
        mPointers.append(id, c);
        return this;
    }

    public boolean getIntermediateNote() {
        return mIntermediateNote;
    }

    public MonkeyMotionEvent setIntermediateNote(boolean b) {
        mIntermediateNote = b;
        return this;
    }

    public int getAction() {
        return mAction;
    }

    public long getDownTime() {
        return mDownTime;
    }

    public MonkeyMotionEvent setDownTime(long downTime) {
        mDownTime = downTime;
        return this;
    }

    public long getEventTime() {
        return mEventTime;
    }

    public MonkeyMotionEvent setEventTime(long eventTime) {
        mEventTime = eventTime;
        return this;
    }

    public MonkeyMotionEvent setMetaState(int metaState) {
        mMetaState = metaState;
        return this;
    }

    public MonkeyMotionEvent setPrecision(float xPrecision, float yPrecision) {
        mXPrecision = xPrecision;
        mYPrecision = yPrecision;
        return this;
    }

    public MonkeyMotionEvent setDeviceId(int deviceId) {
        mDeviceId = deviceId;
        return this;
    }

    public MonkeyMotionEvent setEdgeFlags(int edgeFlags) {
        mEdgeFlags = edgeFlags;
        return this;
    }

    public MonkeyMotionEvent setType(int type) {
        this.type = type;
        return this;
    }

    /**
     * @return instance of a motion event
     * Reuses ThreadLocal int[] and PointerCoords[] to avoid allocation per call.
     */
    /* private */ MotionEvent getEvent() {
        int pointerCount = mPointers.size();
        if (pointerCount > MAX_POINTERS) {
            pointerCount = MAX_POINTERS;
        }
        int[] pointerIds = sPointerIds.get();
        MotionEvent.PointerCoords[] coords = sPointerCoords.get();
        for (int i = 0; i < pointerCount; i++) {
            pointerIds[i] = mPointers.keyAt(i);
            MotionEvent.PointerCoords src = mPointers.valueAt(i);
            coords[i].x = src.x;
            coords[i].y = src.y;
            coords[i].pressure = src.pressure;
            coords[i].size = src.size;
        }
        long eventTime = mEventTime < 0 ? SystemClock.uptimeMillis() : mEventTime;
        return MotionEvent.obtain(mDownTime, eventTime, mAction, pointerCount, pointerIds, coords,
                mMetaState, mXPrecision, mYPrecision, mDeviceId, mEdgeFlags, mSource, mFlags);
    }

    @Override
    public boolean isThrottlable() {
        return (getAction() == MotionEvent.ACTION_UP);
    }

    @Override
    public int injectEvent(IWindowManager iwm, IActivityManager iam, int verbose) {
        MotionEvent me = getEvent();
        if ((verbose > 0 && !mIntermediateNote) || verbose > 1) {
            StringBuilder msg = new StringBuilder(":Sending ");
            msg.append(getTypeLabel()).append(" (");
            switch (me.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    msg.append("ACTION_DOWN");
                    break;
                case MotionEvent.ACTION_MOVE:
                    msg.append("ACTION_MOVE");
                    break;
                case MotionEvent.ACTION_UP:
                    msg.append("ACTION_UP");
                    break;
                case MotionEvent.ACTION_CANCEL:
                    msg.append("ACTION_CANCEL");
                    break;
                case MotionEvent.ACTION_POINTER_DOWN:
                    msg.append("ACTION_POINTER_DOWN ").append(me.getPointerId(me.getActionIndex()));
                    break;
                case MotionEvent.ACTION_POINTER_UP:
                    msg.append("ACTION_POINTER_UP ").append(me.getPointerId(me.getActionIndex()));
                    break;
                default:
                    msg.append(me.getAction());
                    break;
            }
            msg.append("):");

            int pointerCount = me.getPointerCount();
            for (int i = 0; i < pointerCount; i++) {
                msg.append(" ").append(me.getPointerId(i));
                msg.append(":(").append(me.getX(i)).append(",").append(me.getY(i)).append(")");
            }
            Logger.println(msg.toString());
        }
        try {
            // SCRCPY_VS_FASTBOT_OPTIMIZATION_ANALYSIS §三.1: use focused display so touch goes to correct display in multi-display.
            int displayId = AndroidDevice.getFocusedDisplayId();
            if (displayId != 0 && !AndroidDevice.supportsInputEvents(displayId)) {
                return MonkeyEvent.INJECT_FAIL;
            }
            if (!AndroidDevice.setInputEventDisplayId(me, displayId)) {
                return MonkeyEvent.INJECT_FAIL;
            }
            if (!InputManager.getInstance().injectInputEvent(me,
                    InputManager.INJECT_INPUT_EVENT_MODE_WAIT_FOR_RESULT)) {
                return MonkeyEvent.INJECT_FAIL;
            }
        } catch (SecurityException e) {
            return MonkeyEvent.INJECT_FAIL;
        } finally {
            me.recycle();
        }
        return MonkeyEvent.INJECT_SUCCESS;
    }

    protected abstract String getTypeLabel();
}
