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

package com.android.commands.monkey.utils;

import android.os.Build;

/**
 * Android API version constants for version checks.
 *
 * Uses literal API level values so this class compiles on any compileSdkVersion
 * (older SDKs may not define Build.VERSION_CODES.Q, R, UPSIDE_DOWN_CAKE, etc.).
 * These values correspond to Build.VERSION_CODES and provide readable names
 * for version checks throughout the codebase.
 *
 * Usage example:
 * <pre>
 * if (Build.VERSION.SDK_INT >= AndroidVersions.API_29_ANDROID_10) {
 *     // Use API 29+ features
 * }
 * </pre>
 */
public final class AndroidVersions {

    private AndroidVersions() {
        // Utility class, no instantiation
    }

    // API 11 - Android 3.0 (Honeycomb)
    public static final int API_11_ANDROID_3_0 = 11;

    // API 22 - Android 5.1 (Lollipop MR1)
    public static final int API_22_ANDROID_5_1 = 22;

    // API 29 - Android 10 (Q)
    public static final int API_29_ANDROID_10 = 29;

    // API 30 - Android 11 (R)
    public static final int API_30_ANDROID_11 = 30;

    // API 34 - Android 14
    public static final int API_34_ANDROID_14 = 34;

    // API 35 - Android 15
    public static final int API_35_ANDROID_15 = 35;

    /**
     * Check if the current SDK version is at least the given API level.
     * 
     * @param apiLevel The API level constant (e.g., API_29_ANDROID_10)
     * @return true if current SDK_INT >= apiLevel
     */
    public static boolean isAtLeast(int apiLevel) {
        return Build.VERSION.SDK_INT >= apiLevel;
    }

    /**
     * Check if the current SDK version is below the given API level.
     * 
     * @param apiLevel The API level constant (e.g., API_29_ANDROID_10)
     * @return true if current SDK_INT < apiLevel
     */
    public static boolean isBelow(int apiLevel) {
        return Build.VERSION.SDK_INT < apiLevel;
    }
}
