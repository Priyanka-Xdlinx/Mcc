package com.orblinx.mcc.auth;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import org.springframework.stereotype.Component;

/**
 * Per-address limit on sign-in attempts, to slow password guessing across many
 * accounts (the per-account lockout handles guessing at one).
 *
 * Keyed on the socket's remote address only: X-Forwarded-For is set by the
 * caller and would let anyone pick a fresh address per attempt.
 */
@Component
public class LoginThrottle {

    private static final int MAX_TRACKED = 10_000;

    private final int limit;
    private final long windowMs;
    private final Map<String, Deque<Long>> attempts = new ConcurrentHashMap<>();

    public LoginThrottle(AuthProperties properties) {
        this.limit = properties.loginsPerIp();
        this.windowMs = properties.loginWindow().toMillis();
    }

    /** Records an attempt; false when this address has used up its window. */
    public boolean tryAcquire(String address) {
        long now = System.currentTimeMillis();
        if (attempts.size() > MAX_TRACKED) attempts.values().removeIf(d -> expired(d, now));
        Deque<Long> times = attempts.computeIfAbsent(address == null ? "?" : address, k -> new ArrayDeque<>());
        synchronized (times) {
            while (!times.isEmpty() && times.peekFirst() <= now - windowMs) times.pollFirst();
            if (times.size() >= limit) return false;
            times.addLast(now);
            return true;
        }
    }

    /** Seconds until the oldest attempt in the window drops out. */
    public long retryAfterSeconds(String address) {
        Deque<Long> times = attempts.get(address == null ? "?" : address);
        if (times == null) return 0;
        synchronized (times) {
            Long first = times.peekFirst();
            return first == null ? 0 : Math.max(1, (first + windowMs - System.currentTimeMillis() + 999) / 1000);
        }
    }

    private boolean expired(Deque<Long> times, long now) {
        synchronized (times) {
            Long last = times.peekLast();
            return last == null || last <= now - windowMs;
        }
    }
}
