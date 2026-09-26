package com.orblinx.mcc;

import com.orblinx.mcc.decode.*;
import com.orblinx.mcc.store.SchemaRepo;
import com.orblinx.mcc.wire.*;

import java.io.DataInputStream;
import java.io.EOFException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import org.sqlite.SQLiteDataSource;

/**
 * Replays a captured XMCC byte stream through the real decode path, so tests
 * exercise the same code the live feed does rather than a parallel copy.
 */
public final class Replay {

    /** One decoded reading, keyed the way the reference tool prints it. */
    public record Row(long masterFrame, int subframe, String tier, String entry,
                      String name, String formatted) {}

    public static SchemaRepo schema() {
        Path db = repoRoot().resolve("mcc-app/mcc.db");
        if (!Files.exists(db)) {
            throw new IllegalStateException("build the schema first: python3 schema/build_schema.py");
        }
        SQLiteDataSource ds = new SQLiteDataSource();
        ds.setUrl("jdbc:sqlite:" + db);
        return new SchemaRepo(ds);
    }

    public static Path repoRoot() {
        Path p = Path.of("").toAbsolutePath();       // .../mcc-app/backend
        while (p != null && !Files.exists(p.resolve("updated xdos"))) p = p.getParent();
        if (p == null) throw new IllegalStateException("cannot locate the workspace root");
        return p;
    }

    public static List<Row> decode(Path capture, SchemaRepo schema) throws Exception {
        ParameterDecoder decoder = new ParameterDecoder(schema.enums());
        TierAssembler assembler = new TierAssembler();
        List<Row> rows = new ArrayList<>();

        try (InputStream raw = Files.newInputStream(capture)) {
            DataInputStream in = new DataInputStream(raw);
            byte[] header = new byte[XmccHeader.LENGTH];
            while (true) {
                try {
                    in.readFully(header);
                } catch (EOFException eof) {
                    break;
                }
                XmccHeader h = XmccHeader.parse(header);
                byte[] payload = new byte[h.payloadLength];
                in.readFully(payload);
                Subframe sf = new Subframe(h.masterFrame, h.subframe, h.spacecraftTime,
                                           header.clone(), payload);

                for (Tier tier : Tier.values()) {
                    byte[] block = assembler.feed(tier, sf);
                    if (block == null) continue;
                    for (EntryDef e : schema.entriesOf(tier)) {
                        MessageDef msg = schema.message(e.messageId());
                        for (DecodedValue v : decoder.decodeEntry(e, msg, block, Quality.OK)) {
                            rows.add(new Row(sf.masterFrame(), sf.subframe(), tier.name(),
                                             e.tcId(), v.name(), v.formatted()));
                        }
                    }
                }
            }
        }
        return rows;
    }
}
