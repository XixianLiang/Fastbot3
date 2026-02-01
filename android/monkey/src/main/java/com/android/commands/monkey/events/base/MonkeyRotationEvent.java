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
import com.android.commands.monkey.utils.Logger;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/**
 * Monkey screen rotation event.
 * Accepts either rotation degrees (0, 90, 180, 270) or Surface rotation constants (0, 1, 2, 3).
 * IWindowManager requires rotation constant, not degree.
 * Multi-version probe + cache for freeze/thaw (SCRCPY_VS_FASTBOT_OPTIMIZATION_ANALYSIS §二.2).
 */
public class MonkeyRotationEvent extends MonkeyEvent {

    private static final int DEFAULT_DISPLAY = 0;
    private static final String ROTATION_CALLER = "com.bytedance.fastbot";

    /** Cached freeze: -1 = not resolved, 3 = freezeDisplayRotation(displayId, rotation, caller), 2 = (displayId, rotation), 1 = freezeRotation(rotation). */
    private static Method sFreezeMethod;
    private static int sFreezeVersion = -1;

    /** Cached thaw: -1 = not resolved, 2 = thawDisplayRotation(displayId, caller), 1 = thawDisplayRotation(displayId), 0 = thawRotation(). */
    private static Method sThawMethod;
    private static int sThawVersion = -1;

    private static synchronized void resolveFreezeMethod(IWindowManager iwm) {
        if (sFreezeVersion >= 0) {
            return;
        }
        Class<?> iwmClass = iwm.getClass();
        try {
            sFreezeMethod = iwmClass.getMethod("freezeDisplayRotation", int.class, int.class, String.class);
            sFreezeVersion = 3;
            return;
        } catch (NoSuchMethodException ignored) {
        }
        try {
            sFreezeMethod = iwmClass.getMethod("freezeDisplayRotation", int.class, int.class);
            sFreezeVersion = 2;
            return;
        } catch (NoSuchMethodException ignored) {
        }
        sFreezeVersion = 1; // use iwm.freezeRotation(rotation)
    }

    private static synchronized void resolveThawMethod(IWindowManager iwm) {
        if (sThawVersion >= 0) {
            return;
        }
        Class<?> iwmClass = iwm.getClass();
        try {
            sThawMethod = iwmClass.getMethod("thawDisplayRotation", int.class, String.class);
            sThawVersion = 2;
            return;
        } catch (NoSuchMethodException ignored) {
        }
        try {
            sThawMethod = iwmClass.getMethod("thawDisplayRotation", int.class);
            sThawVersion = 1;
            return;
        } catch (NoSuchMethodException ignored) {
        }
        sThawVersion = 0; // use iwm.thawRotation()
    }

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

        // inject rotation event (multi-version probe + cache)
        try {
            resolveFreezeMethod(iwm);
            if (sFreezeVersion == 1) {
                iwm.freezeRotation(rotationConstant);
            } else {
                try {
                    if (sFreezeVersion == 3) {
                        sFreezeMethod.invoke(iwm, DEFAULT_DISPLAY, rotationConstant, ROTATION_CALLER);
                    } else {
                        sFreezeMethod.invoke(iwm, DEFAULT_DISPLAY, rotationConstant);
                    }
                } catch (InvocationTargetException e) {
                    Throwable cause = e.getCause();
                    if (cause instanceof IllegalArgumentException) {
                        Logger.warningPrintln("MonkeyRotationEvent: " + cause.getMessage());
                        return MonkeyEvent.INJECT_FAIL;
                    }
                    throw new RuntimeException(e);
                } catch (IllegalAccessException e) {
                    throw new RuntimeException(e);
                }
            }
            if (!mPersist) {
                resolveThawMethod(iwm);
                if (sThawVersion == 0) {
                    iwm.thawRotation();
                } else {
                    try {
                        if (sThawVersion == 2) {
                            sThawMethod.invoke(iwm, DEFAULT_DISPLAY, ROTATION_CALLER);
                        } else {
                            sThawMethod.invoke(iwm, DEFAULT_DISPLAY);
                        }
                    } catch (InvocationTargetException e) {
                        Throwable cause = e.getCause();
                        if (cause instanceof IllegalArgumentException) {
                            Logger.warningPrintln("MonkeyRotationEvent thaw: " + cause.getMessage());
                            return MonkeyEvent.INJECT_FAIL;
                        }
                        throw new RuntimeException(e);
                    } catch (IllegalAccessException e) {
                        throw new RuntimeException(e);
                    }
                }
            }
            AndroidDevice.invalidateDisplayBoundsCache();  // display size may have changed
            AndroidDevice.invalidateDisplayBarHeights();   // status bar / bottom bar heights may have changed
            return MonkeyEvent.INJECT_SUCCESS;
        } catch (RemoteException ex) {
            return MonkeyEvent.INJECT_ERROR_REMOTE_EXCEPTION;
        }
    }
}
