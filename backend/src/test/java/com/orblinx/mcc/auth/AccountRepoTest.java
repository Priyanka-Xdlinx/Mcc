package com.orblinx.mcc.auth;

import static org.junit.jupiter.api.Assertions.*;

import java.time.Instant;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import com.orblinx.mcc.TestDb;
import com.orblinx.mcc.auth.AccountRepo.Role;

class AccountRepoTest {

    AccountRepo repo;
    long id;

    @BeforeEach
    void setUp() {
        TestDb.clearUserData();
        repo = new AccountRepo(TestDb.authDbPath());
        id = repo.create("Alice", "Alice A", "hash", Role.OPERATOR, false).id();
    }

    @Test
    void usernamesAreCaseInsensitiveAndUnique() {
        assertTrue(repo.byUsername("alice").isPresent());
        assertThrows(org.springframework.dao.DuplicateKeyException.class,
                () -> repo.create("ALICE", "x", "hash", Role.OPERATOR, false));
    }

    @Test
    void locksAtTheLimitAndAnExpiredLockStartsTheCountAgain() {
        Instant later = Instant.now().plusSeconds(900);
        for (int i = 0; i < 4; i++) repo.recordFailedLogin(id, 5, later);
        assertFalse(repo.byId(id).orElseThrow().lockedAt(Instant.now()));
        repo.recordFailedLogin(id, 5, later);
        assertTrue(repo.byId(id).orElseThrow().lockedAt(Instant.now()));

        // the lock has run out: one more failure counts as the first, and clears the lock
        repo.recordLogin(id);
        for (int i = 0; i < 5; i++) repo.recordFailedLogin(id, 5, Instant.now().minusSeconds(1));
        repo.recordFailedLogin(id, 5, later);
        AccountRepo.Account a = repo.byId(id).orElseThrow();
        assertEquals(1, a.failedLogins());
        assertNull(a.lockedUntil());
    }

    @Test
    void sessionsEndOnRevokePasswordChangeOrDisable() {
        Instant expiry = Instant.now().plusSeconds(3600);
        String s1 = repo.createSession(id, expiry, "ip", "ua");
        String s2 = repo.createSession(id, expiry, "ip", "ua");
        assertTrue(repo.sessionLive(s1, id, 0));
        assertFalse(repo.sessionLive(s1, id + 1, 0), "a session belongs to one account");

        repo.revokeSession(s1);
        assertFalse(repo.sessionLive(s1, id, 0));
        assertTrue(repo.sessionLive(s2, id, 0));

        repo.setPassword(id, "hash2", false); // bumps token_version
        assertFalse(repo.sessionLive(s2, id, 0));
        assertTrue(repo.sessionLive(s2, id, 1));

        repo.update(id, "Alice A", Role.OPERATOR, false); // disabled, version 2
        assertFalse(repo.sessionLive(s2, id, 2));
    }

    @Test
    void expiredSessionsAreNotLive() {
        String s = repo.createSession(id, Instant.now().minusSeconds(1), "ip", "ua");
        assertFalse(repo.sessionLive(s, id, 0));
    }

    @Test
    void signingKeysAreStableAcrossRestarts() {
        byte[] first = repo.signingKey("access");
        byte[] again = new AccountRepo(TestDb.authDbPath()).signingKey("access");
        assertArrayEquals(first, again);
        assertFalse(java.util.Arrays.equals(first, repo.signingKey("refresh")));
    }

    @Test
    void passwordPolicy() {
        assertNotNull(PasswordPolicy.check("short", "bob"));
        assertNotNull(PasswordPolicy.check("bob-is-my-password", "bob"));
        assertNotNull(PasswordPolicy.check("aaaaaaaaaaaaaaaa", "bob"));
        assertNotNull(PasswordPolicy.check("x".repeat(40) + "é".repeat(20), "bob"), "over 72 bytes");
        assertNull(PasswordPolicy.check("correct horse battery", "bob"));
        assertNotNull(PasswordPolicy.checkUsername("a b"));
        assertNull(PasswordPolicy.checkUsername("op.jane"));
    }
}
