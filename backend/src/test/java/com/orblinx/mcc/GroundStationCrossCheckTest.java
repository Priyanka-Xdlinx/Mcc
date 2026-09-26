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
 * Level 3b - compare against the ground station's OWN decode of the same run.
 *
 * xdos_tm_view wrote RateGroup_TM_09_21_16_34.log while the capture replayed in
 * these tests was being taken, so the two describe identical bytes. Agreement
 * here is meaningful precisely because nothing was shared between the two
 * implementations.
 *
 * Two divergences are expected and are the point of the exercise - the ground
 * station's display is wrong in both, and reproducing them would mean copying
 * defects into the MCC:
 *
 *   KD-1  Every XLINK-S response begins with product_key (16 B) + service_event
 *         (4 B). The LUT builder computed its picks in MIB coordinates, which
 *         include that prefix; the display path then prepends 20 more zero bytes
 *         (src/cli/tm_view_main.cpp:136) and reads everything 20 bytes early.
 *   KD-2  All-0xFF means "the subsystem did not answer", but the display decides
 *         that per entry rather than per parameter, so a single 0xFF field inside
 *         an otherwise healthy reply renders as -1 instead of "no answer".
 *
 * And one that is cosmetic rather than wrong:
 *
 *   KD-3  flag_labels_for() in src/cli/value_format.cpp appends an operator label
 *         to single-byte UINT fields whose name looks like a flag, so 9 prints as
 *         "9 (set)". The number is identical; only a suffix is added. The vendor's
 *         own Python reference does not do this, so adopting it would break parity
 *         with that tool. The check below therefore requires the numeric part to
 *         match exactly and only a parenthesised label to differ.
 */
class GroundStationCrossCheckTest {

    private static final Pattern FRAME =
            Pattern.compile("^==== Subframe\\s+\\S+ \\S+\\s+MF#(\\d+) slot (\\d+)/16.*$");
    private static final Pattern ENTRY =
            Pattern.compile("^\\s{2}\\[(VH|H|M|L)\\s*\\]\\s+(\\S+)\\s+(\\S+)\\s+(UP|DOWN|PARTIAL)\\s*$");
    private static final Pattern VALUE =
            Pattern.compile("^\\s{6}(\\S.*?)\\s+=\\s(.*?)\\s*$");

    private record Key(long mf, int sf, String entry, String name) {}

    @Test
    void differsFromTheGroundStationOnlyWhereTheGroundStationIsWrong() throws Exception {
        Path root = Replay.repoRoot();
        Path capture = root.resolve("mcc-app/testdata/live_capture.xmcc");
        Path gsLog = root.resolve("updated xdos/RateGroup_TM_09_21_16_34.log");
        assertTrue(Files.exists(gsLog), "missing ground station log: " + gsLog);

        SchemaRepo schema = Replay.schema();
        Map<Key, String> mine = new HashMap<>();
        for (Replay.Row r : Replay.decode(capture, schema)) {
            mine.put(new Key(r.masterFrame(), r.subframe(), r.entry(), r.name()), r.formatted());
        }
        Map<Key, String> theirs = parseGroundStationLog(gsLog);

        int compared = 0, agreed = 0;
        List<String> kd1 = new ArrayList<>(), kd2 = new ArrayList<>(),
                     kd3 = new ArrayList<>(), unexplained = new ArrayList<>();

        for (Map.Entry<Key, String> e : theirs.entrySet()) {
            String ours = mine.get(e.getKey());
            if (ours == null) continue;               // outside the captured window
            compared++;
            if (ours.equals(e.getValue())) { agreed++; continue; }

            String entry = e.getKey().entry();
            String detail = String.format("%s / %s   gs=%s  java=%s",
                    entry, e.getKey().name(), e.getValue(), ours);
            if (entry.startsWith("XL-")) {
                kd1.add(detail);
            } else if (ours.startsWith("-- (no answer)")) {
                kd2.add(detail);
            } else if (e.getValue().startsWith(ours + " (") && e.getValue().endsWith(")")) {
                // Same number, with an operator flag label appended.
                kd3.add(detail);
            } else {
                unexplained.add(detail);
            }
        }

        assertTrue(compared > 1000, "too few overlapping readings to be meaningful: " + compared);

        System.out.printf("L3b: %d readings compared against the ground station%n", compared);
        System.out.printf("     %d identical%n", agreed);
        System.out.printf("     %d differ on XLINK offsets (KD-1)%n", kd1.size());
        System.out.printf("     %d differ on the 0xFF sentinel (KD-2)%n", kd2.size());
        System.out.printf("     %d differ only by a cosmetic flag label (KD-3)%n", kd3.size());
        if (!kd2.isEmpty()) System.out.println("     KD-2 e.g. " + kd2.get(0));
        if (!kd3.isEmpty()) System.out.println("     KD-3 e.g. " + kd3.get(0));

        assertTrue(unexplained.isEmpty(),
                unexplained.size() + " difference(s) are NOT on the known-divergence register:\n"
                + String.join("\n", unexplained.subList(0, Math.min(20, unexplained.size()))));

        assertFalse(kd1.isEmpty(),
                "expected the XLINK offset divergence to show up; if it vanished, either the "
                + "ground station was fixed or this decoder has started copying its bug");
    }

    private static Map<Key, String> parseGroundStationLog(Path p) throws Exception {
        Map<Key, String> out = new HashMap<>();
        long mf = -1;
        int sf = -1;
        String entry = null;
        for (String line : Files.readAllLines(p)) {
            Matcher m = FRAME.matcher(line);
            if (m.matches()) {
                mf = Long.parseLong(m.group(1));
                sf = Integer.parseInt(m.group(2));
                entry = null;
                continue;
            }
            m = ENTRY.matcher(line);
            if (m.matches()) { entry = m.group(2); continue; }
            if (line.startsWith("    HEX:")) continue;
            m = VALUE.matcher(line);
            if (m.matches() && entry != null) {
                out.put(new Key(mf, sf, entry, m.group(1)), m.group(2));
            }
        }
        return out;
    }
}
