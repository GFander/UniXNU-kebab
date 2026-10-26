#!/usr/bin/env python3
"""Extract the OnePlus 8T (kebab) Samsung AMB655X DSI initialisation sequence.

kebab has no ``dsi_panel_kebab.c``: the vendor kernel keeps the panel data in
the device tree, in a ``qcom,mdss-dsi-*`` node that
``techpack/display/msm/dsi/dsi_panel.c`` parses at probe time.  This script
reads that node, decodes every command packet and writes a JSON table plus a
C header.

Source (pinned, one file, no kernel tree is cloned):

  OnePlusOSS/android_kernel_oneplus_sm8250
  commit 98719830b0de5584f5588bbe9f4c8304d97a64c6 ("Synchronize codes for
  OnePlus 8T Oxygen OS 11.0.13.13.KB05AA ...")
  arch/arm64/boot/dts/vendor/qcom/dsi-panel-samsung_amb655x_fhd_dsc.dtsi
  27742 bytes, sha256 7d7118f24bc5a8d99c66da3e49b2f3757ce17e0c258c7edfddec386d1257fa98

Wire format inside ``qcom,mdss-dsi-on-command = [ ... ]`` is the one parsed by
``dsi_panel_create_cmd_packets()`` in the same tree:

  byte 0     MIPI DSI data type (0x05 DCS short write, 0x15 DCS short write
             with one parameter, 0x39 DCS long write, 0x29 generic long write,
             0x03/0x13/0x23 generic short write, 0x06 DCS read, ...)
  byte 1     last-command flag (1 -> MIPI_DSI_MSG_LASTCOMMAND)
  byte 2     virtual channel
  byte 3     ack request (1 -> MIPI_DSI_MSG_REQ_ACK)
  byte 4     wait in milliseconds, applied *after* the packet
             (dsi_panel_create_cmd_packets sets both msg.wait_ms and
             post_wait_ms from it; dsi_panel_tx_cmd_set() sleeps after every
             command with usleep_range(wait_ms * 1000, wait_ms * 1000 + 10))
  byte 5:6   payload length, big endian
  byte 7..   payload; payload[0] is the register for DCS writes, the rest are
             the parameters

Usage:

  scripts/extract-panel-init.py
        fetch (cached under ~/.cache/kebab-panel), extract, write
        scripts/panel/kebab-panel-amb655x.json and .h, print a summary

  scripts/extract-panel-init.py --source /path/to/dsi-panel-....dtsi
  scripts/extract-panel-init.py --dtb /path/to/kebab.dtb
        additionally cross-check the decoded init stream against the compiled
        on-command blob of a stock kebab device tree (independent pipeline)
  scripts/extract-panel-init.py --provenance
        print every decoded step next to the source line it came from
  scripts/extract-panel-init.py --check
        regenerate in memory and compare with the committed outputs
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import urllib.request
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

PANEL_NODE = "qcom,mdss_dsi_samsung_amb655x_dsc_cmd"
PANEL_LABEL = "dsi_samsung_amb655x_dsc_cmd"

SRC_REPO = "OnePlusOSS/android_kernel_oneplus_sm8250"
SRC_COMMIT = "98719830b0de5584f5588bbe9f4c8304d97a64c6"
SRC_PATH = "arch/arm64/boot/dts/vendor/qcom/dsi-panel-samsung_amb655x_fhd_dsc.dtsi"
SRC_BYTES = 27742
SRC_SHA256 = "7d7118f24bc5a8d99c66da3e49b2f3757ce17e0c258c7edfddec386d1257fa98"
SRC_URL = "https://raw.githubusercontent.com/%s/%s/%s" % (SRC_REPO, SRC_COMMIT, SRC_PATH)

DEFAULT_JSON = REPO_ROOT / "scripts" / "panel" / "kebab-panel-amb655x.json"
DEFAULT_HEADER = REPO_ROOT / "scripts" / "panel" / "kebab-panel-amb655x.h"

# MIPI DSI data types.  The vendor driver takes the numeric values from
# drm/drm_mipi_dsi.h; only 0x05, 0x15 and 0x39 occur in this panel file.
DTYPES = {
    0x01: "dsi_sync_event",
    0x02: "dsi_eotp",
    0x03: "generic_short_write_0",
    0x04: "generic_read_0",
    0x05: "dcs_short_write",
    0x06: "dcs_read",
    0x08: "generic_long_write",
    0x09: "null_packet",
    0x0A: "generic_read_1",
    0x0B: "generic_long_read",
    0x12: "generic_short_write_1",
    0x13: "generic_short_write_1",
    0x14: "generic_read_1",
    0x15: "dcs_short_write_param",
    0x16: "dcs_read_1",
    0x18: "generic_long_write_1",
    0x19: "generic_long_read_1",
    0x21: "dcs_long_write",
    0x22: "generic_short_write_2",
    0x23: "generic_short_write_2",
    0x24: "generic_read_2",
    0x28: "generic_long_write_2",
    0x29: "generic_long_write",
    0x2A: "generic_long_read_2",
    0x32: "dcs_long_write_2",
    0x37: "generic_long_write",
    0x38: "generic_long_read",
    0x39: "dcs_long_write",
    0x3C: "dcs_read",
}

# Short names used in the JSON and in C identifiers.
SET_NAMES = {
    "qcom,mdss-dsi-on-command": "on",
    "qcom,mdss-dsi-off-command": "off",
    "qcom,mdss-dsi-timing-switch-command": "switch",
    "qcom,mdss-dsi-panel-command": "panel",
    "qcom,mdss-dsi-panel-gamma-change-write-command": "gamma_change",
    "qcom,mdss-dsi-seed-command": "seed",
    "qcom,mdss-dsi-panel-level2-key-enable-command": "level2_key_enable",
    "qcom,mdss-dsi-panel-level2-key-disable-command": "level2_key_disable",
    "qcom,mdss-dsi-panel-register-read-command": "register_read",
    "qcom,mdss-dsi-panel-aod-on-command-1": "aod_on_1",
    "qcom,mdss-dsi-panel-aod-on-command-3": "aod_on_3",
    "qcom,mdss-dsi-panel-aod-on-command-5": "aod_on_5",
    "qcom,mdss-dsi-panel-aod-off-command": "aod_off",
    "qcom,mdss-dsi-panel-hbm-on-command-4": "hbm_on_4",
    "qcom,mdss-dsi-panel-hbm-on-command-5": "hbm_on_5",
    "qcom,mdss-dsi-panel-hbm-off-command": "hbm_off",
    "qcom,mdss-dsi-panel-hbm-brightness-on-command": "hbm_brightness_on",
    "qcom,mdss-dsi-panel-hbm-brightness-off-command": "hbm_brightness_off",
}

# Command sets copied into the C header as ready-to-send streams.
HEADER_SETS = ("on", "off", "switch", "gamma_change", "seed")


class ExtractError(Exception):
    pass


# --------------------------------------------------------------------------
# DTS text helpers


def line_of(text: str, pos: int) -> int:
    return text.count("\n", 0, pos) + 1


def strip_comments(text: str) -> str:
    """Blank out C and C++ comments while preserving offsets and newlines."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if c == "\n" else " " for c in text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def brace_match(text: str, open_idx: int) -> int:
    depth = 0
    for i in range(open_idx, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    raise ExtractError("unbalanced braces at offset %d" % open_idx)


def scan_nodes(text: str):
    """Yield (name, body_start, body_end, line) for every ``name { ... }``."""
    for m in re.finditer(r"([A-Za-z0-9_,.@\-]+)\s*\{", text):
        open_idx = m.end() - 1
        try:
            close = brace_match(text, open_idx)
        except ExtractError:
            continue
        yield m.group(1), open_idx + 1, close, line_of(text, m.start())


def parse_cell_groups(value: str):
    """Turn ``<1 10>, <0 1>, <1 10>`` / ``<1080>`` into nested lists."""
    groups = []
    for g in re.finditer(r"<([^>]*)>", value):
        items = []
        for tok in g.group(1).split():
            if tok.startswith("&"):
                items.append(tok)
            elif tok.lower().startswith("0x"):
                items.append(int(tok, 16))
            else:
                try:
                    items.append(int(tok, 10))
                except ValueError:
                    items.append(tok)
        groups.append(items)
    return groups


def parse_byte_array(value: str):
    """Return [(byte, absolute offset inside ``value``)] for a ``[ .. ]``."""
    m = re.search(r"\[(.*)\]", value, re.S)
    if not m:
        raise ExtractError("not a byte array: %r" % value[:32])
    base = m.start(1)
    out = []
    for t in re.finditer(r"(?<![0-9A-Za-z_])[0-9A-Fa-f]{2}(?![0-9A-Za-z_])", m.group(1)):
        out.append((int(t.group(0), 16), base + t.start()))
    return out


def iter_items(text: str, start: int, end: int):
    """Yield (kind, name, value_start, value_end, line) inside a block."""
    i = start
    while i < end:
        while i < end and text[i].isspace():
            i += 1
        if i >= end or text[i] == "}":
            break
        j = i
        while j < end and (text[j].isalnum() or text[j] in "_,.-+#@"):
            j += 1
        name = text[i:j]
        if not name:
            i += 1
            continue
        k = j
        while k < end and text[k].isspace():
            k += 1
        if k < end and text[k] == "{":
            close = brace_match(text, k)
            yield "node", name, k + 1, close, line_of(text, i)
            i = close + 1
            continue
        if k < end and text[k] == "=":
            k += 1
            vstart = k
            angle = bracket = 0
            in_str = False
            while k < end:
                c = text[k]
                if in_str:
                    if c == '"':
                        in_str = False
                elif c == '"':
                    in_str = True
                elif c == "<":
                    angle += 1
                elif c == ">":
                    angle -= 1
                elif c == "[":
                    bracket += 1
                elif c == "]":
                    bracket -= 1
                elif c == ";" and angle == 0 and bracket == 0:
                    break
                k += 1
            yield "prop", name, vstart, k, line_of(text, i)
            i = k + 1
            continue
        if k < end and text[k] == ";":
            yield "bool", name, k, k, line_of(text, i)
            i = k + 1
            continue
        nl = text.find("\n", i)
        i = end if nl < 0 else nl + 1


def classify(value: str) -> str:
    v = value.strip()
    if v.startswith("["):
        return "bytes"
    if v.startswith("<"):
        return "cells"
    if v.startswith('"'):
        return "string"
    return "raw"


# --------------------------------------------------------------------------
# source handling


def fetch_source(cache: Path) -> Path:
    """Return the pinned source file, downloading it once into ``cache``."""
    if cache.exists():
        data = cache.read_bytes()
        if len(data) == SRC_BYTES and hashlib.sha256(data).hexdigest() == SRC_SHA256:
            return cache
        cache.unlink()
    cache.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(SRC_URL, headers={"User-Agent": "kebab-panel-extract/1"})
    try:
        with urllib.request.urlopen(req, timeout=120) as fh:
            data = fh.read()
    except Exception as exc:  # noqa: BLE001 - surfaced as an extraction error
        raise ExtractError("cannot fetch %s (%s); pass --source with a local copy" % (SRC_URL, exc))
    got = hashlib.sha256(data).hexdigest()
    if len(data) != SRC_BYTES or got != SRC_SHA256:
        raise ExtractError(
            "fetched file does not match the pinned size/hash (%d bytes, sha256 %s)" % (len(data), got)
        )
    cache.write_bytes(data)
    return cache


# --------------------------------------------------------------------------
# command decoding


def decode_stream(raw: bytes, lines):
    """Split a DTS command blob into 7-byte-header packets."""
    steps, errors, i = [], [], 0
    while i < len(raw):
        if len(raw) - i < 7:
            errors.append("trailing %d bytes at offset %d" % (len(raw) - i, i))
            break
        dtype, last, vc, ack, wait, hi, lo = raw[i : i + 7]
        n = (hi << 8) | lo
        if i + 7 + n > len(raw):
            errors.append("packet at offset %d overruns the array" % i)
            break
        payload = raw[i + 7 : i + 7 + n]
        steps.append(
            {
                "index": len(steps),
                "line": lines[i],
                "line_end": lines[i + 6 + n] if n else lines[i + 6],
                "dtype": dtype,
                "type": DTYPES.get(dtype, "unknown"),
                "last": bool(last),
                "vc": vc,
                "ack": bool(ack),
                "wait_ms": wait,
                "len": n,
                "reg": ("0x%02X" % payload[0]) if n else None,
                "payload_hex": payload.hex().upper(),
            }
        )
        i += 7 + n
    return steps, errors


def canonical(steps) -> bytes:
    """Checksum input: dtype, wait_ms, big-endian length, payload, in order."""
    out = bytearray()
    for s in steps:
        out += struct.pack(">BBH", s["dtype"], s["wait_ms"], s["len"])
        out += bytes.fromhex(s["payload_hex"])
    return bytes(out)


def wire_stream(steps) -> bytes:
    """Re-encode the packets exactly as the DTS array stores them."""
    out = bytearray()
    for s in steps:
        out += struct.pack(
            ">BBBBBH",
            s["dtype"],
            1 if s["last"] else 0,
            s["vc"],
            1 if s["ack"] else 0,
            s["wait_ms"],
            s["len"],
        )
        out += bytes.fromhex(s["payload_hex"])
    return bytes(out)


def crc32_hex(data: bytes) -> str:
    return "0x%08X" % (zlib.crc32(data) & 0xFFFFFFFF)


# --------------------------------------------------------------------------
# panel extraction


def cell0(groups, default=None):
    if groups and groups[0]:
        return groups[0][0]
    return default


def extract(path: Path):
    raw_text = path.read_text(encoding="utf-8")
    digest = hashlib.sha256(raw_text.encode("utf-8")).hexdigest()
    text = strip_comments(raw_text)

    panel_nodes = [n for n in scan_nodes(text) if n[0] == PANEL_NODE]
    if len(panel_nodes) != 1:
        raise ExtractError(
            "expected exactly one %s node, found %d" % (PANEL_NODE, len(panel_nodes))
        )
    _, pstart, pend, _ = panel_nodes[0]

    panel_props = {}
    for kind, name, vs, ve, line in iter_items(text, pstart, pend):
        if kind == "node":
            continue
        panel_props[name] = (
            kind,
            text[vs:ve].strip() if kind == "prop" else "",
            line,
        )

    def scalar(name, default=None):
        item = panel_props.get(name)
        if not item:
            return default
        kind, value, _ = item
        if kind == "bool":
            return True
        kind2 = classify(value)
        if kind2 == "cells":
            return parse_cell_groups(value)
        if kind2 == "string":
            return value.strip().strip('"')
        return value

    def scalar0(name, default=None):
        return cell0(scalar(name), default)

    nodes = list(scan_nodes(text))
    # The panel data is split: the timings live in the panel node, but the PHY
    # timing and topology overrides live in the &dsi_samsung_amb655x_dsc_cmd
    # nodes at the bottom of the file.  Accept blocks under either.
    panel_ranges = [(s, e) for name, s, e, _ in nodes if name in (PANEL_NODE, PANEL_LABEL)]
    timings = {}
    for name, s, e, _ in nodes:
        if name != "qcom,mdss-dsi-display-timings":
            continue
        if not any(rs <= s < rm for rs, rm in panel_ranges):
            continue
        for tname, ts, te, tline in nodes:
            if not (s <= ts < e):
                continue
            m = re.match(r"timing@(\d+)$", tname)
            if not m:
                continue
            idx = int(m.group(1))
            entry = timings.setdefault(idx, {"index": idx, "blocks": [], "props": {}})
            entry["blocks"].append(tline)
            for kind, pname, vs, ve, pline in iter_items(text, ts, te):
                if kind == "node":
                    continue
                if kind == "bool":
                    entry["props"][pname] = ("bool", "", pline)
                else:
                    entry["props"][pname] = (classify(text[vs:ve].strip()), text[vs:ve].strip(), pline)

    if not timings:
        raise ExtractError("no timing@N nodes found under %s" % PANEL_NODE)

    result_timings = []
    for idx in sorted(timings):
        props = timings[idx]["props"]

        def tcell(prop, default=None):
            item = props.get(prop)
            if not item:
                return default
            kind, value, _ = item
            if kind == "bool":
                return True
            return cell0(parse_cell_groups(value), default)

        timing = {
            "index": idx,
            "source_lines": timings[idx]["blocks"],
            "framerate": tcell("qcom,mdss-dsi-panel-framerate"),
            "clock_hz": tcell("qcom,mdss-dsi-panel-clockrate"),
            "width": tcell("qcom,mdss-dsi-panel-width"),
            "height": tcell("qcom,mdss-dsi-panel-height"),
            "mdp_transfer_time_us": tcell("qcom,mdss-mdp-transfer-time-us"),
            "h_front_porch": tcell("qcom,mdss-dsi-h-front-porch"),
            "h_back_porch": tcell("qcom,mdss-dsi-h-back-porch"),
            "h_pulse_width": tcell("qcom,mdss-dsi-h-pulse-width"),
            "h_sync_skew": tcell("qcom,mdss-dsi-h-sync-skew"),
            "v_front_porch": tcell("qcom,mdss-dsi-v-front-porch"),
            "v_back_porch": tcell("qcom,mdss-dsi-v-back-porch"),
            "v_pulse_width": tcell("qcom,mdss-dsi-v-pulse-width"),
            "phy_timings": None,
            "display_topology": None,
            "default_topology_index": None,
            "dsc": {},
            "command_sets": {},
        }

        if "qcom,mdss-dsi-panel-phy-timings" in props:
            timing["phy_timings"] = [b for b, _ in parse_byte_array(props["qcom,mdss-dsi-panel-phy-timings"][1])]
        if "qcom,display-topology" in props:
            timing["display_topology"] = parse_cell_groups(props["qcom,display-topology"][1])
        if "qcom,default-topology-index" in props:
            timing["default_topology_index"] = cell0(parse_cell_groups(props["qcom,default-topology-index"][1]))

        for dsc_key, out_key in (
            ("qcom,mdss-dsc-encoders", "encoders"),
            ("qcom,mdss-dsc-slice-width", "slice_width"),
            ("qcom,mdss-dsc-slice-height", "slice_height"),
            ("qcom,mdss-dsc-slice-per-pkt", "slice_per_pkt"),
            ("qcom,mdss-dsc-bit-per-component", "bit_per_component"),
            ("qcom,mdss-dsc-bit-per-pixel", "bit_per_pixel"),
        ):
            if dsc_key in props:
                value = cell0(parse_cell_groups(props[dsc_key][1]))
                if value is not None:
                    timing["dsc"][out_key] = value
        timing["dsc"]["block_prediction_enable"] = "qcom,mdss-dsc-block-prediction-enable" in props

        for pname, (kind, value, pline) in props.items():
            if kind != "bytes" or "command" not in pname or pname.endswith("-state"):
                continue
            toks = parse_byte_array(value)
            if not toks:
                continue
            raw = bytes(b for b, _ in toks)
            lines = [pline + value.count("\n", 0, off) for _, off in toks]
            steps, errors = decode_stream(raw, lines)
            if errors:
                raise ExtractError("%s: %s" % (pname, "; ".join(errors)))
            set_name = SET_NAMES.get(pname)
            if set_name is None:
                set_name = (
                    pname.replace("qcom,mdss-dsi-", "")
                    .replace("-command", "")
                    .replace("-", "_")
                )
            if wire_stream(steps) != raw:
                raise ExtractError("%s: re-encoded stream differs from the DTS array" % pname)
            canon = canonical(steps)
            entry = {
                "property": pname,
                "line": pline,
                "stream_bytes": len(raw),
                "count": len(steps),
                "payload_bytes": sum(s["len"] for s in steps),
                "delay_steps": [
                    {"index": s["index"], "wait_ms": s["wait_ms"], "register": s["reg"], "line": s["line"]}
                    for s in steps
                    if s["wait_ms"]
                ],
                "crc32": crc32_hex(canon),
                "sha256": hashlib.sha256(canon).hexdigest(),
                "stream_sha256": hashlib.sha256(raw).hexdigest(),
                "steps": steps,
            }
            state_prop = pname + "-state"
            if state_prop in props:
                entry["state"] = props[state_prop][1].strip('"')
            timing["command_sets"][set_name] = entry

        result_timings.append(timing)

    # Board data lives in the &dsi_samsung_amb655x_dsc_cmd overrides.
    board = {
        "props": {},
        "supply_entries": None,
        "gpios": {},
        "ulps": False,
        "clocks": [],
        "display_active": False,
    }
    for name, s, e, _ in nodes:
        if name != PANEL_LABEL:
            continue
        for kind, pname, vs, ve, pline in iter_items(text, s, e):
            if kind == "bool":
                if pname == "qcom,ulps-enabled":
                    board["ulps"] = True
                if pname == "qcom,dsi-display-active":
                    board["display_active"] = True
                continue
            if kind != "prop":
                continue
            value = text[vs:ve].strip()
            board["props"][pname] = value
            gpio_key = {
                "qcom,platform-reset-gpio": "reset",
                "qcom,platform-te-gpio": "te",
                "qcom,vddd-gpio": "vddd",
            }.get(pname)
            if gpio_key:
                groups = parse_cell_groups(value)
                if groups and len(groups[0]) >= 2:
                    board["gpios"][gpio_key] = groups[0][1]
            elif pname == "qcom,dsi-select-clocks":
                board["clocks"] = re.findall(r'"([^"]+)"', value)
            elif pname == "qcom,panel-supply-entries":
                groups = parse_cell_groups(value)
                if groups and groups[0]:
                    board["supply_entries"] = groups[0][0]

    def board_scalar(name, default=None):
        value = board["props"].get(name)
        if value is None:
            return default
        kind = classify(value)
        if kind == "cells":
            return cell0(parse_cell_groups(value), default)
        if kind == "string":
            return value.strip('"')
        return value

    def prop0(name, default=None):
        """Panel-node property, falling back to the &label override node."""
        if name in panel_props:
            return scalar(name, default)
        return board_scalar(name, default)

    def prop00(name, default=None):
        value = prop0(name, None)
        if value is None:
            return default
        return cell0(value, default) if isinstance(value, list) else value

    panel = {
        "node": PANEL_NODE,
        "label": PANEL_LABEL,
        "name": scalar("qcom,mdss-dsi-panel-name"),
        "manufacture": scalar("qcom,mdss-dsi-panel-manufacture"),
        "version": scalar("qcom,mdss-dsi-panel-version"),
        "type": scalar("qcom,mdss-dsi-panel-type"),
        "bpp": scalar0("qcom,mdss-dsi-bpp"),
        "lanes": sum(1 for i in range(4) if ("qcom,mdss-dsi-lane-%d-state" % i) in panel_props),
        "lane_map": scalar("qcom,mdss-dsi-lane-map"),
        "traffic_mode": scalar("qcom,mdss-dsi-traffic-mode"),
        "physical_mm": [
            scalar0("qcom,mdss-pan-physical-width-dimension"),
            scalar0("qcom,mdss-pan-physical-height-dimension"),
        ],
        "reset_sequence": scalar("qcom,mdss-dsi-reset-sequence"),
        "init_delay_us": scalar0("qcom,mdss-dsi-init-delay-us"),
        "te": {
            "check": "qcom,mdss-dsi-te-check-enable" in panel_props,
            "using_te_pin": "qcom,mdss-dsi-te-using-te-pin" in panel_props,
            "pin_select": scalar0("qcom,mdss-dsi-te-pin-select"),
            "dcs_command": scalar0("qcom,mdss-dsi-te-dcs-command"),
        },
        "hdr": {
            "enabled": "qcom,mdss-dsi-panel-hdr-enabled" in panel_props,
            "peak_brightness": scalar0("qcom,mdss-dsi-panel-peak-brightness"),
            "average_brightness": scalar0("qcom,mdss-dsi-panel-average-brightness"),
        },
        "backlight": {
            "control": prop0("qcom,mdss-dsi-bl-pmic-control-type"),
            "min_level": prop00("qcom,mdss-dsi-bl-min-level"),
            "max_level": prop00("qcom,mdss-dsi-bl-max-level"),
            "default_level": prop00("qcom,mdss-brightness-default-val"),
            "brightness_max_level": prop00("qcom,mdss-brightness-max-level"),
            "supply_entries": board["supply_entries"],
        },
        "display": {
            "active": board["display_active"],
            "ulps": board["ulps"],
            "clocks": board["clocks"],
            "reset_gpio": board["gpios"].get("reset"),
            "te_gpio": board["gpios"].get("te"),
            "vddd_gpio": board["gpios"].get("vddd"),
        },
    }

    return {
        "schema": "kebab-panel-dsi/1",
        "generator": "scripts/extract-panel-init.py",
        "source": {
            "repository": SRC_REPO,
            "commit": SRC_COMMIT,
            "path": SRC_PATH,
            "url": SRC_URL,
            "bytes": SRC_BYTES,
            "sha256": SRC_SHA256,
            "sha256_of_local_copy": digest,
            "panel_node": PANEL_NODE,
        },
        "checksum_definition": (
            "crc32/sha256 over, for each step in order: dtype (u8), wait_ms (u8), "
            "payload length (u16 big endian), payload bytes"
        ),
        "panel": panel,
        "timings": result_timings,
    }


# --------------------------------------------------------------------------
# device-tree (FDT) cross-check


def fdt_nodes(path: Path):
    data = path.read_bytes()
    (magic, _totalsize, off_struct, off_strings, _off_rsv, _ver, _lastcomp, _cpu,
     size_str, _size_struct) = struct.unpack(">10I", data[:40])
    if magic != 0xD00DFEED:
        raise ExtractError("%s is not an FDT blob" % path)
    strings = data[off_strings : off_strings + size_str]
    pos = off_struct
    stack = []
    nodes = []
    while True:
        tok = struct.unpack(">I", data[pos : pos + 4])[0]
        pos += 4
        if tok == 1:  # FDT_BEGIN_NODE
            end = data.index(b"\0", pos)
            name = data[pos:end].decode("latin1")
            pos = (end + 4) & ~3
            stack.append(name)
            nodes.append(["/".join(stack), {}])
        elif tok == 2:  # FDT_END_NODE
            stack.pop()
        elif tok == 3:  # FDT_PROP
            ln, nameoff = struct.unpack(">II", data[pos : pos + 8])
            pos += 8
            nend = strings.index(b"\0", nameoff)
            pname = strings[nameoff:nend].decode("latin1")
            nodes[-1][1][pname] = data[pos : pos + ln]
            pos = (pos + ln + 3) & ~3
        elif tok == 4:  # FDT_NOP
            continue
        elif tok == 9:  # FDT_END
            break
        else:
            raise ExtractError("bad FDT token %d in %s" % (tok, path))
    return nodes


def cross_check_dtb(path: Path, timings):
    nodes = fdt_nodes(path)
    wanted = [n for n in nodes if n[0].split("/")[-1] == PANEL_NODE]
    if not wanted:
        raise ExtractError("%s has no %s node" % (path, PANEL_NODE))
    base = wanted[0][0]

    def norm(path_str):
        # Drop an optional unit address only if present; keep the node path.
        return path_str

    found = {}
    for p, props in nodes:
        if not p.startswith(base + "/"):
            continue
        m = re.match(r"timing@(\d+)$", p.split("/")[-1])
        if m and "qcom,mdss-dsi-on-command" in props:
            found[int(m.group(1))] = props["qcom,mdss-dsi-on-command"]

    report = {
        "file": path.name,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "bytes": path.stat().st_size,
        "panel_path": norm(base),
        "timings": [],
    }
    ok = True
    for timing in timings:
        idx = timing["index"]
        raw = wire_stream(timing["command_sets"]["on"]["steps"])
        theirs = found.get(idx)
        match = theirs is not None and theirs == raw
        ok = ok and match
        report["timings"].append(
            {
                "index": idx,
                "fdt_bytes": len(theirs) if theirs is not None else 0,
                "extracted_bytes": len(raw),
                "match": match,
                "fdt_sha256": hashlib.sha256(theirs).hexdigest() if theirs is not None else None,
            }
        )
    report["match"] = ok
    return report


# --------------------------------------------------------------------------
# C header


def c_ident(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def c_bytes(name: str, data: bytes, note: str) -> str:
    out = ["/* %s */" % note, "static const uint8_t %s[%d] = {" % (name, len(data))]
    for off in range(0, len(data), 12):
        out.append("\t" + ", ".join("0x%02x" % b for b in data[off : off + 12]) + ",")
    out.append("};")
    return "\n".join(out)


def c_cmds(name: str, steps) -> str:
    out = ["static const struct kebab_dsi_cmd %s[] = {" % name]
    offset = 0
    for s in steps:
        out.append(
            "\t{ .dtype = 0x%02x, .wait_ms = %d, .len = %d, .offset = %d },"
            % (s["dtype"], s["wait_ms"], s["len"], offset)
        )
        offset += s["len"]
    out.append("};")
    return "\n".join(out)


def build_header(doc) -> str:
    panel = doc["panel"]
    timings = doc["timings"]
    phy_len = len(timings[0]["phy_timings"])
    lines = [
        "/*",
        " * kebab (OnePlus 8T, SM8250) Samsung AMB655X panel DSI tables.",
        " *",
        " * Generated by scripts/extract-panel-init.py from",
        " *   %s" % doc["source"]["url"],
        " *   %d bytes, sha256 %s" % (doc["source"]["bytes"], doc["source"]["sha256"]),
        " * Do not edit; rerun the script instead.",
        " */",
        "#ifndef KEBAB_PANEL_AMB655X_H",
        "#define KEBAB_PANEL_AMB655X_H",
        "",
        "#include <stdint.h>",
        "",
        '#define KEBAB_PANEL_NAME "%s"' % panel["name"],
        "#define KEBAB_PANEL_WIDTH %d" % timings[0]["width"],
        "#define KEBAB_PANEL_HEIGHT %d" % timings[0]["height"],
        "#define KEBAB_PANEL_LANES %d" % panel["lanes"],
        "#define KEBAB_PANEL_BPP %d" % panel["bpp"],
        "#define KEBAB_PANEL_PHY_TIMINGS_LEN %d" % phy_len,
        "",
        "struct kebab_dsi_cmd {",
        "\tuint8_t dtype;      /* MIPI DSI data type, e.g. 0x39 DCS long write */",
        "\tuint8_t wait_ms;    /* delay after the packet, in milliseconds */",
        "\tuint16_t len;       /* payload length (header handled separately) */",
        "\tuint16_t offset;    /* payload offset into the matching stream */",
        "};",
        "",
        "struct kebab_panel_timing {",
        "\tuint16_t width;",
        "\tuint16_t height;",
        "\tuint16_t h_front_porch;",
        "\tuint16_t h_back_porch;",
        "\tuint16_t h_pulse_width;",
        "\tuint16_t h_sync_skew;",
        "\tuint16_t v_front_porch;",
        "\tuint16_t v_back_porch;",
        "\tuint16_t v_pulse_width;",
        "\tuint32_t clock_hz;",
        "\tuint32_t mdp_transfer_time_us;",
        "\tuint16_t framerate;",
        "\tuint16_t dsc_slice_width;",
        "\tuint16_t dsc_slice_height;",
        "\tuint8_t dsc_slices_per_pkt;",
        "\tuint8_t dsc_bit_per_pixel;",
        "\tuint8_t dsc_bit_per_component;",
        "\tuint8_t dsc_encoders;",
        "\tuint8_t phy_timings[KEBAB_PANEL_PHY_TIMINGS_LEN];",
        "};",
        "",
        "struct kebab_dsi_cmd_set {",
        "\tconst char *name;",
        "\tuint8_t mode;              /* timing index the set was taken from */",
        "\tconst uint8_t *stream;     /* DTS encoding: 7-byte header + payload */",
        "\tuint16_t stream_len;",
        "\tconst struct kebab_dsi_cmd *cmds;",
        "\tuint16_t count;",
        "\tuint32_t crc32;",
        "};",
        "",
    ]

    emitted = {}  # raw blob -> symbol
    table_rows = []
    for timing in timings:
        idx = timing["index"]
        for set_name in HEADER_SETS:
            cs = timing["command_sets"].get(set_name)
            if not cs:
                continue
            raw = wire_stream(cs["steps"])
            symbol = emitted.get(raw)
            if symbol is None:
                base = "kebab_panel_%s" % c_ident(set_name)
                symbol = base
                n = 1
                while symbol in emitted.values():
                    symbol = "%s_t%d" % (base, n)
                    n += 1
                emitted[raw] = symbol
                note = "%s.%s (timing@%d): %d steps, %d bytes, crc32 %s" % (
                    panel["node"],
                    cs["property"],
                    idx,
                    cs["count"],
                    len(raw),
                    cs["crc32"],
                )
                lines.append(c_bytes(symbol, raw, note))
                lines.append("")
                lines.append(c_cmds(symbol + "_cmds", cs["steps"]))
                lines.append(
                    '_Static_assert(sizeof(%s) == %d, "%s length");' % (symbol, len(raw), symbol)
                )
                lines.append("")
            table_rows.append((set_name, idx, symbol, cs))

    lines.append("static const struct kebab_panel_timing kebab_panel_timings[] = {")
    for t in timings:
        dsc = t["dsc"]
        phy = ", ".join("0x%02x" % b for b in t["phy_timings"])
        lines.append(
            "\t{ /* timing@%d, %d Hz */" % (t["index"], t["framerate"])
        )
        lines.append(
            "\t\t.width = %d, .height = %d," % (t["width"], t["height"])
        )
        lines.append(
            "\t\t.h_front_porch = %d, .h_back_porch = %d, .h_pulse_width = %d, .h_sync_skew = %d,"
            % (t["h_front_porch"], t["h_back_porch"], t["h_pulse_width"], t["h_sync_skew"] or 0)
        )
        lines.append(
            "\t\t.v_front_porch = %d, .v_back_porch = %d, .v_pulse_width = %d,"
            % (t["v_front_porch"], t["v_back_porch"], t["v_pulse_width"])
        )
        lines.append(
            "\t\t.clock_hz = %du, .mdp_transfer_time_us = %du, .framerate = %d,"
            % (t["clock_hz"], t["mdp_transfer_time_us"] or 0, t["framerate"])
        )
        lines.append(
            "\t\t.dsc_slice_width = %d, .dsc_slice_height = %d, .dsc_slices_per_pkt = %d,"
            % (dsc.get("slice_width") or 0, dsc.get("slice_height") or 0, dsc.get("slice_per_pkt") or 0)
        )
        lines.append(
            "\t\t.dsc_bit_per_pixel = %d, .dsc_bit_per_component = %d, .dsc_encoders = %d,"
            % (dsc.get("bit_per_pixel") or 0, dsc.get("bit_per_component") or 0, dsc.get("encoders") or 0)
        )
        lines.append("\t\t.phy_timings = { %s }," % phy)
        lines.append("\t},")
    lines.append("};")
    lines.append("")

    lines.append("static const struct kebab_dsi_cmd_set kebab_panel_cmd_sets[] = {")
    for set_name, idx, symbol, cs in table_rows:
        lines.append(
            '\t{ "%s", %d, %s, %d, %s_cmds, %d, %s },'
            % (set_name, idx, symbol, cs["stream_bytes"], symbol, cs["count"], cs["crc32"])
        )
    lines.append("};")
    lines.append("")
    lines.append("#endif /* KEBAB_PANEL_AMB655X_H */")
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
# output and CLI


def summarise(doc, dtb_report):
    src = doc["source"]
    panel = doc["panel"]
    print("source   : %s@%s" % (src["repository"], src["commit"][:12]))
    print("           %s" % src["path"])
    print("           %d bytes, sha256 %s" % (src["bytes"], src["sha256"]))
    print(
        'panel    : %s  "%s"  %s  %dx%d, %d lanes, %s'
        % (
            panel["node"],
            panel["name"],
            panel["manufacture"],
            doc["timings"][0]["width"],
            doc["timings"][0]["height"],
            panel["lanes"],
            panel["type"],
        )
    )
    print(
        "           reset %s, TE gpio %s, reset gpio %s, backlight %s (1..%s)"
        % (
            panel["reset_sequence"],
            panel["display"]["te_gpio"],
            panel["display"]["reset_gpio"],
            panel["backlight"]["control"],
            panel["backlight"]["max_level"],
        )
    )
    for t in doc["timings"]:
        cs = t["command_sets"]["on"]
        delays = ", ".join(
            "#%d %d ms (reg %s)" % (d["index"], d["wait_ms"], d["register"])
            for d in cs["delay_steps"]
        ) or "none"
        print(
            "mode %d   : %d Hz, clk %d Hz, hfp/hbp/hpw %d/%d/%d, vfp/vbp/vpw %d/%d/%d, dsc %dx%d x%d %dbpp"
            % (
                t["index"],
                t["framerate"],
                t["clock_hz"],
                t["h_front_porch"],
                t["h_back_porch"],
                t["h_pulse_width"],
                t["v_front_porch"],
                t["v_back_porch"],
                t["v_pulse_width"],
                t["dsc"].get("slice_width", 0),
                t["dsc"].get("slice_height", 0),
                t["dsc"].get("slice_per_pkt", 0),
                t["dsc"].get("bit_per_pixel", 0),
            )
        )
        print(
            "           on: %d steps, %d payload bytes, %d stream bytes, delays: %s"
            % (cs["count"], cs["payload_bytes"], cs["stream_bytes"], delays)
        )
        print("               crc32 %s" % cs["crc32"])
    if dtb_report:
        print(
            "cross    : %s (%d bytes, sha256 %s)"
            % (dtb_report["file"], dtb_report["bytes"], dtb_report["sha256"][:16] + "...")
        )
        for t in dtb_report["timings"]:
            print(
                "           timing@%d: fdt %d bytes vs extracted %d bytes -> %s"
                % (t["index"], t["fdt_bytes"], t["extracted_bytes"], "same" if t["match"] else "DIFFERENT")
            )
        print("           %s" % ("MATCH" if dtb_report["match"] else "MISMATCH"))


def print_provenance(doc, source_path: Path):
    text = source_path.read_text(encoding="utf-8").split("\n")
    print("provenance (1-based lines in %s):" % source_path.name)
    for t in doc["timings"]:
        cs = t["command_sets"]["on"]
        print(
            "  timing@%d, %d Hz, qcom,mdss-dsi-on-command starts at line %d"
            % (t["index"], t["framerate"], cs["line"])
        )
        for s in cs["steps"]:
            payload = s["payload_hex"]
            if len(payload) > 36:
                payload = payload[:36] + "..."
            src = text[s["line"] - 1].strip()
            span = "" if s["line_end"] == s["line"] else "  [payload continues to L%d]" % s["line_end"]
            print(
                "   #%02d L%-4d %-21s wait=%-3d len=%-3d reg=%-5s %s%s"
                % (s["index"], s["line"], s["type"], s["wait_ms"], s["len"], s["reg"], payload, span)
            )
            print("        | %s" % src)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Extract the kebab AMB655X panel init sequence")
    ap.add_argument("--source", type=Path, help="local dsi-panel-*.dtsi (default: fetch the pinned URL)")
    ap.add_argument(
        "--cache",
        type=Path,
        default=Path.home() / ".cache" / "kebab-panel" / (SRC_SHA256 + ".dtsi"),
    )
    ap.add_argument("--json", type=Path, default=DEFAULT_JSON)
    ap.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    ap.add_argument("--dtb", type=Path, help="stock kebab DTB used to cross-check the on-command blob")
    ap.add_argument("--provenance", action="store_true", help="print every step with its source line")
    ap.add_argument("--check", action="store_true", help="compare with the committed outputs, do not write")
    args = ap.parse_args(argv)

    try:
        source = args.source if args.source else fetch_source(args.cache)
        doc = extract(source)
        dtb_report = cross_check_dtb(args.dtb, doc["timings"]) if args.dtb else None
        doc["verification"] = {"dtb_cross_check": dtb_report}

        payload = json.dumps(doc, indent=2, ensure_ascii=False) + "\n"
        header = build_header(doc)

        if args.check:
            ok = True
            for path, content in ((args.json, payload), (args.header, header)):
                if not path.exists():
                    print("missing %s" % path)
                    ok = False
                elif path.read_text(encoding="utf-8") != content:
                    print("stale %s" % path)
                    ok = False
            print("check: %s" % ("OK" if ok else "FAILED"))
            return 0 if ok else 1

        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(payload, encoding="utf-8")
        args.header.write_text(header, encoding="utf-8")

        summarise(doc, dtb_report)
        print("wrote    : %s (%d bytes)" % (args.json, len(payload)))
        print("wrote    : %s (%d bytes)" % (args.header, len(header)))
        if args.provenance:
            print()
            print_provenance(doc, source)
        return 0
    except ExtractError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
