package com.orblinx.mcc.auth;

import com.orblinx.mcc.Replay;

import java.nio.file.Path;
import java.util.List;
import java.util.Objects;
import java.util.Set;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.http.*;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import org.springframework.security.crypto.bcrypt.BCryptPasswordEncoder;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Each role over real HTTP: what the token says it may do, and what the
 * security chain lets it reach. The accounts are made directly, because a
 * sign-in through the API would first demand a new password.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
class PermissionRulesTest {

    private static final String PASSWORD = "correct-horse-battery-1";
    private static final ObjectMapper JSON = new ObjectMapper();

    @TempDir
    static Path dataDir;

    @DynamicPropertySource
    static void database(DynamicPropertyRegistry r) {
        r.add("mcc.db", () -> dataDir.resolve("mcc.db").toString());
        r.add("mcc.auth.db", () -> dataDir.resolve("mcc-auth.db").toString());
        r.add("mcc.data-dir", () -> dataDir.toString());
        r.add("mcc.xdos-home", () -> Replay.repoRoot().resolve("xdos").toString());
        r.add("mcc.auth.secure-cookies", () -> "false");
        r.add("mcc.feed.port", () -> "1");       // no feed in tests
    }

    @Autowired
    TestRestTemplate http;

    @Autowired
    AccountRepo accounts;

    @Autowired
    AppTokenService tokens;

    private String operatorCookie;
    private String adminCookie;

    @BeforeEach
    void signIn() {
        http.getRestTemplate().setRequestFactory(new JdkClientHttpRequestFactory());
        String hash = new BCryptPasswordEncoder(4).encode(PASSWORD);
        if (accounts.byUsername("ops").isEmpty()) {
            accounts.create("ops", "Ops", hash, AccountRepo.Role.OPERATOR, false);
            accounts.create("boss", "Boss", hash, AccountRepo.Role.ADMIN, false);
        }
        operatorCookie = login("ops");
        adminCookie = login("boss");
    }

    @Test
    void theAccessTokenCarriesTheRolesPermissions() throws Exception {
        Jwt operator = tokens.accessTokenDecoder().decode(operatorCookie);
        Jwt admin = tokens.accessTokenDecoder().decode(adminCookie);
        assertEquals(names(Permission.of(AccountRepo.Role.OPERATOR)), names(AppTokenService.permissionsOf(operator)));
        assertEquals(names(Permission.of(AccountRepo.Role.ADMIN)), names(AppTokenService.permissionsOf(admin)));
        assertEquals(names(Permission.of(AccountRepo.Role.OPERATOR)), permissionList(me(operatorCookie)),
                     "/api/me reports the same set as the token");
    }

    @Test
    void anOperatorReachesItsFeaturesButNotUserAccounts() {
        assertEquals(HttpStatus.OK, get("/api/pages", operatorCookie));
        assertEquals(HttpStatus.OK, get("/api/schema", operatorCookie));
        assertEquals(HttpStatus.OK, get("/api/telemetry/latest", operatorCookie));
        assertEquals(HttpStatus.FORBIDDEN, get("/api/admin/users", operatorCookie));
    }

    @Test
    void anAdminReachesUserAccounts() {
        assertEquals(HttpStatus.OK, get("/api/admin/users", adminCookie));
    }

    @Test
    void anAnonymousCallerIsRefused() {
        assertEquals(HttpStatus.UNAUTHORIZED, get("/api/pages", null));
    }

    // ── helpers ─────────────────────────────────────────────────────────────

    private static List<String> names(Set<Permission> permissions) {
        return permissions.stream().map(Enum::name).sorted().toList();
    }
    private static List<String> permissionList(JsonNode me) {
        List<String> out = new java.util.ArrayList<>();
        me.get("permissions").forEach(n -> out.add(n.asText()));
        return out.stream().sorted().toList();
    }

    private JsonNode me(String cookie) {
        ResponseEntity<String> r = exchange("/api/me", HttpMethod.GET, cookie);
        assertEquals(HttpStatus.OK, r.getStatusCode());
        try {
            return JSON.readTree(r.getBody());
        } catch (Exception ex) {
            throw new AssertionError(ex);
        }
    }

    private HttpStatusCode get(String path, String cookie) {
        return exchange(path, HttpMethod.GET, cookie).getStatusCode();
    }

    private ResponseEntity<String> exchange(String path, HttpMethod method, String cookie) {
        HttpHeaders h = new HttpHeaders();
        if (cookie != null) h.add(HttpHeaders.COOKIE, "access_token=" + cookie);
        return http.exchange(path, method, new HttpEntity<>(h), String.class);
    }

    /** Signs in through POST /auth/login, echoing the XSRF cookie as the browser would; returns the access token. */
    private String login(String username) {
        ResponseEntity<Void> csrf = http.getForEntity("/auth/csrf", Void.class);
        String xsrf = cookie(csrf, "XSRF-TOKEN");
        HttpHeaders h = new HttpHeaders();
        h.setContentType(MediaType.APPLICATION_JSON);
        h.add(HttpHeaders.COOKIE, "XSRF-TOKEN=" + xsrf);
        h.add("X-XSRF-TOKEN", xsrf);
        ResponseEntity<String> ok = http.exchange("/auth/login", HttpMethod.POST,
                new HttpEntity<>("{\"username\":\"" + username + "\",\"password\":\"" + PASSWORD + "\"}", h),
                String.class);
        assertEquals(HttpStatus.OK, ok.getStatusCode(), ok.getBody());
        return cookie(ok, "access_token");
    }

    private static String cookie(ResponseEntity<?> r, String name) {
        return Objects.requireNonNull(r.getHeaders().get(HttpHeaders.SET_COOKIE), "no Set-Cookie").stream()
                .filter(c -> c.startsWith(name + "="))
                .map(c -> c.substring(name.length() + 1, c.indexOf(';')))
                .findFirst().orElseThrow(() -> new AssertionError("no " + name + " cookie"));
    }
}
