package com.orblinx.mcc.store;

import com.orblinx.mcc.TestDb;
import com.orblinx.mcc.auth.AccountRepo;
import com.orblinx.mcc.auth.AccountRepo.Role;

import java.util.*;

import org.junit.jupiter.api.*;

import static org.junit.jupiter.api.Assertions.*;

/** Display storage against the throwaway test database - never the real one. */
class DisplayRepoTest {

    private DisplayRepo repo;
    private AccountRepo accounts;
    private long admin;
    private long asha;
    /** Two parameters the flown LUT really downlinks. */
    private int p1;
    private int p2;

    @BeforeEach
    void setUp() {
        TestDb.clearUserData();
        accounts = new AccountRepo(TestDb.authDbPath());
        admin = accounts.create("admin", "Administrator", "hash", Role.ADMIN, false).id();
        asha = accounts.create("asha", "Asha Rao", "hash", Role.OPERATOR, false).id();
        repo = new DisplayRepo(TestDb.jdbc(), TestDb.decode(), accounts);
        List<Integer> ids = TestDb.jdbc().queryForList(
                "SELECT DISTINCT parameter_id FROM pick_param WHERE parameter_id IS NOT NULL ORDER BY 1 LIMIT 2",
                Integer.class);
        p1 = ids.get(0);
        p2 = ids.get(1);
    }

    private static List<Integer> params(Integer... first) {
        List<Integer> p = new ArrayList<>(Arrays.asList(first));
        while (p.size() < DisplayRepo.SLOTS) p.add(null);
        return p;
    }

    @Test
    @DisplayName("pages carry their owner's display name and keep their slots")
    void pagesByAccount() {
        DisplayRepo.Page p = repo.createPage(asha, "Power", params(p1, null, p2));
        assertEquals("Asha Rao", p.ownerName());
        assertEquals(asha, p.ownerId());
        assertEquals(DisplayRepo.SLOTS, p.params().size());
        assertEquals(p2, p.params().get(2));
        assertNotNull(p.createdAt());

        assertThrows(DisplayRepo.NameClashException.class, () -> repo.createPage(asha, "Power", params()));
        assertDoesNotThrow(() -> repo.createPage(admin, "Power", params()), "the same name under another owner is fine");
        assertEquals(1, repo.pages(asha, "pow").size());
        assertEquals(2, repo.pages(null, "POWER").size(), "search ignores case");
        assertEquals(0, repo.pages(null, "%").size(), "search text is literal, not a pattern");
        assertTrue(repo.isDownlinked(p1));
        assertFalse(repo.isDownlinked(-1));

        DisplayRepo.Page renamed = repo.updatePage(p.id(), "Power 2", params(p2));
        assertEquals("Power 2", renamed.name());
        assertEquals("Power 2", repo.pages(null, null).get(0).name(), "newest update first");
    }

    @Test
    @DisplayName("a deleted account's pages stay, owned by a former user")
    void ownerDeleted() {
        DisplayRepo.Page p = repo.createPage(asha, "Power", params(p1));
        repo.saveWorkspace(asha, "{\"v\":1}");
        accounts.delete(asha);
        DisplayRepo.Page after = repo.page(p.id()).orElseThrow();
        // mcc.db and mcc-auth.db are separate files, so there is no foreign key to
        // cascade this; DisplayRepo itself nulls out an owner id it cannot resolve.
        assertNull(after.ownerId());
        assertEquals("former user", after.ownerName());
    }

    @Test
    @DisplayName("layouts are stored per account as JSON")
    void workspaces() {
        assertTrue(repo.workspace(asha).isEmpty());
        repo.saveWorkspace(asha, "{\"v\":1,\"windows\":[]}");
        repo.saveWorkspace(asha, "{\"v\":1,\"windows\":[{\"key\":\"w1\"}]}");
        assertTrue(repo.workspace(asha).orElseThrow().layout().contains("w1"));
        assertTrue(repo.workspace(admin).isEmpty());
    }
}
