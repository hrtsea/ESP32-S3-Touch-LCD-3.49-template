#!/usr/bin/env python3
"""Generate main/tz_cities.h: a curated ~120-city table mapping a friendly
city name to its IANA zone and the POSIX TZ string used at runtime via
setenv("TZ", ...); tzset();.

The POSIX strings are extracted from the system's tzdata (last line of each
TZif file), so DST rules stay correct as long as the build host's tzdata is
reasonably recent. Run from repo root:

    python3 scripts/gen_tz.py
"""

import os
import sys
import pathlib

ZONEINFO = pathlib.Path("/usr/share/zoneinfo")
OUT = pathlib.Path(__file__).resolve().parent.parent / "main" / "tz_cities.h"

# Continent groups in display order. Each entry: (display_name, IANA_zone).
# ~120 cities, weighted by population / common-pick. Includes all 30/45-min
# offsets (Newfoundland, India, Iran, Nepal, Adelaide, Chatham).
CONTINENTS = [
    ("Africa", [
        ("Cairo",         "Africa/Cairo"),
        ("Lagos",         "Africa/Lagos"),
        ("Johannesburg",  "Africa/Johannesburg"),
        ("Nairobi",       "Africa/Nairobi"),
        ("Casablanca",    "Africa/Casablanca"),
        ("Algiers",       "Africa/Algiers"),
        ("Addis Ababa",   "Africa/Addis_Ababa"),
        ("Accra",         "Africa/Accra"),
        ("Khartoum",      "Africa/Khartoum"),
        ("Dakar",         "Africa/Dakar"),
        ("Tunis",         "Africa/Tunis"),
        ("Kinshasa",      "Africa/Kinshasa"),
    ]),
    ("Americas", [
        ("New York",      "America/New_York"),
        ("Chicago",       "America/Chicago"),
        ("Denver",        "America/Denver"),
        ("Phoenix",       "America/Phoenix"),
        ("Los Angeles",   "America/Los_Angeles"),
        ("Anchorage",     "America/Anchorage"),
        ("Honolulu",      "Pacific/Honolulu"),
        ("Toronto",       "America/Toronto"),
        ("Vancouver",     "America/Vancouver"),
        ("St Johns",      "America/St_Johns"),
        ("Halifax",       "America/Halifax"),
        ("Mexico City",   "America/Mexico_City"),
        ("Guatemala",     "America/Guatemala"),
        ("Panama",        "America/Panama"),
        ("Havana",        "America/Havana"),
        ("Bogota",        "America/Bogota"),
        ("Lima",          "America/Lima"),
        ("Caracas",       "America/Caracas"),
        ("Santiago",      "America/Santiago"),
        ("Buenos Aires",  "America/Argentina/Buenos_Aires"),
        ("Sao Paulo",     "America/Sao_Paulo"),
        ("Manaus",        "America/Manaus"),
        ("Montevideo",    "America/Montevideo"),
        ("La Paz",        "America/La_Paz"),
        ("Asuncion",      "America/Asuncion"),
        ("San Juan PR",   "America/Puerto_Rico"),
    ]),
    ("Asia", [
        ("Tokyo",         "Asia/Tokyo"),
        ("Seoul",         "Asia/Seoul"),
        ("Pyongyang",     "Asia/Pyongyang"),
        ("Beijing",       "Asia/Shanghai"),
        ("Hong Kong",     "Asia/Hong_Kong"),
        ("Taipei",        "Asia/Taipei"),
        ("Singapore",     "Asia/Singapore"),
        ("Kuala Lumpur",  "Asia/Kuala_Lumpur"),
        ("Jakarta",       "Asia/Jakarta"),
        ("Bangkok",       "Asia/Bangkok"),
        ("Hanoi",         "Asia/Ho_Chi_Minh"),
        ("Manila",        "Asia/Manila"),
        ("Yangon",        "Asia/Yangon"),
        ("Dhaka",         "Asia/Dhaka"),
        ("Kolkata",       "Asia/Kolkata"),
        ("Mumbai",        "Asia/Kolkata"),
        ("Colombo",       "Asia/Colombo"),
        ("Kathmandu",     "Asia/Kathmandu"),
        ("Karachi",       "Asia/Karachi"),
        ("Tashkent",      "Asia/Tashkent"),
        ("Almaty",        "Asia/Almaty"),
        ("Tehran",        "Asia/Tehran"),
        ("Baghdad",       "Asia/Baghdad"),
        ("Riyadh",        "Asia/Riyadh"),
        ("Dubai",         "Asia/Dubai"),
        ("Jerusalem",     "Asia/Jerusalem"),
        ("Beirut",        "Asia/Beirut"),
        ("Yerevan",       "Asia/Yerevan"),
        ("Tbilisi",       "Asia/Tbilisi"),
        ("Yekaterinburg", "Asia/Yekaterinburg"),
        ("Novosibirsk",   "Asia/Novosibirsk"),
        ("Krasnoyarsk",   "Asia/Krasnoyarsk"),
        ("Irkutsk",       "Asia/Irkutsk"),
        ("Yakutsk",       "Asia/Yakutsk"),
        ("Vladivostok",   "Asia/Vladivostok"),
        ("Magadan",       "Asia/Magadan"),
        ("Kamchatka",     "Asia/Kamchatka"),
    ]),
    ("Europe", [
        ("London",        "Europe/London"),
        ("Dublin",        "Europe/Dublin"),
        ("Lisbon",        "Europe/Lisbon"),
        ("Reykjavik",     "Atlantic/Reykjavik"),
        ("Paris",         "Europe/Paris"),
        ("Madrid",        "Europe/Madrid"),
        ("Berlin",        "Europe/Berlin"),
        ("Amsterdam",     "Europe/Amsterdam"),
        ("Brussels",      "Europe/Brussels"),
        ("Zurich",        "Europe/Zurich"),
        ("Rome",          "Europe/Rome"),
        ("Vienna",        "Europe/Vienna"),
        ("Prague",        "Europe/Prague"),
        ("Warsaw",        "Europe/Warsaw"),
        ("Stockholm",     "Europe/Stockholm"),
        ("Oslo",          "Europe/Oslo"),
        ("Copenhagen",    "Europe/Copenhagen"),
        ("Helsinki",      "Europe/Helsinki"),
        ("Athens",        "Europe/Athens"),
        ("Bucharest",     "Europe/Bucharest"),
        ("Sofia",         "Europe/Sofia"),
        ("Istanbul",      "Europe/Istanbul"),
        ("Moscow",        "Europe/Moscow"),
        ("Kyiv",          "Europe/Kyiv"),
        ("Minsk",         "Europe/Minsk"),
        ("Riga",          "Europe/Riga"),
        ("Tallinn",       "Europe/Tallinn"),
    ]),
    ("Oceania", [
        ("Sydney",        "Australia/Sydney"),
        ("Melbourne",     "Australia/Melbourne"),
        ("Brisbane",      "Australia/Brisbane"),
        ("Perth",         "Australia/Perth"),
        ("Adelaide",      "Australia/Adelaide"),
        ("Darwin",        "Australia/Darwin"),
        ("Hobart",        "Australia/Hobart"),
        ("Auckland",      "Pacific/Auckland"),
        ("Wellington",    "Pacific/Auckland"),
        ("Chatham",       "Pacific/Chatham"),
        ("Fiji",          "Pacific/Fiji"),
        ("Port Moresby",  "Pacific/Port_Moresby"),
        ("Noumea",        "Pacific/Noumea"),
        ("Apia",          "Pacific/Apia"),
        ("Tahiti",        "Pacific/Tahiti"),
        ("Guam",          "Pacific/Guam"),
        ("Midway",        "Pacific/Midway"),
    ]),
    ("Antarctica", [
        ("McMurdo",       "Antarctica/McMurdo"),
        ("Casey",         "Antarctica/Casey"),
        ("Davis",         "Antarctica/Davis"),
        ("Vostok",        "Antarctica/Vostok"),
        ("Rothera",       "Antarctica/Rothera"),
        ("Palmer",        "Antarctica/Palmer"),
        ("Troll",         "Antarctica/Troll"),
    ]),
]

DEFAULT_CITY_LABEL = "Singapore"


def posix_for(zone: str) -> str:
    p = ZONEINFO / zone
    if not p.exists():
        raise FileNotFoundError(f"zoneinfo missing: {zone}")
    data = p.read_bytes()
    if not data.endswith(b"\n"):
        raise ValueError(f"{zone}: tzfile has no v2/v3 trailer")
    end = len(data) - 1
    start = data.rfind(b"\n", 0, end)
    if start < 0:
        raise ValueError(f"{zone}: cannot locate POSIX trailer")
    s = data[start + 1:end].decode("ascii")
    if not s:
        raise ValueError(f"{zone}: empty POSIX string (zone has no current rule)")
    return s


def c_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> int:
    rows = []
    default_idx = 0
    cont_ranges = []
    flat = []
    for cont, cities in CONTINENTS:
        first = len(flat)
        for label, zone in cities:
            posix = posix_for(zone)
            if label == DEFAULT_CITY_LABEL:
                default_idx = len(flat)
            flat.append((label, zone, posix))
        last = len(flat)  # exclusive
        cont_ranges.append((cont, first, last))

    lines = []
    lines.append("/* Auto-generated by scripts/gen_tz.py. Do not edit by hand. */")
    lines.append("#ifndef TZ_CITIES_H")
    lines.append("#define TZ_CITIES_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    const char *name;     /* friendly label, e.g. \"Singapore\" */")
    lines.append("    const char *zone;     /* IANA zone, e.g. \"Asia/Singapore\" */")
    lines.append("    const char *posix_tz; /* POSIX TZ string, e.g. \"<+08>-8\" */")
    lines.append("} tz_city_t;")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    const char *name;     /* continent label */")
    lines.append("    uint16_t    first;    /* first index into k_tz_cities (inclusive) */")
    lines.append("    uint16_t    last;     /* last index (exclusive) */")
    lines.append("} tz_continent_t;")
    lines.append("")
    lines.append(f"#define TZ_CITY_COUNT {len(flat)}")
    lines.append(f"#define TZ_CONTINENT_COUNT {len(cont_ranges)}")
    lines.append(f"#define TZ_DEFAULT_CITY_INDEX {default_idx}  /* {flat[default_idx][0]} */")
    lines.append("")
    lines.append("static const tz_city_t k_tz_cities[TZ_CITY_COUNT] = {")
    for i, (label, zone, posix) in enumerate(flat):
        lines.append(f"    {{ {c_str(label):<22}, {c_str(zone):<36}, {c_str(posix)} }},  /* {i} */")
    lines.append("};")
    lines.append("")
    lines.append("static const tz_continent_t k_tz_continents[TZ_CONTINENT_COUNT] = {")
    for cont, first, last in cont_ranges:
        lines.append(f"    {{ {c_str(cont):<14}, {first}, {last} }},")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* TZ_CITIES_H */")

    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT}: {len(flat)} cities, {len(cont_ranges)} continents, default={flat[default_idx][0]}",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
