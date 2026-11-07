# OMI version 1

This development format stores a single contiguous regular-file byte stream. All integers are unsigned little-endian. A writer produces one header, implicit sequential chunk records, and one commit trailer.

| File offset | Width | Field |
| --- | ---: | --- |
| 0 | 7 | ASCII OPENOMI |
| 7 | 1 | Version, currently 1 |
| 8 | 1 | Codec ID |
| 9 | 7 | Reserved, all zero |
| 16 | 8 | Logical byte count, inclusive range 1 through 2^50 |
| 24 | variable | Chunk records |
| End minus 40 | 8 | ASCII OMIDONE followed by byte 1 |
| End minus 32 | 32 | BLAKE3-256 of the header and all chunk records, including payloads |

Each record begins with a 4-byte payload length from 1 through 2,097,152 and a 32-byte BLAKE3-256 digest of the decoded bytes. The payload immediately follows. Logical chunk length is the lesser of 1,048,576 and the remaining logical bytes. There are exactly ceil(logical size / 1,048,576) records. No logical offsets are stored, so overlap, reordering by an offset, and holes are unrepresentable. The final record must end immediately before the 40-byte commit.

| ID | Encoding |
| --- | --- |
| 0 | Raw bytes of exact logical chunk length |
| 1 | One Zstandard frame with the exact content size, level 3 for writers |
| 2 | One independent raw LZ4 block |
| 3 | One LZMA2 properties byte (at most 16), then one LZMA2 stream with an end marker. Writers use level 5, a dictionary at most 1 MiB and one thread. |

Readers check lengths against the physical file size before reading payloads, enforce decoder output bounds and compare every decoded hash. They reject unknown versions and codec IDs, nonzero reserved fields, extra bytes and invalid commits. Header exploration returns explicitly unverified metadata. Digests detect corruption but are unkeyed and supply no origin authentication or confidentiality. Geometry, partition and boot metadata, encryption, incremental parents, and resumable journals are reserved for future format work and are absent from version 1.
