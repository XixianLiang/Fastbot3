
/**
 * @author Zhao Zhang
 */

package com.android.commands.monkey.source;

import com.android.commands.monkey.utils.Logger;
import com.google.gson.JsonObject;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.HashMap;

public class CoverageData {
    public float coverage;
    public String[] totalActivities;
    public String[] testedActivities;
    public int totalActivitiesCount;
    public int testedActivitiesCount;
    public int stepsCount;
    public HashMap<String, Integer> activityCountHistory;

    public CoverageData(int stepsCount, float coverage, String[] totalActivities, String[] testedActivities, HashMap<String, Integer> activityCountHistory) {
        this.stepsCount = stepsCount;
        this.coverage = coverage;
        this.totalActivities = totalActivities;
        this.testedActivities = testedActivities;
        this.totalActivitiesCount = totalActivities != null ? totalActivities.length : 0;
        this.testedActivitiesCount = testedActivities != null ? testedActivities.length : 0;
        this.activityCountHistory = activityCountHistory;
    }

    public JSONObject toJSON() {
        JSONObject obj = new JSONObject();
        try {

            obj.put("stepsCount", stepsCount);
            obj.put("coverage", coverage);
            obj.put("totalActivitiesCount", totalActivitiesCount);
            obj.put("testedActivitiesCount", testedActivitiesCount);

            JSONArray totalActivitiesArray = new JSONArray();
            if (totalActivities != null) {
                for (String activity : totalActivities) {
                    totalActivitiesArray.put(activity);
                }
            }
            obj.put("totalActivities", totalActivitiesArray);

            JSONArray testedActivitiesArray = new JSONArray();
            if (testedActivities != null) {
                for (String activity : testedActivities) {
                    testedActivitiesArray.put(activity);
                }
            }
            obj.put("testedActivities", testedActivitiesArray);

            JSONObject activityCountHistoryObj = new JSONObject();
            if (activityCountHistory != null) {
                try {
                    for (String activity : activityCountHistory.keySet()) {
                        activityCountHistoryObj.put(activity, activityCountHistory.get(activity));
                    }
                } catch (JSONException e) {
                    e.printStackTrace();
                }
            }
            obj.put("activityCountHistory", activityCountHistoryObj);

        } catch (JSONException e) {
            Logger.println("Error when dumping CoverageData");
        }
        return obj;
    }
}