package com.cranklab.litertprobe;

import android.app.Instrumentation;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.PowerManager;
import android.os.SystemClock;
import com.google.ai.edge.litertlm.*;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.LinkedHashSet;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import org.json.JSONArray;
import org.json.JSONObject;

/** Explicit backend, no automatic CPU fallback, no access to Playdate saves. */
public final class LiteRtProbe extends Instrumentation {
    private Bundle arguments;
    private File root;
    private String reportName;
    private String instruction = INSTRUCTION;
    private boolean instructionInUser;
    private String chatTemplate;
    private final JSONObject report = new JSONObject();
    private static final String INSTRUCTION = "参考下面的翻译：\nPlaydate 翻译成 Playdate\ncrank 翻译成 摇柄\nD-pad 翻译成 方向键\n将以下文本翻译为中文，注意只需要输出翻译后的结果，不要额外解释：";
    private static final String STRICT = "You are an English-to-Simplified-Chinese translator. Translate the user's entire game description faithfully. Output only the translation, without introductions or comments. Preserve every instruction, number, negation, direction, button name A or B, game title, link and paragraph. Do not summarize or invent content. Translate Playdate as Playdate, crank as 摇柄, and D-pad as 方向键.";
    private static final String STRICT_CN = "将用户提供的全部英文逐句翻译成简体中文。只输出中文译文，不要重复英文原文，不要解释，不要总结。保留数字、A/B按键、专有名称、网址及段落；准确保留操作方向、长按和否定含义。Playdate不翻译；crank译为摇柄；D-pad译为方向键。";
    private static final String SHORT = "A puzzle game for Playdate. Turn the crank to rotate the board and find the path. Use the D-pad to move, press A to select and B to go back. Each level introduces a new challenge. Your progress is saved automatically, so you can stop at any time and continue later. Try to complete all levels with as few moves as possible.";
    private static final String CONTROLS = "Controls: Press A to jump and hold B to brake. Turn the crank clockwise to speed up; turn it counterclockwise to slow down. Do not undock the crank during calibration. Double-tap the screen to exit fullscreen. There are 12 levels, 3 difficulty settings, and no in-app purchases. Your progress is saved automatically. An internet connection is only needed to download new games.";

    @Override public void onCreate(Bundle extras) { arguments = extras; start(); }
    private int thermal() { return Build.VERSION.SDK_INT >= 29 ? ((PowerManager)getTargetContext().getSystemService("power")).getCurrentThermalStatus() : -1; }
    private double seconds(long from) { return (SystemClock.elapsedRealtimeNanos() - from) / 1e9; }
    private void persist(String stage) throws Exception {
        report.put("stage", stage);
        Files.write(new File(root, reportName + ".json").toPath(), report.toString(2).getBytes(StandardCharsets.UTF_8));
        Bundle status = new Bundle(); status.putString("stage", stage); sendStatus(0, status);
    }
    // The production bridge splits on UTF-8 bytes, preferring paragraph/sentence boundaries.
    private List<String> split(String text) {
        List<String> result = new ArrayList<>();
        int begin = 0;
        while (begin < text.length()) {
            int end = begin, bytes = 0;
            while (end < text.length()) {
                int cp = text.codePointAt(end), n = Character.charCount(cp);
                int cost = new String(Character.toChars(cp)).getBytes(StandardCharsets.UTF_8).length;
                if (bytes + cost > 1600) break;
                bytes += cost; end += n;
            }
            if (end < text.length()) {
                int paragraph = text.lastIndexOf("\n\n", end);
                if (paragraph >= begin && text.substring(begin,paragraph).getBytes(StandardCharsets.UTF_8).length > 400) end = paragraph + 2;
                else {
                    int space = text.lastIndexOf(' ', end);
                    if (space >= begin && text.substring(begin,space).getBytes(StandardCharsets.UTF_8).length > 400) end = space + 1;
                }
            }
            if (end <= begin) throw new IllegalArgumentException("invalid chunk boundary");
            result.add(text.substring(begin, end)); begin = end;
        }
        return result;
    }
    private JSONObject translate(Engine engine, String source, int outputLimit, JSONObject run) throws Exception {
        JSONArray chunks = new JSONArray(); run.put("chunks", chunks);
        run.put("thermalStart", thermal()); run.put("pssStartKiB", Debug.getPss());
        long started = SystemClock.elapsedRealtimeNanos();
        StringBuilder output = new StringBuilder(); double firstVisible = -1;
        for (String part : split(source)) {
            long chunkStart = SystemClock.elapsedRealtimeNanos();
            ConversationConfig config = new ConversationConfig(instructionInUser ? null : Contents.Companion.of(instruction), Collections.emptyList(), Collections.emptyList(), new SamplerConfig(20, 0.8, 0.2, 3407), false, null, Collections.singletonMap("enable_thinking", false), null, false, outputLimit, new ThinkingConfig(false, 0), false, false, chatTemplate);
            try (Conversation conversation = engine.createConversation(config)) {
                CountDownLatch done = new CountDownLatch(1);
                AtomicReference<Throwable> error = new AtomicReference<>();
                AtomicLong first = new AtomicLong(0); StringBuilder partial = new StringBuilder();
                StringBuilder hidden = new StringBuilder();
                conversation.sendMessageAsync(instructionInUser ? instruction + "\n\n" + part : part, new MessageCallback() {
                    @Override public void onMessage(Message message) {
                        String delta = message.toString();
                        if (!delta.isEmpty()) { first.compareAndSet(0, SystemClock.elapsedRealtimeNanos()); partial.append(delta); }
                        if (!message.getChannels().isEmpty()) hidden.append(message.getChannels().toString());
                    }
                    @Override public void onDone() { done.countDown(); }
                    @Override public void onError(Throwable failure) { error.set(failure); done.countDown(); }
                });
                if (!done.await(240, TimeUnit.SECONDS)) {
                    conversation.cancelProcess(); done.await(10, TimeUnit.SECONDS);
                    throw new IllegalStateException("inference timed out after 240 seconds");
                }
                if (error.get() != null) throw new IllegalStateException("inference failed", error.get());
                BenchmarkInfo stats = conversation.getBenchmarkInfo();
                JSONObject metric = new JSONObject();
                metric.put("seconds", seconds(chunkStart)); metric.put("inputBytes", part.getBytes(StandardCharsets.UTF_8).length);
                metric.put("visibleFirstTokenSeconds", first.get() == 0 ? -1 : (first.get() - chunkStart)/1e9);
                metric.put("nativeFirstTokenSeconds", stats.getTimeToFirstTokenInSecond());
                metric.put("prefillTokens", stats.getLastPrefillTokenCount()); metric.put("decodeTokens", stats.getLastDecodeTokenCount());
                metric.put("prefillTokensPerSecond", stats.getLastPrefillTokensPerSecond()); metric.put("decodeTokensPerSecond", stats.getLastDecodeTokensPerSecond());
                metric.put("output", partial.toString()); metric.put("channels", hidden.toString());
                metric.put("hitOutputLimit", stats.getLastDecodeTokenCount() >= outputLimit);
                chunks.put(metric);
                run.put("seconds", seconds(started));
                if (stats.getLastDecodeTokenCount() >= outputLimit) throw new IllegalStateException("output reached token cap; translation is incomplete");
                if (partial.length() == 0) throw new IllegalStateException("empty visible output");
                if (firstVisible < 0) firstVisible = (first.get() - started)/1e9;
                if (output.length() > 0) output.append('\n'); output.append(partial);
            }
        }
        run.put("seconds", seconds(started)); run.put("firstVisibleSeconds", firstVisible);
        run.put("output", output.toString()); run.put("chunks", chunks);
        run.put("thermalEnd", thermal()); run.put("pssEndKiB", Debug.getPss());
        return run;
    }
    @Override public void onStart() {
        Bundle result = new Bundle(); Engine engine = null;
        try {
            startActivitySync(new Intent(getTargetContext(), ProbeActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            root = getTargetContext().getExternalFilesDir(null);
            String backend = arguments.getString("backend", "CPU");
            if (!backend.equals("CPU") && !backend.equals("GPU")) throw new IllegalArgumentException("backend must be CPU or GPU");
            String test = arguments.getString("case", "short");
            String prompt = arguments.getString("prompt", "baseline");
            if (!prompt.equals("baseline") && !prompt.equals("strict") && !prompt.equals("strict-cn")) throw new IllegalArgumentException("invalid prompt variant");
            instruction = prompt.equals("strict-cn") ? STRICT_CN : prompt.equals("strict") ? STRICT : INSTRUCTION;
            String placement = arguments.getString("placement", "system");
            if (!placement.equals("system") && !placement.equals("user")) throw new IllegalArgumentException("invalid instruction placement");
            instructionInUser = placement.equals("user");
            if (arguments.getString("template", "model").equals("qwen3")) {
                try (java.io.InputStream input = getTargetContext().getAssets().open("qwen3-template.jinja")) {
                    java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream(); byte[] buffer = new byte[4096]; int size;
                    while ((size=input.read(buffer)) != -1) bytes.write(buffer,0,size);
                    chatTemplate = new String(bytes.toByteArray(),StandardCharsets.UTF_8);
                }
            }
            reportName = arguments.getString("report", backend.toLowerCase() + "-" + test);
            if (!reportName.matches("[a-zA-Z0-9_-]+")) throw new IllegalArgumentException("invalid report name");
            int repeats = Integer.parseInt(arguments.getString("repeats", "2"));
            if (repeats < 1 || repeats > 3) throw new IllegalArgumentException("repeats must be 1..3");
            String filename = arguments.getString("model", "Qwen3-0.6B_dynamic_wi4b32_afp32.litertlm");
            if (!filename.matches("[a-zA-Z0-9_.-]+\\.litertlm")) throw new IllegalArgumentException("invalid model name");
            File model = new File(root, "models/" + filename);
            if (!model.isFile()) throw new IllegalArgumentException("model missing: " + model);
            String source = test.equals("short") ? SHORT : test.equals("controls") ? CONTROLS : new JSONObject(new String(Files.readAllBytes(new File(root,"fixtures.json").toPath()), StandardCharsets.UTF_8)).getString(test);
            report.put("device", Build.MANUFACTURER + " " + Build.MODEL); report.put("soc", Build.VERSION.SDK_INT >= 31 ? Build.SOC_MODEL : "unknown");
            report.put("pid", android.os.Process.myPid());
            report.put("runtime", "LiteRT-LM 0.18.0"); report.put("backend", backend); report.put("fallbackAllowed", false);
            report.put("model", filename); report.put("modelBytes", model.length()); report.put("contextTokens", 2048);
            report.put("source", source); report.put("instruction", instruction); report.put("prompt", prompt);
            report.put("instructionPlacement", placement); report.put("enableThinkingTemplate", false);
            report.put("chatTemplate", chatTemplate == null ? "model-embedded" : "upstream-a8178d7d");
            report.put("sampler", "topK=20 topP=0.8 temperature=0.2 seed=3407 thinking=false");
            report.put("thermalBeforeLoad", thermal()); persist("initializing");
            ExperimentalFlags.INSTANCE.setEnableBenchmark(true);
            File cache = new File(root,"cache/"+backend.toLowerCase()+"/"+filename); cache.mkdirs();
            long started = SystemClock.elapsedRealtimeNanos();
            engine = new Engine(new EngineConfig(model.getAbsolutePath(), backend.equals("GPU") ? new Backend.GPU() : new Backend.CPU(4, null), null, null, 2048, null, cache.getAbsolutePath()));
            engine.initialize(); report.put("initializeSeconds", seconds(started));
            JSONArray runs = new JSONArray(); report.put("runs", runs); persist("initialized");
            for (int i=0; i<repeats; i++) {
                JSONObject run = new JSONObject(); runs.put(run); persist("running-"+i);
                translate(engine,source, test.equals("long") ? 1100 : 512, run); persist("completed-"+i);
            }
            LinkedHashSet<String> drivers = new LinkedHashSet<>();
            for(String mapping: Files.readAllLines(new File("/proc/self/maps").toPath(),StandardCharsets.UTF_8)) {
                if(mapping.contains("libOpenCL") || mapping.contains("libGLESv2_adreno") || mapping.contains("libllvm-glnext")) drivers.add(mapping.substring(mapping.indexOf('/')));
            }
            report.put("mappedGpuLibraries",new JSONArray(drivers));
            engine.close(); engine = null; report.put("success", true); persist("done");
            result.putString("report", new File(root,reportName+".json").toString()); finish(-1,result);
        } catch(Throwable failure) {
            result.putString("error",failure.toString());
            try { report.put("success", false); report.put("error", android.util.Log.getStackTraceString(failure)); if (root != null && reportName != null) persist("failed"); } catch(Exception ignored) {}
            finish(0,result);
        } finally { if(engine != null) engine.close(); }
    }
}
