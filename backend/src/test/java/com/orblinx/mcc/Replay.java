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

/**
 * Replays a captured XMCC byte stream through the real decode path, so tests
 * exercise the same code the live feed does rather than a parallel copy.
 */
public final class Replay {

    /** One decoded reading, keyed the way the reference tool prints it. */
    public record Row(long masterFrame, int subframe, String tier, String entry,
                      String name, String formatted) {}

    /** The decode schema as production loads it: xdos/ into SQLite, then read back. */
    public static SchemaRepo schema() {
        return new SchemaRepo(TestDb.jdbc(), TestDb.decode());
    }

    /** The mcc-app repository root: the directory holding backend/ and xdos/. */
    public static Path repoRoot() {
        Path p = Path.of("").toAbsolutePath();       // .../mcc-app/backend
        while (p != null && !(Files.isDirectory(p.resolve("backend")) && Files.isDirectory(p.resolve("xdos")))) p = p.getParent();
        if (p == null) throw new IllegalStateException("cannot locate the mcc-app root (backend/ and xdos/)");
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
