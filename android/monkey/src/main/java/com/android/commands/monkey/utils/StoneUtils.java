/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */

package com.android.commands.monkey.utils;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.HashMap;

/**
 * @author Zhang Zhao
 */

public class StoneUtils {
    private static final String TAG = StoneUtils.class.getSimpleName();

    /**
     * Execute shell command
     * @param command shell command to execute
     * @return output of the execution result.
     */
    public static String executeShellCommand(String command) {
        String line = "";
        StringBuilder sb = new StringBuilder();
        BufferedReader br = null;
        Process proc = null;
        try {
            proc = Runtime.getRuntime().exec(command);
            br = new BufferedReader(new InputStreamReader(proc.getInputStream()));
            while ((line = br.readLine()) != null) {
                sb.append(line).append("\n");
            }
        } catch (Exception e) {
            Logger.errorPrintln("executeShellCommand() error! command: "+command);
            Logger.errorPrintln(e.getMessage());
        } finally {
            if (br != null) {
                try {
                    br.close();
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }
            if (proc != null) {
                proc.destroy();
            }
        }
        return sb.toString();
    }

    /**
     * Write string to file
     *
     * @param path path of the file to write to
     * @param data data to write
     * @return return true if succeed to write
     */
    public static boolean writeStringToFile(String path, String data, boolean isAppend) throws IOException {
        boolean result = false;
        FileWriter fw = null;
        try {
            File file = new File(path);
            fw = new FileWriter(file, isAppend);
            fw.write(data);
            result = true;
        } catch (Exception e) {
            e.printStackTrace();
        } finally {
            if (fw!=null)
                fw.close();
        }
        return result;
    }

    public static boolean writeStringToFile(File file, String data, boolean isAppend) throws IOException{
        if (!file.exists()){
            boolean res = file.createNewFile();
            if (!res) {
                throw new IOException("Fail to create file: " + file);
            }
        }

        FileWriter fileWriter = null;
        try {
            fileWriter = new FileWriter(file.getAbsolutePath(), isAppend);
            fileWriter.write(data);
        } catch (IOException e) {
            Logger.println("cannot write to " + file.getAbsolutePath());
        } finally {
            if (fileWriter != null) {
                try {
                    fileWriter.close();
                } catch (IOException e) {
                    Logger.println("cannot close filewriter");
                }
            }
        }
        return file.exists();
    }

    /***
     * Ensure the dir exists.
     */
    public static boolean ensureDir(File dir) {
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir.exists();
    }

    public static void sleep(long time) {
        try {
            Thread.sleep(time);
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    public static <K, V> V getOrDefaultFromHashMap(HashMap<K, V> map, K key, V defaultValue) {
        V value = map.get(key);
        return (value != null) ? value : defaultValue;
    }
}
