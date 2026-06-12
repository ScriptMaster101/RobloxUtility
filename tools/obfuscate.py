#!/usr/bin/env python3
"""
Lua obfuscator - WeAreDevs-style output.

Encodes the input Lua source as an octal-escaped blob in a `P` table, wraps
everything in an IIFE that decodes via `loadstring`. Attribution comment +
attribution string are preserved so the file's origin remains recoverable.

Usage:
    python obfuscate.py <input.lua> <output.lua> [--attribution URL]
"""
import argparse
import sys
import textwrap

DEFAULT_ATTRIBUTION = "https://github.com/ScriptMaster101/RobloxUtility"


def encode_octal(s: str) -> str:
    """Encode a string as Lua octal escapes: \\NNN per UTF-8 byte.

    Iterates over UTF-8 bytes (not characters) so multi-byte chars like
    U+2014 EM DASH get 3 separate 3-digit escapes (\342\200\224) instead
    of one 5+ digit escape (\20024) that breaks the decoder.
    """
    return "".join(f"\\{b:03o}" for b in s.encode("utf-8"))


def obfuscate(source: str, attribution: str) -> str:
    """
    Return a single-line obfuscated blob.

    Layout (after the attribution block comment):
        return(function(...)
            local P = {"<encoded source>", "<encoded attribution>"};
            local L = loadstring or load;
            local function D(s)
                local r = ""
                for i = 1, #s, 3 do
                    r = r .. string.char(tonumber(s:sub(i, i + 2), 8))
                end
                return r
            end;
            L(D(P[1]))()
        end)(...)
    """
    enc_src = encode_octal(source)
    enc_attr = encode_octal(attribution)

    # In a Lua string literal, `\\` decodes to a single `\`. We want the file
    # to contain literal `\NNN` escapes that the runtime decoder parses, so we
    # double-escape here: each `\` in the encoded string becomes `\\` in the
    # Lua source, which the parser turns back into a single `\` in the string.
    lua_src  = enc_src.replace("\\", "\\\\")
    lua_attr = enc_attr.replace("\\", "\\\\")

    # `]==]` lets us embed `--[[` in source without breaking the header.
    header = f"--[==[{attribution}]==]"
    # Single string with ';' as the source<->attribution delimiter. The encoded
    # payload is pure \NNN sequences - no ';' appears inside it.
    body = (
        f"return(function(...)"
        f'local P="{lua_src};{lua_attr}";'
        f"local L=loadstring or load;"
        f"local function D(s)local r=\"\";for i=1,#s,4 do r=r..string.char(tonumber(s:sub(i+1,i+3),8))end;return r end;"
        f"local src,attr=D(P:match'^(.-);'),D(P:match';(.*)$');"
        f"L(src)()"
        f"end)(...)"
    )
    return f"{header} {body}"


def self_check(blob: str, original_source: str) -> list[str]:
    """
    Structural sanity checks. We don't have a Lua interpreter locally,
    so we verify: octal blob length is a multiple of 4, all octal escapes
    decode to bytes that round-trip back to the original source.
    """
    issues = []
    # Pull the encoded source out of the blob.
    try:
        # The pattern is \"NNN;...\" inside the P string; we approximate.
        a = blob.index('local P="') + len('local P="')
        b = blob.index('"', a)
        encoded = blob[a:b]
    except ValueError:
        return ["could not locate encoded P blob in output"]

    # Split off attribution at the ';'
    if ";" not in encoded:
        return ["encoded blob has no ';' separator between source and attribution"]
    enc_src, enc_attr = encoded.split(";", 1)

    # Decode and compare. Each escape is exactly 4 chars: \NNN.
    decoded = bytearray()
    if len(enc_src) % 4 != 0:
        issues.append(f"encoded source length {len(enc_src)} is not a multiple of 4")
    for i in range(0, len(enc_src), 4):
        chunk = enc_src[i : i + 4]
        if not (chunk.startswith("\\") and all(c in "01234567" for c in chunk[1:])):
            issues.append(f"bad octal escape at offset {i}: {chunk!r}")
            break
        decoded.append(int(chunk[1:], 8))
    if bytes(decoded) != original_source.encode("utf-8"):
        issues.append(
            f"round-trip mismatch: decoded {len(decoded)} bytes, "
            f"original {len(original_source.encode('utf-8'))} bytes"
        )

    return issues


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="Input Lua file (plaintext source)")
    ap.add_argument("output", help="Output file (obfuscated blob)")
    ap.add_argument("--attribution", default=DEFAULT_ATTRIBUTION, help="Attribution URL/comment")
    ap.add_argument("--no-check", action="store_true", help="Skip structural verification")
    args = ap.parse_args()

    with open(args.input, "r", encoding="utf-8") as f:
        source = f.read()

    blob = obfuscate(source, args.attribution)

    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        f.write(blob)


    return 0


if __name__ == "__main__":
    sys.exit(main())
