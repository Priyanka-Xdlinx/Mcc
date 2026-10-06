package com.orblinx.mcc;

import com.orblinx.mcc.api.DocsController;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.http.*;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

import static org.junit.jupiter.api.Assertions.*;

/**
 * The whole application against throwaway SQLite files: the decode loader,
 * every repository and the security chain, wired the way production wires
 * them. Then a real sign-in over HTTP, CSRF cookie included.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
class ApplicationStartupTest {

    @TempDir
    static Path dataDir;

    @DynamicPropertySource
    static void database(DynamicPropertyRegistry r) {
        Path db = dataDir.resolve("mcc.db");
        Path authDb = dataDir.resolve("mcc-auth.db");
        r.add("mcc.db", db::toString);
        r.add("mcc.auth.db", authDb::toString);
        r.add("mcc.data-dir", () -> dataDir.toString());
        r.add("mcc.xdos-home", () -> Replay.repoRoot().resolve("xdos").toString());
        r.add("mcc.auth.secure-cookies", () -> "false");
        r.add("mcc.feed.port", () -> "1");       // no feed in tests
    }

    @Autowired
    TestRestTemplate http;

    @Autowired
    DocsController docs;

    /** The JDK's HttpURLConnection cannot read a 401 answer to a POST; java.net.http can. */
    @BeforeEach
    void client() {
        http.getRestTemplate().setRequestFactory(new JdkClientHttpRequestFactory());
    }

    @Test
    void startsAndReadsItsOwnSchema() {
        Map<String, Object> schema = docs.schema();
        assertTrue(String.valueOf(schema.get("engine")).startsWith("SQLite "));
        @SuppressWarnings("unchecked")
        List<Map<String, Object>> tables = (List<Map<String, Object>>) schema.get("tables");
        Map<String, Map<String, Object>> byName = new HashMap<>();
        tables.forEach(t -> byName.put((String) t.get("name"), t));
        assertTrue(byName.keySet().containsAll(List.of("parameter", "telemetry_history", "account", "display_page")),
                  byName.keySet().toString());
        assertEquals(2761L, byName.get("parameter").get("rows"), "the MIB was loaded");
        assertTrue(byName.get("account").get("indexes").toString().toLowerCase(Locale.ROOT).contains("username"));
    }

    @Test
    void signsInTheFirstAdminOverHttp() throws Exception {
        assertEquals(HttpStatus.UNAUTHORIZED, http.getForEntity("/api/schema", String.class).getStatusCode());

        String password = Files.readAllLines(dataDir.resolve("mcc-admin-password.txt")).stream()
                .filter(l -> l.startsWith("password: ")).findFirst().orElseThrow().substring(10);

        // Every write, sign-in included, echoes the XSRF-TOKEN cookie.
        ResponseEntity<Void> csrf = http.getForEntity("/auth/csrf", Void.class);
        String xsrf = cookie(csrf, "XSRF-TOKEN");
        HttpHeaders h = new HttpHeaders();
        h.setContentType(MediaType.APPLICATION_JSON);
        h.add(HttpHeaders.COOKIE, "XSRF-TOKEN=" + xsrf);
        h.add("X-XSRF-TOKEN", xsrf);

        ResponseEntity<String> bad = http.exchange("/auth/login", HttpMethod.POST,
                new HttpEntity<>("{\"username\":\"admin\",\"password\":\"wrong-password-1\"}", h), String.class);
        assertEquals(HttpStatus.UNAUTHORIZED, bad.getStatusCode());
        assertTrue(bad.getBody().contains("Invalid username or password"));

        ResponseEntity<String> ok = http.exchange("/auth/login", HttpMethod.POST,
                new HttpEntity<>("{\"username\":\"admin\",\"password\":\"" + password + "\"}", h), String.class);
        assertEquals(HttpStatus.OK, ok.getStatusCode(), ok.getBody());
        assertTrue(ok.getBody().contains("\"mustChangePassword\":true"));
        String access = cookie(ok, "access_token");

        // A temporary password allows /api/me and nothing else.
        HttpHeaders signedIn = new HttpHeaders();
        signedIn.add(HttpHeaders.COOKIE, "access_token=" + access);
        assertEquals(HttpStatus.OK, http.exchange("/api/me", HttpMethod.GET, new HttpEntity<>(signedIn), String.class)
                                        .getStatusCode());
        assertEquals(HttpStatus.FORBIDDEN, http.exchange("/api/schema", HttpMethod.GET, new HttpEntity<>(signedIn),
                                                         String.class).getStatusCode());
    }

    private static String cookie(ResponseEntity<?> r, String name) {
        return Objects.requireNonNull(r.getHeaders().get(HttpHeaders.SET_COOKIE), "no Set-Cookie").stream()
                .filter(c -> c.startsWith(name + "="))
                .map(c -> c.substring(name.length() + 1, c.indexOf(';')))
                .findFirst().orElseThrow(() -> new AssertionError("no " + name + " cookie"));
    }
}
