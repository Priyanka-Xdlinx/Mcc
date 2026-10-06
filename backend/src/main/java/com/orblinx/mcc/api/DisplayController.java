package com.orblinx.mcc.api;

import com.orblinx.mcc.auth.CurrentUser;
import com.orblinx.mcc.auth.Permission;
import com.orblinx.mcc.store.DisplayRepo;
import com.orblinx.mcc.store.DisplayRepo.NameClashException;
import com.orblinx.mcc.store.DisplayRepo.Page;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.Parameter;
import io.swagger.v3.oas.annotations.tags.Tag;

import java.nio.charset.StandardCharsets;
import java.util.*;

import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.http.converter.HttpMessageNotReadableException;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.web.bind.annotation.*;

/**
 * Display pages: named sets of twenty parameter slots that users open as
 * floating windows, plus each user's saved window layout.
 *
 * The owner is the signed-in account. Anyone signed in may list and open any
 * page; only its owner may overwrite it, and its owner or an admin may delete it.
 */
@RestController
@RequestMapping("/api")
@Tag(name = "Display pages")
public class DisplayController {

    private static final int PAGE_NAME_MAX = 60;
    private static final int LAYOUT_MAX_BYTES = 64 * 1024;

    private final DisplayRepo repo;
    private final ObjectMapper json;

    public DisplayController(DisplayRepo repo, ObjectMapper json) {
        this.repo = repo;
        this.json = json;
    }

    // ── pages ───────────────────────────────────────────────────────────────

    @GetMapping("/pages")
    @Operation(summary = "Display pages",
               description = "Everyone's pages, most recently updated first. Anyone signed in may list and open any page.")
    public List<Page> pages(
            @Parameter(description = "Only pages owned by this account id") @RequestParam(required = false) Long owner,
            @Parameter(description = "Only pages whose name contains this text (case-insensitive)")
            @RequestParam(required = false) String q) {
        return repo.pages(owner, q);
    }

    @GetMapping("/pages/{id}")
    @Operation(summary = "One display page", description = "404 if there is no such page.")
    public ResponseEntity<?> page(@PathVariable long id) {
        return repo.page(id).<ResponseEntity<?>>map(ResponseEntity::ok)
                   .orElseGet(() -> error(HttpStatus.NOT_FOUND, "no page " + id));
    }

    @PostMapping("/pages")
    @Operation(summary = "Save a new page",
               description = "Body {name, params}: name 1-60 characters, params exactly 20 entries, each a "
                           + "downlinked parameter id or null. Owned by the signed-in user. 201 on success, "
                           + "400 bad input, 409 if you already have a page by that name.")
    public ResponseEntity<?> create(@AuthenticationPrincipal Jwt jwt, @RequestBody(required = false) JsonNode body) {
        return create(CurrentUser.of(jwt), body);
    }

    ResponseEntity<?> create(CurrentUser me, JsonNode body) {
        PageInput in;
        try {
            in = pageInput(body);
        } catch (IllegalArgumentException e) {
            return error(HttpStatus.BAD_REQUEST, e.getMessage());
        }
        try {
            return ResponseEntity.status(HttpStatus.CREATED).body(repo.createPage(me.accountId(), in.name, in.params));
        } catch (NameClashException e) {
            return error(HttpStatus.CONFLICT, e.getMessage());
        }
    }

    @PutMapping("/pages/{id}")
    @Operation(summary = "Overwrite a page",
               description = "Owner only. Same body and validation as creating. 403 if you are not the owner "
                           + "(use Save as, i.e. POST, to copy it instead), 409 on a name clash.")
    public ResponseEntity<?> update(@AuthenticationPrincipal Jwt jwt, @PathVariable long id,
                                    @RequestBody(required = false) JsonNode body) {
        return update(CurrentUser.of(jwt), id, body);
    }

    ResponseEntity<?> update(CurrentUser me, long id, JsonNode body) {
        Optional<Page> existing = repo.page(id);
        if (existing.isEmpty()) return error(HttpStatus.NOT_FOUND, "no page " + id);
        if (!Objects.equals(existing.get().ownerId(), me.accountId())) {
            return error(HttpStatus.FORBIDDEN, "page " + id + " belongs to " + existing.get().ownerName());
        }
        PageInput in;
        try {
            in = pageInput(body);
        } catch (IllegalArgumentException e) {
            return error(HttpStatus.BAD_REQUEST, e.getMessage());
        }
        try {
            return ResponseEntity.ok(repo.updatePage(id, in.name, in.params));
        } catch (NameClashException e) {
            return error(HttpStatus.CONFLICT, e.getMessage());
        }
    }

    @DeleteMapping("/pages/{id}")
    @Operation(summary = "Delete a page",
               description = "Its owner or an admin. 204 on success, 403 for anyone else, 404 if absent.")
    public ResponseEntity<?> delete(@AuthenticationPrincipal Jwt jwt, @PathVariable long id) {
        return delete(CurrentUser.of(jwt), id);
    }

    ResponseEntity<?> delete(CurrentUser me, long id) {
        Optional<Page> existing = repo.page(id);
        if (existing.isEmpty()) return error(HttpStatus.NOT_FOUND, "no page " + id);
        if (!Objects.equals(existing.get().ownerId(), me.accountId()) && !me.has(Permission.DISPLAY_DELETE_ANY)) {
            return error(HttpStatus.FORBIDDEN, "page " + id + " belongs to " + existing.get().ownerName());
        }
        repo.deletePage(id);
        return ResponseEntity.noContent().build();
    }

    // ── workspace ───────────────────────────────────────────────────────────

    @GetMapping("/me/workspace")
    @Operation(summary = "Your saved window layout",
               description = "{layout, updatedAt}; both null if nothing has been saved yet.")
    public Map<String, Object> workspace(@AuthenticationPrincipal Jwt jwt) {
        return workspace(CurrentUser.of(jwt));
    }

    Map<String, Object> workspace(CurrentUser me) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("layout", null);
        m.put("updatedAt", null);
        repo.workspace(me.accountId()).ifPresent(w -> {
            try {
                m.put("layout", json.readTree(w.layout()));
            } catch (Exception e) {
                m.put("layout", null);      // unreadable - start from an empty desk
            }
            m.put("updatedAt", w.updatedAt());
        });
        return m;
    }

    @PutMapping("/me/workspace")
    @Operation(summary = "Save your window layout",
               description = "Body {layout}: any JSON up to 64 KB, stored as is. 204 on success.")
    public ResponseEntity<?> saveWorkspace(@AuthenticationPrincipal Jwt jwt, @RequestBody(required = false) JsonNode body) {
        return saveWorkspace(CurrentUser.of(jwt), body);
    }

    ResponseEntity<?> saveWorkspace(CurrentUser me, JsonNode body) {
        JsonNode layout = body == null ? null : body.get("layout");
        if (layout == null || layout.isNull()) return error(HttpStatus.BAD_REQUEST, "layout is required");
        String text = layout.toString();
        if (text.getBytes(StandardCharsets.UTF_8).length > LAYOUT_MAX_BYTES) {
            return error(HttpStatus.BAD_REQUEST, "layout is larger than 64 KB");
        }
        repo.saveWorkspace(me.accountId(), text);
        return ResponseEntity.noContent().build();
    }

    /** A body that is not JSON at all gets the same {error} shape as every other 400. */
    @ExceptionHandler(HttpMessageNotReadableException.class)
    public ResponseEntity<?> unreadable(HttpMessageNotReadableException e) {
        return error(HttpStatus.BAD_REQUEST, "request body is not valid JSON");
    }

    // ── helpers ─────────────────────────────────────────────────────────────

    private record PageInput(String name, List<Integer> params) { }

    /** Validate {name, params}; the message names the first thing wrong. */
    private PageInput pageInput(JsonNode body) {
        String name = text(body, "name");
        if (name == null || name.isEmpty() || name.length() > PAGE_NAME_MAX) {
            throw new IllegalArgumentException("name must be 1-" + PAGE_NAME_MAX + " characters");
        }
        JsonNode arr = body.get("params");
        if (arr == null || !arr.isArray() || arr.size() != DisplayRepo.SLOTS) {
            throw new IllegalArgumentException("params must be an array of exactly " + DisplayRepo.SLOTS
                                               + " entries (parameter id or null)");
        }
        List<Integer> params = new ArrayList<>(DisplayRepo.SLOTS);
        for (int i = 0; i < arr.size(); i++) {
            JsonNode n = arr.get(i);
            if (n.isNull()) { params.add(null); continue; }
            if (!n.isIntegralNumber() || !n.canConvertToInt()) {
                throw new IllegalArgumentException("params[" + i + "] is not a parameter id: " + n);
            }
            int pid = n.intValue();
            if (!repo.isDownlinked(pid)) {
                throw new IllegalArgumentException("parameter " + pid + " (params[" + i
                                                   + "]) is not downlinked by the flown LUT");
            }
            params.add(pid);
        }
        return new PageInput(name, params);
    }

    private static String text(JsonNode body, String field) {
        if (body == null) return null;
        JsonNode n = body.get(field);
        return n == null || !n.isTextual() ? null : n.asText().trim();
    }

    private static ResponseEntity<?> error(HttpStatus status, String message) {
        return ResponseEntity.status(status).body(Map.of("error", message));
    }
}
