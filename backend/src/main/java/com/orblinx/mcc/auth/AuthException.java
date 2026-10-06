package com.orblinx.mcc.auth;

import org.springframework.http.HttpStatus;

/** A refusal with a message that is safe to show the user as-is. */
public class AuthException extends RuntimeException {

    private final HttpStatus status;

    public AuthException(HttpStatus status, String message) {
        super(message, null, false, false);
        this.status = status;
    }

    public HttpStatus status() {
        return status;
    }
}
