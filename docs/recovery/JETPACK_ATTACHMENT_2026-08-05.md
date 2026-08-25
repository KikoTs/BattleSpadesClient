# Retail third-person jetpack attachment

Scope: client presentation only. No server or original-client file was changed.

## Proven model rows

`shared/constants.py` places the four selectable packs at IDs 66..69. The
`JETPACK_MODELS` table in `aoslib/models.py` resolves them as follows:

| ID | KV6 stem |
|---:|---|
| 66 | `jetpack` |
| 67 | `Jetpack2` |
| 68 | `JetpackEngineer` |
| 69 | `JetpackUGCBuilder` |

The sentinel `NO_JETPACK` is 65. It is not a drawable pack.

## Proven attachment construction

The symbol-rich macOS `aoslib.character.so.i64` decompiles
`Character.set_jetpack_model` at `0xDDEF0` (source lines 214-218) to the
following semantic sequence:

```text
jetpack_model = DisplayList(JETPACK_MODELS[jetpack_type], z_offset=6)
jetpack_model.y = -0.6
jetpack_model.z = 0.8
jetpack_model.size = 0.075
```

This is the attachment's model-space transform. The pack remains a child of
the character display hierarchy, so ordinary rendering applies the character
world transform after this local transform. `Character.set_dead` retains the
same `jetpack_model`; `Character.update_dead` rotates the owning dead
character. There is no recovered evidence for a second death-only pack offset
or a generic replacement pack. Consequently the exact selected ID/model must
be retained across the one-second corpse fuse.

## Native contract

`retail_jetpack_model(id)` owns the row-to-asset mapping and
`retail_jetpack_attachment()` owns the recovered DisplayList parameters. A
renderer must convert those model-space values through its established KV6
axis convention exactly once; baking another correction into the pack model
would double-offset the death path.

