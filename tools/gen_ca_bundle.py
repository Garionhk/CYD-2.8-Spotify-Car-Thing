#!/usr/bin/env python3
"""Build data/x509_crt_bundle.bin, the root-CA bundle WiFiClientSecure::setCACertBundle() reads.

Format (ESP-IDF esp_crt_bundle, v4.4 -- Arduino-ESP32 core 2.0.x):
    u16 BE  number of certificates
    then per certificate, sorted by subject DER:
        u16 BE  subject length
        u16 BE  public key length
        subject DER (the X.509 Name)
        public key DER (SubjectPublicKeyInfo)

Only the matching root is parsed during a TLS handshake, so a full bundle costs flash, not RAM.
This covers Spotify's hosts today (accounts: Starfield G2, api: DigiCert G2, art CDN) and
keeps working if they change CA.

Usage:
    security find-certificate -a -p /System/Library/Keychains/SystemRootCertificates.keychain > roots.pem
    python3 tools/gen_ca_bundle.py roots.pem data/x509_crt_bundle.bin
"""
import base64
import datetime
import re
import struct
import sys


def tlv(buf, pos):
    """Return (tag, header_len, value_len) of the DER element at pos."""
    tag = buf[pos]
    first = buf[pos + 1]
    if first < 0x80:
        return tag, 2, first
    n = first & 0x7F
    length = int.from_bytes(buf[pos + 2:pos + 2 + n], "big")
    return tag, 2 + n, length


def children(buf, pos):
    """Yield (start, end) of each element inside the constructed element at pos."""
    _, hl, vl = tlv(buf, pos)
    p, end = pos + hl, pos + hl + vl
    while p < end:
        _, chl, cvl = tlv(buf, p)
        yield p, p + chl + cvl
        p += chl + cvl


def parse_time(raw, tag):
    s = raw.decode()
    if tag == 0x17:  # UTCTime YYMMDDHHMMSSZ
        year = int(s[:2])
        year += 2000 if year < 50 else 1900
        s = str(year) + s[2:]
    return datetime.datetime.strptime(s[:14], "%Y%m%d%H%M%S")


def subject_and_key(der):
    cert = next(children(der, 0))           # TBSCertificate
    parts = list(children(der, cert[0]))
    if der[parts[0][0]] == 0xA0:            # [0] version, optional
        parts = parts[1:]
    # serial, signature, issuer, validity, subject, subjectPublicKeyInfo
    validity, subject, spki = parts[3], parts[4], parts[5]
    times = list(children(der, validity[0]))
    not_after = None
    s, e = times[1]
    tag, hl, _ = tlv(der, s)
    not_after = parse_time(der[s + hl:e], tag)
    return der[subject[0]:subject[1]], der[spki[0]:spki[1]], not_after


def main(src, dst):
    pem = open(src).read()
    blocks = re.findall(r"-----BEGIN CERTIFICATE-----(.+?)-----END CERTIFICATE-----", pem, re.S)
    now = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)
    entries, seen, expired = [], set(), 0
    for b in blocks:
        der = base64.b64decode("".join(b.split()))
        subj, key, not_after = subject_and_key(der)
        if not_after < now:
            expired += 1
            continue
        if (subj, key) in seen:
            continue
        seen.add((subj, key))
        entries.append((subj, key))
    entries.sort(key=lambda e: e[0])
    out = bytearray(struct.pack(">H", len(entries)))
    for subj, key in entries:
        out += struct.pack(">HH", len(subj), len(key)) + subj + key
    open(dst, "wb").write(out)
    print(f"{len(entries)} roots ({expired} expired skipped) -> {dst}, {len(out)} bytes")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
