// SPDX-License-Identifier: Apache-2.0
// XLink-S product-key catalog.
//
// The vendor ships one product-key file per XLink transceiver.  Each file
// contains a metadata header (serial number, MAC, unit type) followed by 1..N
// blocks — one per supported (XmtSymbFreq, RcvSymbFreq) configuration.  Each
// block lists per-config capability flags (XmtCh0_En, XmtOffsetMod, RsMode,
// etc.) and finally the unique 128-bit key that authorises that configuration.
//
// The operator picks the (Xmt, Rcv) profile they want to run; the GS then
// automatically uses the matching key when talking to the transceiver.
// Attempting to run a service with the wrong key produces an
// "ACCESS_VIOLATION" NAK plus exponential backoff (per spec §3).

#pragma once
#include "subsystems/xlink_codec.hpp"
#include <optional>
#include <string>
#include <vector>

namespace xdos::xlink {

// One config-key pair from the key file.  The catalog is a vector of these.
struct KeyEntry {
    int xmt_symb_freq_khz = 0;    // e.g. 2048
    int rcv_symb_freq_khz = 0;    // e.g.  512
    xlink::codec::ProductKey key{};
    std::vector<std::string> features;  // "XmtCh0_En", "Use SPI", "XmtOffsetMod", ...
    std::string raw_key_text;     // original "ad563c56-ea8b..." for display
};

struct KeyCatalog {
    std::string serial_number;    // e.g. "2223010151"
    std::string mac;              // e.g. "00-05-43-01-52-AD"
    std::string unit_model;       // e.g. "XLK-SATX18C FM"
    std::vector<KeyEntry> entries;

    // Parse a vendor key file.  Returns std::nullopt on I/O failure or if the
    // file contains no valid entries.
    static std::optional<KeyCatalog> load(const std::string& path);

    // Find the entry matching a specific (xmt_khz, rcv_khz) pair.  Returns
    // nullptr if the configuration isn't in the catalog.
    const KeyEntry* find(int xmt_khz, int rcv_khz) const;
};

} // namespace xdos::xlink
