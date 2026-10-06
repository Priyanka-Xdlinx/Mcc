package com.orblinx.mcc.api;

import com.orblinx.mcc.TestDb;
import com.orblinx.mcc.auth.AccountRepo;
import com.orblinx.mcc.auth.AccountRepo.Role;
import com.orblinx.mcc.auth.CurrentUser;
import com.orblinx.mcc.auth.Permission;
import com.orblinx.mcc.store.DisplayRepo;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.util.*;

import org.junit.jupiter.api.*;
import org.springframework.http.ResponseEntity;

import static org.junit.jupiter.api.Assertions.*;

/** Display pages through the controller, acting as signed-in accounts. */
class DisplayControllerTest {

    private static final ObjectMapper JSON = new ObjectMapper();

    private DisplayController api;
    private CurrentUser asha;
    private CurrentUser ravi;
    private CurrentUser admin;
    /** Two parameters the flown LUT really downlinks. */
    private int p1;
    private int p2;

    @BeforeEach
    void setUp() {
        TestDb.clearUserData();
        AccountRepo accounts = new AccountRepo(TestDb.authDbPath());
        asha = new CurrentUser(accounts.create("asha", "Asha", "h", Role.OPERATOR, false).id(),
                Permission.of(Role.OPERATOR));
        ravi = new CurrentUser(accounts.create("ravi", "Ravi", "h", Role.OPERATOR, false).id(),
                Permission.of(Role.OPERATOR));
        admin = new CurrentUser(accounts.create("admin", "Admin", "h", Role.ADMIN, false).id(),
                Permission.of(Role.ADMIN));
        api = new DisplayController(new DisplayRepo(TestDb.jdbc(), TestDb.decode(), accounts), JSON);
        List<Integer> ids = TestDb.jdbc().queryForList(
                "SELECT DISTINCT parameter_id FROM pick_param WHERE parameter_id IS NOT NULL ORDER BY 1 LIMIT 2",
                Integer.class);
        p1 = ids.get(0);
        p2 = ids.get(1);
    }

    private static JsonNode body(String s) throws Exception { return JSON.readTree(s); }

    private static String page(String name, Integer... first) {
        List<Integer> p = new ArrayList<>(Arrays.asList(first));
        while (p.size() < DisplayRepo.SLOTS) p.add(null);
        try {
            return "{\"name\":\"" + name + "\",\"params\":" + JSON.writeValueAsString(p) + "}";
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    private static int status(ResponseEntity<?> r) { return r.getStatusCode().value(); }

    @Test
    @DisplayName("pages belong to the signed-in account: validation, ownership and name clashes")
    void pages() throws Exception {
        ResponseEntity<?> created = api.create(asha, body(page("Power", p1, null, p2)));
        assertEquals(201, status(created));
        DisplayRepo.Page p = (DisplayRepo.Page) created.getBody();
        assertEquals("Asha", p.ownerName());
        assertEquals(asha.accountId(), p.ownerId());

        assertEquals(409, status(api.create(asha, body(page("Power")))));
        assertEquals(201, status(api.create(ravi, body(page("Power")))), "the same name under another owner is fine");
        ResponseEntity<?> bad = api.create(asha, body(page("Y", -555)));
        assertEquals(400, status(bad));
        assertTrue(bad.getBody().toString().contains("-555"), "the error names the bad id");
        assertEquals(400, status(api.create(asha, body("{\"name\":\"Y\",\"params\":[1,2]}"))));

        assertEquals(403, status(api.update(ravi, p.id(), body(page("Mine")))));
        assertEquals(403, status(api.update(admin, p.id(), body(page("Mine")))), "admins copy, they do not overwrite");
        assertEquals(403, status(api.delete(ravi, p.id())));
        assertEquals(200, status(api.update(asha, p.id(), body(page("Power 2", p2)))));
        assertEquals(404, status(api.update(asha, 9999, body(page("X")))));

        assertEquals(204, status(api.delete(admin, p.id())), "an admin may delete anyone's page");
        assertEquals(404, status(api.page(p.id())));
    }

    @Test
    @DisplayName("each account has its own layout, stored as given")
    void workspace() throws Exception {
        assertNull(api.workspace(asha).get("layout"));

        JsonNode layout = body("{\"layout\":{\"v\":1,\"windows\":[{\"key\":\"w1\",\"pageId\":3}]}}");
        assertEquals(204, status(api.saveWorkspace(asha, layout)));
        assertEquals(layout.get("layout"), api.workspace(asha).get("layout"));
        assertNotNull(api.workspace(asha).get("updatedAt"));
        assertNull(api.workspace(ravi).get("layout"), "another account's layout is untouched");

        assertEquals(400, status(api.saveWorkspace(asha, body("{}"))));
        assertEquals(400, status(api.saveWorkspace(asha, body("{\"layout\":\"" + "x".repeat(70_000) + "\"}"))));
    }
}
