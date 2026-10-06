package com.orblinx.mcc.auth;

import java.util.Set;

import org.springframework.http.HttpStatus;
import org.springframework.security.oauth2.jwt.Jwt;

/**
 * Who is making a request, as the verified access token says, for controllers
 * outside this package. SecurityConfig has already checked that the session is
 * live before a controller sees the token.
 */
public record CurrentUser(long accountId, Set<Permission> permissions) {

    public static CurrentUser of(Jwt jwt) {
        if (jwt == null) throw new AuthException(HttpStatus.UNAUTHORIZED, "Not signed in");
        long id = AuthController.claims(jwt).accountId();
        return new CurrentUser(id, AppTokenService.permissionsOf(jwt));
    }

    public boolean has(Permission permission) {
        return permissions.contains(permission);
    }
}
