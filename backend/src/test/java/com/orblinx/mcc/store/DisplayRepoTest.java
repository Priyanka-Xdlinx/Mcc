package com.orblinx.mcc.store;

import com.orblinx.mcc.api.DisplayController;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

import org.junit.jupiter.api.*;
import org.springframework.http.ResponseEntity;
import org.springframework.jdbc.core.JdbcTemplate;
import org.sqlite.SQLiteDataSource;

import static org.junit.jupiter.api.Assertions.*;

/** Display pages against a throwaway SQLite file - never mcc.db. */
class DisplayRepoTest {

    private static final ObjectMapper JSON = new ObjectMapper();

    private Path dbFile;
    private DisplayRepo repo;
    private DisplayController api;

    @BeforeEach
    void setUp() throws Exception {
        dbFile = Files.createTempFile("mcc-display-test", ".db");
        SQLiteDataSource ds = new SQLiteDataSource();
        ds.setUrl("jdbc:sqlite:" + dbFile);
        ds.setEnforceForeignKeys(true);
        // Just enough of the decode schema for the downlinked-id check.
        JdbcTemplate jdbc = new JdbcTemplate(ds);
        jdbc.execute("CREATE TABLE pick_param (pick_id INTEGER, seq INTEGER, name TEXT, parameter_id INTEGER)");
        jdbc.update("INSERT INTO pick_param VALUES (1, 0, 'a', 100), (1, 1, 'b', 101), (1, 2, 'pad', NULL)");
        repo = new DisplayRepo(ds);
        new DisplayRepo(ds);                  // startup DDL must be idempotent
        api = new DisplayController(repo, JSON);
    }

    @AfterEach
    void tearDown() throws Exception {
        Files.deleteIfExists(dbFile);
    }

    private static JsonNode body(String s) throws Exception { return JSON.readTree(s); }

    private static String params(Integer... first) {
        List<Integer> p = new ArrayList<>(Arrays.asList(first));
        while (p.size() < 20) p.add(null);
        try { return JSON.writeValueAsString(p); } catch (Exception e) { throw new RuntimeException(e); }
    }

    @Test
    @DisplayName("users are get-or-create by name, case-insensitively")
    void users() throws Exception {
        DisplayRepo.User a = (DisplayRepo.User) api.pickUser(body("{\"name\":\"  Asha \"}")).getBody();
        DisplayRepo.User b = (DisplayRepo.User) api.pickUser(body("{\"name\":\"ASHA\"}")).getBody();
        assertEquals(a.id(), b.id());
        assertEquals("Asha", b.name());
        assertEquals(400, api.pickUser(body("{\"name\":\"  \"}")).getStatusCode().value());
        assertEquals(400, api.pickUser(body("{\"name\":\"" + "x".repeat(41) + "\"}")).getStatusCode().value());
        assertEquals(1, repo.users().size());
    }

    @Test
    @DisplayName("pages: validation, ownership, name clash and page counts")
    void pages() throws Exception {
        long asha = repo.userByName("Asha").id();
        long ravi = repo.userByName("Ravi").id();
        String a = String.valueOf(asha), r = String.valueOf(ravi);

        ResponseEntity<?> created = api.create(a, body("{\"name\":\"Power\",\"params\":" + params(100, null, 101) + "}"));
        assertEquals(201, created.getStatusCode().value());
        DisplayRepo.Page p = (DisplayRepo.Page) created.getBody();
        assertEquals("Asha", p.ownerName());
        assertEquals(20, p.params().size());
        assertEquals(101, p.params().get(2));

        assertEquals(401, api.create(null, body("{\"name\":\"X\",\"params\":" + params() + "}")).getStatusCode().value());
        assertEquals(401, api.create("999", body("{\"name\":\"X\",\"params\":" + params() + "}")).getStatusCode().value());
        assertEquals(409, api.create(a, body("{\"name\":\"Power\",\"params\":" + params() + "}")).getStatusCode().value());
        assertEquals(201, api.create(r, body("{\"name\":\"Power\",\"params\":" + params() + "}")).getStatusCode().value(),
                     "the same name under another owner is fine");

        ResponseEntity<?> bad = api.create(a, body("{\"name\":\"Y\",\"params\":" + params(555) + "}"));
        assertEquals(400, bad.getStatusCode().value());
        assertTrue(bad.getBody().toString().contains("555"), "the error names the bad id");
        assertEquals(400, api.create(a, body("{\"name\":\"Y\",\"params\":[1,2]}")).getStatusCode().value());

        assertEquals(403, api.update(r, p.id(), body("{\"name\":\"Mine\",\"params\":" + params() + "}")).getStatusCode().value());
        assertEquals(403, api.delete(r, p.id()).getStatusCode().value());
        api.create(a, body("{\"name\":\"Thermal\",\"params\":" + params() + "}"));
        assertEquals(409, api.update(a, p.id(), body("{\"name\":\"Thermal\",\"params\":" + params() + "}")).getStatusCode().value());
        assertEquals(200, api.update(a, p.id(), body("{\"name\":\"Power 2\",\"params\":" + params(101) + "}")).getStatusCode().value());

        assertEquals(List.of(2, 1), repo.users().stream().map(DisplayRepo.User::pages).toList());
        assertEquals(1, repo.pages(asha, "power").size());
        assertEquals(3, repo.pages(null, null).size());
        assertEquals("Power 2", repo.pages(null, null).get(0).name(), "newest update first");

        assertEquals(204, api.delete(a, p.id()).getStatusCode().value());
        assertEquals(404, api.page(p.id()).getStatusCode().value());
    }

    @Test
    @DisplayName("workspaces are stored opaque and only by their owner")
    void workspace() throws Exception {
        long asha = repo.userByName("Asha").id();
        long ravi = repo.userByName("Ravi").id();
        @SuppressWarnings("unchecked")
        Map<String, Object> empty = (Map<String, Object>) api.workspace(asha).getBody();
        assertNull(empty.get("layout"));

        JsonNode layout = body("{\"layout\":{\"v\":1,\"windows\":[{\"key\":\"w1\",\"pageId\":3}]}}");
        assertEquals(403, api.saveWorkspace(String.valueOf(ravi), asha, layout).getStatusCode().value());
        assertEquals(204, api.saveWorkspace(String.valueOf(asha), asha, layout).getStatusCode().value());
        @SuppressWarnings("unchecked")
        Map<String, Object> saved = (Map<String, Object>) api.workspace(asha).getBody();
        assertEquals(layout.get("layout"), saved.get("layout"));
        assertNotNull(saved.get("updatedAt"));
        assertEquals(404, api.workspace(9999).getStatusCode().value());
    }
}
