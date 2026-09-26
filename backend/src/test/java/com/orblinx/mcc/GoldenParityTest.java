package com.orblinx.mcc;

import com.orblinx.mcc.store.SchemaRepo;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Level 3a - the decoder must agree, value for value, with the ground station's
 * own reference decoder over the same captured bytes.
 *
 * The golden file is produced by the vendor's tools/mcc_decode.py in --compat
 * mib mode, which follows the MIB and the interface spec. Its default --compat
 * gs mode instead reproduces the operator display, including the display's
 * 20-byte XLINK offset bug; matching that would mean copying a defect.
 */
class GoldenParityTest {

    private static final Pattern FRAME =
            Pattern.compile("^==== MF#(\\d+) SF (\\d+)/16\\s+.*====\\s*$");
    private static final Pattern ENTRY =
            Pattern.compile("^\\s{2}\\[(VH|H|M|L)\\]\\s+(\\S+)\\s+(\\S+)\\s+(.*?)\\s*$");
    private static final Pattern VALUE =
            Pattern.compile("^\\s{6}(\\S.*?)\\s+=\\s(.*?)\\s*$");

    @Test
    void javaDecoderMatchesReferenceDecoder() throws Exception {
        Path root = Replay.repoRoot();
        Path capture = root.resolve("mcc-app/testdata/live_capture.xmcc");
        Path golden = root.resolve("mcc-app/testdata/golden_mib.txt");
        assertTrue(Files.exists(capture), "missing capture: " + capture);
        assertTrue(Files.exists(golden), "missing golden file: " + golden);

        SchemaRepo schema = Replay.schema();
        List<Replay.Row> mine = Replay.decode(capture, schema);
        List<Replay.Row> theirs = parseGolden(golden);

        assertFalse(theirs.isEmpty(), "golden file parsed to nothing");
        assertEquals(theirs.size(), mine.size(),
                "different number of decoded readings: reference " + theirs.size()
                + ", java " + mine.size());

        List<String> diffs = new ArrayList<>();
        for (int i = 0; i < theirs.size() && diffs.size() < 25; i++) {
            Replay.Row a = theirs.get(i), b = mine.get(i);
            if (!a.equals(b)) {
                diffs.add(String.format("MF#%d SF%d [%s] %s / %s%n    reference: %s%n    java     : %s",
                        a.masterFrame(), a.subframe(), a.tier(), a.entry(), a.name(),
                        a.formatted(), b.name().equals(a.name()) ? b.formatted()
                                        : "(" + b.name() + ") " + b.formatted()));
            }
        }
        assertTrue(diffs.isEmpty(),
                diffs.size() + " reading(s) differ from the reference decoder:\n"
                + String.join("\n", diffs));

        System.out.printf("L3a: %d readings identical to the reference decoder%n", mine.size());
    }

    private static List<Replay.Row> parseGolden(Path p) throws Exception {
        List<Replay.Row> out = new ArrayList<>();
        long mf = -1;
        int sf = -1;
        String tier = null, entry = null;
        for (String line : Files.readAllLines(p)) {
            Matcher m = FRAME.matcher(line);
            if (m.matches()) {
                mf = Long.parseLong(m.group(1));
                sf = Integer.parseInt(m.group(2));
                continue;
            }
            m = ENTRY.matcher(line);
            if (m.matches()) {
                tier = m.group(1);
                entry = m.group(2);
                continue;
            }
            m = VALUE.matcher(line);
            if (m.matches() && entry != null) {
                out.add(new Replay.Row(mf, sf, tier, entry, m.group(1), m.group(2)));
            }
        }
        return out;
    }
}
