package com.orblinx.mcc.api;

import com.orblinx.mcc.store.DisplayRepo;
import com.orblinx.mcc.store.DisplayRepo.NameClashException;
import com.orblinx.mcc.store.DisplayRepo.Page;
import com.orblinx.mcc.store.DisplayRepo.User;

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
import org.springframework.web.bind.annotation.*;

/**
 * Display pages: named sets of twenty parameter slots that operators open as
 * floating windows, plus each operator's saved window layout.
 *
 * There is no login. The acting operator is whatever id the browser sends in
 * X-MCC-User, which is enough to stop one operator overwriting another's page
 * by accident - it is not, and is not meant to be, access control.
 */
@RestController
@RequestMapping("/api")
@Tag(name = "Display pages")
public class DisplayController {

    static final String USER_HEADER = "X-MCC-User";
    private static final int USER_NAME_MAX = 40;
    private static final int PAGE_NAME_MAX = 60;
    private static final int LAYOUT_MAX_BYTES = 64 * 1024;

    private final DisplayRepo repo;
    private final ObjectMapper json;

    public DisplayController(DisplayRepo repo, ObjectMapper json) {
        this.repo = repo;
        this.json = json;
    }

    // ── users ───────────────────────────────────────────────────────────────

    @GetMapping("/users")
    @Operation(summary = "Operators",
               description = "Every operator who has picked a name, with how many pages each owns, sorted by name.")
    public List<User> users() {
        return repo.users();
    }

    @PostMapping("/users")
    @Operation(summary = "Get or create an operator by name",
               description = "Body {name}. Matched case-insensitively; created if new. 200 either way, "
                           + "400 if the trimmed name is empty or longer than 40 characters.")
    public ResponseEntity<?> pickUser(@RequestBody(required = false) JsonNode body) {
        String name = text(body, "name");
        if (name == null || name.isEmpty() || name.length() > USER_NAME_MAX) {
            return error(HttpStatus.BAD_REQUEST, "name must be 1-" + USER_NAME_MAX + " characters");
        }
        return ResponseEntity.ok(repo.userByName(name));
    }

    // ── pages ───────────────────────────────────────────────────────────────

    @GetMapping("/pages")
    @Operation(summary = "Display pages",
               description = "Every operator's pages, most recently updated first. Anyone may list and open any page.")
    public List<Page> pages(
            @Parameter(description = "Only pages owned by this user id") @RequestParam(required = false) Long owner,
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
                           + "downlinked parameter id or null. Owned by the X-MCC-User. 201 on success, "
                           + "400 bad input, 401 no or unknown user, 409 if that owner already has the name.")
    public ResponseEntity<?> create(@RequestHeader(value = USER_HEADER, required = false) String userHeader,
                                    @RequestBody(required = false) JsonNode body) {
        Long user = actingUser(userHeader);
        if (user == null) return error(HttpStatus.UNAUTHORIZED, "no or unknown " + USER_HEADER);
        PageInput in;
        try {
            in = pageInput(body);
        } catch (IllegalArgumentException e) {
            return error(HttpStatus.BAD_REQUEST, e.getMessage());
        }
        try {
            return ResponseEntity.status(HttpStatus.CREATED).body(repo.createPage(user, in.name, in.params));
        } catch (NameClashException e) {
            return error(HttpStatus.CONFLICT, e.getMessage());
        }
    }

    @PutMapping("/pages/{id}")
    @Operation(summary = "Overwrite a page",
               description = "Owner only. Same body and validation as creating. 403 if the X-MCC-User is not "
                           + "the owner (use Save as, i.e. POST, to copy it instead), 409 on a name clash.")
    public ResponseEntity<?> update(@RequestHeader(value = USER_HEADER, required = false) String userHeader,
                                    @PathVariable long id,
                                    @RequestBody(required = false) JsonNode body) {
        Long user = actingUser(userHeader);
        if (user == null) return error(HttpStatus.UNAUTHORIZED, "no or unknown " + USER_HEADER);
        Optional<Page> existing = repo.page(id);
        if (existing.isEmpty()) return error(HttpStatus.NOT_FOUND, "no page " + id);
        if (existing.get().ownerId() != user) {
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
    @Operation(summary = "Delete a page", description = "Owner only. 204 on success, 403 if not the owner, 404 if absent.")
    public ResponseEntity<?> delete(@RequestHeader(value = USER_HEADER, required = false) String userHeader,
                                    @PathVariable long id) {
        Long user = actingUser(userHeader);
        if (user == null) return error(HttpStatus.UNAUTHORIZED, "no or unknown " + USER_HEADER);
        Optional<Page> existing = repo.page(id);
        if (existing.isEmpty()) return error(HttpStatus.NOT_FOUND, "no page " + id);
        if (existing.get().ownerId() != user) {
            return error(HttpStatus.FORBIDDEN, "page " + id + " belongs to " + existing.get().ownerName());
        }
        repo.deletePage(id);
        return ResponseEntity.noContent().build();
    }

    // ── workspaces ──────────────────────────────────────────────────────────

    @GetMapping("/users/{id}/workspace")
    @Operation(summary = "An operator's saved window layout",
               description = "{layout, updatedAt}; both null if nothing has been saved yet. 404 for an unknown user.")
    public ResponseEntity<?> workspace(@PathVariable long id) {
        if (repo.user(id).isEmpty()) return error(HttpStatus.NOT_FOUND, "no user " + id);
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("layout", null);
        m.put("updatedAt", null);
        repo.workspace(id).ifPresent(w -> {
            try {
                m.put("layout", json.readTree(w.layout()));
            } catch (Exception e) {
                m.put("layout", null);      // unreadable - start from an empty desk
            }
            m.put("updatedAt", w.updatedAt());
        });
        return ResponseEntity.ok(m);
    }

    @PutMapping("/users/{id}/workspace")
    @Operation(summary = "Save an operator's window layout",
               description = "Body {layout}: any JSON up to 64 KB, stored as is. Only the operator "
                           + "themselves may save it (403 otherwise). 204 on success.")
    public ResponseEntity<?> saveWorkspace(@RequestHeader(value = USER_HEADER, required = false) String userHeader,
                                           @PathVariable long id,
                                           @RequestBody(required = false) JsonNode body) {
        Long user = actingUser(userHeader);
        if (user == null) return error(HttpStatus.UNAUTHORIZED, "no or unknown " + USER_HEADER);
        if (user != id) return error(HttpStatus.FORBIDDEN, "you can only save your own workspace");
        JsonNode layout = body == null ? null : body.get("layout");
        if (layout == null || layout.isNull()) return error(HttpStatus.BAD_REQUEST, "layout is required");
        String text = layout.toString();
        if (text.getBytes(StandardCharsets.UTF_8).length > LAYOUT_MAX_BYTES) {
            return error(HttpStatus.BAD_REQUEST, "layout is larger than 64 KB");
        }
        repo.saveWorkspace(id, text);
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

    /** The id in X-MCC-User if it names a known operator, else null. */
    private Long actingUser(String header) {
        if (header == null) return null;
        try {
            long id = Long.parseLong(header.trim());
            return repo.user(id).isPresent() ? id : null;
        } catch (NumberFormatException e) {
            return null;
        }
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
