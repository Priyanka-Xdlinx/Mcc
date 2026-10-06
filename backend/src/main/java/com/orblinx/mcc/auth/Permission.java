package com.orblinx.mcc.auth;

import java.util.EnumSet;
import java.util.List;
import java.util.Set;

/**
 * What an account may do, one permission per feature or action. Each role
 * grants a fixed set (see {@link #of}); the set is carried in the access token
 * as the {@code perms} claim, and SecurityConfig checks it on every request.
 *
 * Not a permission, on purpose: /api/me and changing your own password. A user
 * with a temporary password must reach those, so every signed-in user may.
 */
public enum Permission {

    /** The Telemetry and Charts tabs: latest values, limits, history, the live WebSocket, the stack status. */
    TELEMETRY_VIEW,
    /** The Decode trace tab: decoded frames, frame decode and the byte-level trace. */
    DECODE_VIEW,
    /** The Docs tab: the database schema, the REST docs and the live-stream reference. */
    DOCS_VIEW,
    /** The Display tab: list and open display pages, read your workspace layout. */
    DISPLAY_VIEW,
    /** Create and change display pages, delete pages you own, save your workspace layout. */
    DISPLAY_EDIT,
    /** Delete display pages owned by someone else. */
    DISPLAY_DELETE_ANY,
    /** See whether the simulator stack is running. */
    SIM_VIEW,
    /** Start and stop the simulator stack. */
    SIM_CONTROL,
    /** List user accounts. */
    USERS_VIEW,
    /** Create, change, reset the password of and delete user accounts. */
    USERS_MANAGE;

    /** The Spring authority name, e.g. {@code PERM_USERS_MANAGE}. */
    public String authority() {
        return "PERM_" + name();
    }

    /** The permissions a role grants. ADMIN has all of them. */
    public static Set<Permission> of(AccountRepo.Role role) {
        return switch (role) {
            case ADMIN -> EnumSet.allOf(Permission.class);
            case OPERATOR -> EnumSet.of(TELEMETRY_VIEW, DECODE_VIEW, DOCS_VIEW, DISPLAY_VIEW, DISPLAY_EDIT,
                                        SIM_VIEW, SIM_CONTROL);
        };
    }

    /**
     * What this account is granted right now. An account that must change its
     * password is granted nothing, the same as it gets no role authorities.
     */
    static Set<Permission> grantedTo(AccountRepo.Account account) {
        return account.mustChangePassword() ? EnumSet.noneOf(Permission.class) : of(account.role());
    }

    /** Names for the token and the API, in declaration order. */
    static List<String> names(Set<Permission> permissions) {
        return permissions.stream().map(Enum::name).toList();
    }
}
