"""Resolve stock addresses and named canonical functions in an unpruned audit link."""

import collections
import csv
import hashlib
import re
import pefile

# The archived pair list names the symbol at the time the original run was
# recorded. Resolve it against either that MAP or a later semantics-only rename.
RENAMED = {
    "_Model_FindPart1": "_Model_FindRoadCollision",
    "_Model_FindPart2": "_Model_FindObjectCollision",
    "_Obj_FindPart673d20": "_Model_FindDynamicCollisionChild",
    "_Query_Parts1": "_World_QueryObjectCollision",
    "_Query_Parts2": "_World_QueryRoadCollision",
    "_Query_4e6621": "_World_TestRoadCollisionSegment",
    "_Carat_ReadFill2": "_Ini_ReadIntsWithDefault",
    "@Car_555500@8": "@Car_GetWeightedStateMagnitude@8",
    "_Rec_4015a0Both": "_Ai_ActivateVehicleAndRoute",
    "@SceneNode5f98c0_Draw@16": "@ShadedReference_Draw@16",
    "_Shell_ButtonD": "_Shell_CreateMainMenuButton",
    "_Obj_Lookup4e5b22": "_World_ResolveSegmentDestination",
    "_Near3": "_CoordinateWithinSegmentSpan",
    "_Player_PlaceField20": "_Ai_GetPlayerImage",
    "_Player_Call477bd0": "_Ai_SetPlayerDeadline",
    "_SchValue_Make10008": "_SchValue_MakeCharacter",
    "_Clamp542250": "_Vehicle_ClampPacketComponent",
    "_Rec_Field8": "_Ai_GetPresentFlags",
    "@Node5f1a70_Destroy@4": "@SceneSlotNode_Destroy@4",
    "@Eval4a3e40@28": "@Env_DefineNamedValue@28",
    "_Shell_NewObjRef": "_Shell_CreateCursorReference",
    "_SchValue_MakePairB": "_SchValue_MakeEmptyPair",
    "@Sce5ef660_Destroy@4": "@SceneNode_DestroyBase@4",
    "@SchItem4b1090_Construct@12": "@SchPair_ConstructEmpty@12",
    "_Room_ObjectAt": "_World_FindRoadRoomObjectAt",
    "@Window_Set80@12": "@Surface_SetDrawMode@12",
    "_Gate_600b50": "_DirectPlay_ErrorText",
    "_Player_Driver": "_Player_FirstActiveCargo",
    "_Obj723d44_Delete": "_Music_StopPlaybackThread",
    "_Player_Value136": "_Player_GetBalance",
    "_Font_LoadImage": "_Font_ReadGlyphAtlas",
    "@Media_Call5cc680@20": "@Tree_GrowAndFinishRoot@20",
    "_Shell_NewScreenA@8": "_Shell_CreateFixedWidthButton@8",
    "_Query_4e5b22": "_World_QuerySegmentDestination",
    "@Sce_5fccd0@16": "@Room_QueryChildSlot@16",
    "@SceneNode5fc2c0_Test@16": "@Portal_QueryFace@16",
    "_Shell_NewScreenB": "_Shell_CreateUniformStyleButton",
    "_Effects_Register": "_Mmt_ReportNullAssertion",
    "_Ai_OperatorNew": "_Stl_RetryMallocAfterFailure",
    "_Data_Read5e9360": "_Mask_LoadFile",
    "_Data_Read5e94d0": "_Mask_ReadHeaderFromFile",
    "_Data_Read5e9690": "_Mask_ReadPixelsFromFile",
    "_Data_Read5e9910": "_Sound_LoadFile",
    "_Data_Read5e99a0": "_Sound_SetRawFileSize",
    "_Data_Read5e99c0": "_Sound_ReadBytesFromFile",
    "_Sch4ad330": "_Sch_RegisterNativeProcedure",
    "_Sch4b8070": "_Sch_WriteDatum",
    "_Iter_Advance": "_DwordIterator_Advance",
    "_Carat_ReadFill": "_Ini_ReadFloatsWithDefault",
    "_World_4e634f": "_World_ReturnNoHit",
    "@Car_556650@8": "@Car_GetActiveCabHeightOffset@8",
}


def load_pairs(args):
    stock = "31fbd1dbbe7706183f09a3688482791255db2bfb557c02675374b96796ce2ebd"
    if hashlib.sha256(args.original.read_bytes()).hexdigest() != stock:
        raise ValueError("Expected Steam 8.1")
    symbols = collections.defaultdict(set)
    mapping = args.map.read_text()
    pe = pefile.PE(str(args.candidate), fast_load=True)
    if f"Timestamp is {pe.FILE_HEADER.TimeDateStamp:08x}" not in mapping:
        raise ValueError("Wrong canonical MAP")
    for name, addr in re.findall(
        r"^\s+\w+:\w+\s+(\S+)\s+([\da-f]{8})\s+f\s+", mapping, re.M
    ):
        symbols[name].add(int(addr, 16))
    with args.pairs.open(encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) != len({row["stock_va"] for row in rows}):
        raise ValueError("Duplicate stock addresses")
    for row in rows:
        name = row["candidate_symbol"]
        row["candidate_va"] = ""
        if name:
            if name not in symbols and name in RENAMED:
                name = RENAMED[name]
                row["candidate_symbol"] = name
            addresses = symbols[name]
            if len(addresses) != 1:
                raise ValueError(f"Missing or ambiguous symbol: {name}")
            row["candidate_va"] = f"{next(iter(addresses)):x}"
            if name == "_Shell_CreateMainMenuButton":
                row["parameters"] = "ptr int int int"
                row["result_type"] = "ptr"
            elif name == "_World_ResolveSegmentDestination":
                row["parameters"] = "ptr ptr ptr ptr"
                row["result_type"] = "ptr"
            elif name == "_Ai_SetPlayerDeadline":
                row["parameters"] = "ptr double"
                row["result_type"] = "void"
            elif name == "_Markets_Grow":
                row["parameters"] = "double"
            elif name == "@Tree_GrowAndFinishRoot@20":
                row["parameters"] = "ptr ptr int ptr ptr"
                row["result_type"] = "ptr"
            elif name in ("_Player_FirstActiveCargo", "_DirectPlay_ErrorText"):
                row["result_type"] = "ptr"
    return rows
