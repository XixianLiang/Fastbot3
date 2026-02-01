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
import android.os.Build;
import android.os.RemoteException;
import android.view.Display;
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
 */
public class MonkeyRotationEvent extends MonkeyEvent {

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

        // inject rotation event
        try {

            if (Build.VERSION.SDK_INT >= 35){
                try{
                    Class<?> iwmClass = iwm.getClass();
                    Method freezeMethod = iwmClass.getMethod("freezeDisplayRotation", int.class, int.class, String.class);
                    freezeMethod.invoke(iwm, Display.DEFAULT_DISPLAY, rotationConstant, "com.bytedance.fastbot");
                } catch (InvocationTargetException e) {
                    Throwable cause = e.getCause();
                    if (cause instanceof IllegalArgumentException) {
                        Logger.warningPrintln("MonkeyRotationEvent: " + cause.getMessage());
                        return MonkeyEvent.INJECT_FAIL;
                    }
                    throw new RuntimeException(e);
                } catch (IllegalAccessException | NoSuchMethodException e) {
                    throw new RuntimeException(e);
                }
            }
            else {
                iwm.freezeRotation(rotationConstant);
            }
            if (!mPersist) {
                if (Build.VERSION.SDK_INT >= 35){
                    try{
                        Class<?> iwmClass = iwm.getClass();
                        Method thawMethod = iwmClass.getMethod("thawDisplayRotation", int.class, String.class);
                        thawMethod.invoke(iwm, Display.DEFAULT_DISPLAY, "com.bytedance.fastbot");
                    } catch (InvocationTargetException e) {
                        Throwable cause = e.getCause();
                        if (cause instanceof IllegalArgumentException) {
                            Logger.warningPrintln("MonkeyRotationEvent thaw: " + cause.getMessage());
                            return MonkeyEvent.INJECT_FAIL;
                        }
                        throw new RuntimeException(e);
                    } catch (IllegalAccessException | NoSuchMethodException e) {
                        throw new RuntimeException(e);
                    }
                } else {
                    iwm.thawRotation();
                }
            }
            AndroidDevice.invalidateDisplayBoundsCache();  // display size may have changed
            return MonkeyEvent.INJECT_SUCCESS;
        } catch (RemoteException ex) {
            return MonkeyEvent.INJECT_ERROR_REMOTE_EXCEPTION;
        }
    }
}
