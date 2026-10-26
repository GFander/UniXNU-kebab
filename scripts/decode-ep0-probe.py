#!/usr/bin/env python3
"""Decode KEBAB EP0 PROBE reports from a captured USB console log.

The kernel side is bsd/dev/kebab_usb.c (patch 0091); the packed word and the
32-entry ring are described in docs/EP0-PROBE.md.  The text this parser matches
is what the code actually prints: one header line and up to eight history
lines with four words each, every line carrying the same prefix:

    KEBAB EP0 PROBE: progress 0x2c031205 seq 44 req 1 phase 3 status 5
    KEBAB EP0 PROBE: [12]0x0d011000 [13]0x0e021000 [14]0x0f031000 [15]0x00000000

The header is printed once at the first ud_configure() (a baseline without
history) and once from the panic pump (header plus the whole ring).  A line
may be split or cut by a flaky host, so the parser accepts partial history
lines and missing headers; it says so in the verdict instead of guessing.

Usage:
    python3 scripts/decode-ep0-probe.py ep0.log
    python3 scripts/decode-ep0-probe.py --json ep0.log
    cat ep0.log | python3 scripts/decode-ep0-probe.py
    python3 scripts/decode-ep0-probe.py --selftest

Exit status: 0 if a probe report was decoded, 1 if none was found, 2 if the
input could not be read.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

SCHEMA_VERSION = 1
PROBE_PREFIX = "KEBAB EP0 PROBE:"
HISTORY_SIZE = 32
HISTORY_PER_LINE = 4

# Request numbers, as produced by kebab_usb_ep0_request() in patch 0091.
REQUESTS = {
    0: "non-request event",
    1: "GET_DESCRIPTOR",
    2: "SET_ADDRESS",
    3: "GET_CONFIGURATION",
    4: "SET_CONFIGURATION",
    5: "SET_LINE_CODING",
    6: "GET_LINE_CODING",
    7: "SET_CONTROL_LINE_STATE",
    8: "GET_STATUS/CLEAR_FEATURE",
    255: "other",
}

# Phase numbers, per docs/EP0-PROBE.md and the note sites in kebab_usb.c.
PHASES = {
    1: "SETUP received",
    2: "data stage submitted",
    3: "data stage complete",
    4: "waiting for status ready",
    5: "status stage submitted",
    6: "status stage complete",
    7: "SETUP re-armed",
    16: "bus reset",
    17: "EP0 rebuild",
}

# Status numbers; 16+n is the DEPCMD status n.
STATUSES = {
    0: "ok",
    1: "timeout",
    2: "DEPCMD rejected",
    3: "endpoint not ready",
    4: "not enough capacity",
    5: "status stage out of order",
}


def request_name(code: int) -> str:
    return REQUESTS.get(code) or "unknown(0x%02x)" % (code & 0xFF)


def phase_name(code: int) -> str:
    return PHASES.get(code) or "unknown(0x%02x)" % (code & 0xFF)


def status_name(code: int) -> str:
    if code in STATUSES:
        return STATUSES[code]
    if 16 <= code <= 31:
        return "DEPCMD status %d" % (code - 16)
    return "unknown(0x%02x)" % (code & 0xFF)


def word_hex(word: int) -> str:
    return "0x%08x" % (word & 0xFFFFFFFF)


def count_text(number: int, noun: str) -> str:
    return "%d %s%s" % (number, noun, "" if number == 1 else "s")


def unpack_word(word: int) -> Dict[str, int]:
    return {
        "seq": (word >> 24) & 0xFF,
        "request": (word >> 8) & 0xFF,
        "phase": (word >> 16) & 0xFF,
        "status": word & 0xFF,
    }


def pack_word(seq: int, request: int, phase: int, status: int) -> int:
    """Test-side packing, matching kebab_usb_ep0_note()."""
    return (((seq & 0xFF) << 24) | ((phase & 0xFF) << 16) |
            ((request & 0xFF) << 8) | (status & 0xFF))


def kebab_request_code(type_byte: int, request: int) -> int:
    """Python copy of kebab_usb_ep0_request(), used to read setup lines."""
    if (type_byte & 0x60) == 0x20:                 # class
        return {0x20: 5, 0x21: 6, 0x22: 7}.get(request, 255)
    if (type_byte & 0x60) != 0:                    # vendor
        return 255
    return {0: 8, 1: 8, 5: 2, 6: 1, 8: 3, 9: 4}.get(request, 255)


def _decoded(code: int, name: str) -> Dict[str, object]:
    return {"code": code, "name": name}


ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
PROGRESS_RE = re.compile(r"^progress\s+0x([0-9a-fA-F]{8})(.*)$")
FIELD_RE = re.compile(r"\b(seq|req|phase|status)\s+(\d+)\b")
SLOT_RE = re.compile(r"\[(\d{1,3})\]\s*0x([0-9a-fA-F]{8})")

SETUP_RE = re.compile(
    r"KEBAB USB: setup ([0-9a-fA-F]{2})/([0-9a-fA-F]{2}) "
    r"value 0x([0-9a-fA-F]{4}) len (\d+)")
UNSUPPORTED_RE = re.compile(
    r"KEBAB USB: unsupported request ([0-9a-fA-F]{2})/([0-9a-fA-F]{2}) "
    r"len (\d+)")
DATA_REJECTED_RE = re.compile(r"KEBAB USB: SET_LINE_CODING data stage rejected")
REBUILD_FAILED_RE = re.compile(r"KEBAB USB: EP0 rebuild failed")
BUS_RESET_RE = re.compile(r"KEBAB USB: bus reset, rebuilding EP0")
CONNECTED_RE = re.compile(r"KEBAB USB: connected, speed")
CONFIGURED_RE = re.compile(r"KEBAB USB: configured")
DISCONNECT_RE = re.compile(r"KEBAB USB: disconnect")


@dataclass
class Header:
    word: int
    line: int
    fields: Dict[str, int] = field(default_factory=dict)
    partial: bool = False
    consistent: bool = True


@dataclass
class Slot:
    index: int
    word: int
    line: int


@dataclass
class Report:
    header: Optional[Header] = None
    slots: List[Slot] = field(default_factory=list)
    history_lines: int = 0
    partial_history: bool = False
    malformed: List[Tuple[int, str]] = field(default_factory=list)

    @property
    def last_line(self) -> int:
        lines = [slot.line for slot in self.slots]
        if self.header is not None:
            lines.append(self.header.line)
        return max(lines) if lines else 0


def _clean(line: str) -> str:
    return ANSI_RE.sub("", line).rstrip("\r")


def parse_lines(lines: List[str]) -> Dict[str, object]:
    """Split a log into probe reports, probe lines and kernel noise."""
    reports: List[Report] = []
    current: Optional[Report] = None
    probe_lines = 0
    noise_lines = 0
    malformed = 0

    for lineno, raw in enumerate(lines, 1):
        line = _clean(raw)
        index = line.find(PROBE_PREFIX)
        if index < 0:
            if line.strip():
                noise_lines += 1
            continue

        probe_lines += 1
        rest = line[index + len(PROBE_PREFIX):].strip()

        if rest.startswith("progress"):
            if current is not None:
                reports.append(current)
            match = PROGRESS_RE.match(rest)
            if match is None:
                current = Report()
                current.malformed.append((lineno, raw.strip()))
                malformed += 1
                continue
            word = int(match.group(1), 16)
            fields = {key: int(value)
                      for key, value in FIELD_RE.findall(match.group(2))}
            header = Header(word=word, line=lineno, fields=fields)
            header.partial = len(fields) < 4
            decoded = unpack_word(word)
            key_map = {"seq": "seq", "req": "request",
                       "phase": "phase", "status": "status"}
            header.consistent = all(decoded[key_map[key]] == value
                                    for key, value in fields.items())
            current = Report(header=header)
            continue

        if rest.startswith("["):
            if current is None:
                current = Report()
            groups = list(SLOT_RE.finditer(rest))
            if not groups:
                current.malformed.append((lineno, raw.strip()))
                malformed += 1
                continue
            current.history_lines += 1
            if len(groups) < HISTORY_PER_LINE:
                current.partial_history = True
            for group in groups:
                slot_index = int(group.group(1))
                word = int(group.group(2), 16)
                if slot_index >= HISTORY_SIZE:
                    current.malformed.append(
                        (lineno, "slot %d out of range" % slot_index))
                    malformed += 1
                    continue
                current.slots.append(Slot(slot_index, word, lineno))
            continue

        # A line with the prefix but neither a header nor a history line.
        if current is None:
            current = Report()
        current.malformed.append((lineno, raw.strip()))
        malformed += 1

    if current is not None:
        reports.append(current)

    return {
        "reports": reports,
        "probe_lines": probe_lines,
        "noise_lines": noise_lines,
        "malformed": malformed,
    }


def _note_for(transitions: List[Dict[str, object]], index: int) -> str:
    """One short, sequence-aware interpretation for a transition."""
    item = transitions[index]
    phase = item["phase"]["code"]
    status = item["status"]["code"]
    nxt = transitions[index + 1]["phase"]["code"] if index + 1 < len(transitions) else None

    if phase == 1:
        if nxt == 2:
            return "SETUP accepted; data stage submitted next"
        if nxt in (4, 5):
            return "SETUP accepted; status path follows (no data stage)"
        if nxt == 16:
            return "SETUP accepted; bus reset before any submit"
        if nxt == 1:
            return "SETUP accepted; next SETUP with no submit in between"
        if nxt is None:
            return "SETUP received; nothing was submitted after it"
        return "SETUP accepted; next event is %s" % phase_name(nxt)
    if phase == 2:
        if status == 2:
            return "data stage submit rejected; no completion possible"
        if nxt == 3:
            return "data stage submitted; completion follows"
        if nxt == 16:
            return "data stage submitted; bus reset before completion"
        if nxt == 1:
            return "data stage submitted; next SETUP before completion"
        return "data stage submitted; no completion recorded"
    if phase == 3:
        if nxt in (4, 5):
            return "data stage completed; status path follows"
        if nxt == 16:
            return "data stage completed; bus reset before the status stage"
        if nxt in (1, 17):
            return "data stage completed; next transfer started without status"
        return "data stage completed; status stage never submitted"
    if phase == 4:
        if status == 5:
            return "XferNotReady(status=2) out of order; ignored"
        if nxt == 5:
            return "controller ready for status; status submitted"
        return "controller ready for status; no status submit recorded"
    if phase == 5:
        if status == 2:
            return "status stage submit rejected"
        if nxt == 6:
            return "status stage submitted; completion follows"
        if nxt == 16:
            return "status stage submitted; bus reset before completion"
        return "status stage submitted; no completion recorded"
    if phase == 6:
        if nxt == 7:
            return "status stage completed; SETUP re-armed"
        if nxt == 16:
            return "status stage completed; bus reset before re-arm"
        return "status stage completed; SETUP not re-armed"
    if phase == 7:
        if nxt == 1:
            return "SETUP re-armed; next SETUP follows"
        if nxt == 16:
            return "SETUP re-armed; bus reset follows"
        if nxt is None:
            return "SETUP re-armed; idle at the end of the window"
        return "SETUP re-armed; next event is %s" % phase_name(nxt)
    if phase == 16:
        if nxt == 17:
            return "bus reset; EP0 rebuild follows"
        if nxt == 1:
            return "bus reset; next SETUP without a rebuild note"
        return "bus reset; no rebuild recorded"
    if phase == 17:
        if nxt == 1:
            return "EP0 rebuilt; host sent SETUP"
        if nxt is None:
            return "EP0 rebuilt; no SETUP after it"
        return "EP0 rebuilt; next event is %s" % phase_name(nxt)
    return "phase not in the decoder tables"


def _sequence_summary(transitions: List[Dict[str, object]],
                      last: Optional[Dict[str, object]]) -> str:
    if not transitions:
        return "no transitions in the captured window"

    last_setup = None
    for index, item in enumerate(transitions):
        if item["phase"]["code"] == 1:
            last_setup = index
    if last_setup is None:
        tail = transitions[-1]
        return ("no SETUP in the captured window; last event is %s at seq %d"
                % (tail["phase"]["name"], tail["seq"]))

    tail = transitions[last_setup:]
    parts: List[str] = []
    for phase in (2, 3, 4, 5, 6, 7):
        if any(item["phase"]["code"] == phase for item in tail):
            parts.append(phase_name(phase))
    if any(item["phase"]["code"] in (16, 17) for item in tail):
        parts.append("bus reset/rebuild")

    summary = "SETUP (%s)" % tail[0]["request"]["name"]
    if parts:
        summary += " -> " + " -> ".join(parts)
    else:
        summary += "; nothing followed"

    if any(item["phase"]["code"] == 7 for item in tail):
        outcome = "the transfer completed"
        if any(item["phase"]["code"] == 16 for item in tail):
            outcome = "the transfer completed; a bus reset followed"
    elif any(item["phase"]["code"] == 16 for item in tail):
        outcome = "a bus reset interrupted the transfer"
    elif any(item["phase"]["code"] == 6 for item in tail):
        outcome = "the status stage completed, but SETUP was not re-armed"
    elif any(item["phase"]["code"] == 5 and item["status"]["code"] != 0
             for item in tail):
        outcome = "the status stage submit was rejected"
    elif any(item["phase"]["code"] == 5 for item in tail):
        outcome = "the status stage was submitted but never completed"
    elif any(item["phase"]["code"] == 4 and item["status"]["code"] == 5
             for item in tail):
        outcome = "the status readiness arrived out of order"
    elif any(item["phase"]["code"] == 4 for item in tail):
        outcome = "the status stage was never submitted after readiness"
    elif any(item["phase"]["code"] == 2 and item["status"]["code"] != 0
             for item in tail):
        outcome = "the data stage submit was rejected"
    elif any(item["phase"]["code"] == 3 for item in tail):
        outcome = "the status stage was never submitted"
    elif any(item["phase"]["code"] == 2 for item in tail):
        outcome = "the data stage was submitted but never completed"
    else:
        outcome = "no data or status stage was submitted"
    return summary + "; " + outcome


def _build_context(lines: List[str], report: Optional[Report]) -> Dict[str, object]:
    """Kernel text that is useful around a probe report."""
    end = report.last_line if report is not None else 0
    context: Dict[str, object] = {
        "last_setup": None,
        "unsupported": [],
        "hints": {
            "data_rejected": 0,
            "rebuild_failed": 0,
            "bus_reset": 0,
            "connected": 0,
            "configured": 0,
            "disconnect": 0,
        },
        "after_report": {
            "lines": 0,
            "setup": 0,
            "bus_reset": 0,
            "connected": 0,
            "configured": 0,
            "disconnect": 0,
        },
    }
    setup_list: List[Dict[str, object]] = context["unsupported"]  # type: ignore

    for lineno, raw in enumerate(lines, 1):
        line = _clean(raw)
        if PROBE_PREFIX in line:
            continue
        after = lineno > end

        match = SETUP_RE.search(line)
        if match:
            type_byte = int(match.group(1), 16)
            req_byte = int(match.group(2), 16)
            code = kebab_request_code(type_byte, req_byte)
            entry = {
                "line": lineno,
                "type": type_byte,
                "req": req_byte,
                "value": int(match.group(3), 16),
                "length": int(match.group(4)),
                "request": _decoded(code, request_name(code)),
            }
            context["last_setup"] = entry
            if after:
                context["after_report"]["setup"] += 1  # type: ignore

        match = UNSUPPORTED_RE.search(line)
        if match:
            type_byte = int(match.group(1), 16)
            req_byte = int(match.group(2), 16)
            code = kebab_request_code(type_byte, req_byte)
            setup_list.append({
                "line": lineno,
                "type": type_byte,
                "req": req_byte,
                "length": int(match.group(3)),
                "request": _decoded(code, request_name(code)),
            })

        if DATA_REJECTED_RE.search(line):
            context["hints"]["data_rejected"] += 1  # type: ignore
        if REBUILD_FAILED_RE.search(line):
            context["hints"]["rebuild_failed"] += 1  # type: ignore
        if BUS_RESET_RE.search(line):
            context["hints"]["bus_reset"] += 1  # type: ignore
            if after:
                context["after_report"]["bus_reset"] += 1  # type: ignore
        if CONNECTED_RE.search(line):
            context["hints"]["connected"] += 1  # type: ignore
            if after:
                context["after_report"]["connected"] += 1  # type: ignore
        if CONFIGURED_RE.search(line):
            context["hints"]["configured"] += 1  # type: ignore
            if after:
                context["after_report"]["configured"] += 1  # type: ignore
        if DISCONNECT_RE.search(line):
            context["hints"]["disconnect"] += 1  # type: ignore
            if after:
                context["after_report"]["disconnect"] += 1  # type: ignore
        if after and line.strip():
            context["after_report"]["lines"] += 1  # type: ignore

    return context


def _analyze_report(report: Report, context: Dict[str, object]) -> Dict[str, object]:
    header = report.header
    header_decoded = unpack_word(header.word) if header is not None else None

    transitions: List[Dict[str, object]] = []
    for slot in report.slots:
        if slot.word == 0:
            continue
        decoded = unpack_word(slot.word)
        transitions.append({
            "slot": slot.index,
            "seq": decoded["seq"],
            "word": slot.word,
            "word_hex": word_hex(slot.word),
            "request": _decoded(decoded["request"], request_name(decoded["request"])),
            "phase": _decoded(decoded["phase"], phase_name(decoded["phase"])),
            "status": _decoded(decoded["status"], status_name(decoded["status"])),
            "line": slot.line,
        })
    for index in range(len(transitions)):
        transitions[index]["note"] = _note_for(transitions, index)

    slot_count = len(report.slots)
    zero_count = sum(1 for slot in report.slots if slot.word == 0)
    slot_gaps: List[Tuple[int, int]] = []
    for previous, current in zip(report.slots, report.slots[1:]):
        if (previous.index + 1) % HISTORY_SIZE != current.index:
            slot_gaps.append((previous.index, current.index))

    first_nonzero = None
    last_nonzero = None
    for index, slot in enumerate(report.slots):
        if slot.word:
            if first_nonzero is None:
                first_nonzero = index
            last_nonzero = index
    zero_in_middle = False
    if first_nonzero is not None and last_nonzero is not None:
        zero_in_middle = any(report.slots[i].word == 0
                             for i in range(first_nonzero, last_nonzero + 1))

    sequence_gaps: List[Tuple[int, int]] = []
    wraps: List[Tuple[int, int]] = []
    for previous, current in zip(transitions, transitions[1:]):
        if (previous["seq"] + 1) % 256 != current["seq"]:
            sequence_gaps.append((previous["seq"], current["seq"]))
        if previous["seq"] == 255:
            wraps.append((previous["seq"], current["seq"]))

    newest = transitions[-1] if transitions else None
    header_seq = header_decoded["seq"] if header_decoded else None
    history_lags_header = bool(
        header_decoded and newest and newest["seq"] != header_seq)
    front_truncated = bool(
        header_decoded and report.slots and
        report.slots[0].index != (header_seq + 1) % HISTORY_SIZE)
    history_complete = (slot_count == HISTORY_SIZE and
                        not report.partial_history and
                        not slot_gaps and
                        not history_lags_header and
                        not front_truncated)

    if header_decoded is not None:
        last_word = header.word
        last_source = "header"
    elif newest is not None:
        last_word = newest["word"]
        last_source = "history"
    else:
        last_word = None
        last_source = None
    last_decoded = unpack_word(last_word) if last_word is not None else None

    sequence: Dict[str, object] = {
        "wrapped": None,
        "wraps": [[a, b] for a, b in wraps],
        "gaps": len(sequence_gaps),
        "last_byte": last_decoded["seq"] if last_decoded else None,
        "total": None,
        "lower_bound": None,
        "ambiguous": False,
        "note": "",
    }
    if transitions:
        sequence["wrapped"] = bool(wraps)  # type: ignore
        if (header_decoded is not None and not front_truncated and
                not sequence_gaps and transitions[0]["seq"] == 1 and
                len(transitions) == last_decoded["seq"] and
                len(transitions) < 256):
            sequence["total"] = len(transitions)
        if wraps and last_decoded is not None:
            sequence["lower_bound"] = 256 + last_decoded["seq"]  # type: ignore
        elif sequence["total"] is not None:
            sequence["lower_bound"] = sequence["total"]
        sequence["ambiguous"] = bool(
            sequence["wrapped"] is False and sequence["total"] is None)
        if sequence["wrapped"]:
            sequence["note"] = (
                "wrapped inside the window (0x%02x -> 0x00); the last absolute "
                "value is at least %d, the exact count is not recoverable"
                % (wraps[-1][0], sequence["lower_bound"]))
        elif sequence["total"] is not None:
            sequence["note"] = (
                "%d transitions since boot; no wrap"
                % sequence["total"])
        else:
            sequence["note"] = (
                "no wrap in the visible window; only the low 8 bits are packed, "
                "so an earlier wrap cannot be excluded")
    else:
        sequence["note"] = "no history to read the counter from"

    if last_decoded is not None:
        last = {
            "word": last_word,
            "word_hex": word_hex(last_word),
            "seq": last_decoded["seq"],
            "request": _decoded(last_decoded["request"],
                                request_name(last_decoded["request"])),
            "phase": _decoded(last_decoded["phase"],
                              phase_name(last_decoded["phase"])),
            "status": _decoded(last_decoded["status"],
                               status_name(last_decoded["status"])),
            "source": last_source,
        }
    else:
        last = None

    last_phase = last_decoded["phase"] if last_decoded else None
    last_status = last_decoded["status"] if last_decoded else None
    last_request = last_decoded["request"] if last_decoded else None

    reset_positions = [i for i, item in enumerate(transitions)
                       if item["phase"]["code"] == 16]
    if last_phase == 16:
        last_reset_seq = last_decoded["seq"]
        transitions_since_reset = 0
    elif reset_positions:
        last_reset_seq = transitions[reset_positions[-1]]["seq"]
        transitions_since_reset = len(transitions) - 1 - reset_positions[-1]
    else:
        last_reset_seq = None
        transitions_since_reset = None

    observed_submit = [item for item in transitions if item["phase"]["code"] == 5]
    observed_complete = [item for item in transitions if item["phase"]["code"] == 6]

    last_setup_pos = None
    for index, item in enumerate(transitions):
        if item["phase"]["code"] == 1:
            last_setup_pos = index
    if history_lags_header:
        # The header word belongs to a transfer whose setup is outside the
        # visible history; only the word itself can answer for that transfer.
        cycle: List[Dict[str, object]] = []
    elif last_setup_pos is not None:
        cycle = transitions[last_setup_pos:]
    else:
        cycle = transitions

    submitted = (any(item["phase"]["code"] == 5 for item in cycle) or
                 last_phase in (5, 6, 7))
    completed = (any(item["phase"]["code"] == 6 for item in cycle) or
                 last_phase in (6, 7))
    submitted_ever = bool(observed_submit) or last_phase in (5, 6, 7)
    completed_ever = bool(observed_complete) or last_phase in (6, 7)

    cycle_submit = [item for item in cycle if item["phase"]["code"] == 5]
    if cycle_submit:
        submit_evidence = "phase 5 at seq %d" % cycle_submit[-1]["seq"]
    elif last_phase in (5, 6, 7):
        submit_evidence = "implied by the final phase %d" % last_phase
    else:
        submit_evidence = "no phase 5 after the last SETUP"
    if observed_submit:
        submit_ever_evidence = "phase 5 at seq %d" % observed_submit[-1]["seq"]
    elif last_phase in (5, 6, 7):
        submit_ever_evidence = "implied by the final phase %d" % last_phase
    else:
        submit_ever_evidence = "never"

    cycle_complete = [item for item in cycle if item["phase"]["code"] == 6]
    if cycle_complete:
        complete_evidence = "phase 6 at seq %d" % cycle_complete[-1]["seq"]
    elif last_phase in (6, 7):
        complete_evidence = "implied by the final phase %d" % last_phase
    else:
        complete_evidence = "no phase 6 after the last SETUP"
    if observed_complete:
        complete_ever_evidence = "phase 6 at seq %d" % observed_complete[-1]["seq"]
    elif last_phase in (6, 7):
        complete_ever_evidence = "implied by the final phase %d" % last_phase
    else:
        complete_ever_evidence = "never"

    unknown_codes: List[str] = []
    for item in transitions:
        if item["request"]["code"] not in REQUESTS:
            unknown_codes.append("request %s" % item["request"]["name"])
        if item["phase"]["code"] not in PHASES:
            unknown_codes.append("phase %s" % item["phase"]["name"])
        status_code = item["status"]["code"]
        if status_code not in STATUSES and not 16 <= status_code <= 31:
            unknown_codes.append("status %s" % item["status"]["name"])
    unknown_codes = sorted(set(unknown_codes))

    baseline_like = bool(
        report.history_lines == 0 and last_phase == 1 and
        last_request == 4 and last_status == 0)

    anomalies: List[str] = []
    if header is None:
        anomalies.append("header line missing; last word taken from history")
    else:
        if header.partial:
            anomalies.append("header line truncated; fields read from the word")
        if not header.consistent:
            anomalies.append("header fields disagree with the packed word")
    if report.partial_history:
        anomalies.append("a history line is truncated (fewer than four words)")
    if slot_gaps:
        anomalies.append("history slot numbers are not consecutive")
    if front_truncated:
        anomalies.append("history does not start at the expected ring slot")
    if history_lags_header:
        anomalies.append("history ends before the header word (newest entries missing)")
    if report.history_lines == 0 and not baseline_like and last is not None:
        anomalies.append("history is missing after the header line")
    if zero_in_middle:
        anomalies.append("zero word inside the history")
    if sequence_gaps:
        anomalies.append("%s inside the history"
                         % count_text(len(sequence_gaps), "sequence gap"))
    if unknown_codes:
        anomalies.append("%s: %s"
                         % (count_text(len(unknown_codes), "unknown code"),
                            ", ".join(unknown_codes)))
    if context["hints"]["rebuild_failed"]:  # type: ignore
        anomalies.append("the kernel printed 'EP0 rebuild failed'")
    if report.malformed:
        anomalies.append("%s in this report"
                         % count_text(len(report.malformed), "malformed line"))

    if header is None:
        if history_complete:
            capture = "complete history, header line missing"
        elif slot_count:
            capture = "partial (%d/32 slots; header line missing)" % slot_count
        else:
            capture = "no header and no history"
    elif report.history_lines == 0:
        if baseline_like:
            capture = "baseline (header only, no history)"
        else:
            capture = "header only (history cut before the first line)"
    elif history_complete:
        capture = "complete (32/32 slot words, %d transitions)" % len(transitions)
    elif front_truncated and history_lags_header:
        capture = "partial (both ends of the history missing)"
    elif front_truncated:
        capture = "partial (oldest history lines missing)"
    elif history_lags_header:
        capture = "truncated (newest history entries missing)"
    else:
        capture = "partial (%d/32 slots)" % slot_count

    context_complete = bool(
        header is not None and history_complete and
        not front_truncated and not history_lags_header)

    healthy = None
    failure = "unknown"
    failure_point = None
    headline = ""
    detail = ""
    next_steps: List[str] = []

    def no_report(reason: str, text: str) -> None:
        nonlocal failure, failure_point, headline, detail, next_steps
        failure = reason
        failure_point = text
        headline = text
        next_steps = [
            "force a panic after 'KEBAB USB: configured' so the panic pump "
            "prints the history, then decode that capture",
        ]

    if last is None:
        if report.history_lines:
            no_report("no transitions",
                      "the report has history lines but every slot is zero")
        else:
            no_report("no data",
                      "probe baseline only: the header word is the only reading")
    elif report.history_lines == 0 and baseline_like:
        # The baseline header is printed from ud_configure() during
        # enumeration, before the transfer it belongs to has finished.  Its
        # phase is not a failure, and there is no ring to interpret.
        healthy = None
        failure = "no data"
        failure_point = ("probe baseline only: the header word is the only "
                         "reading")
        headline = ("baseline probe word captured; no history yet, so there is "
                    "nothing to diagnose")
        detail = ("the baseline header is printed once at the first "
                  "ud_configure(); it is a snapshot of the state machine, not "
                  "a failure")
        next_steps = [
            "force a panic after 'KEBAB USB: configured' so the panic pump "
            "prints the history, then decode that capture",
        ]
    elif last_phase == 7 and last_status == 0:
        healthy = True
        failure = "none"
        headline = ("EP0 completed the last control transfer and re-armed SETUP; "
                    "no EP0 failure is visible in this window")
        detail = ("the last recorded transition is the SETUP re-arm, so the "
                  "state machine is idle and ready for the next request")
        after = context["after_report"]  # type: ignore
        if after["lines"]:
            detail += ("; the log after the probe report has %s: bus "
                       "reset %d, setup %d, configured %d"
                       % (count_text(after["lines"], "line"),
                          after["bus_reset"], after["setup"],
                          after["configured"]))
            next_steps.append(
                "if the port was reopened after the panic, judge it from those "
                "tail lines; a bus reset without a configured line means "
                "re-enumeration did not finish")
        next_steps.insert(0,
            "if this report came from the panic pump, any reopen failure "
            "happened after the snapshot, so read the lines after the probe")
        next_steps.append(
            "if the console is still silent, look above EP0: bulk IN transfers, "
            "the log ring, or the panic hook")
    elif last_phase == 1:
        healthy = False
        last_setup = context["last_setup"]  # type: ignore
        matching_unsupported = []
        if (last_setup is not None and
                last_setup["request"]["code"] == last_request):
            matching_unsupported = [
                item for item in context["unsupported"]  # type: ignore
                if item["type"] == last_setup["type"] and
                item["req"] == last_setup["req"]]
        if matching_unsupported:
            failure = "setup stalled"
            bytes_text = "0x%02x/0x%02x" % (
                matching_unsupported[-1]["type"],
                matching_unsupported[-1]["req"])
            failure_point = (
                "the handler stalled EP0 for unsupported request %s before "
                "submitting a data or status stage" % bytes_text)
            detail = ("the last setup line and the last unsupported-request line "
                      "match, so this is the deliberate stall path, not a hang")
            next_steps = [
                "add this request to ud_setup() or keep the stall deliberately",
                "check the host's retry pattern in the log",
            ]
        elif last_request in (2, 4, 7):
            healthy = False
            failure = "status readiness missing"
            failure_point = (
                "SETUP for a no-data request was received; the status stage "
                "waits for XferNotReady(status=2), which was not recorded")
            detail = ("the handler ran and called ud_status_stage(false), but no "
                      "phase-4 transition followed; either the controller did not "
                      "raise the readiness event or the host did not start the "
                      "status stage")
            next_steps = [
                "check that XferNotReady(status=2) arrives for this request",
                "check ud.state == EP0_WAIT_STATUS and the type==3/status==2 decode",
            ]
        elif last_request in (1, 3, 5, 6):
            healthy = False
            failure = "setup stalled"
            failure_point = (
                "SETUP for a data-stage request was received but no data TRB "
                "submit was recorded")
            detail = ("the request handler returned or stalled before ud_start(); "
                      "the probe records the submit only after ud_start() returns")
            next_steps = [
                "check the request handler for an early return or ud_ep_stall()",
                "look for 'unsupported request' next to the setup line",
            ]
        else:
            healthy = False
            failure = "setup stalled"
            failure_point = (
                "SETUP was received but no data or status stage was submitted")
            detail = ("for an unsupported or vendor request the handler may have "
                      "stalled EP0; the probe cannot tell a stall from a hang")
            next_steps = [
                "look for 'unsupported request' next to the setup line",
                "check the bRequest against the request table",
            ]
    elif last_phase == 2:
        healthy = False
        if last_status == 2:
            failure = "data submit rejected"
            failure_point = ("the controller rejected the data-stage TRB "
                             "(DEPCMD status nonzero)")
            detail = ("ud_start() returned nonzero at the data stage; for "
                      "SET_LINE_CODING the 64-byte OUT capacity and the TRB "
                      "parameters are the usual suspects, otherwise the IN TRB "
                      "address or direction")
            next_steps = [
                "check the DEPCMD return in ud_start() (TRB address split, "
                "endpoint number, transfer resource)",
                "confirm EP0 was rebuilt after the last reset",
            ]
        elif last_status == 0:
            failure = "data stage incomplete"
            failure_point = ("the data stage was submitted but its completion "
                             "event never arrived")
            if last_request == 5:
                detail = ("OUT data stage: the host may not have sent the seven "
                          "bytes (port not opened, DTR not asserted, wrong "
                          "wLength), or the receive capacity is not 64-byte "
                          "aligned")
            else:
                detail = ("IN data stage: the host may not be reading, or the TRB "
                          "length/address is wrong")
            next_steps = [
                "check the host side (port open, DTR/RTS, wLength)",
                "check the event thread and the TRB parameters",
            ]
        else:
            failure = "data submit error"
            failure_point = ("the data-stage submit returned status %d (%s)"
                             % (last_status, status_name(last_status)))
            detail = "the controller reported an error before any completion"
            next_steps = ["check the DEPCMD return and the endpoint state"]
    elif last_phase == 3:
        healthy = False
        failure = "status not submitted"
        failure_point = ("the data stage completed but no status stage was "
                         "submitted")
        detail = ("after data completion the state machine must wait in "
                  "EP0_WAIT_STATUS for XferNotReady(status=2); no phase-4/5 "
                  "transition followed, so the readiness event never arrived or "
                  "was ignored")
        next_steps = [
            "check the type==3 && status==2 branch in ud_event()",
            "check that the data completion path calls ud_status_stage()",
            "verify STATUS3 is used after a data stage",
        ]
    elif last_phase == 4:
        healthy = False
        if last_status == 5:
            failure = "status wait out of order"
            failure_point = ("XferNotReady(status=2) arrived while the state "
                             "machine was not waiting for the status stage")
            detail = ("a completion was lost, the state was reset by a retry, or "
                      "this readiness belongs to an earlier transfer")
            next_steps = [
                "check the ordering of the EP0 completion and readiness events",
                "check that ud.state is still EP0_WAIT_STATUS when the event "
                "arrives",
            ]
        else:
            failure = "status not submitted"
            failure_point = ("the controller announced status-stage readiness but "
                             "no status TRB submit was recorded")
            detail = ("ud_start() either failed before the phase-5 note or the "
                      "record was cut inside the submit")
            next_steps = [
                "check the status TRB type (STATUS2 vs STATUS3) and direction",
                "check the ud_start() return value and the note placement",
            ]
    elif last_phase == 5:
        healthy = False
        if last_status == 2:
            failure = "status submit rejected"
            failure_point = ("the controller rejected the status-stage TRB "
                             "(DEPCMD status nonzero)")
            detail = ("the readiness direction and ud.ep0_status3 decide between "
                      "STATUS2 and STATUS3; a mismatch is rejected")
            next_steps = [
                "check STATUS2 vs STATUS3 selection (ud.ep0_status3)",
                "check the endpoint direction from the readiness event",
            ]
        else:
            failure = "status stage incomplete"
            failure_point = ("the status stage was submitted but its completion "
                             "event never arrived")
            detail = ("the host is waiting for the status stage to finish; a "
                      "direction/type mismatch or a lost completion event leaves "
                      "it hanging")
            next_steps = [
                "check the status TRB type and direction",
                "check that the completion event is drained and not consumed by "
                "another path",
            ]
    elif last_phase == 6:
        healthy = False
        failure = "setup not rearmed"
        failure_point = ("the status stage completed but SETUP was not re-armed")
        detail = ("the next control request cannot be received; after phase 6 "
                  "the code calls ud_start_setup() and records phase 7")
        next_steps = [
            "check ud_start_setup() and its ud_start() return value",
            "check for a crash between the completion and the re-arm",
        ]
    elif last_phase == 16:
        healthy = False
        failure = "bus reset without rebuild"
        failure_point = "a bus reset arrived and EP0 was not rebuilt"
        detail = ("ud_ep0_rebuild() runs after the reset note; a missing phase 17 "
                  "means it returned early (look for 'EP0 rebuild failed') or "
                  "never ran")
        next_steps = [
            "check DEPSTARTCFG/SETEPCONFIG/SETTRANSFRESOURCE/DALEPENA return codes",
            "if the log shows repeated resets, fix the reset storm first",
        ]
    elif last_phase == 17:
        healthy = False
        failure = "rebuilt, no setup"
        failure_point = ("EP0 was rebuilt after a bus reset but no SETUP "
                         "followed before this snapshot")
        detail = ("the report was taken with EP0 armed and waiting: if it came "
                  "from the panic pump, the host had not re-enumerated up to "
                  "that point; if it came after a reopen attempt, the host did "
                  "not complete enumeration")
        after = context["after_report"]  # type: ignore
        if after["lines"]:
            if after["bus_reset"] and not after["setup"]:
                detail += (". After the probe report the log shows %s but no "
                           "SETUP" % count_text(after["bus_reset"],
                                              "bus reset"))
            elif after["setup"]:
                detail += (". After the probe report the log shows %s, so the "
                           "host did re-enumerate"
                           % count_text(after["setup"], "SETUP line"))
        else:
            detail += ". No output followed the probe report"
        next_steps = [
            "close and reopen the host port and watch for bus reset / configured "
            "lines after the probe report",
            "try the other host OS and a direct cable without a hub",
            "if the host re-enumerated but the device saw no SETUP, inspect the "
            "reset/connect ordering",
        ]
    elif last_phase is not None:
        healthy = False
        failure = "unknown phase"
        failure_point = ("the last word has phase %s, which this decoder does "
                         "not know" % phase_name(last_phase))
        detail = ("the decoder tables are older than the kernel; update them from "
                  "the note sites in bsd/dev/kebab_usb.c")
        next_steps = ["add the new phase code to PHASES and re-run --selftest"]

    if last_decoded is not None and last_status is not None:
        status_code = last_status
        if status_code not in STATUSES and not 16 <= status_code <= 31:
            anomalies.append("last word has an unknown status")

    verdict = {
        "healthy": healthy,
        "failure": failure,
        "failure_point": failure_point,
        "headline": headline,
        "detail": detail,
        "next_steps": next_steps,
        "last_phase": last["phase"] if last else None,
        "last_request": last["request"] if last else None,
        "last_status": last["status"] if last else None,
        "last_event_bus_reset": last_phase == 16,
        "bus_reset_seen": bool(last_phase == 16 or reset_positions),
        "last_reset_seq": last_reset_seq,
        "transitions_since_reset": transitions_since_reset,
        "status_stage_submitted": submitted,
        "status_stage_submitted_ever": submitted_ever,
        "status_stage_completed": completed,
        "status_stage_completed_ever": completed_ever,
        "status_stage_submitted_evidence": submit_evidence,
        "status_stage_submitted_ever_evidence": submit_ever_evidence,
        "status_stage_completed_evidence": complete_evidence,
        "status_stage_completed_ever_evidence": complete_ever_evidence,
        "sequence_wrapped": sequence["wrapped"],
        "capture": capture,
        "context_complete": context_complete,
        "anomalies": anomalies,
    }

    report_view = {
        "header": None,
        "last": last,
        "history": {
            "lines": report.history_lines,
            "slots": slot_count,
            "complete": history_complete,
            "partial_line": report.partial_history,
            "front_truncated": front_truncated,
            "history_lags_header": history_lags_header,
            "zero_slots": zero_count,
            "slot_gaps": len(slot_gaps),
            "sequence_gaps": len(sequence_gaps),
            "header_lost": header is None,
        },
        "transitions": transitions,
        "sequence": sequence,
        "sequence_summary": _sequence_summary(transitions, last),
        "verdict": verdict,
    }
    if header_decoded is not None:
        report_view["header"] = {
            "present": True,
            "line": header.line,
            "word": header.word,
            "word_hex": word_hex(header.word),
            "seq": header_decoded["seq"],
            "request": _decoded(header_decoded["request"],
                                request_name(header_decoded["request"])),
            "phase": _decoded(header_decoded["phase"],
                              phase_name(header_decoded["phase"])),
            "status": _decoded(header_decoded["status"],
                               status_name(header_decoded["status"])),
            "partial": header.partial,
            "fields_consistent": header.consistent,
        }
    return report_view


def analyze_text(text: str, source: str = "<stdin>") -> Dict[str, object]:
    lines = text.splitlines()
    parsed = parse_lines(lines)
    all_reports: List[Report] = parsed["reports"]  # type: ignore
    reports = [report for report in all_reports
               if report.header is not None or report.slots]

    counts = {
        "lines": len(lines),
        "probe_lines": parsed["probe_lines"],
        "noise_lines": parsed["noise_lines"],
        "reports": len(reports),
        "malformed_probe_lines": parsed["malformed"],
    }

    if not reports:
        return {
            "schema": SCHEMA_VERSION,
            "source": source,
            "counts": counts,
            "reports": [],
            "selected": None,
            "report": None,
            "context": _build_context(lines, None),
        }

    with_history = [index for index, report in enumerate(reports)
                    if report.slots]
    selected_index = with_history[-1] if with_history else len(reports) - 1
    selected = reports[selected_index]
    context = _build_context(lines, selected)
    report_view = _analyze_report(selected, context)

    summaries = []
    for index, report in enumerate(reports, 1):
        if report.history_lines == 0:
            kind = "baseline (header only)"
        elif report.header is None:
            kind = "%d history lines, header missing" % report.history_lines
        else:
            kind = "%d history lines, %d slots" % (
                report.history_lines, len(report.slots))
        summaries.append({
            "index": index,
            "line": report.last_line,
            "header": report.header is not None,
            "word": report.header.word if report.header else None,
            "word_hex": word_hex(report.header.word)
            if report.header else None,
            "history_lines": report.history_lines,
            "slots": len(report.slots),
            "transitions": sum(1 for slot in report.slots if slot.word),
            "kind": kind,
        })

    return {
        "schema": SCHEMA_VERSION,
        "source": source,
        "counts": counts,
        "reports": summaries,
        "selected": selected_index + 1,
        "report": report_view,
        "context": context,
    }


def format_human(result: Dict[str, object]) -> str:
    counts = result["counts"]  # type: ignore
    lines: List[str] = []
    lines.append("EP0 probe decoder: %s" % result["source"])
    lines.append("  %d lines, %d probe lines, %d kernel noise lines, "
                 "%d malformed probe lines"
                 % (counts["lines"], counts["probe_lines"],
                    counts["noise_lines"], counts["malformed_probe_lines"]))

    if result["selected"] is None:
        lines.append("")
        lines.append("No complete %r report found." % PROBE_PREFIX)
        if counts["probe_lines"]:
            lines.append("  %s were present, but none had a complete header "
                         "or history line."
                         % count_text(counts["probe_lines"], "probe line"))
        lines.append("  Check that the kernel contains patch 0091 and that "
                     "kebabusb=1 is set.")
        return "\n".join(lines) + "\n"

    reports = result["reports"]  # type: ignore
    summary = ", ".join(
        "%d at line %d (%s)" % (item["index"], item["line"], item["kind"])
        for item in reports)
    lines.append("  reports: %s; decoding report %d"
                 % (summary, result["selected"]))
    if result["selected"] != len(reports):
        lines.append("  note: report %d is later but has no history; decoded the "
                     "newest report with history." % len(reports))

    report = result["report"]  # type: ignore
    last = report["last"]
    if last is not None:
        lines.append("")
        lines.append("Last word %s" % last["word_hex"])
        lines.append("  seq %d, request %d (%s), phase %d (%s), status %d (%s)"
                     % (last["seq"], last["request"]["code"],
                        last["request"]["name"], last["phase"]["code"],
                        last["phase"]["name"], last["status"]["code"],
                        last["status"]["name"]))
        if last["source"] == "history":
            lines.append("  (header line missing; this word is the newest "
                         "history entry)")

    transitions = report["transitions"]
    lines.append("")
    if transitions:
        lines.append("History, oldest first (%d transition%s):"
                     % (len(transitions), "" if len(transitions) == 1 else "s"))
        lines.append("  seq  word        %-24s  %-28s  %-27s  %s"
                     % ("request", "phase", "status", "note"))
        for item in transitions:
            lines.append("  %3d  %s  %-24s  %-28s  %-27s  %s"
                         % (item["seq"], item["word_hex"],
                            item["request"]["name"], item["phase"]["name"],
                            item["status"]["name"], item["note"]))
    else:
        lines.append("History: none in this report")

    lines.append("")
    lines.append("Sequence: %s" % report["sequence_summary"])

    verdict = report["verdict"]
    lines.append("")
    lines.append("Verdict")
    if last is not None:
        lines.append("  last phase reached       : %d (%s)"
                     % (last["phase"]["code"], last["phase"]["name"]))
    else:
        lines.append("  last phase reached       : none")
    def stage_text(primary: object, ever: object, ever_evidence: str) -> str:
        if primary:
            return "yes in the last transfer"
        if ever:
            return ("no in the last transfer; yes in the window (%s)"
                    % ever_evidence)
        return "no in the last transfer or the window"

    lines.append("  status stage submitted   : %s"
                 % stage_text(verdict["status_stage_submitted"],
                              verdict["status_stage_submitted_ever"],
                              verdict["status_stage_submitted_ever_evidence"]))
    lines.append("  status stage completed   : %s"
                 % stage_text(verdict["status_stage_completed"],
                              verdict["status_stage_completed_ever"],
                              verdict["status_stage_completed_ever_evidence"]))
    reset_text = "yes" if verdict["last_event_bus_reset"] else "no"
    if not verdict["last_event_bus_reset"] and verdict["last_reset_seq"] is not None:
        count = verdict["transitions_since_reset"]
        reset_text += ("; last bus reset at seq %d (%d transition%s earlier)"
                       % (verdict["last_reset_seq"], count,
                          "" if count == 1 else "s"))
    elif verdict["last_reset_seq"] is None:
        reset_text += "; no bus reset in the window"
    lines.append("  last event was bus reset : %s" % reset_text)
    lines.append("  sequence counter         : %s" % report["sequence"]["note"])
    lines.append("  most likely failure      : %s"
                 % (verdict["failure_point"] or "none"))
    if verdict["detail"]:
        lines.append("  reading                  : %s" % verdict["detail"])
    for index, step in enumerate(verdict["next_steps"]):
        lines.append("  %-24s : %s"
                     % ("next" if index == 0 else "", step))
    lines.append("  capture                  : %s" % verdict["capture"])
    if verdict["anomalies"]:
        lines.append("  anomalies                : %s"
                     % "; ".join(verdict["anomalies"]))

    context = result["context"]  # type: ignore
    lines.append("")
    lines.append("Kernel context")
    last_setup = context["last_setup"]
    if last_setup is not None:
        lines.append("  last setup line          : 0x%02x/0x%02x value 0x%04x "
                     "len %d -> %s"
                     % (last_setup["type"], last_setup["req"],
                        last_setup["value"], last_setup["length"],
                        last_setup["request"]["name"]))
    else:
        lines.append("  last setup line          : none")
    hints = context["hints"]
    lines.append("  hints                    : data-stage rejects %d, rebuild "
                 "failures %d, unsupported requests %d"
                 % (hints["data_rejected"], hints["rebuild_failed"],
                    len(context["unsupported"])))
    after = context["after_report"]
    if after["lines"]:
        lines.append("  after the last probe line: %s (bus reset %d, "
                     "connected %d, configured %d, setup %d, disconnect %d)"
                     % (count_text(after["lines"], "line"),
                        after["bus_reset"], after["connected"],
                        after["configured"], after["setup"],
                        after["disconnect"]))
    else:
        lines.append("  after the last probe line: no further output")

    return "\n".join(lines) + "\n"


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", nargs="?", default="-",
                        help="captured console log, or - for stdin")
    parser.add_argument("--json", action="store_true",
                        help="emit the decoded result as JSON")
    parser.add_argument("--selftest", action="store_true",
                        help="run the synthetic-log selftest (no hardware)")
    args = parser.parse_args(argv)

    if args.selftest:
        return run_selftest()

    if args.log == "-":
        source = "<stdin>"
        try:
            data = sys.stdin.buffer.read()
        except OSError as exc:
            print("decode-ep0-probe: %s" % exc, file=sys.stderr)
            return 2
    else:
        source = args.log
        try:
            with open(args.log, "rb") as handle:
                data = handle.read()
        except OSError as exc:
            print("decode-ep0-probe: %s" % exc, file=sys.stderr)
            return 2

    result = analyze_text(data.decode("utf-8", "replace"), source)
    if args.json:
        json.dump(result, sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        sys.stdout.write(format_human(result))
    return 0 if result["selected"] is not None else 1


# --------------------------------------------------------------- selftest

class _KernelSim:
    """Produce the exact text kebab_usb_ep0_format() would produce."""

    def __init__(self, size: int = HISTORY_SIZE):
        self.size = size
        self.ring = [0] * size
        self.seq = 0
        self.progress = 0

    def note(self, request: int, phase: int, status: int = 0) -> int:
        self.seq += 1
        word = pack_word(self.seq, request, phase, status)
        self.ring[self.seq & (self.size - 1)] = word
        self.progress = word
        return word

    def report_lines(self) -> List[str]:
        word = self.progress
        lines = ["%s progress 0x%08x seq %u req %u phase %u status %u"
                 % (PROBE_PREFIX, word, (word >> 24) & 0xFF,
                    (word >> 8) & 0xFF, (word >> 16) & 0xFF, word & 0xFF)]
        for start in range(0, self.size, HISTORY_PER_LINE):
            cells = []
            for offset in range(HISTORY_PER_LINE):
                slot = (self.seq + 1 + start + offset) & (self.size - 1)
                cells.append("[%02u]0x%08x" % (slot, self.ring[slot]))
            lines.append(PROBE_PREFIX + " " + " ".join(cells))
        return lines

    def log(self) -> str:
        return "\n".join(self.report_lines()) + "\n"


def _transfer(sim: _KernelSim, request: int, data: bool = True) -> None:
    sim.note(request, 1)
    if data:
        sim.note(request, 2)
        sim.note(request, 3)
    sim.note(0, 4)
    sim.note(request, 5)
    sim.note(request, 6)
    sim.note(request, 7)


def _healthy_sim() -> _KernelSim:
    sim = _KernelSim()
    sim.note(0, 16)
    sim.note(0, 17)
    _transfer(sim, 1)                 # GET_DESCRIPTOR, data stage
    _transfer(sim, 2, data=False)     # SET_ADDRESS
    _transfer(sim, 1)                 # GET_DESCRIPTOR (config)
    _transfer(sim, 4, data=False)     # SET_CONFIGURATION
    _transfer(sim, 5)                 # SET_LINE_CODING
    return sim


def run_selftest() -> int:
    """Build synthetic logs, run the real decoder, assert the verdicts."""
    import contextlib
    import io
    import os
    import tempfile

    results: List[Tuple[str, str, bool, str]] = []

    def check(section: str, name: str, condition: object,
              detail: str = "") -> None:
        results.append((section, name, bool(condition), detail))

    def build(sim: _KernelSim) -> Dict[str, object]:
        return analyze_text(sim.log(), "selftest")

    print("EP0 probe decoder selftest (no hardware)")

    # 0. Hand-written fixture in the exact kernel format.  This does not use
    # _KernelSim, so a shared formatting mistake cannot hide here.
    fixture = (
        "KEBAB EP0 PROBE: progress 0x04020102 seq 4 req 1 phase 2 status 2\n"
        "KEBAB EP0 PROBE: [05]0x00000000 [06]0x00000000 [07]0x00000000 "
        "[08]0x00000000\n"
        "KEBAB EP0 PROBE: [09]0x00000000 [10]0x00000000 [11]0x00000000 "
        "[12]0x00000000\n"
        "KEBAB EP0 PROBE: [13]0x00000000 [14]0x00000000 [15]0x00000000 "
        "[16]0x00000000\n"
        "KEBAB EP0 PROBE: [17]0x00000000 [18]0x00000000 [19]0x00000000 "
        "[20]0x00000000\n"
        "KEBAB EP0 PROBE: [21]0x00000000 [22]0x00000000 [23]0x00000000 "
        "[24]0x00000000\n"
        "KEBAB EP0 PROBE: [25]0x00000000 [26]0x00000000 [27]0x00000000 "
        "[28]0x00000000\n"
        "KEBAB EP0 PROBE: [29]0x00000000 [30]0x00000000 [31]0x00000000 "
        "[00]0x00000000\n"
        "KEBAB EP0 PROBE: [01]0x01100000 [02]0x02110000 [03]0x03010100 "
        "[04]0x04020102\n")
    result = analyze_text(fixture, "selftest")
    report = result["report"]
    verdict = report["verdict"]
    check("static kernel fixture", "nine probe lines",
          result["counts"]["probe_lines"] == 9)
    check("static kernel fixture", "four transitions from slots 1..4",
          len(report["transitions"]) == 4)
    check("static kernel fixture", "28 zero slots dropped",
          report["history"]["zero_slots"] == 28)
    check("static kernel fixture", "last word 0x04020102",
          report["last"]["word"] == 0x04020102 and
          report["last"]["status"]["name"] == "DEPCMD rejected")
    check("static kernel fixture", "failure is data submit rejected",
          verdict["failure"] == "data submit rejected")
    check("static kernel fixture", "absolute sequence is 4",
          report["sequence"]["total"] == 4)
    check("static kernel fixture", "history is complete",
          report["history"]["complete"] is True)

    # 1. Healthy enumeration.
    sim = _healthy_sim()
    result = build(sim)
    report = result["report"]
    verdict = report["verdict"]
    check("healthy enumeration", "one report found", result["selected"] == 1)
    check("healthy enumeration", "last phase is SETUP re-armed",
          report["last"]["phase"]["code"] == 7,
          str(report["last"]))
    check("healthy enumeration", "verdict is healthy", verdict["healthy"] is True)
    check("healthy enumeration", "no failure point",
          verdict["failure"] == "none")
    check("healthy enumeration", "status stage submitted",
          verdict["status_stage_submitted"] is True)
    check("healthy enumeration", "status stage completed",
          verdict["status_stage_completed"] is True)
    check("healthy enumeration", "full 32-entry window",
          report["history"]["complete"] is True and
          len(report["transitions"]) == HISTORY_SIZE)
    check("healthy enumeration", "no sequence wrap",
          report["sequence"]["wrapped"] is False)
    check("healthy enumeration", "absolute count is ambiguous",
          report["sequence"]["ambiguous"] is True)
    check("healthy enumeration", "capture is complete",
          "complete" in verdict["capture"])

    # 2. Baseline report: header only.
    baseline = _KernelSim()
    baseline.note(4, 1)
    baseline_log = baseline.report_lines()[0] + "\n"
    result = analyze_text(baseline_log, "selftest")
    verdict = result["report"]["verdict"]
    check("baseline header only", "one baseline report", result["selected"] == 1)
    check("baseline header only", "no transitions",
          len(result["report"]["transitions"]) == 0)
    check("baseline header only", "capture says baseline",
          "baseline" in verdict["capture"])
    check("baseline header only", "no failure yet",
          verdict["failure"] == "no data" and verdict["healthy"] is None)

    # 3. Stall at the data stage (DEPCMD rejected).
    stalled = _KernelSim()
    stalled.note(0, 16)
    stalled.note(0, 17)
    stalled.note(5, 1)
    stalled.note(5, 2, 2)
    stalled_log = ("KEBAB USB: setup 21/20 value 0x0000 len 7\n"
                   "KEBAB USB: SET_LINE_CODING data stage rejected\n" +
                   stalled.log())
    result = analyze_text(stalled_log, "selftest")
    report = result["report"]
    verdict = report["verdict"]
    check("data stage stall", "last phase is data submitted",
          report["last"]["phase"]["code"] == 2)
    check("data stage stall", "status is DEPCMD rejected",
          report["last"]["status"]["name"] == "DEPCMD rejected")
    check("data stage stall", "failure names the rejection",
          verdict["failure"] == "data submit rejected")
    check("data stage stall", "no status stage",
          verdict["status_stage_submitted"] is False)
    check("data stage stall", "note mentions the rejection",
          "rejected" in report["transitions"][-1]["note"])
    check("data stage stall", "kernel hint counted",
          result["context"]["hints"]["data_rejected"] == 1)

    # 4. Reset in the middle of a transfer, then a rebuild.
    reset = _KernelSim()
    reset.note(0, 16)
    reset.note(0, 17)
    reset.note(1, 1)
    reset.note(1, 2)
    reset.note(0, 16)
    reset.note(0, 17)
    result = build(reset)
    report = result["report"]
    verdict = report["verdict"]
    check("reset mid-transfer", "last phase is EP0 rebuild",
          report["last"]["phase"]["code"] == 17)
    check("reset mid-transfer", "last event is not a reset",
          verdict["last_event_bus_reset"] is False)
    check("reset mid-transfer", "a reset was seen",
          verdict["bus_reset_seen"] is True)
    check("reset mid-transfer", "failure is rebuilt, no SETUP",
          verdict["failure"] == "rebuilt, no setup")
    check("reset mid-transfer", "last reset seq and distance",
          verdict["last_reset_seq"] == 5 and
          verdict["transitions_since_reset"] == 1)
    check("reset mid-transfer", "sequence summary keeps the request",
          "GET_DESCRIPTOR" in report["sequence_summary"])
    check("reset mid-transfer", "phase-2 note blames the reset",
          "reset" in report["transitions"][3]["note"])
    check("reset mid-transfer", "no status stage",
          verdict["status_stage_submitted"] is False)
    check("reset mid-transfer", "no status stage earlier either",
          verdict["status_stage_submitted_ever"] is False)

    # 5. Reset with no rebuild.
    reset2 = _KernelSim()
    reset2.note(0, 16)
    reset2.note(0, 17)
    reset2.note(1, 1)
    reset2.note(1, 2)
    reset2.note(0, 16)
    result = build(reset2)
    verdict = result["report"]["verdict"]
    check("reset without rebuild", "last event is a bus reset",
          verdict["last_event_bus_reset"] is True)
    check("reset without rebuild", "failure names the missing rebuild",
          verdict["failure"] == "bus reset without rebuild")
    check("reset without rebuild", "no transitions since the reset",
          verdict["transitions_since_reset"] == 0)

    # 5b. A completed transfer followed by a reset and rebuild.
    completed_reset = _KernelSim()
    completed_reset.note(0, 16)
    completed_reset.note(0, 17)
    _transfer(completed_reset, 1)
    completed_reset.note(0, 16)
    completed_reset.note(0, 17)
    result = build(completed_reset)
    report = result["report"]
    verdict = report["verdict"]
    check("reset after completion", "last phase is a rebuild",
          report["last"]["phase"]["code"] == 17)
    check("reset after completion", "summary says completed, then reset",
          "completed; a bus reset followed" in report["sequence_summary"],
          report["sequence_summary"])
    check("reset after completion", "failure is rebuilt, no setup",
          verdict["failure"] == "rebuilt, no setup")

    # 6. Wrapped sequence counter inside the 32-entry window.
    wrapped = _KernelSim()
    wrapped.note(0, 16)
    wrapped.note(0, 17)
    pattern = [(1, 1), (1, 2), (1, 3), (0, 4), (1, 5), (1, 6), (1, 7)]
    index = 0
    while wrapped.seq < 275:
        request, phase = pattern[index % len(pattern)]
        wrapped.note(request, phase)
        index += 1
    result = build(wrapped)
    report = result["report"]
    verdict = report["verdict"]
    check("wrapped sequence", "counter wrapped in the window",
          report["sequence"]["wrapped"] is True)
    check("wrapped sequence", "lower bound is 271 or more",
          report["sequence"]["lower_bound"] == 275)
    check("wrapped sequence", "first and last sequence bytes",
          report["transitions"][0]["seq"] == 244 and
          report["transitions"][-1]["seq"] == 19)
    check("wrapped sequence", "last phase is SETUP re-armed",
          report["last"]["phase"]["code"] == 7)
    check("wrapped sequence", "healthy despite the wrap",
          verdict["healthy"] is True)
    check("wrapped sequence", "note mentions the wrap",
          "wrapped" in report["sequence"]["note"])

    # 6b. More than 256 transitions, but no wrap inside the visible window.
    wrapped2 = _KernelSim()
    wrapped2.note(0, 16)
    wrapped2.note(0, 17)
    index = 0
    while wrapped2.seq < 300:
        request, phase = pattern[index % len(pattern)]
        wrapped2.note(request, phase)
        index += 1
    result = build(wrapped2)
    report = result["report"]
    check("wrapped sequence", "no false wrap for 300 transitions",
          report["sequence"]["wrapped"] is False)
    check("wrapped sequence", "earlier wrap stays possible",
          "earlier wrap cannot be excluded" in report["sequence"]["note"])

    # 7. Truncated panic report: cut in the middle of the history.
    truncated = _KernelSim()
    truncated.note(0, 16)
    truncated.note(0, 17)
    index = 0
    while truncated.seq < 34:
        request, phase = pattern[index % len(pattern)]
        truncated.note(request, phase)
        index += 1
    truncated.note(1, 1)
    truncated.note(1, 2)
    truncated.note(1, 3)
    lines = truncated.report_lines()
    truncated_log = ("\n".join(lines[:3]) + "\n" + lines[3][:70] + "\n")
    result = analyze_text(truncated_log, "selftest")
    report = result["report"]
    verdict = report["verdict"]
    check("truncated log", "11 slots parsed",
          report["history"]["slots"] == 11,
          str(report["history"]))
    check("truncated log", "partial line recorded",
          report["history"]["partial_line"] is True)
    check("truncated log", "history lags the header",
          report["history"]["history_lags_header"] is True)
    check("truncated log", "not complete",
          report["history"]["complete"] is False)
    check("truncated log", "last word still comes from the header",
          report["last"]["seq"] == 37 and
          report["last"]["phase"]["code"] == 3)
    check("truncated log", "verdict classifies the last word",
          verdict["failure"] == "status not submitted")
    check("truncated log", "last transfer lacks a status, an older one has it",
          verdict["status_stage_submitted"] is False and
          verdict["status_stage_submitted_ever"] is True)
    check("truncated log", "verdict says the context is incomplete",
          verdict["context_complete"] is False)
    check("truncated log", "capture is truncated",
          "truncated" in verdict["capture"])
    check("truncated log", "anomaly names the missing newest entries",
          any("newest entries" in item for item in verdict["anomalies"]))
    human = format_human(result)
    check("truncated log", "human output shows the truncation",
          "truncated" in human)

    # 7b. A header cut inside the hex digits yields no complete report.
    result = analyze_text("KEBAB EP0 PROBE: progress 0x0\n", "selftest")
    check("truncated log", "cut header is not a report",
          result["selected"] is None and
          result["counts"]["probe_lines"] == 1 and
          result["counts"]["malformed_probe_lines"] == 1)

    # 7c. A panic report cut down to just its header is not a baseline.
    header_only = _KernelSim()
    header_only.note(0, 16)
    header_only.note(0, 17)
    header_only.note(1, 1)
    header_only.note(1, 2)
    header_only.note(1, 3)
    result = analyze_text(header_only.report_lines()[0] + "\n", "selftest")
    verdict = result["report"]["verdict"]
    check("truncated log", "header-only panic is not called a baseline",
          "baseline" not in verdict["capture"])
    check("truncated log", "header-only still classifies the last word",
          verdict["failure"] == "status not submitted")
    check("truncated log", "header-only reports the missing history",
          any("history is missing" in item for item in verdict["anomalies"]))

    # 8. Kernel noise between probe lines, CRLF and ANSI codes.
    noisy = _healthy_sim()
    base = _KernelSim()
    base.note(4, 1)
    probe_lines = noisy.report_lines()
    noise = [
        "KEBAB USB: setup 80/06 value 0x0100 len 64",
        "\x1b[0m[    1.234] panic(cpu 0 caller 0xffffff): kebab usb console test",
        "KEBAB USB: host said hello",
        "KEBAB USB: configured, notify=5 bulk_out=2 bulk_in=3",
    ]
    log_lines = [base.report_lines()[0]]
    for index, line in enumerate(probe_lines):
        log_lines.append(line)
        if index < len(noise):
            log_lines.append(noise[index])
    result = analyze_text("\r\n".join(log_lines) + "\r\n", "selftest")
    report = result["report"]
    verdict = report["verdict"]
    check("kernel noise", "two reports found",
          result["counts"]["reports"] == 2)
    check("kernel noise", "selected report is the one with history",
          result["selected"] == 2)
    check("kernel noise", "noise lines counted",
          result["counts"]["noise_lines"] == len(noise))
    check("kernel noise", "no malformed probe lines",
          result["counts"]["malformed_probe_lines"] == 0)
    check("kernel noise", "all 32 transitions survive",
          len(report["transitions"]) == HISTORY_SIZE)
    check("kernel noise", "last word matches the simulation",
          report["last"]["word"] == noisy.progress)
    check("kernel noise", "verdict is still healthy",
          verdict["healthy"] is True)

    # 9. Unknown request/phase/status codes must not crash.
    unknown = _KernelSim()
    unknown.note(0, 16)
    unknown.note(1, 5, 20)
    unknown.note(42, 99, 200)
    result = build(unknown)
    report = result["report"]
    verdict = report["verdict"]
    check("unknown codes", "request name is unknown(0x2a)",
          report["last"]["request"]["name"] == "unknown(0x2a)")
    check("unknown codes", "phase name is unknown(0x63)",
          report["last"]["phase"]["name"] == "unknown(0x63)")
    check("unknown codes", "status name is unknown(0xc8)",
          report["last"]["status"]["name"] == "unknown(0xc8)")
    check("unknown codes", "DEPCMD status 20 decodes to status 4",
          report["transitions"][1]["status"]["name"] == "DEPCMD status 4")
    check("unknown codes", "verdict names the unknown phase",
          verdict["failure"] == "unknown phase")
    check("unknown codes", "anomaly lists the unknown codes",
          any("unknown code" in item for item in verdict["anomalies"]))

    # 10. Status stage submitted but never completed.
    status5 = _KernelSim()
    status5.note(0, 16)
    status5.note(0, 17)
    status5.note(1, 1)
    status5.note(1, 2)
    status5.note(1, 3)
    status5.note(0, 4)
    status5.note(1, 5)
    result = build(status5)
    report = result["report"]
    verdict = report["verdict"]
    check("status stage hanging", "submitted but not completed",
          verdict["status_stage_submitted"] is True and
          verdict["status_stage_completed"] is False)
    check("status stage hanging", "failure names the missing completion",
          verdict["failure"] == "status stage incomplete")
    check("status stage hanging", "summary says never completed",
          "never completed" in report["sequence_summary"],
          report["sequence_summary"])

    # 11. Status complete with no SETUP re-arm.
    status6 = _KernelSim()
    status6.note(0, 16)
    status6.note(0, 17)
    status6.note(1, 1)
    status6.note(1, 2)
    status6.note(1, 3)
    status6.note(0, 4)
    status6.note(1, 5)
    status6.note(1, 6)
    result = build(status6)
    report = result["report"]
    verdict = report["verdict"]
    check("missing SETUP re-arm", "status completed",
          verdict["status_stage_completed"] is True)
    check("missing SETUP re-arm", "failure names the missing re-arm",
          verdict["failure"] == "setup not rearmed")
    check("missing SETUP re-arm", "summary says completed but not re-armed",
          "SETUP was not re-armed" in report["sequence_summary"],
          report["sequence_summary"])

    # 12. CLI and JSON round trip, using a real temporary file.
    json_path = None
    try:
        handle, json_path = tempfile.mkstemp(prefix="ep0-selftest-",
                                             suffix=".log", text=True)
        with os.fdopen(handle, "w") as stream:
            stream.write(_healthy_sim().log())
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = main(["--json", json_path])
        payload = json.loads(buffer.getvalue())
        check("json mode", "exit status 0", code == 0)
        check("json mode", "schema version", payload["schema"] == 1)
        check("json mode", "verdict is serialized",
              payload["report"]["verdict"]["healthy"] is True)
        check("json mode", "transitions are serialized",
              len(payload["report"]["transitions"]) == HISTORY_SIZE)
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = main([json_path])
        text = buffer.getvalue()
        check("human mode", "exit status 0", code == 0)
        check("human mode", "prints the history table",
              "History, oldest first" in text)
        check("human mode", "prints the verdict", "Verdict" in text)
    finally:
        if json_path:
            try:
                os.unlink(json_path)
            except OSError:
                pass

    failed = [item for item in results if not item[2]]
    section = None
    for name, label, ok, detail in results:
        if name != section:
            section = name
            print("[%s]" % section)
        print("  %s %s%s" % ("ok  " if ok else "FAIL", label,
                             (" -- " + detail) if (detail and not ok) else ""))
    print("")
    print("selftest: %d/%d passed" % (len(results) - len(failed), len(results)))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
