import ida_auto
import ida_funcs
import ida_hexrays
import idautils
import idaapi

ida_auto.auto_wait()

targets = {
    "interpolated_position": 0x1028B850,
    "character": 0x1028D888,
    "characters": 0x1028D92C,
    "players": 0x1028EC38,
    "position": 0x1028C0B4,
    "positions": 0x1028F0CC,
    "update": 0x1028D238,
}

refs = {}
for name, ea in targets.items():
    functions = set()
    for xref in idautils.XrefsTo(ea, 0):
        function = ida_funcs.get_func(xref.frm)
        if function is not None:
            functions.add(function.start_ea)
    refs[name] = functions
    print(name, [hex(function) for function in sorted(functions)])

candidates = ((refs["interpolated_position"] &
               (refs["positions"] | refs["characters"] | refs["character"])) |
              (refs["positions"] - {0x10001B60}))
print("CANDIDATES", [hex(function) for function in sorted(candidates)])
for function in sorted(candidates):
    try:
        print("DECOMPILE", hex(function))
        print(str(ida_hexrays.decompile(function)))
    except Exception as error:
        print("FAILED", hex(function), repr(error))

idaapi.qexit(0)
