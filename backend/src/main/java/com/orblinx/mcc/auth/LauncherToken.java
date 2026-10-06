package com.orblinx.mcc.auth;

import java.io.IOException;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.authority.SimpleGrantedAuthority;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.stereotype.Component;
import org.springframework.web.filter.OncePerRequestFilter;

/**
 * Lets the local launchers (mcc.py, start.sh, stop.sh) check the backend and
 * start or stop the feed without a user session.
 *
 * Each start writes a fresh random token to .mcc-launcher-token in mcc.data-dir;
 * a request that sends it in X-MCC-Launcher is accepted only from this machine
 * (loopback) and only for /api/status and /api/sim/**. Anyone who can read that
 * file can already run the launchers, so it grants nothing new.
 */
@Component
public class LauncherToken {

    public static final String HEADER = "X-MCC-Launcher";
    /** Authority granted to the launcher; SecurityConfig allows it on its two paths only. */
    static final String AUTHORITY = "LAUNCHER";
    static final String FILE = ".mcc-launcher-token";

    private static final Logger log = LoggerFactory.getLogger(LauncherToken.class);

    private final String token = SecureValues.random32();

    public LauncherToken(@Value("${mcc.data-dir}") String dataDir) {
        Path file = Path.of(dataDir).toAbsolutePath().normalize().resolve(FILE);
        try {
            Files.writeString(file, token);
        } catch (IOException ex) {
            log.warn("Could not write {}: the launch scripts cannot check or drive the backend ({})",
                     file, ex.getMessage());
        }
    }

    /** True for a loopback request to a launcher path carrying the current token. */
    boolean accepts(HttpServletRequest request) {
        String sent = request.getHeader(HEADER);
        if (sent == null || !SecureValues.constantTimeEquals(token, sent)) return false;
        String path = request.getRequestURI().substring(request.getContextPath().length());
        if (!path.equals("/api/status") && !path.startsWith("/api/sim/")) return false;
        try {
            return InetAddress.getByName(request.getRemoteAddr()).isLoopbackAddress();
        } catch (UnknownHostException ex) {
            return false;
        }
    }

    /** Authenticates an accepted launcher request; anything else passes through untouched. */
    OncePerRequestFilter filter() {
        return new OncePerRequestFilter() {
            @Override
            protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response,
                                            FilterChain chain) throws ServletException, IOException {
                if (SecurityContextHolder.getContext().getAuthentication() == null && accepts(request)) {
                    SecurityContextHolder.getContext().setAuthentication(UsernamePasswordAuthenticationToken
                            .authenticated("launcher", null, List.of(new SimpleGrantedAuthority(AUTHORITY))));
                }
                chain.doFilter(request, response);
            }
        };
    }
}
