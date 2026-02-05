/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */

package com.android.commands.monkey.utils;

import java.util.regex.Pattern;

/**
 * @author Dingchun Wang
 */

/**
 * Input method tool class, call adb to switch the specified input method.
 * Uses exec(String[]) to avoid command injection when switching IME.
 */
public class InputUtils {
    /** IME component format: package/component e.g. com.android.inputmethod.latin/.LatinIME */
    private static final Pattern IME_COMPONENT_PATTERN = Pattern.compile("^[a-zA-Z0-9_.]+/[a-zA-Z0-9_.]+$");
    private static final int IME_MAX_LENGTH = 256;

    public static String getDefaultIme() {
        return StoneUtils.executeShellCommand(new String[]{"settings", "get", "secure", "default_input_method"});
    }

    /**
     * Switch to the given IME. The ime string must be a valid component name (package/component);
     * otherwise the call is ignored to prevent command injection.
     */
    public static void switchToIme(String ime) {
        if (ime == null || ime.length() > IME_MAX_LENGTH || !IME_COMPONENT_PATTERN.matcher(ime).matches()) {
            Logger.warningPrintln("InputUtils.switchToIme: invalid ime (rejected for safety), ignored.");
            return;
        }
        StoneUtils.executeShellCommand(new String[]{"ime", "enable", ime});
        StoneUtils.executeShellCommand(new String[]{"ime", "set", ime});
    }
}
