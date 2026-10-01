#!/usr/bin/env python3
"""Attribute loaded firmware sections, not discarded objects or ELF debug data.

Run after a PlatformIO build. Uses the target's GNU nm and size tools; no
third-party Python packages are needed. JSON output can be kept under .dev/.
"""
import argparse
import collections
import json
from pathlib import Path
import re
import subprocess

LOADED = {".flash.text", ".flash.rodata", ".flash.appdesc", ".iram0.text",
          ".iram0.vectors", ".dram0.data", ".rtc.text", ".rtc.data",
          ".rtc.force_fast", ".rtc.force_slow", ".iram0.data"}


def inspect(build_dir, toolchain):
    suffix = ".exe" if (toolchain / "xtensa-esp32s3-elf-nm.exe").exists() else ""
    nm = subprocess.check_output([str(toolchain / ("xtensa-esp32s3-elf-nm" + suffix)),
                                  "-S", "--size-sort", "--radix=d", "-C",
                                  str(build_dir / "firmware.elf")], text=True)
    symbols = []
    for line in nm.splitlines():
        match = re.match(r"(\d+)\s+(\d+)\s+(\w)\s+(.*)", line)
        if match:
            symbols.append(dict(address=int(match[1]), size=int(match[2]),
                                type=match[3], name=match[4]))
    entries, sections = [], {}
    live, section, pending = False, "", ""
    for line in (build_dir / "firmware.map").read_text().splitlines():
        if line == "Linker script and memory map":
            live = True
        if not live:
            continue
        match = re.match(r"^(\.[\w.]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", line)
        if match:
            section = match[1]
            sections[section] = dict(address=int(match[2], 16), size=int(match[3], 16))
        if re.match(r"^\.[\w.]+\s*$", line):
            section = line.strip()
        match = re.match(r"^ (\.[^\s]+)(?:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(.+))?$", line)
        if match:
            pending = match[1]
            if match[2] and re.search(r"\.o(?:bj)?\)?$", match[4]):
                entries.append(dict(section=section, input=pending, address=int(match[2], 16),
                                    size=int(match[3], 16), object=match[4]))
            continue
        match = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(.+\.o(?:bj)?\)?)$", line)
        if match:
            entries.append(dict(section=section, input=pending, address=int(match[1], 16),
                                size=int(match[2], 16), object=match[3]))
    objects, archives = collections.Counter(), collections.Counter()
    for entry in entries:
        if entry["section"] not in LOADED or not entry["size"]:
            continue
        obj = entry["object"]
        objects[obj] += entry["size"]
        match = re.search(r"(?:/|\\)([^/\\]+\.a)\(", obj)
        group = match[1] if match else ("Application sources" if "/src/" in obj else obj)
        archives[group] += entry["size"]
    def loaded_symbol(symbol):
        return any(info["address"] <= symbol["address"] < info["address"] + info["size"]
                   for name, info in sections.items() if name in LOADED)
    flash_symbols = [symbol for symbol in symbols if loaded_symbol(symbol)]
    unique_symbols = {}
    for symbol in flash_symbols:
        unique_symbols.setdefault((symbol["address"], symbol["size"]), symbol)
    assets = [symbol for symbol in flash_symbols if re.search(
        r"Manrope|Inter_18|Roboto|nicoclean|^ucFont$|^ucSmallFont$|WeatherIcons.*k_|WeatherIcons.*kWeather|logo_|wifi_.*qr|INDEX_HTML", symbol["name"])]
    return dict(sections=sections, loaded_section_bytes=sum(info["size"] for name, info in sections.items() if name in LOADED),
                attributed_bytes=sum(archives.values()), archives=archives.most_common(),
                objects=objects.most_common(), top_symbols=sorted(unique_symbols.values(), key=lambda s: s["size"], reverse=True)[:50],
                assets=assets, entries=[e for e in entries if e["section"] in LOADED])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path(".pio/build/TRMNL_7inch5_OG_DIY_Kit"))
    parser.add_argument("--toolchain", type=Path, required=True, help="Target bin directory containing GNU nm")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.build_dir, args.toolchain)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("Loaded sections:", result["loaded_section_bytes"], "bytes; attributed input sections:", result["attributed_bytes"])
    for name, size in result["archives"][:20]:
        print(f"{size:>9,}  {name}")
