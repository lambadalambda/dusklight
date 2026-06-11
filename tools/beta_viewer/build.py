#!/usr/bin/env python3
"""Build the beta-content viewer site from a Twilight Princess disc image.

Usage: python3 build.py <dump.rvz> [outdir]

Extracts the unused actors' model archives, converts every BMD/BDL inside to
glTF, and assembles a static gallery site (serve with `python3 -m http.server`).
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import bmd2gltf
from jres import yaz0, rarc_files

HERE = Path(__file__).parent

# arc name -> (display title, actor source, note)
TARGETS = {
    "B_go": ("Statue Boss prototype “B_go”", "d_a_b_go.cpp",
             "Cut boss: 1000 HP enemy skeleton with wait/walk/attack AI, draw "
             "hidden behind a debug flag, damage handling never implemented. "
             "Spawns 31 B_gos Goron children around itself."),
    "B_gos": ("Goron child “B_gos”", "d_a_b_gos.cpp",
              "Companion of the cut statue boss. Has walk/ball/stick actions and "
              "a turn-to-stone animation that exists nowhere in the retail game."),
    "E_is": ("Moving statue enemy “E_is”", "d_a_e_is.cpp",
             "動くイデリア石像 — 'moving stone statue' enemy, 772 lines of "
             "finished AI (attack/trap/startle/death), never placed in any map."),
    "E_yd": ("Shadow plant enemy “E_yd”", "d_a_e_yd.cpp",
             "1,541-line ambush plant on the Big Baba rig (appear/bite/revive/"
             "escape) with its own leaf companion actor. Cut twilit Baba variant."),
    "Zant": ("Zant NPC", "d_a_npc_zant.cpp",
             "Unused placeable NPC version of Zant, separate from the boss actors."),
    "zelRf": ("Robed Zelda (hooded)", "d_a_npc_zelR.cpp",
              "フード付きローブゼルダ — unused hooded-robe Zelda NPC with path/"
              "dialog-flow logic."),
    "zelRo": ("Robed Zelda (unhooded)", "d_a_npc_zelRo.cpp",
              "フードなしローブゼルダ — unused robe Zelda without the hood."),
    "Seirei": ("Light Spirit NPC “a”", "d_a_npc_seirei.cpp",
               "光の精霊ａ — conversational Light Spirit NPC; retail spirits are "
               "cutscene effects only."),
    "seiB": ("Light Spirit NPC “b”", "d_a_npc_seib.cpp", "光の精霊ｂ."),
    "seiC": ("Light Spirit NPC “c”", "d_a_npc_seic.cpp", "光の精霊ｃ."),
    "seiD": ("Light Spirit NPC “d”", "d_a_npc_seid.cpp", "光の精霊ｄ."),
    "gnd": ("Ganondorf NPC", "d_a_npc_gnd.cpp",
            "ガノンドロフ — unused placeable Ganondorf NPC (702 lines), separate "
            "from the boss fight actors."),
    "DrainSol1": ("Sewer soldier 1", "d_a_npc_drainSol.cpp",
                  "下水道の兵士 — cut NPC apparently for the Hyrule Castle sewers."),
    "DrainSol2": ("Sewer soldier 2", "d_a_npc_drainSol.cpp", "Second sewer soldier variant."),
    "Shop0": ("Placeholder shopkeeper “Shop0”", "d_a_npc_shop0.cpp",
              "Unused shop NPC that wears a Goron model as a placeholder."),
    "K_cube00": ("Test cube 00", "d_a_obj_testcube.cpp", "Literal developer test object."),
    "K_cube01": ("Test cube 01", "d_a_obj_testcube.cpp", "Second test cube variant."),
    "Tbox2": ("Unused treasure chest “Tbox2”", "d_a_tbox2.cpp",
              "Chest variant whose opened state is not saved; tboxEL0/EL1 names "
              "are never placed."),
    "Knj": ("Unused NPC “Knj”", "d_a_npc_knj.cpp",
            "551-line NPC never placed nor spawned; identity unclear."),
}


def build_nodx(workdir: Path) -> Path:
    exe = workdir / "nodx"
    if exe.exists():
        return exe
    repo = HERE.parent.parent
    cands = sorted(repo.glob("build/*/_deps/nod_prebuilt-src"))
    if not cands:
        sys.exit("no nod prebuilt found under build/ - configure the project first")
    nod = cands[0]
    dylib = workdir / "libnod.dylib"
    src_dylib = nod / "lib" / "libnod.dylib"
    args = ["cc", "-O1", f"-I{nod}/include", str(HERE / "nodx.c"), "-o", str(exe)]
    if src_dylib.exists():
        shutil.copy(src_dylib, dylib)
        subprocess.run(["install_name_tool", "-id", str(dylib), str(dylib)], check=True)
        args += [f"-L{workdir}", "-lnod", "-Wl,-rpath," + str(workdir)]
    else:
        args += [str(nod / "lib" / "libnod.a")]
    subprocess.run(args, check=True)
    return exe


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rvz = sys.argv[1]
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "site"
    models_dir = out / "models"
    models_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as td:
        tdp = Path(td)
        nodx = build_nodx(tdp)
        arc_dir = tdp / "arcs"
        arc_dir.mkdir()
        subprocess.run([str(nodx), rvz, "res/Object/", str(arc_dir)],
                       check=True, stdout=subprocess.DEVNULL)

        index = []
        for arc_name, (title, source, note) in TARGETS.items():
            arc_path = arc_dir / f"res_Object_{arc_name}.arc"
            if not arc_path.exists():
                print(f"!! missing {arc_path.name}, skipping")
                continue
            raw = yaz0(arc_path.read_bytes())
            entry_models = []
            for fname, content in rarc_files(raw):
                if not (fname.endswith(".bmd") or fname.endswith(".bdl")):
                    continue
                model_id = f"{arc_name}_{Path(fname).stem}"
                try:
                    n_prims, n_tex = bmd2gltf.convert(content, models_dir, model_id)
                except Exception as ex:
                    print(f"!! {arc_name}/{fname}: {ex}")
                    continue
                entry_models.append(f"models/{model_id}.gltf")
                print(f"{arc_name}/{fname}: {n_prims} prims, {n_tex} textures")
            if entry_models:
                index.append({"title": title, "arc": arc_name, "source": source,
                              "note": note, "models": entry_models})

    (out / "models.json").write_text(json.dumps(index, indent=1))
    for f in ("index.html", "app.js"):
        shutil.copy(HERE / f, out / f)
    print(f"\nsite ready: {out}\nserve with: python3 -m http.server -d {out} 8000")


if __name__ == "__main__":
    main()
