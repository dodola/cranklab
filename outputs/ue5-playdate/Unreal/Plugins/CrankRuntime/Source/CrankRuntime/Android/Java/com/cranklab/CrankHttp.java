package com.cranklab;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.List;
import java.util.Map;
import java.util.zip.GZIPInputStream;

/** Blocking worker transport; callbacks are delivered separately on the Lua thread. */
public final class CrankHttp {
    private static final int MAX_RESPONSE = 32 * 1024 * 1024;
    public static Object[] execute(String url, String method, String headers, byte[] body, int timeout) {
        HttpURLConnection connection = null;
        int status = 0;
        try {
            connection = (HttpURLConnection)new URL(url).openConnection();
            connection.setConnectTimeout(timeout);
            connection.setReadTimeout(timeout);
            connection.setRequestMethod(method);
            connection.setRequestProperty("User-Agent", "CrankRuntime/0.1");
            for (String line : headers.split("\\r?\\n")) {
                int colon = line.indexOf(':');
                if (colon > 0) connection.setRequestProperty(line.substring(0, colon).trim(), line.substring(colon + 1).trim());
            }
            if (body.length > 0) {
                connection.setDoOutput(true);
                connection.setFixedLengthStreamingMode(body.length);
                try (java.io.OutputStream output = connection.getOutputStream()) { output.write(body); }
            }
            status = connection.getResponseCode();
            StringBuilder raw = new StringBuilder();
            for (Map.Entry<String, List<String>> entry : connection.getHeaderFields().entrySet()) {
                for (String value : entry.getValue()) {
                    if (entry.getKey() != null) raw.append(entry.getKey()).append(": ");
                    raw.append(value).append("\r\n");
                }
            }
            InputStream stream = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
            if (stream == null) return new Object[] {status, raw.toString(), new byte[0], null};
            if ("gzip".equalsIgnoreCase(connection.getContentEncoding())) stream = new GZIPInputStream(stream);
            try (InputStream input = stream; ByteArrayOutputStream output = new ByteArrayOutputStream()) {
                byte[] chunk = new byte[16384]; int count;
                while ((count = input.read(chunk)) != -1) {
                    if (output.size() > MAX_RESPONSE - count) throw new java.io.IOException("HTTP response exceeds 32 MB limit");
                    output.write(chunk, 0, count);
                }
                return new Object[] {status, raw.toString(), output.toByteArray(), null};
            }
        } catch (Exception error) {
            return new Object[] {status, "", new byte[0], error.toString()};
        } finally {
            if (connection != null) connection.disconnect();
        }
    }
}
