package com.orblinx.mcc.auth;

import java.util.List;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.PutMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

/** User accounts. ADMIN role only (enforced in SecurityConfig for /api/admin/**). */
@RestController
@RequestMapping("/api/admin/users")
@Tag(name = "User accounts", description = "Admins only")
public class AccountController {

    private final AuthService auth;

    public AccountController(AuthService auth) {
        this.auth = auth;
    }

    public record NewAccount(String username, String displayName, String role, String password) {
    }

    public record AccountChange(String displayName, String role, Boolean enabled) {
    }

    public record PasswordReset(String password) {
    }

    @GetMapping
    @Operation(summary = "List accounts")
    public List<AuthService.AccountView> list() {
        return auth.accounts();
    }

    @PostMapping(consumes = MediaType.APPLICATION_JSON_VALUE)
    @Operation(summary = "Create an account", description = "{username, displayName, role: ADMIN|OPERATOR, "
            + "password}. The user must change the password at first sign-in. 409 if the username is taken.")
    public ResponseEntity<AuthService.AccountView> create(@AuthenticationPrincipal Jwt jwt,
                                                          @RequestBody NewAccount body) {
        if (body == null) throw new AuthException(HttpStatus.BAD_REQUEST, "Body required");
        return ResponseEntity.status(HttpStatus.CREATED).body(auth.create(admin(jwt), body.username(),
                body.displayName(), body.role(), body.password()));
    }

    @PatchMapping(path = "/{id}", consumes = MediaType.APPLICATION_JSON_VALUE)
    @Operation(summary = "Change an account", description = "Any of {displayName, role, enabled}. Disabling "
            + "signs the user out everywhere. The last enabled admin cannot be disabled or demoted.")
    public AuthService.AccountView update(@AuthenticationPrincipal Jwt jwt, @PathVariable long id,
                                          @RequestBody AccountChange body) {
        if (body == null) throw new AuthException(HttpStatus.BAD_REQUEST, "Body required");
        return auth.update(admin(jwt), id, body.displayName(), body.role(), body.enabled());
    }

    @PutMapping(path = "/{id}/password", consumes = MediaType.APPLICATION_JSON_VALUE)
    @Operation(summary = "Reset a password", description = "{password}: a temporary password the user must "
            + "change at next sign-in. Signs the user out everywhere.")
    public ResponseEntity<Void> resetPassword(@AuthenticationPrincipal Jwt jwt, @PathVariable long id,
                                              @RequestBody PasswordReset body) {
        auth.resetPassword(admin(jwt), id, body == null ? null : body.password());
        return ResponseEntity.noContent().build();
    }

    @DeleteMapping("/{id}")
    @Operation(summary = "Delete an account")
    public ResponseEntity<Void> delete(@AuthenticationPrincipal Jwt jwt, @PathVariable long id) {
        auth.delete(admin(jwt), id);
        return ResponseEntity.noContent().build();
    }

    private static long admin(Jwt jwt) {
        return AuthController.claims(jwt).accountId();
    }
}
