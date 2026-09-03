# Memory patches

This folder can contain simple D2RLoader memory patches for this mod.

Use these files for small edits such as replacing bytes, writing values, NOPing code, or adding simple `jmp` / `call` patches. More complex changes should usually be done with a plugin DLL instead.

Patch files must use the `.json` extension. Files such as `example.json.template` are included only as examples and will not be loaded.

## Basic example

```json
{
  "version": 1,
  "name": "Short patch name",
  "description": "What this patch changes.",
  "patches": [
    {
      "description": "What this edit does.",
      "op": "bytes",
      "rva": "0x123456",
      "expected": "48 89 5C 24 08",
      "bytes": "90 90 90 90 90"
    }
  ]
}
```

## Notes

* Patch files must be valid JSON. Comments and trailing commas are not allowed.
* `rva` is relative to `D2R.exe`, not a full `0x140000000` address.
* Always replace the example RVA and bytes with values from the D2R version you are patching.
* `expected` is required. It makes sure the original bytes match before the patch is applied.
* `name` and `description` are optional, but they make logs and the Extensions tab easier to read.

## Supported operations

```text
bytes
nop
fill
write-u8
write-u16
write-u32
write-u64
jmp-rel32
call-rel32
```

## Operation fields

```text
bytes:                  rva, expected, bytes
nop:                    rva, expected, size
fill:                   rva, expected, size, value
write-u8/u16/u32/u64:   rva, expected, value
jmp-rel32/call-rel32:   rva, expected, targetRva, optional size
```

Byte values can be written as a hex string, a decimal array, or a string array:

```json
"48 8B 05"
[72, 139, 5]
["0x48", "0x8B", "0x05"]
```

For `jmp-rel32` and `call-rel32`, use `targetRva` for the destination.
