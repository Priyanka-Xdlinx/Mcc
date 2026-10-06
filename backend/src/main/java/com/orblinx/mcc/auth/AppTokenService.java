package com.orblinx.mcc.auth;

import java.time.Duration;
import java.time.Instant;
import java.util.EnumSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;

import javax.crypto.SecretKey;
import javax.crypto.spec.SecretKeySpec;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.security.oauth2.jwt.JwtClaimNames;
import org.springframework.security.oauth2.jwt.JwtClaimsSet;
import org.springframework.security.oauth2.jwt.JwtDecoder;
import org.springframework.security.oauth2.jwt.JwtEncoder;
import org.springframework.security.oauth2.jwt.JwtEncoderParameters;
import org.springframework.security.oauth2.jwt.JwtException;
import org.springframework.stereotype.Service;

/**
 * Issues and verifies the HS256 access and refresh tokens. Each kind has its own
 * key, audience and {@code typ} claim, so one can never be accepted as the
 * other. Both name the session ({@code sid}) and the account's token version
 * ({@code ver}), which is how a token is revoked before it expires.
 */
@Service
public class AppTokenService {

    static final String ISSUER = "mcc";
    static final String ACCESS_AUDIENCE = "mcc-api";
    static final String REFRESH_AUDIENCE = "mcc-refresh";
    static final String TYP = "typ";
    static final String SID = "sid";
    static final String VER = "ver";
    static final String ROLE = "role";
    /** The account's permission names (see {@link Permission}); empty while a password change is pending. */
    static final String PERMS = "perms";
    /** Present (true) while the account must change its password before doing anything else. */
    static final String PWD_CHANGE = "pwc";

    private static final Logger log = LoggerFactory.getLogger(AppTokenService.class);
    private static final Duration SKEW = Duration.ofSeconds(30);

    /** What a verified token says. */
    public record Claims(long accountId, String sessionId, int tokenVersion) {
    }

    private final AuthProperties properties;
    private final JwtEncoder accessEncoder;
    private final JwtEncoder refreshEncoder;
    private final JwtDecoder accessDecoder;
    private final JwtDecoder refreshDecoder;

    public AppTokenService(AuthProperties properties, AccountRepo repo) {
        this.properties = properties;
        SecretKey accessKey = new SecretKeySpec(repo.signingKey("access"), "HmacSHA256");
        SecretKey refreshKey = new SecretKeySpec(repo.signingKey("refresh"), "HmacSHA256");
        this.accessEncoder = Jwts.hmacEncoder(accessKey);
        this.refreshEncoder = Jwts.hmacEncoder(refreshKey);
        this.accessDecoder = decoder(accessKey, ACCESS_AUDIENCE, "access");
        this.refreshDecoder = decoder(refreshKey, REFRESH_AUDIENCE, "refresh");
    }

    private static JwtDecoder decoder(SecretKey key, String audience, String typ) {
        return Jwts.hmacDecoder(key, SKEW, Jwts.issuer(ISSUER), Jwts.audience(audience), Jwts.equalTo(TYP, typ),
                Jwts.nonBlank(JwtClaimNames.SUB), Jwts.nonBlank(SID));
    }

    /** Decoder used by the resource server for /api/** and /ws/**. */
    public JwtDecoder accessTokenDecoder() {
        return accessDecoder;
    }

    public String issueAccessToken(AccountRepo.Account account, String sessionId) {
        JwtClaimsSet.Builder claims = base(account, sessionId, ACCESS_AUDIENCE, properties.accessTtl(), "access")
                .claim(ROLE, account.role().name())
                .claim(PERMS, Permission.names(Permission.grantedTo(account)))
                .claim("username", account.username())
                .claim("name", account.displayName());
        if (account.mustChangePassword()) claims.claim(PWD_CHANGE, true);
        return accessEncoder.encode(JwtEncoderParameters.from(Jwts.HS256_HEADER, claims.build())).getTokenValue();
    }

    public String issueRefreshToken(AccountRepo.Account account, String sessionId, Instant sessionExpiry) {
        Duration ttl = Duration.between(Instant.now(), sessionExpiry);
        JwtClaimsSet claims = base(account, sessionId, REFRESH_AUDIENCE, ttl, "refresh").build();
        return refreshEncoder.encode(JwtEncoderParameters.from(Jwts.HS256_HEADER, claims)).getTokenValue();
    }

    /** Signature, expiry and claims only; the caller still checks the session is live. */
    public Optional<Claims> verifyRefreshToken(String token) {
        if (token == null || token.isBlank()) return Optional.empty();
        try {
            return claimsOf(refreshDecoder.decode(token));
        } catch (JwtException ex) {
            log.debug("Refresh token rejected: {}", ex.getClass().getSimpleName());
            return Optional.empty();
        }
    }

    static Optional<Claims> claimsOf(Jwt jwt) {
        try {
            Object ver = jwt.getClaim(VER);
            if (!(ver instanceof Number n)) return Optional.empty();
            return Optional.of(new Claims(Long.parseLong(jwt.getSubject()), jwt.getClaimAsString(SID), n.intValue()));
        } catch (NumberFormatException ex) {
            return Optional.empty();
        }
    }

    /** The permissions the verified token grants; unknown names from an older or newer build are ignored. */
    static Set<Permission> permissionsOf(Jwt jwt) {
        Set<Permission> granted = EnumSet.noneOf(Permission.class);
        List<String> names = jwt.getClaimAsStringList(PERMS);
        if (names == null) return granted;
        for (String name : names) {
            try {
                granted.add(Permission.valueOf(name));
            } catch (IllegalArgumentException ignored) {
                // A name this build does not know grants nothing.
            }
        }
        return granted;
    }

    public Duration accessTtl() {
        return properties.accessTtl();
    }

    private static JwtClaimsSet.Builder base(AccountRepo.Account account, String sessionId, String audience,
                                             Duration ttl, String typ) {
        Instant now = Instant.now();
        return JwtClaimsSet.builder()
                .issuer(ISSUER)
                .audience(List.of(audience))
                .subject(Long.toString(account.id()))
                .issuedAt(now)
                .expiresAt(now.plus(ttl))
                .id(UUID.randomUUID().toString())
                .claim(TYP, typ)
                .claim(SID, sessionId)
                .claim(VER, account.tokenVersion());
    }
}
