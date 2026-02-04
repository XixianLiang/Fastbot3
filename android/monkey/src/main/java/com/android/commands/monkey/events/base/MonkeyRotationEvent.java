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
import android.os.RemoteException;
import android.view.IWindowManager;
import android.view.Surface;

import com.android.commands.monkey.events.MonkeyEvent;
import com.android.commands.monkey.framework.AndroidDevice;
import com.android.commands.monkey.framework.APIAdapter;
import com.android.commands.monkey.utils.Logger;

import java.lang.reflect.InvocationTargetException;

/**
 * Monkey screen rotation event.
 * Accepts either rotation degrees (0, 90, 180, 270) or Surface rotation constants (0, 1, 2, 3).
 * IWindowManager requires rotation constant, not degree.
 *
 * Freeze/thaw reflection is delegated to {@link APIAdapter#freezeDisplayRotation} and
 * {@link APIAdapter#thawDisplayRotation} (multi-signature probe + cache).
 */
public class MonkeyRotationEvent extends MonkeyEvent {

    private static final int DEFAULT_DISPLAY = 0;

    public final int mRotationDegree;
    public final boolean mPersist;

    /**
     * Construct a rotation Event.
     *
     * @param degree  Rotation: either degrees (0, 90, 180, 270) or Surface constants (0, 1, 2, 3), or -1 to unfreeze.
     * @param persist Should we keep the rotation lock after the orientation change.
     */
    public MonkeyRotationEvent(int degree, boolean persist) {
        super(EVENT_TYPE_ROTATION);
        mRotationDegree = degree;
        mPersist = persist;
    }

    /**
     * Convert to the rotation constant required by IWindowManager (0, 1, 2, 3 or -1).
     * Accepts either degrees (0, 90, 180, 270) or already-constant (0, 1, 2, 3).
     */
    private static int toRotationConstant(int degreeOrConstant) {
        if (degreeOrConstant == -1) {
            return -1;
        }
        switch (degreeOrConstant) {
            case 0:
                return Surface.ROTATION_0;
            case 1:
                return Surface.ROTATION_90;
            case 2:
                return Surface.ROTATION_180;
            case 3:
                return Surface.ROTATION_270;
            case 90:
                return Surface.ROTATION_90;
            case 180:
                return Surface.ROTATION_180;
            case 270:
                return Surface.ROTATION_270;
            default:
                Logger.warningPrintln("MonkeyRotationEvent: invalid rotation " + degreeOrConstant + ", using ROTATION_0");
                return Surface.ROTATION_0;
        }
    }

    @Override
    public int injectEvent(IWindowManager iwm, IActivityManager iam, int verbose) {
        int rotationConstant = toRotationConstant(mRotationDegree);
        if (verbose > 0) {
            Logger.println(":Sending rotation degree=" + mRotationDegree + " (constant=" + rotationConstant + "), persist=" + mPersist);
        }

        try {
            APIAdapter.freezeDisplayRotation(iwm, DEFAULT_DISPLAY, rotationConstant, APIAdapter.ROTATION_CALLER);
            if (!mPersist) {
                APIAdapter.thawDisplayRotation(iwm, DEFAULT_DISPLAY, APIAdapter.ROTATION_CALLER);
            }
            AndroidDevice.invalidateDisplayBoundsCache();  // display size may have changed
            AndroidDevice.invalidateDisplayBarHeights();   // status bar / bottom bar heights may have changed
            AndroidDevice.invalidateFocusedDisplayIdCache(); // top task / display may have changed
            return MonkeyEvent.INJECT_SUCCESS;
        } catch (InvocationTargetException e) {
            Throwable cause = e.getCause();
            if (cause instanceof IllegalArgumentException) {
                Logger.warningPrintln("MonkeyRotationEvent: " + cause.getMessage());
                return MonkeyEvent.INJECT_FAIL;
            }
            throw new RuntimeException(e);
        } catch (RemoteException ex) {
            return MonkeyEvent.INJECT_ERROR_REMOTE_EXCEPTION;
        } catch (Exception e) {
            if (e.getCause() != null && e.getCause() instanceof IllegalArgumentException) {
                Logger.warningPrintln("MonkeyRotationEvent: " + e.getCause().getMessage());
                return MonkeyEvent.INJECT_FAIL;
            }
            throw new RuntimeException(e);
        }
    }
}
