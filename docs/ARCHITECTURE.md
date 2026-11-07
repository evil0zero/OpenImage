# Architecture and threat model

    CLI / Terminal / Qt form
            |
            v
    Shared regular-file operation engine
        |              |
        v              v
    Native file I/O    OMI reader/writer + built-in codecs
        |
        v
    Staging directory -> verify readback -> publish to new path

The engine opens sources read-only. Windows opens local paths with write sharing disabled, rejects final reparse points and checks the opened handle type. Linux uses no-follow opens, checks for regular files and takes advisory locks. The engine assumes stable offline input and trusted directory ancestors. It has no privileged worker and supports no physical-disk writes.

An OMI image can be supplied by an untrusted party. Its signature, feature fields, offsets, lengths and codec IDs are checked before use. Decoding is limited to 1 MiB of output per record and a payload of at most 2 MiB. Chunk hashes bind decoded data to its recorded digest. A separate digest checks the serialized stream and commit. A malicious author can regenerate both digests, so readers cannot infer provenance from successful verification. Decoder libraries execute in the same unprivileged process. Parser fuzzing, separate sandboxing, authenticated encryption and a current vulnerability audit remain release blockers.

Backup, restore and clone build a new file privately and verify readback before publishing it. The final operation refuses to replace an existing name, including when competing processes race to publish. Existing outputs and inputs must remain intact. Windows uses a write-through move. Linux uses a hard link and syncs the parent directory. Power-loss durability is unqualified. Directory replacement by an adversary with access to the output hierarchy is outside this build's trust model.
