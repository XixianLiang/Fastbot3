package com.android.commands.monkey.utils;

import com.bytedance.fastbot.AiClient;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.jacoco.core.analysis.Analyzer;
import org.jacoco.core.analysis.CoverageBuilder;
import org.jacoco.core.analysis.IClassCoverage;
import org.jacoco.core.analysis.ICounter;
import org.jacoco.core.data.ExecutionDataStore;
import org.jacoco.core.tools.ExecFileLoader;

/**
 * LLMDroid-Fastbot style coverage: <b>Jacoco</b> (method counters from .ec + class files) or
 * <b>AndroLog</b> (logcat METHOD= lines). Wired from {@link com.android.commands.monkey.Monkey} when {@code --use-code-coverage}
 * is set; otherwise {@link #getCoverage()} falls back to {@link AiClient#getLlmdroidCoverageMetric()}
 * for native stagnation.
 */
public class CodeCoverage {

    private static final String filePath = "/sdcard/codecoverage.txt";

    // AndroLog
    private static int mTotal = 0;
    private static String mLogIdentifier;
    private static AndrologBridge mAndrologBridge = null;

    // Jacoco
    private static String mOutputDir = "";
    private static String mEcFileName = "";
    private static String mEcFilePath = "";
    private static String mClassFilePath = "";
    private static double mLastCoverage = 0.00001;
    private static JacocoBridge mJacocoBridge = null;

    /** AndroLog: {@code /sdcard/config.json} must define {@code TotalMethod} and {@code Tag}. */
    public CodeCoverage(int total_, String id) {
        saveToFile("code coverage", false);
        mTotal = total_;
        mLogIdentifier = id;
        Logger.format("[AndroLog] total methods: %d, TAG: %s\n", mTotal, mLogIdentifier);

        try {
            Process process = Runtime.getRuntime().exec("logcat -c");
            process.waitFor();
            Logger.println("[AndroLog] clear adb log cache");
        } catch (IOException | InterruptedException e) {
            e.printStackTrace();
            Thread.currentThread().interrupt();
        }

        mAndrologBridge = new AndrologBridge();
        mAndrologBridge.startLogcatListener();
    }

    /** Jacoco: {@code /sdcard/config.json} must define {@code EcFilePath} and {@code ClassFilePath}. */
    public CodeCoverage(File outputDir, String ecFileName, String ecFilePath, String classFilePath) {
        saveToFile("code coverage", false);
        mOutputDir = outputDir.getAbsolutePath();
        mEcFileName = ecFileName;
        mEcFilePath = ecFilePath;
        mClassFilePath = classFilePath;
        mJacocoBridge = new JacocoBridge(mOutputDir);
        Logger.println("mOutputDir: " + mOutputDir);
        Logger.println("mEcFileName: " + mEcFileName);
        Logger.println("mEcFilePath: " + mEcFilePath);
        Logger.println("mClassFilePath: " + mClassFilePath);
    }

    private static void saveToFile(String content, boolean append) {
        try (FileWriter writer = new FileWriter(filePath, append);
             BufferedWriter bufferedWriter = new BufferedWriter(writer)) {
            bufferedWriter.write(content);
            bufferedWriter.newLine();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    /**
     * Jacoco / AndroLog when initialized from {@link com.android.commands.monkey.Monkey}; else native RL+activity metric via
     * {@link AiClient#getLlmdroidCoverageMetric()}.
     */
    public static double getCoverage() {
        if (mJacocoBridge != null) {
            return getCoverageUsingJacoco();
        }
        if (mAndrologBridge != null) {
            return getCoverageUsingAndrolog();
        }
        return AiClient.getLlmdroidCoverageMetric();
    }

    /**
     * True only when external coverage backend is active via {@code --use-code-coverage jacoco|androlog}.
     * Native LLMDroid uses this to decide coverage-mode vs time-mode switching.
     */
    public static boolean isExternalCoverageEnabled() {
        return mJacocoBridge != null || mAndrologBridge != null;
    }

    public static double getCoverageUsingAndrolog() {
        return mAndrologBridge.computeIncrement();
    }

    public static double getCoverageUsingJacoco() {
        final CountDownLatch latch = new CountDownLatch(1);

        Thread thread = new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    Logger.println("Preparing to call getMethodCoverage method.");
                    JacocoBridge jacocoBridge = new JacocoBridge(mOutputDir);
                    float result = jacocoBridge.getMethodCoverage();
                    result *= 100.0f;

                    synchronized (CodeCoverage.class) {
                        mLastCoverage = result;
                    }

                    Logger.println(String.format("Result from getMethodCoverage: %.5f%%", result));

                } catch (IOException ex) {
                    Logger.println(String.format("Caught an IOException from Java: %s", ex.getMessage()));
                } finally {
                    latch.countDown();
                }
            }
        });

        thread.start();

        double coverage = 0.00001;
        try {
            if (!latch.await(1, TimeUnit.SECONDS)) {
                Logger.println("Coverage calculation took too long, returning previous coverage.");
                synchronized (CodeCoverage.class) {
                    coverage = mLastCoverage;
                }
            } else {
                Logger.println(String.format("New coverage: %.5f%%", mLastCoverage));
                synchronized (CodeCoverage.class) {
                    coverage = mLastCoverage;
                }
            }
        } catch (InterruptedException e) {
            Logger.println("Thread interrupted while waiting for coverage calculation.");
            Thread.currentThread().interrupt();
        }

        saveToFile(String.format("%.5f%%", coverage), true);
        return coverage;
    }

    private static class AndrologBridge {

        private int methodCount = 0;
        private int lastMethodCount = 1;
        private double rate = 0.0;

        private final Map<String, Integer> summary = new HashMap<>();
        private final Set<String> visitedComponents = new HashSet<>();

        public void startLogcatListener() {
            Thread thread = new Thread(new Runnable() {
                @Override
                public void run() {
                    while (true) {
                        try {
                            Runtime.getRuntime().exec("logcat -c");
                            System.out.println("[CodeCoverage] clear adb log cache");

                            String cmd = "logcat -s " + mLogIdentifier;
                            Process process = Runtime.getRuntime().exec(cmd);
                            BufferedReader bufferedReader = new BufferedReader(
                                    new InputStreamReader(process.getInputStream()));

                            String line;
                            while ((line = bufferedReader.readLine()) != null) {
                                analyzeLine(line);
                            }
                            System.out.println("[CodeCoverage] ******************** !! thread stop !! *************************");
                            System.out.println("[CodeCoverage] ******************** !! restart logcat !! *************************");
                        } catch (IOException e) {
                            e.printStackTrace();
                        }
                    }
                }
            });
            thread.setDaemon(true);
            thread.start();
            System.out.println("[Androlog] Logcat Listener started");
        }

        private void increment(String key) {
            synchronized (CodeCoverage.class) {
                methodCount++;
            }
        }

        private void incrementMethod(String method) {
            incrementComponent("methods", method);
        }

        private void incrementComponent(String type, String component) {
            if (visitedComponents.add(type + component)) {
                increment(type);
            }
        }

        private String extractContent(String line, String prefix) {
            Pattern pattern = Pattern.compile("=(.*)");
            Matcher matcher = pattern.matcher(line);
            if (matcher.find()) {
                return matcher.group(1);
            }
            return null;
        }

        private String getLogType(String line) {
            Pattern pattern = Pattern.compile("(\\w+?)=");
            Matcher matcher = pattern.matcher(line);
            if (matcher.find()) {
                return matcher.group(1);
            }
            return null;
        }

        private void analyzeLine(String line) {
            String logType = getLogType(line);

            if (Objects.equals(logType, "METHOD")) {
                incrementMethod(extractContent(line, "METHOD="));
            }
        }

        public void printSummary() {
            for (Map.Entry<String, Integer> entry : summary.entrySet()) {
                System.out.println(entry.getKey() + " : " + entry.getValue());
            }
        }

        public Map<String, Integer> getSummary() {
            return summary;
        }

        public Integer getMethodSummary() {
            return summary.get("methods");
        }

        public double computeIncrement() {
            int currentMethodCount;
            synchronized (CodeCoverage.class) {
                currentMethodCount = methodCount;
            }
            rate = ((double) (currentMethodCount - lastMethodCount) / lastMethodCount) * 100;
            double percentage = ((double) currentMethodCount / mTotal) * 100;
            lastMethodCount = currentMethodCount;

            String str = String.format("[%s] %8.5f%% (%d/%d) --> %8.5f", mLogIdentifier, percentage, currentMethodCount, mTotal, rate);
            System.out.println(str);
            saveToFile(str, true);
            return percentage;
        }
    }

    private static class JacocoBridge {

        private final String adbPullPath;

        public JacocoBridge(String pullPath) {
            adbPullPath = (pullPath != null && !pullPath.isEmpty()) ? pullPath : ".";
        }

        private void sendBroadcast() {
            try {
                String command = String.format("am broadcast -a com.llmdroid.jacoco.COLLECT_COVERAGE --es coverageFile %s", mEcFileName);
                Process process = Runtime.getRuntime().exec(command);
                BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(process.getInputStream()));
                String line;
                while ((line = bufferedReader.readLine()) != null) {
                    System.out.println(line);
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
        }

        public float getMethodCoverage() throws IOException {
            sendBroadcast();

            String targetPath = mEcFilePath + "/" + mEcFileName;
            adbPullFile(targetPath);

            ExecFileLoader execFileLoader = new ExecFileLoader();
            File ecFile = new File(adbPullPath + "/" + mEcFileName);
            execFileLoader.load(ecFile);
            ExecutionDataStore store = execFileLoader.getExecutionDataStore();

            Logger.println("Begin analyzing...");
            CoverageBuilder coverageBuilder = new CoverageBuilder();
            Analyzer analyzer = new Analyzer(store, coverageBuilder);
            analyzer.analyzeAll(new File(mClassFilePath));

            int sumCoveredCount = 0;
            int sumMissedCount = 0;
            int sumTotalCount = 0;
            for (IClassCoverage clazz : coverageBuilder.getClasses()) {
                ICounter methodCounter = clazz.getMethodCounter();
                sumCoveredCount += methodCounter.getCoveredCount();
                sumMissedCount += methodCounter.getMissedCount();
                sumTotalCount += methodCounter.getTotalCount();
            }

            Logger.format("[Jacoco]: covered: %d; missed: %d, total: %d\n",
                    sumCoveredCount, sumMissedCount, sumTotalCount);

            if (sumTotalCount == 0) {
                return 0f;
            }
            return (float) sumCoveredCount / sumTotalCount;
        }

        private void adbPullFile(String source) {
            String command = String.format("cp %s %s/", source, adbPullPath);

            try {
                Process process = Runtime.getRuntime().exec(command);
                BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()));
                String line;
                while ((line = reader.readLine()) != null) {
                    System.out.println(line);
                }
                int exitCode = process.waitFor();
                if (exitCode == 0) {
                    System.out.println("File copied successfully.");
                } else {
                    System.out.println("Failed to copy file, command: " + command);
                }
            } catch (IOException | InterruptedException e) {
                e.printStackTrace();
                Thread.currentThread().interrupt();
            }
        }
    }
}
