package com.orblinx.mcc.auth;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.convert.converter.Converter;
import org.springframework.http.HttpMethod;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.security.authentication.AbstractAuthenticationToken;
import org.springframework.security.config.Customizer;
import org.springframework.security.config.annotation.web.builders.HttpSecurity;
import org.springframework.security.config.annotation.web.configurers.AbstractHttpConfigurer;
import org.springframework.security.config.http.SessionCreationPolicy;
import org.springframework.security.core.GrantedAuthority;
import org.springframework.security.core.authority.SimpleGrantedAuthority;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.security.oauth2.jwt.JwtDecoder;
import org.springframework.security.oauth2.server.resource.InvalidBearerTokenException;
import org.springframework.security.oauth2.server.resource.authentication.JwtAuthenticationToken;
import org.springframework.security.oauth2.server.resource.web.BearerTokenResolver;
import org.springframework.security.oauth2.server.resource.web.authentication.BearerTokenAuthenticationFilter;
import org.springframework.security.web.AuthenticationEntryPoint;
import org.springframework.security.web.SecurityFilterChain;
import org.springframework.security.web.csrf.CookieCsrfTokenRepository;
import org.springframework.security.web.csrf.CsrfFilter;
import org.springframework.security.web.csrf.CsrfToken;
import org.springframework.security.web.csrf.CsrfTokenRequestAttributeHandler;
import org.springframework.security.web.header.writers.ReferrerPolicyHeaderWriter.ReferrerPolicy;
import org.springframework.security.web.util.matcher.RequestMatcher;
import org.springframework.web.filter.OncePerRequestFilter;

/**
 * Who may call what:
 * <ul>
 *   <li>/auth/** sign-in endpoints, the web UI and the API docs: anyone</li>
 *   <li>/api/me and /api/me/password: any signed-in user, including one who
 *       must still change a temporary password</li>
 *   <li>each feature under /api/ and /ws/: the {@link Permission} it needs (telemetry, decode,
 *       docs, display pages, simulator, user accounts); see {@link Permission#of} for who has what</li>
 *   <li>/api/status and /api/sim/**: also the local launch scripts (see {@link LauncherToken})</li>
 *   <li>anything else under /api/ and /ws/: any active signed-in user</li>
 * </ul>
 * The token comes from the HttpOnly access_token cookie, and is honoured only
 * while its session row is live (see {@link AccountRepo}).
 *
 * CSRF is double-submit: every response sets a readable XSRF-TOKEN cookie, and
 * every write, sign-in included, must echo it in X-XSRF-TOKEN. Only
 * /auth/refresh is exempt: it changes nothing but the caller's own cookie, and
 * has to work on a page that has not fetched a token yet.
 */
@Configuration
@EnableConfigurationProperties(AuthProperties.class)
public class SecurityConfig {

    /** Granted to a signed-in user who has no password change pending. */
    static final String ACTIVE = "ACTIVE";

    private static final Set<String> SAFE_METHODS = Set.of("GET", "HEAD", "TRACE", "OPTIONS");


    @Bean
    SecurityFilterChain securityFilterChain(HttpSecurity http, AuthProperties properties, AuthService auth,
                                            LauncherToken launcher) throws Exception {
        // A launcher request carries no cookie for a forged request to ride on, so it needs no CSRF token.
        RequestMatcher csrfProtected = request -> !SAFE_METHODS.contains(request.getMethod())
                && !"/auth/refresh".equals(requestPath(request))
                && !launcher.accepts(request);
        AuthenticationEntryPoint unauthorized = (request, response, ex) ->
                writeError(response, HttpStatus.UNAUTHORIZED, "Not signed in");
        http
            .cors(Customizer.withDefaults())
            // Our own CsrfFilter rather than http.csrf(): the resource server switches
            // http.csrf() off for any request carrying a bearer token, and here the
            // bearer token IS the cookie a forged request would ride on.
            .csrf(AbstractHttpConfigurer::disable)
            .addFilterBefore(csrfFilter(properties.secureCookies(), csrfProtected),
                             BearerTokenAuthenticationFilter.class)
            .addFilterAfter(new CsrfCookieFilter(), BearerTokenAuthenticationFilter.class)
            .addFilterAfter(launcher.filter(), BearerTokenAuthenticationFilter.class)
            .sessionManagement(session -> session.sessionCreationPolicy(SessionCreationPolicy.STATELESS))
            .formLogin(AbstractHttpConfigurer::disable)
            .httpBasic(AbstractHttpConfigurer::disable)
            .logout(AbstractHttpConfigurer::disable)
            .requestCache(AbstractHttpConfigurer::disable)
            .headers(headers -> headers
                // The Docs page shows Swagger UI in a same-origin iframe.
                .frameOptions(frame -> frame.sameOrigin())
                .referrerPolicy(referrer -> referrer.policy(ReferrerPolicy.SAME_ORIGIN)))
            .authorizeHttpRequests(authorize -> authorize
                .requestMatchers(HttpMethod.GET, "/auth/csrf").permitAll()
                .requestMatchers(HttpMethod.POST, "/auth/login", "/auth/refresh", "/auth/logout").permitAll()
                .requestMatchers("/auth/**").denyAll()
                .requestMatchers("/error").permitAll()
                .requestMatchers(HttpMethod.GET, "/api/me").authenticated()
                .requestMatchers(HttpMethod.POST, "/api/me/password").authenticated()
                // Telemetry tab. The launch scripts check /api/status (see LauncherToken).
                .requestMatchers("/api/telemetry/**", "/ws/telemetry")
                    .hasAuthority(Permission.TELEMETRY_VIEW.authority())
                .requestMatchers("/api/status")
                    .hasAnyAuthority(Permission.TELEMETRY_VIEW.authority(), LauncherToken.AUTHORITY)
                // Decode trace tab
                .requestMatchers("/api/frame-decode", "/api/frames", "/api/frames/**", "/api/trace")
                    .hasAuthority(Permission.DECODE_VIEW.authority())
                // Docs tab
                .requestMatchers("/api/schema").hasAuthority(Permission.DOCS_VIEW.authority())
                // Display tab: reading pages and your workspace, then changing them
                .requestMatchers(HttpMethod.GET, "/api/pages", "/api/pages/**", "/api/me/workspace")
                    .hasAuthority(Permission.DISPLAY_VIEW.authority())
                .requestMatchers("/api/pages", "/api/pages/**", "/api/me/workspace")
                    .hasAuthority(Permission.DISPLAY_EDIT.authority())
                // Simulator: its status, then start and stop. The launch scripts may use both.
                .requestMatchers(HttpMethod.GET, "/api/sim/status")
                    .hasAnyAuthority(Permission.SIM_VIEW.authority(), LauncherToken.AUTHORITY)
                .requestMatchers("/api/sim/**")
                    .hasAnyAuthority(Permission.SIM_CONTROL.authority(), LauncherToken.AUTHORITY)
                // User accounts (the list, then everything that changes an account)
                .requestMatchers(HttpMethod.GET, "/api/admin/users").hasAuthority(Permission.USERS_VIEW.authority())
                .requestMatchers("/api/admin/**").hasAuthority(Permission.USERS_MANAGE.authority())
                // Anything not listed above: any active signed-in user
                .requestMatchers("/api/**", "/ws/**").hasAuthority(ACTIVE)
                // The web UI (index.html, assets) and the API docs.
                .anyRequest().permitAll())
            .oauth2ResourceServer(resourceServer -> resourceServer
                .bearerTokenResolver(cookieBearerTokenResolver())
                .jwt(jwt -> jwt.jwtAuthenticationConverter(sessionChecked(auth)))
                .authenticationEntryPoint(unauthorized))
            .exceptionHandling(exceptions -> exceptions
                .authenticationEntryPoint(unauthorized)
                .accessDeniedHandler((request, response, ex) ->
                        writeError(response, HttpStatus.FORBIDDEN, "Forbidden")));
        return http.build();
    }

    @Bean
    JwtDecoder jwtDecoder(AppTokenService tokenService) {
        return tokenService.accessTokenDecoder();
    }

    /**
     * A valid signature is not enough: the session must still be live, so
     * sign-out, a disabled account or a password change end access at once
     * instead of when the token expires. A user with a temporary password gets
     * no authorities, which leaves them /api/me and /api/me/password only.
     */
    static Converter<Jwt, AbstractAuthenticationToken> sessionChecked(AuthService auth) {
        return jwt -> {
            AppTokenService.Claims claims = AppTokenService.claimsOf(jwt)
                    .filter(auth::accessTokenLive)
                    .orElseThrow(() -> new InvalidBearerTokenException("Session ended"));
            List<GrantedAuthority> authorities = new ArrayList<>();
            if (!Boolean.TRUE.equals(jwt.getClaim(AppTokenService.PWD_CHANGE))) {
                authorities.add(new SimpleGrantedAuthority(ACTIVE));
                authorities.add(new SimpleGrantedAuthority("ROLE_" + jwt.getClaimAsString(AppTokenService.ROLE)));
                for (Permission p : AppTokenService.permissionsOf(jwt)) {
                    authorities.add(new SimpleGrantedAuthority(p.authority()));
                }
            }
            return new JwtAuthenticationToken(jwt, authorities, Long.toString(claims.accountId()));
        };
    }

    /** The access_token cookie, only under /api/ and /ws/; nothing else is read. */
    static BearerTokenResolver cookieBearerTokenResolver() {
        return request -> {
            String path = requestPath(request);
            if (!path.startsWith("/api/") && !path.startsWith("/ws/")) return null;
            return AuthCookies.read(request, AuthCookies.ACCESS_COOKIE);
        };
    }

    private static CsrfFilter csrfFilter(boolean secureCookies, RequestMatcher csrfProtected) {
        CookieCsrfTokenRepository repository = CookieCsrfTokenRepository.withHttpOnlyFalse();
        repository.setCookieCustomizer(cookie -> cookie.secure(secureCookies)
                .sameSite(AuthCookies.SAME_SITE)
                .path("/"));
        CsrfFilter filter = new CsrfFilter(repository);
        filter.setRequireCsrfProtectionMatcher(csrfProtected);
        // The plain handler, not the XOR one: the frontend echoes the cookie verbatim.
        filter.setRequestHandler(new CsrfTokenRequestAttributeHandler());
        filter.setAccessDeniedHandler((request, response, ex) ->
                writeError(response, HttpStatus.FORBIDDEN, "Missing or stale X-XSRF-TOKEN"));
        return filter;
    }

    private static String requestPath(HttpServletRequest request) {
        String uri = request.getRequestURI();
        String context = request.getContextPath();
        return context != null && !context.isEmpty() && uri.startsWith(context) ? uri.substring(context.length()) : uri;
    }

    /** Same {error} shape as the rest of the API. */
    private static void writeError(HttpServletResponse response, HttpStatus status, String message)
            throws IOException {
        response.setStatus(status.value());
        response.setContentType(MediaType.APPLICATION_JSON_VALUE);
        response.getWriter().write("{\"error\":\"" + message.replace("\"", "'") + "\"}");
    }

    /**
     * Spring defers the CSRF token until something reads it; nothing here
     * renders a form, so without this read the XSRF-TOKEN cookie would only be
     * written on a request that had already failed the check.
     */
    static final class CsrfCookieFilter extends OncePerRequestFilter {

        @Override
        protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response,
                                        FilterChain chain) throws ServletException, IOException {
            CsrfToken token = (CsrfToken) request.getAttribute(CsrfToken.class.getName());
            if (token != null) token.getToken();
            chain.doFilter(request, response);
        }
    }
}
