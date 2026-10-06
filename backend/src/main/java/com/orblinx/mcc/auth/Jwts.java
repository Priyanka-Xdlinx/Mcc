package com.orblinx.mcc.auth;

import java.time.Duration;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

import javax.crypto.SecretKey;

import com.nimbusds.jose.jwk.source.ImmutableSecret;
import org.springframework.security.oauth2.core.DelegatingOAuth2TokenValidator;
import org.springframework.security.oauth2.core.OAuth2TokenValidator;
import org.springframework.security.oauth2.jose.jws.MacAlgorithm;
import org.springframework.security.oauth2.jwt.JwsHeader;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.security.oauth2.jwt.JwtClaimNames;
import org.springframework.security.oauth2.jwt.JwtClaimValidator;
import org.springframework.security.oauth2.jwt.JwtEncoder;
import org.springframework.security.oauth2.jwt.JwtIssuerValidator;
import org.springframework.security.oauth2.jwt.JwtTimestampValidator;
import org.springframework.security.oauth2.jwt.NimbusJwtDecoder;
import org.springframework.security.oauth2.jwt.NimbusJwtEncoder;

/** HS256 encoders/decoders and claim validators shared by the token services. */
final class Jwts {

    static final JwsHeader HS256_HEADER = JwsHeader.with(MacAlgorithm.HS256).type("JWT").build();

    private Jwts() {
    }

    static JwtEncoder hmacEncoder(SecretKey key) {
        return new NimbusJwtEncoder(new ImmutableSecret<>(key));
    }

    /** Signature, exp/nbf with the given skew, a required exp, and the extra checks. */
    @SafeVarargs
    static NimbusJwtDecoder hmacDecoder(SecretKey key, Duration skew, OAuth2TokenValidator<Jwt>... extra) {
        NimbusJwtDecoder decoder = NimbusJwtDecoder.withSecretKey(key).macAlgorithm(MacAlgorithm.HS256).build();
        decoder.setJwtValidator(validators(skew, extra));
        return decoder;
    }

    @SafeVarargs
    static OAuth2TokenValidator<Jwt> validators(Duration skew, OAuth2TokenValidator<Jwt>... extra) {
        List<OAuth2TokenValidator<Jwt>> all = new ArrayList<>();
        all.add(new JwtTimestampValidator(skew));
        all.add(claim(JwtClaimNames.EXP, exp -> exp != null));
        all.addAll(List.of(extra));
        return new DelegatingOAuth2TokenValidator<>(all);
    }

    static OAuth2TokenValidator<Jwt> issuer(String issuer) {
        return new JwtIssuerValidator(issuer);
    }

    static OAuth2TokenValidator<Jwt> audience(String audience) {
        return claim(JwtClaimNames.AUD, aud -> aud instanceof Collection<?> c && c.contains(audience));
    }

    static OAuth2TokenValidator<Jwt> nonBlank(String name) {
        return claim(name, v -> v instanceof String s && !s.isBlank());
    }

    static OAuth2TokenValidator<Jwt> equalTo(String name, String expected) {
        return claim(name, expected::equals);
    }

    private static OAuth2TokenValidator<Jwt> claim(String name, java.util.function.Predicate<Object> test) {
        return new JwtClaimValidator<>(name, test);
    }
}
