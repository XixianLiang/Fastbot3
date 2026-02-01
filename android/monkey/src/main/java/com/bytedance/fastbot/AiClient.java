/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */

package com.bytedance.fastbot;

import android.graphics.PointF;
import android.os.Build;
import android.os.SystemClock;

import com.android.commands.monkey.fastbot.client.Operate;
import com.android.commands.monkey.fastbot.client.OperateResult;
import com.android.commands.monkey.utils.Logger;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;

/**
 * @author Jianqiang Guo, Zhao Zhang
 */

public class AiClient {

    private static final AiClient singleton;

    static {
        boolean success;
        long begin = SystemClock.elapsedRealtimeNanos();
        success = tryToLoadNativeLib(false);
        if (!success){
            success = tryToLoadNativeLib(true);
        }
        long end = SystemClock.elapsedRealtimeNanos();
        Logger.infoFormat("load fastbot_native takes %d ms.", TimeUnit.NANOSECONDS.toMillis(end - begin));
        singleton = new AiClient(success);
    }

    public enum AlgorithmType {
        Random(0),
        SataRL(1),
        SataNStep(2),
        NStepQ(3),
        Reuse(4);

        private final int _value;

        AlgorithmType(int value) {
            this._value = value;
        }

        public int value() {
            return this._value;
        }
    }

    public static void InitAgent(AlgorithmType agentType, String packagename) {
        singleton.fgdsaf5d(agentType.value(), packagename, 0);
    }

    private boolean loaded = false;

    protected AiClient(boolean success) {
        loaded = success;
    }

    private static boolean tryToLoadNativeLib(boolean fromAPK){
        String path = "";
        try {
            path = getAiPath(fromAPK);
            System.load(path);
            Logger.println("fastbot native : library load!");
            Logger.println("fastbot native path is : "+path);
        } catch (UnsatisfiedLinkError e) {
            Logger.errorPrintln("Error: Could not load library!");
            Logger.errorPrintln(path);
            e.printStackTrace();
            return false;
        }
        return true;
    }

    private static String getAiPathFromAPK(){
        String[] abis = Build.SUPPORTED_ABIS;
        List<String> abisList = Arrays.asList(abis);
        if (abisList.contains("x86_64")) {
            return "/data/local/tmp/monkey.apk!/lib/x86_64/libfastbot_native.so";
        } else if (abisList.contains("x86")) {
            return "/data/local/tmp/monkey.apk!/lib/x86/libfastbot_native.so";
        } else if (abisList.contains("arm64-v8a")) {
            return "/data/local/tmp/monkey.apk!/lib/arm64-v8a/libfastbot_native.so";
        } else {
            return "/data/local/tmp/monkey.apk!/lib/armeabi-v7a/libfastbot_native.so";
        }
    }

    private static String getAiPathLocally(){
        String[] abis = Build.SUPPORTED_ABIS;
        List<String> abisList = Arrays.asList(abis);
        if (abisList.contains("x86_64")) {
            return "/data/local/tmp/x86_64/libfastbot_native.so";
        } else if (abisList.contains("x86")) {
            return "/data/local/tmp/x86/libfastbot_native.so";
        } else if (abisList.contains("arm64-v8a")) {
            return "/data/local/tmp/arm64-v8a/libfastbot_native.so";
        } else {
            return "/data/local/tmp/armeabi-v7a/libfastbot_native.so";
        }
    }

    private static String getAiPath(boolean fromAPK) {
        if(Build.VERSION.SDK_INT<=Build.VERSION_CODES.LOLLIPOP_MR1) {
            return getAiPathLocally();
        }else {
            if (fromAPK) {
                return getAiPathFromAPK();
            }else {
                return getAiPathLocally();
            }
        }
    }

    public static void loadResMapping(String resmapping) {
        if (null == singleton) {
            Logger.println("// Error: AiCore not initted!");
            return;
        }
        if (!singleton.loaded) {
            Logger.println("// Error: Could not load native library!");
            Logger.println("Please report this bug issue to github");
            System.exit(1);
        }
        singleton.jdasdbil(resmapping);
    }

    public static Operate getAction(String activity, String pageDesc) {
        return singleton.b1bhkadf(activity, pageDesc);
    }

    /**
     * Report current activity for coverage tracking (performance: coverage in C++, PERF §3.4).
     */
    public static void reportActivity(String activity) {
        if (activity != null && singleton.loaded) {
            singleton.reportActivityNative(activity);
        }
    }

    /**
     * Get coverage summary from native: {"stepsCount":N,"testedActivities":["a1",...]}
     */
    public static String getCoverageJson() {
        if (!singleton.loaded) return "{}";
        String s = singleton.getCoverageJsonNative();
        return s != null ? s : "{}";
    }

    /**
     * Get next fuzz action JSON from native (performance §3.3). Returns one fuzz action as JSON;
     * simplify=true picks from rotation/app_switch/drag/pinch/click only.
     */
    public static String getNextFuzzAction(int displayWidth, int displayHeight, boolean simplify) {
        if (!singleton.loaded) return null;
        return singleton.getNextFuzzActionNative(displayWidth, displayHeight, simplify);
    }

    /**
     * Get action from XML supplied as Direct ByteBuffer (performance: avoids JNI string copy).
     * Tries structured result first to avoid JSON parse (opt4); falls back to JSON if needed.
     */
    public static Operate getActionFromBuffer(String activity, ByteBuffer xmlBuffer) {
        if (xmlBuffer == null || !xmlBuffer.isDirect() || xmlBuffer.remaining() <= 0) {
            return null;
        }
        int byteLength = xmlBuffer.remaining();
        OperateResult r = singleton.getActionFromBufferNativeStructured(activity, xmlBuffer, byteLength);
        if (r != null) {
            return Operate.fromOperateResult(r);
        }
        String operateStr = singleton.getActionFromBufferNative(activity, xmlBuffer, byteLength);
        if (operateStr == null || operateStr.length() < 1) {
            return null;
        }
        return Operate.fromJson(operateStr);
    }

    private native void jdasdbil(String b9);

    private native String b0bhkadf(String a0, String a1);
    private native void fgdsaf5d(int b7, String b2, int t);
    private native boolean nkksdhdk(String a0, float p1, float p2);

    /**
     * Batch check: multiple points in one JNI call (performance optimization).
     * @param activity current activity name
     * @param xCoords x coordinates, same length as yCoords
     * @param yCoords y coordinates
     * @return array of booleans, true if point is in black rect (shielded), null if error or native not loaded
     */
    private native boolean[] checkPointsInShieldNative(String activity, float[] xCoords, float[] yCoords);

    /**
     * Get action from XML in Direct ByteBuffer (performance: avoid GetStringUTFChars copy, PERF §3.1).
     * @param activity current activity name
     * @param xmlBuffer direct ByteBuffer containing UTF-8 XML bytes; position/limit define the region
     * @param byteLength number of bytes to read (must be buffer limit/remaining, not capacity)
     * @return Operate JSON string, or null/empty on error
     */
    private native String getActionFromBufferNative(String activity, ByteBuffer xmlBuffer, int byteLength);

    /** Structured result to avoid JSON parse (SECURITY_AND_OPTIMIZATION §7 opt4). Returns null on error. */
    private native OperateResult getActionFromBufferNativeStructured(String activity, ByteBuffer xmlBuffer, int byteLength);

    private native void reportActivityNative(String activity);
    private native String getCoverageJsonNative();
    private native String getNextFuzzActionNative(int displayWidth, int displayHeight, boolean simplify);

    public static native String getNativeVersion();

    public static boolean checkPointIsShield(String activity, PointF point) {
        return singleton.nkksdhdk(activity, point.x, point.y);
    }

    /**
     * Batch check points for black rect (shield). Reduces JNI round-trips from up to N to 1.
     * @param activity current activity name
     * @param xCoords x coordinates
     * @param yCoords y coordinates (same length as xCoords)
     * @return array of booleans (true = in shield), or null if error; length same as input
     */
    public static boolean[] checkPointsInShield(String activity, float[] xCoords, float[] yCoords) {
        if (!singleton.loaded || xCoords == null || yCoords == null || xCoords.length != yCoords.length) {
            return null;
        }
        return singleton.checkPointsInShieldNative(activity, xCoords, yCoords);
    }

    public Operate b1bhkadf(String activity, String pageDesc) {
        if (!loaded) {
            Logger.println("// Error: Could not load native library!");
            Logger.println("Please report this bug issue to github");
            System.exit(1);
        }
        String operateStr = b0bhkadf(activity, pageDesc);

        if (operateStr.length() < 1) {
            Logger.errorPrintln("native get operate failed " + operateStr);
            return null;
        }
        return Operate.fromJson(operateStr);
    }

}
